from __future__ import annotations

"""HairSystem Novelty01 formal matrix shim for thesis revision 70.32.

70.32 preserves the 70.31 paired 90-run scientific design and native Viewer
ExitCode 92 integrity rule. It fixes one harness-only false-negative race:
Windows can destroy the normal top-level Viewer window immediately before the
process object transitions to exited. 70.31 treated that normal shutdown gap as
"window-lost-before-process-exit" and could convert a native Viewer success into
harness ExitCode 96.

70.32 allows a bounded 2.0 second grace only after the top-level window has
already disappeared. If the process exits during that grace, normal shutdown is
accepted. If it remains alive, the loss is still a violation. Real client-area
changes, display-size changes, native nonzero exits, and especially native 92
remain fail-closed exactly as before.
"""

import ctypes
import json
import sys
import time
from pathlib import Path
from typing import Any

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import hairgpu_novelty01_matrix_7031 as revision7031  # type: ignore

FORMAL_REVISION = "70.32"
CONTRACT_RELATIVE = Path("docs/experiments/NOVELTY01_FORMAL_CONTRACT_7032.json")
WINDOW_GUARD_REVISION = "fixed-client-window-guard-v2"
WINDOW_GUARD_EVIDENCE_NAME = "formal_window_guard_7032.json"
NORMAL_SHUTDOWN_WINDOW_GRACE_SECONDS = 2.0

# Rebind the inherited 70.31 implementation to the actual 70.32 revision and
# contract. The inherited run_process function resolves these module globals at
# call time, so no production path is bypassed.
revision7031.FORMAL_REVISION = FORMAL_REVISION
revision7031.CONTRACT_RELATIVE = CONTRACT_RELATIVE
revision7031.WINDOW_GUARD_REVISION = WINDOW_GUARD_REVISION
revision7031.WINDOW_GUARD_EVIDENCE_NAME = WINDOW_GUARD_EVIDENCE_NAME
revision7031.revision7030.FORMAL_REVISION = FORMAL_REVISION
revision7031.revision7030.CONTRACT_RELATIVE = CONTRACT_RELATIVE
revision7031.legacy.FORMAL_CONTRACT_RELATIVE = CONTRACT_RELATIVE


class _FormalWindowGuard7032(revision7031._FormalWindowGuard):
    """70.31 guard plus a bounded normal-shutdown window-loss grace."""

    def _process_exited_within_shutdown_grace(self) -> bool:
        deadline = time.monotonic() + NORMAL_SHUTDOWN_WINDOW_GRACE_SECONDS
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                return True
            if self._stop_event.wait(0.025):
                # stop() is called only after run_process has observed process
                # completion, so a stop request also means this is shutdown.
                return self.process.poll() is not None
        return self.process.poll() is not None

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
                        revision7031.EXPECTED_CLIENT_WIDTH,
                        revision7031.EXPECTED_CLIENT_HEIGHT,
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
                    # Normal Win32 shutdown commonly destroys HWND before the
                    # process handle is signalled. Give only that transition a
                    # bounded grace. A persistent live process with no formal
                    # window remains a fail-closed violation.
                    if self._process_exited_within_shutdown_grace():
                        break
                    self._append_violation("window-lost-before-process-exit", None)
                    break

                width, height = self._client_size(user32, self._hwnd)
                if (width, height) != (
                    revision7031.EXPECTED_CLIENT_WIDTH,
                    revision7031.EXPECTED_CLIENT_HEIGHT,
                ):
                    self._append_violation(
                        "client-area-changed",
                        {"width": width, "height": height},
                    )
                    break

                primary_width, primary_height = self._primary_size(user32)
                if (primary_width, primary_height) != (
                    revision7031.EXPECTED_CLIENT_WIDTH,
                    revision7031.EXPECTED_CLIENT_HEIGHT,
                ):
                    self._append_violation(
                        "primary-display-changed",
                        {"width": primary_width, "height": primary_height},
                    )
                    break
        except Exception as exc:
            self._failure_reason = f"Window guard exception: {exc}"

    def stop(self, viewer_return_code: int) -> dict[str, Any]:
        payload = super().stop(viewer_return_code)
        payload["schema"] = "hairgpu-formal-window-guard-v2-7032"
        payload["formalRevision"] = FORMAL_REVISION
        payload["guardRevision"] = WINDOW_GUARD_REVISION
        payload["normalShutdownWindowGraceSeconds"] = (
            NORMAL_SHUTDOWN_WINDOW_GRACE_SECONDS
        )
        payload["normalShutdownWindowLossIsViolation"] = False
        revision7031._write_guard_json(self.evidence_path, payload)
        return payload


# The inherited run_process resolves _FormalWindowGuard from its own module
# globals at call time. Replace exactly that class; do not replace run_process,
# validate_outputs, execute_case, or native Viewer return-code handling.
revision7031._FormalWindowGuard = _FormalWindowGuard7032

# Refresh exact revision-specific summary metadata.
revision7031.legacy.build_matrix_summary = revision7031._build_matrix_summary_7031

_RUNTIME_GLOBALS = revision7031.legacy.final_run.execute_case.__globals__
if _RUNTIME_GLOBALS.get("run_process") is not revision7031._run_process_7031:
    raise RuntimeError("70.32 lost the proven 70.31 production run_process binding")
if _RUNTIME_GLOBALS.get("validate_outputs") is not revision7031.revision7030._validate_outputs_7030:
    raise RuntimeError("70.32 lost the proven 70.30 validation-policy binding")


if __name__ == "__main__":
    raise SystemExit(revision7031.legacy.main())
