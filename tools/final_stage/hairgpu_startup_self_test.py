#!/usr/bin/env python3
"""Repeat the real HairSystem Viewer startup and graceful shutdown path.

This test launches the normal GUI executable without self-test shortcuts. It waits
for the top-level window, lets the real message/render loop run for a fixed period,
then sends WM_CLOSE and requires a clean zero exit code.
"""

from __future__ import annotations

import argparse
import ctypes
from ctypes import wintypes
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
from typing import Any


WM_CLOSE = 0x0010
GW_OWNER = 4
HWND_BOTTOM = 1
SWP_NOSIZE = 0x0001
SWP_NOACTIVATE = 0x0010
SWP_NOOWNERZORDER = 0x0200


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json_atomic(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(
        json.dumps(payload, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    temporary.replace(path)


def configure_user32() -> Any:
    user32 = ctypes.WinDLL("user32", use_last_error=True)
    user32.EnumWindows.argtypes = [
        ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM),
        wintypes.LPARAM,
    ]
    user32.EnumWindows.restype = wintypes.BOOL
    user32.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user32.GetWindowThreadProcessId.restype = wintypes.DWORD
    user32.GetWindow.argtypes = [wintypes.HWND, ctypes.c_uint]
    user32.GetWindow.restype = wintypes.HWND
    user32.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user32.PostMessageW.restype = wintypes.BOOL
    user32.SetWindowPos.argtypes = [
        wintypes.HWND,
        wintypes.HWND,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        wintypes.UINT,
    ]
    user32.SetWindowPos.restype = wintypes.BOOL
    return user32


def find_top_level_window(user32: Any, process_id: int) -> int | None:
    handles: list[int] = []
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)

    @callback_type
    def callback(hwnd: int, _lparam: int) -> bool:
        owner = user32.GetWindow(hwnd, GW_OWNER)
        if owner:
            return True
        observed_process_id = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(observed_process_id))
        if observed_process_id.value == process_id:
            handles.append(int(hwnd))
            return False
        return True

    user32.EnumWindows(callback, 0)
    return handles[0] if handles else None


def terminate_process_tree(process: subprocess.Popen[Any]) -> None:
    if process.poll() is not None:
        return
    subprocess.run(
        ["taskkill", "/PID", str(process.pid), "/T", "/F"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)


def run_repetition(
    user32: Any,
    viewer: Path,
    index: int,
    startup_timeout: float,
    run_seconds: float,
    shutdown_timeout: float,
) -> dict[str, Any]:
    started_at = utc_now()
    process = subprocess.Popen(
        [str(viewer)],
        cwd=str(viewer.parent),
        stdin=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        creationflags=getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0),
    )
    result: dict[str, Any] = {
        "repetition": index,
        "processId": process.pid,
        "startedAtUtc": started_at,
        "windowCreated": False,
        "gracefulClosePosted": False,
        "exitCode": None,
        "passed": False,
        "error": "",
    }
    try:
        startup_deadline = time.monotonic() + startup_timeout
        hwnd: int | None = None
        while time.monotonic() < startup_deadline:
            exit_code = process.poll()
            if exit_code is not None:
                raise RuntimeError(f"Viewer exited before creating a top-level window: {exit_code}")
            hwnd = find_top_level_window(user32, process.pid)
            if hwnd:
                break
            time.sleep(0.05)
        if not hwnd:
            raise TimeoutError("Timed out waiting for the Viewer top-level window.")

        result["windowCreated"] = True
        result["windowHandle"] = hwnd

        # Keep the real window/render path active while moving the test window
        # outside the visible desktop so 20 repetitions do not interrupt work.
        user32.SetWindowPos(
            hwnd,
            HWND_BOTTOM,
            -32000,
            -32000,
            0,
            0,
            SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER,
        )

        run_deadline = time.monotonic() + run_seconds
        while time.monotonic() < run_deadline:
            exit_code = process.poll()
            if exit_code is not None:
                raise RuntimeError(f"Viewer exited during the startup observation interval: {exit_code}")
            time.sleep(0.05)

        if not user32.PostMessageW(hwnd, WM_CLOSE, 0, 0):
            raise ctypes.WinError(ctypes.get_last_error())
        result["gracefulClosePosted"] = True

        try:
            exit_code = process.wait(timeout=shutdown_timeout)
        except subprocess.TimeoutExpired as exc:
            raise TimeoutError("Viewer did not terminate after WM_CLOSE.") from exc
        result["exitCode"] = exit_code
        if exit_code != 0:
            raise RuntimeError(f"Viewer returned a non-zero exit code: {exit_code}")
        result["passed"] = True
    except Exception as exc:  # noqa: BLE001 - preserve exact test failure
        result["error"] = str(exc)
        terminate_process_tree(process)
        result["exitCode"] = process.poll()
    finally:
        result["finishedAtUtc"] = utc_now()
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--viewer", required=True, type=Path)
    parser.add_argument("--repetitions", type=int, default=20)
    parser.add_argument("--startup-timeout", type=float, default=120.0)
    parser.add_argument("--run-seconds", type=float, default=3.0)
    parser.add_argument("--shutdown-timeout", type=float, default=30.0)
    parser.add_argument("--report", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    viewer = args.viewer.resolve()
    report_path = (
        args.report.resolve()
        if args.report
        else viewer.parent / "startup20_summary.json"
    )
    summary: dict[str, Any] = {
        "schemaVersion": "hairgpu-startup-self-test-v1",
        "startedAtUtc": utc_now(),
        "viewer": str(viewer),
        "viewerSha256": "",
        "requestedRepetitions": args.repetitions,
        "completedRepetitions": 0,
        "runSecondsPerRepetition": args.run_seconds,
        "passed": False,
        "runs": [],
    }

    if sys.platform != "win32":
        summary["error"] = "The startup self-test requires Windows."
        write_json_atomic(report_path, summary)
        print(summary["error"], file=sys.stderr)
        return 2
    if not viewer.is_file():
        summary["error"] = f"Viewer executable was not found: {viewer}"
        write_json_atomic(report_path, summary)
        print(summary["error"], file=sys.stderr)
        return 3
    if args.repetitions <= 0 or args.startup_timeout <= 0 or args.run_seconds <= 0 or args.shutdown_timeout <= 0:
        summary["error"] = "All repetition and timeout values must be positive."
        write_json_atomic(report_path, summary)
        print(summary["error"], file=sys.stderr)
        return 4

    summary["viewerSha256"] = sha256_file(viewer)
    user32 = configure_user32()
    for index in range(1, args.repetitions + 1):
        print(f"Startup repetition {index}/{args.repetitions}", flush=True)
        run = run_repetition(
            user32,
            viewer,
            index,
            args.startup_timeout,
            args.run_seconds,
            args.shutdown_timeout,
        )
        summary["runs"].append(run)
        summary["completedRepetitions"] = index
        write_json_atomic(report_path, summary)
        if not run["passed"]:
            summary["error"] = f"Repetition {index} failed: {run['error']}"
            summary["finishedAtUtc"] = utc_now()
            write_json_atomic(report_path, summary)
            print(summary["error"], file=sys.stderr)
            return 10 + index

    summary["passed"] = True
    summary["finishedAtUtc"] = utc_now()
    write_json_atomic(report_path, summary)
    print(f"All {args.repetitions} real Viewer startup/shutdown repetitions passed.")
    print(f"Report: {report_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
