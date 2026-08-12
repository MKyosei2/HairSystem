from __future__ import annotations

"""HairSystem Novelty01 formal matrix policy shim for thesis revision 70.31.

70.31 keeps the 70.30 90-run/no-Smoke evidence policy unchanged and fixes the
long-run Viewer ExitCode=92 failure mode without weakening the native integrity
gate. A Windows-side guard is attached to every HairSystem.exe --formal-run
process before measurement. It removes user-resize/maximize affordances while
preserving the already-established 1920x1080 client area, monitors the client
area for the complete process lifetime, and writes per-case guard evidence.

A real client-area change is never repaired after it happens and Viewer exit 92
is never converted into success. If the native Viewer returns success but the
guard was unavailable, could not lock the expected client area, or observed a
size/display violation, this shim fails closed with exit code 96.
"""

import ctypes
import json
import os
import subprocess
import sys
import threading
import time
from pathlib import Path
from typing import Any, Mapping, Sequence

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import hairgpu_final_run as final_run  # type: ignore
import hairgpu_novelty01_matrix_7030 as revision7030  # type: ignore

FORMAL_REVISION = "70.31"
SAMPLING_POLICY = "paired-thesis-90-v1"
FATAL_POLICY = "nan-inf-crash-input-mismatch-corruption-v1"
CONTRACT_RELATIVE = Path("docs/experiments/NOVELTY01_FORMAL_CONTRACT_7031.json")
WINDOW_GUARD_REVISION = "fixed-client-window-guard-v1"
WINDOW_GUARD_EVIDENCE_NAME = "formal_window_guard_7031.json"
WINDOW_GUARD_FAILURE_EXIT = 96
EXPECTED_CLIENT_WIDTH = 1920
EXPECTED_CLIENT_HEIGHT = 1080

# Keep the already-proven 70.30 validator policy, but make every dynamically
# generated result/summary identify the actual revision used for this session.
revision7030.FORMAL_REVISION = FORMAL_REVISION
revision7030.CONTRACT_RELATIVE = CONTRACT_RELATIVE
legacy = revision7030.legacy
legacy.FORMAL_CONTRACT_RELATIVE = CONTRACT_RELATIVE

_RUNTIME_GLOBALS = legacy.final_run.execute_case.__globals__
_original_run_process = _RUNTIME_GLOBALS.get("run_process")
if not callable(_original_run_process):
    raise RuntimeError(
        "70.31 could not resolve the production run_process binding used by execute_case"
    )


def _is_formal_viewer_command(command: Sequence[str]) -> bool:
    if not command:
        return False
    executable = Path(str(command[0])).name.lower()
    return executable == "hairsystem.exe" and any(
        str(argument).strip().lower() == "--formal-run" for argument in command[1:]
    )


def _write_guard_json(path: Path, payload: Mapping[str, Any]) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    temporary.write_text(
        json.dumps(dict(payload), ensure_ascii=False, indent=2, allow_nan=False) + "\n",
        encoding="utf-8",
    )
    os.replace(temporary, path)


def _guard_result_return_code(
    viewer_return_code: int,
    evidence: Mapping[str, Any] | None,
) -> int:
    """Preserve every native failure, especially ExitCode 92.

    The guard may only turn a nominal native success into a fail-closed harness
    failure. It can never turn a native failure into success or replace its code.
    """
    native = int(viewer_return_code)
    if native != 0:
        return native
    if not isinstance(evidence, Mapping) or evidence.get("status") != "PASS":
        return WINDOW_GUARD_FAILURE_EXIT
    violations = evidence.get("violations", [])
    if not isinstance(violations, list) or violations:
        return WINDOW_GUARD_FAILURE_EXIT
    return 0


class _FormalWindowGuard:
    """Locks and monitors one formal Viewer top-level window on Windows."""

    GWL_STYLE = -16
    GWL_EXSTYLE = -20
    WS_THICKFRAME = 0x00040000
    WS_MAXIMIZEBOX = 0x00010000
    SC_SIZE = 0xF000
    SC_MAXIMIZE = 0xF030
    MF_BYCOMMAND = 0x00000000
    MF_GRAYED = 0x00000001
    SWP_NOZORDER = 0x0004
    SWP_NOACTIVATE = 0x0010
    SWP_FRAMECHANGED = 0x0020
    SM_CXSCREEN = 0
    SM_CYSCREEN = 1

    def __init__(self, process: subprocess.Popen[str], evidence_path: Path):
        self.process = process
        self.pid = int(process.pid)
        self.evidence_path = Path(evidence_path)
        self._stop_event = threading.Event()
        self._thread: threading.Thread | None = None
        self._lock = threading.Lock()
        self._started_utc = time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())
        self._hwnd = 0
        self._locked = False
        self._failure_reason = ""
        self._violations: list[dict[str, Any]] = []
        self._initial_client: dict[str, int] | None = None
        self._locked_client: dict[str, int] | None = None
        self._style_before: int | None = None
        self._style_after: int | None = None
        self._primary_at_lock: dict[str, int] | None = None

    def start(self) -> None:
        self._thread = threading.Thread(
            target=self._run,
            name=f"hairgpu-formal-window-guard-{self.pid}",
            daemon=True,
        )
        self._thread.start()

    @staticmethod
    def _rect_type() -> type[ctypes.Structure]:
        class RECT(ctypes.Structure):
            _fields_ = [
                ("left", ctypes.c_long),
                ("top", ctypes.c_long),
                ("right", ctypes.c_long),
                ("bottom", ctypes.c_long),
            ]
        return RECT

    def _user32(self) -> Any:
        if sys.platform != "win32":
            raise RuntimeError("70.31 formal window guard requires Windows")
        return ctypes.WinDLL("user32", use_last_error=True)

    def _find_top_level_window(self, user32: Any) -> int:
        candidates: list[int] = []
        callback_type = ctypes.WINFUNCTYPE(
            ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p
        )

        def callback(hwnd: int, _lparam: int) -> bool:
            process_id = ctypes.c_ulong(0)
            user32.GetWindowThreadProcessId(
                ctypes.c_void_p(hwnd), ctypes.byref(process_id)
            )
            if int(process_id.value) != self.pid:
                return True
            if not user32.IsWindowVisible(ctypes.c_void_p(hwnd)):
                return True
            candidates.append(int(hwnd))
            return True

        callback_ref = callback_type(callback)
        user32.EnumWindows(callback_ref, 0)
        for hwnd in candidates:
            width, height = self._client_size(user32, hwnd)
            if width > 0 and height > 0:
                return hwnd
        return 0

    def _client_size(self, user32: Any, hwnd: int) -> tuple[int, int]:
        RECT = self._rect_type()
        rect = RECT()
        if not user32.GetClientRect(ctypes.c_void_p(hwnd), ctypes.byref(rect)):
            return 0, 0
        return max(0, int(rect.right - rect.left)), max(0, int(rect.bottom - rect.top))

    def _primary_size(self, user32: Any) -> tuple[int, int]:
        return (
            int(user32.GetSystemMetrics(self.SM_CXSCREEN)),
            int(user32.GetSystemMetrics(self.SM_CYSCREEN)),
        )

    def _append_violation(self, kind: str, observed: Any) -> None:
        with self._lock:
            self._violations.append(
                {
                    "kind": kind,
                    "observed": observed,
                    "capturedAtUtc": time.strftime(
                        "%Y-%m-%dT%H:%M:%SZ", time.gmtime()
                    ),
                }
            )

    def _lock_window(self, user32: Any, hwnd: int) -> bool:
        width, height = self._client_size(user32, hwnd)
        self._initial_client = {"width": width, "height": height}
        primary_width, primary_height = self._primary_size(user32)
        self._primary_at_lock = {
            "width": primary_width,
            "height": primary_height,
        }
        if (width, height) != (EXPECTED_CLIENT_WIDTH, EXPECTED_CLIENT_HEIGHT):
            self._failure_reason = (
                "Viewer never reached the required 1920x1080 client area before lock"
            )
            return False
        if (primary_width, primary_height) != (
            EXPECTED_CLIENT_WIDTH,
            EXPECTED_CLIENT_HEIGHT,
        ):
            self._failure_reason = (
                f"Primary display changed before window lock: "
                f"{primary_width}x{primary_height}"
            )
            return False

        style_signed = int(user32.GetWindowLongW(ctypes.c_void_p(hwnd), self.GWL_STYLE))
        style = style_signed & 0xFFFFFFFF
        ex_style_signed = int(
            user32.GetWindowLongW(ctypes.c_void_p(hwnd), self.GWL_EXSTYLE)
        )
        ex_style = ex_style_signed & 0xFFFFFFFF
        self._style_before = style
        locked_style = style & ~(self.WS_THICKFRAME | self.WS_MAXIMIZEBOX)

        if locked_style != style:
            RECT = self._rect_type()
            window_rect = RECT()
            if not user32.GetWindowRect(
                ctypes.c_void_p(hwnd), ctypes.byref(window_rect)
            ):
                self._failure_reason = "GetWindowRect failed while applying formal lock"
                return False

            target = RECT(0, 0, EXPECTED_CLIENT_WIDTH, EXPECTED_CLIENT_HEIGHT)
            if not user32.AdjustWindowRectEx(
                ctypes.byref(target),
                ctypes.c_ulong(locked_style),
                False,
                ctypes.c_ulong(ex_style),
            ):
                self._failure_reason = "AdjustWindowRectEx failed while preserving client area"
                return False

            ctypes.set_last_error(0)
            previous = user32.SetWindowLongW(
                ctypes.c_void_p(hwnd),
                self.GWL_STYLE,
                ctypes.c_long(locked_style).value,
            )
            if int(previous) == 0 and ctypes.get_last_error() != 0:
                self._failure_reason = "SetWindowLongW failed while disabling formal resize"
                return False

            outer_width = int(target.right - target.left)
            outer_height = int(target.bottom - target.top)
            if not user32.SetWindowPos(
                ctypes.c_void_p(hwnd),
                None,
                int(window_rect.left),
                int(window_rect.top),
                outer_width,
                outer_height,
                self.SWP_NOZORDER | self.SWP_NOACTIVATE | self.SWP_FRAMECHANGED,
            ):
                self._failure_reason = "SetWindowPos failed while preserving formal client area"
                return False

        system_menu = user32.GetSystemMenu(ctypes.c_void_p(hwnd), False)
        if system_menu:
            user32.EnableMenuItem(
                system_menu,
                self.SC_SIZE,
                self.MF_BYCOMMAND | self.MF_GRAYED,
            )
            user32.EnableMenuItem(
                system_menu,
                self.SC_MAXIMIZE,
                self.MF_BYCOMMAND | self.MF_GRAYED,
            )

        time.sleep(0.05)
        observed_width, observed_height = self._client_size(user32, hwnd)
        self._locked_client = {
            "width": observed_width,
            "height": observed_height,
        }
        self._style_after = int(
            user32.GetWindowLongW(ctypes.c_void_p(hwnd), self.GWL_STYLE)
        ) & 0xFFFFFFFF

        if (observed_width, observed_height) != (
            EXPECTED_CLIENT_WIDTH,
            EXPECTED_CLIENT_HEIGHT,
        ):
            self._failure_reason = (
                "Applying the non-resizable style did not preserve the required client area: "
                f"{observed_width}x{observed_height}"
            )
            return False
        if self._style_after & self.WS_THICKFRAME:
            self._failure_reason = "WS_THICKFRAME remained enabled after formal lock"
            return False
        if self._style_after & self.WS_MAXIMIZEBOX:
            self._failure_reason = "WS_MAXIMIZEBOX remained enabled after formal lock"
            return False
        return True

    def _run(self) -> None:
        try:
            user32 = self._user32()
            deadline = time.monotonic() + 20.0
            while not self._stop_event.is_set() and time.monotonic() < deadline:
                if self.process.poll() is not None:
                    break
                hwnd = self._find_top_level_window(user32)
                if hwnd:
                    width, height = self._client_size(user32, hwnd)
                    if (width, height) == (
                        EXPECTED_CLIENT_WIDTH,
                        EXPECTED_CLIENT_HEIGHT,
                    ):
                        self._hwnd = hwnd
                        self._locked = self._lock_window(user32, hwnd)
                        break
                time.sleep(0.05)

            if not self._locked and not self._failure_reason:
                self._failure_reason = (
                    "Could not acquire and lock a 1920x1080 formal Viewer window"
                )

            while self._locked and not self._stop_event.wait(0.20):
                if self.process.poll() is not None:
                    break
                if not user32.IsWindow(ctypes.c_void_p(self._hwnd)):
                    if self.process.poll() is None:
                        self._append_violation("window-lost-before-process-exit", None)
                    break
                width, height = self._client_size(user32, self._hwnd)
                if (width, height) != (
                    EXPECTED_CLIENT_WIDTH,
                    EXPECTED_CLIENT_HEIGHT,
                ):
                    # Do not restore the window here. Once the render client area
                    # really changed, the measurement is contaminated. Native
                    # Viewer ExitCode 92 remains the authoritative fatal signal.
                    self._append_violation(
                        "client-area-changed",
                        {"width": width, "height": height},
                    )
                    break
                primary_width, primary_height = self._primary_size(user32)
                if (primary_width, primary_height) != (
                    EXPECTED_CLIENT_WIDTH,
                    EXPECTED_CLIENT_HEIGHT,
                ):
                    self._append_violation(
                        "primary-display-changed",
                        {"width": primary_width, "height": primary_height},
                    )
                    break
        except Exception as exc:  # fail closed, but never kill/repair the Viewer
            self._failure_reason = f"Window guard exception: {exc}"

    def stop(self, viewer_return_code: int) -> dict[str, Any]:
        self._stop_event.set()
        if self._thread is not None:
            self._thread.join(timeout=5.0)
        with self._lock:
            violations = list(self._violations)
        status = "PASS" if self._locked and not self._failure_reason and not violations else "FAIL"
        payload: dict[str, Any] = {
            "schema": "hairgpu-formal-window-guard-v1-7031",
            "formalRevision": FORMAL_REVISION,
            "guardRevision": WINDOW_GUARD_REVISION,
            "status": status,
            "pid": self.pid,
            "hwnd": self._hwnd,
            "expectedClient": {
                "width": EXPECTED_CLIENT_WIDTH,
                "height": EXPECTED_CLIENT_HEIGHT,
            },
            "initialClient": self._initial_client,
            "lockedClient": self._locked_client,
            "primaryDisplayAtLock": self._primary_at_lock,
            "styleBefore": self._style_before,
            "styleAfter": self._style_after,
            "resizeFrameDisabled": bool(
                self._style_after is not None
                and not (self._style_after & self.WS_THICKFRAME)
            ),
            "maximizeDisabled": bool(
                self._style_after is not None
                and not (self._style_after & self.WS_MAXIMIZEBOX)
            ),
            "viewerExit92RemainsFatal": True,
            "autoRecoveryAfterActualResize": False,
            "failureReason": self._failure_reason,
            "violations": violations,
            "viewerExitCode": int(viewer_return_code),
            "startedAtUtc": self._started_utc,
            "completedAtUtc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        }
        _write_guard_json(self.evidence_path, payload)
        return payload


def _run_process_7031(
    command: list[str],
    cwd: Path,
    environment: Mapping[str, str],
    timeout_seconds: int,
    on_started: Any = None,
) -> tuple[int, str, str, bool]:
    if not _is_formal_viewer_command(command):
        return _original_run_process(
            command,
            cwd,
            environment,
            timeout_seconds,
            on_started=on_started,
        )

    telemetry_dir_text = str(environment.get("HAIRGPU_N01_TELEMETRY_DIR", "")).strip()
    guard_holder: dict[str, _FormalWindowGuard] = {}
    evidence_path = (
        Path(telemetry_dir_text) / WINDOW_GUARD_EVIDENCE_NAME
        if telemetry_dir_text
        else Path(cwd) / WINDOW_GUARD_EVIDENCE_NAME
    )

    def started(process: subprocess.Popen[str]) -> None:
        if on_started is not None:
            on_started(process)
        guard = _FormalWindowGuard(process, evidence_path)
        guard_holder["guard"] = guard
        guard.start()

    return_code, stdout, stderr, timed_out = _original_run_process(
        command,
        cwd,
        environment,
        timeout_seconds,
        on_started=started,
    )

    guard = guard_holder.get("guard")
    if guard is None:
        evidence: dict[str, Any] = {
            "schema": "hairgpu-formal-window-guard-v1-7031",
            "formalRevision": FORMAL_REVISION,
            "guardRevision": WINDOW_GUARD_REVISION,
            "status": "FAIL",
            "expectedClient": {
                "width": EXPECTED_CLIENT_WIDTH,
                "height": EXPECTED_CLIENT_HEIGHT,
            },
            "viewerExit92RemainsFatal": True,
            "autoRecoveryAfterActualResize": False,
            "failureReason": "run_process did not attach the formal window guard",
            "violations": [],
            "viewerExitCode": int(return_code),
        }
        _write_guard_json(evidence_path, evidence)
    else:
        evidence = guard.stop(return_code)

    guarded_code = _guard_result_return_code(return_code, evidence)
    if guarded_code != return_code:
        stderr = (
            stderr
            + "\n70.31 formal window guard failed closed after a nominal Viewer success. "
            + f"Evidence={evidence_path}\n"
        )
    return guarded_code, stdout, stderr, timed_out


# Patch the exact hidden implementation module used by execute_case. Public
# aliases are updated as well, but the globals binding is authoritative.
_RUNTIME_GLOBALS["run_process"] = _run_process_7031
setattr(legacy.final_run, "run_process", _run_process_7031)
setattr(final_run, "run_process", _run_process_7031)

if legacy.final_run.execute_case.__globals__.get("run_process") is not _run_process_7031:
    raise RuntimeError(
        "70.31 window guard is not bound to the production execute_case path"
    )

# Verify that importing 70.31 did not accidentally undo the proven 70.30
# validation-policy binding.
if legacy.final_run.execute_case.__globals__.get("validate_outputs") is not revision7030._validate_outputs_7030:
    raise RuntimeError(
        "70.31 lost the 70.30 production validate_outputs policy binding"
    )

_original_build_matrix_summary = legacy.build_matrix_summary


def _build_matrix_summary_7031(*args: Any, **kwargs: Any) -> dict[str, Any]:
    payload = _original_build_matrix_summary(*args, **kwargs)
    payload["formalRevision"] = FORMAL_REVISION
    payload["samplingPolicy"] = SAMPLING_POLICY
    payload["fatalIssuePolicy"] = FATAL_POLICY
    payload["formalWindowGuardRevision"] = WINDOW_GUARD_REVISION
    payload["viewerExit92RemainsFatal"] = True
    return payload


legacy.build_matrix_summary = _build_matrix_summary_7031


if __name__ == "__main__":
    raise SystemExit(legacy.main())
