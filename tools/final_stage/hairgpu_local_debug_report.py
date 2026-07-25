from __future__ import annotations

"""Post-process Visual Studio local-debug telemetry.

This tool intentionally labels F5/debugger runs as diagnostic evidence.  It creates
portable CSV/JSON/Markdown/SVG artifacts, hashes every file, and can commit only the
run directory without staging unrelated source edits.
"""

import argparse
import csv
import hashlib
import json
import math
import os
import shutil
import statistics
import subprocess
import sys
import time
from datetime import datetime, timezone
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

EVIDENCE_CLASS = "diagnostic_debugger"
SCHEMA_VERSION = "hairgpu-local-debug-evidence-v3"


def _run(
    command: Sequence[str],
    cwd: Path,
    *,
    check: bool = False,
) -> subprocess.CompletedProcess[str]:
    """Run a helper without allowing a missing executable to destroy the report.

    Visual Studio inherits a different PATH from an interactive terminal on many
    Windows installations.  Diagnostic CSV/graphs remain valid even when Git is
    unavailable, so missing delivery tools are represented as return code 127.
    """
    argv = [str(item) for item in command]
    try:
        completed = subprocess.run(
            argv,
            cwd=str(cwd),
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
    except FileNotFoundError as exc:
        completed = subprocess.CompletedProcess(
            argv,
            127,
            stdout=f"executable not found: {argv[0]} ({exc})",
        )
    except OSError as exc:
        completed = subprocess.CompletedProcess(
            argv,
            126,
            stdout=f"failed to launch {argv[0]}: {exc}",
        )
    if check and completed.returncode != 0:
        raise subprocess.CalledProcessError(
            completed.returncode,
            completed.args,
            output=completed.stdout,
        )
    return completed


def _find_git_executable() -> str | None:
    resolved = shutil.which("git")
    if resolved:
        return resolved
    candidates: list[Path] = []
    for key in ("ProgramFiles", "ProgramFiles(x86)"):
        root = os.environ.get(key)
        if root:
            candidates.extend((
                Path(root) / "Git" / "cmd" / "git.exe",
                Path(root) / "Git" / "bin" / "git.exe",
            ))
    local = os.environ.get("LOCALAPPDATA")
    if local:
        candidates.append(Path(local) / "Programs" / "Git" / "cmd" / "git.exe")
    for candidate in candidates:
        if candidate.is_file():
            return str(candidate)
    return None


def _git(repo_root: Path, *args: str, check: bool = False) -> subprocess.CompletedProcess[str]:
    executable = _find_git_executable()
    if executable is None:
        completed = subprocess.CompletedProcess(
            ["git", *args],
            127,
            stdout="Git executable was not found; report generation continues without commit/push.",
        )
        if check:
            raise subprocess.CalledProcessError(
                completed.returncode,
                completed.args,
                output=completed.stdout,
            )
        return completed
    return _run([executable, *args], repo_root, check=check)


def _read_json(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {}
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    return value if isinstance(value, dict) else {}


def _write_json(path: Path, value: Any) -> None:
    path.write_text(
        json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n",
        encoding="utf-8",
    )


def _to_float(value: Any) -> float | None:
    try:
        result = float(value)
    except (TypeError, ValueError):
        return None
    return result if math.isfinite(result) else None


def _to_int(value: Any, default: int = 0) -> int:
    try:
        return int(float(value))
    except (TypeError, ValueError):
        return default


def _column(rows: Sequence[Mapping[str, str]], name: str) -> list[float]:
    values: list[float] = []
    for row in rows:
        value = _to_float(row.get(name))
        if value is not None:
            values.append(value)
    return values


def _percentile(values: Sequence[float], percentile: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    rank = max(0.0, min(100.0, percentile)) / 100.0 * (len(ordered) - 1)
    low = int(math.floor(rank))
    high = int(math.ceil(rank))
    if low == high:
        return ordered[low]
    t = rank - low
    return ordered[low] * (1.0 - t) + ordered[high] * t


def _stats(values: Sequence[float]) -> dict[str, float | int | None]:
    if not values:
        return {
            "count": 0,
            "minimum": None,
            "mean": None,
            "median": None,
            "p95": None,
            "p99": None,
            "maximum": None,
        }
    return {
        "count": len(values),
        "minimum": min(values),
        "mean": statistics.fmean(values),
        "median": statistics.median(values),
        "p95": _percentile(values, 95.0),
        "p99": _percentile(values, 99.0),
        "maximum": max(values),
    }



def _pid_is_running(process_id: int) -> bool:
    """Return whether the watched Viewer process still exists."""

    if process_id <= 0 or process_id == os.getpid():
        return False
    if os.name == "nt":
        import ctypes
        from ctypes import wintypes

        process_query_limited_information = 0x1000
        still_active = 259
        kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
        open_process = kernel32.OpenProcess
        open_process.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
        open_process.restype = wintypes.HANDLE
        get_exit_code = kernel32.GetExitCodeProcess
        get_exit_code.argtypes = (wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD))
        get_exit_code.restype = wintypes.BOOL
        close_handle = kernel32.CloseHandle
        close_handle.argtypes = (wintypes.HANDLE,)
        close_handle.restype = wintypes.BOOL

        handle = open_process(
            process_query_limited_information,
            False,
            int(process_id),
        )
        if not handle:
            return False
        try:
            exit_code = wintypes.DWORD(0)
            return bool(get_exit_code(handle, ctypes.byref(exit_code))) and (
                int(exit_code.value) == still_active
            )
        finally:
            close_handle(handle)

    proc_stat = Path(f"/proc/{int(process_id)}/stat")
    if proc_stat.is_file():
        try:
            fields = proc_stat.read_text(encoding="utf-8", errors="replace").split()
            if len(fields) >= 3 and fields[2] == "Z":
                return False
        except OSError:
            pass
    try:
        os.kill(int(process_id), 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    except OSError:
        return False
    return True


def _wait_for_process_exit(process_id: int, timeout_seconds: float) -> bool:
    """Wait for Visual Studio to terminate or normally close the Viewer."""

    deadline = (
        time.monotonic() + timeout_seconds
        if timeout_seconds > 0.0
        else None
    )
    while _pid_is_running(process_id):
        if deadline is not None and time.monotonic() >= deadline:
            return False
        time.sleep(0.20)
    # Let the filesystem release stream handles before rename/sanitization.
    time.sleep(0.35)
    return True


def _sanitize_partial_csv(partial: Path, final: Path) -> int:
    """Promote only complete CSV rows after a forced process termination."""

    with partial.open("r", encoding="utf-8-sig", newline="") as handle:
        reader = csv.DictReader(handle)
        fieldnames = list(reader.fieldnames or [])
        if not fieldnames:
            raise RuntimeError(f"Partial telemetry has no CSV header: {partial}")
        rows: list[dict[str, str]] = []
        for row in reader:
            if None in row or any(value is None for value in row.values()):
                continue
            if _to_int(row.get("sampleIndex"), -1) < 0:
                continue
            frame_ms = (
                _to_float(row.get("renderFrameMilliseconds"))
                or _to_float(row.get("frameMilliseconds"))
            )
            if frame_ms is None or frame_ms <= 0.0:
                continue
            rows.append({name: str(row.get(name, "")) for name in fieldnames})

    temporary = final.with_suffix(final.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)
        handle.flush()
        os.fsync(handle.fileno())
    temporary.replace(final)
    partial.unlink(missing_ok=True)
    return len(rows)


def _sanitize_partial_jsonl(partial: Path, final: Path) -> int:
    """Discard a trailing torn JSON line while preserving complete samples."""

    valid_lines: list[str] = []
    with partial.open("r", encoding="utf-8-sig") as handle:
        for raw_line in handle:
            line = raw_line.strip()
            if not line:
                continue
            try:
                value = json.loads(line)
            except json.JSONDecodeError:
                continue
            if isinstance(value, dict):
                valid_lines.append(json.dumps(
                    value, ensure_ascii=False, separators=(",", ":"), allow_nan=False
                ))

    temporary = final.with_suffix(final.suffix + ".tmp")
    with temporary.open("w", encoding="utf-8", newline="\n") as handle:
        for line in valid_lines:
            handle.write(line + "\n")
        handle.flush()
        os.fsync(handle.fileno())
    temporary.replace(final)
    partial.unlink(missing_ok=True)
    return len(valid_lines)


def _write_per_second_csv(
    path: Path,
    frame_rows: Sequence[Mapping[str, str]],
) -> list[dict[str, Any]]:
    """Create deterministic one-second buckets when the viewer sidecar is absent."""

    buckets: list[dict[str, Any]] = []
    current: list[Mapping[str, str]] = []
    current_seconds = 0.0
    second_index = 1
    wall_start = 0.0

    def flush(complete: bool) -> None:
        nonlocal current, current_seconds, second_index, wall_start
        if not current:
            return

        frame_ms = [
            value for value in (_to_float(row.get("renderFrameMilliseconds"))
                                or _to_float(row.get("frameMilliseconds"))
                                for row in current)
            if value is not None and value > 0.0
        ]
        duration = max(sum(frame_ms) / 1000.0, 1.0e-6)
        ticks = sum(_to_int(row.get("simulationTicksThisFrame")) for row in current)
        gpu_frame = [
            value for value in (_to_float(row.get("gpuFrameMilliseconds")) for row in current)
            if value is not None and value > 0.0
        ]
        gpu_sim = [
            value for value in (_to_float(row.get("gpuSimulationMilliseconds")) for row in current)
            if value is not None and value > 0.0
        ]
        gpu_draw = [
            value for value in (_to_float(row.get("gpuHairDrawMilliseconds")) for row in current)
            if value is not None and value > 0.0
        ]
        throughput = [
            value for value in (_to_float(row.get("gpuThroughputFps")) for row in current)
            if value is not None and value > 0.0
        ]
        dropped = [
            value for value in (_to_float(row.get("droppedSimulationMilliseconds")) for row in current)
            if value is not None
        ]
        first_dropped = dropped[0] if dropped else 0.0
        last_dropped = dropped[-1] if dropped else 0.0
        dropped_delta = max(0.0, last_dropped - first_dropped)
        render_fps = len(current) / duration
        simulation_hz = ticks / duration

        last = current[-1]
        status = "realtime_60"
        if (
            _to_int(last.get("fatalFailure")) != 0
            or _to_int(last.get("nanStateCount")) != 0
            or _to_int(last.get("infiniteStateCount")) != 0
        ):
            status = "invalid"
        elif simulation_hz < 58.0 or dropped_delta > 1.0:
            status = "simulation_lag"
        elif render_fps < 24.0:
            status = "below_24"
        elif render_fps < 59.0:
            status = "realtime_24"

        row: dict[str, Any] = {
            "secondIndex": second_index,
            "wallTimeStartSeconds": wall_start,
            "wallTimeEndSeconds": wall_start + duration,
            "durationSeconds": duration,
            "completeSecond": 1 if complete else 0,
            "frameCount": len(current),
            "simulationTickCount": ticks,
            "renderFps": render_fps,
            "simulationHz": simulation_hz,
            "meanFrameMilliseconds": statistics.fmean(frame_ms) if frame_ms else 0.0,
            "p95FrameMilliseconds": _percentile(frame_ms, 95.0) or 0.0,
            "maxFrameMilliseconds": max(frame_ms) if frame_ms else 0.0,
            "meanCpuWorkMilliseconds": statistics.fmean(_column(current, "cpuFrameWorkMilliseconds")) if _column(current, "cpuFrameWorkMilliseconds") else 0.0,
            "meanSimulationMilliseconds": statistics.fmean(_column(current, "simulationMilliseconds")) if _column(current, "simulationMilliseconds") else 0.0,
            "meanGpuFrameMilliseconds": statistics.fmean(gpu_frame) if gpu_frame else 0.0,
            "meanGpuSimulationMilliseconds": statistics.fmean(gpu_sim) if gpu_sim else 0.0,
            "meanGpuHairDrawMilliseconds": statistics.fmean(gpu_draw) if gpu_draw else 0.0,
            "gpuTimingCoverage": len(gpu_frame) / max(len(current), 1),
            "meanGpuThroughputFps": statistics.fmean(throughput) if throughput else 0.0,
            "droppedSimulationMillisecondsDelta": dropped_delta,
            "droppedSimulationMillisecondsEnd": last_dropped,
            "nanStateCountEnd": _to_int(last.get("nanStateCount")),
            "infiniteStateCountEnd": _to_int(last.get("infiniteStateCount")),
            "fatalFailureEnd": _to_int(last.get("fatalFailure")),
            "collisionCandidateCountDelta": 0,
            "collisionMissCountDelta": 0,
            "wakeRequestGroupCountDelta": 0,
            "repairAttemptCountDelta": 0,
            "repairSuccessCountDelta": 0,
            "stateTransitionCountDelta": 0,
            "stateOscillationCountDelta": 0,
            "meanStaticCachedGroupCount": statistics.fmean(_column(current, "staticCachedGroupCount")) if _column(current, "staticCachedGroupCount") else 0.0,
            "meanStaticTransformedGroupCount": statistics.fmean(_column(current, "staticTransformedGroupCount")) if _column(current, "staticTransformedGroupCount") else 0.0,
            "meanDynamicGroupCount": statistics.fmean(_column(current, "dynamicGroupCount")) if _column(current, "dynamicGroupCount") else 0.0,
            "meanRepairRequiredGroupCount": statistics.fmean(_column(current, "repairRequiredGroupCount")) if _column(current, "repairRequiredGroupCount") else 0.0,
            "maxFalseStaticRate": max(_column(current, "falseStaticRate"), default=0.0),
            "maxBoundaryErrorMm": max(_column(current, "staticDynamicBoundaryMaxErrorMm"), default=0.0),
            "presentSyncInterval": _to_int(last.get("presentSyncInterval")),
            "evidenceClass": last.get("evidenceClass", EVIDENCE_CLASS),
            "realtimeStatus": status,
        }
        buckets.append(row)
        wall_start += duration
        second_index += 1
        current = []
        current_seconds = 0.0

    for frame in frame_rows:
        frame_ms = (
            _to_float(frame.get("renderFrameMilliseconds"))
            or _to_float(frame.get("frameMilliseconds"))
            or 0.0
        )
        frame_seconds = max(frame_ms, 0.0) / 1000.0
        if current and current_seconds + frame_seconds > 1.0 and current_seconds >= 0.50:
            flush(True)
        current.append(frame)
        current_seconds += frame_seconds

    if current:
        flush(current_seconds >= 0.95)

    if buckets:
        with path.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=list(buckets[0].keys()))
            writer.writeheader()
            writer.writerows(buckets)
    return buckets


def _fmt(value: Any, digits: int = 3) -> str:
    number = _to_float(value)
    return "n/a" if number is None else f"{number:.{digits}f}"


def _escape_xml(text: str) -> str:
    return (
        text.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
        .replace("'", "&apos;")
    )


def _downsample(values: Sequence[float], maximum_points: int = 1200) -> list[float]:
    if len(values) <= maximum_points:
        return list(values)
    step = len(values) / float(maximum_points)
    return [values[min(int(index * step), len(values) - 1)] for index in range(maximum_points)]


def _line_svg(
    path: Path,
    title: str,
    series: Sequence[tuple[str, Sequence[float]]],
    *,
    horizontal_lines: Sequence[tuple[str, float]] = (),
    y_label: str = "",
    x_label: str = "captured frame",
) -> None:
    width, height = 1280, 720
    left, top, right, bottom = 92, 72, 36, 82
    plot_w = width - left - right
    plot_h = height - top - bottom
    sampled = [(name, _downsample(values)) for name, values in series if values]
    all_values = [value for _, values in sampled for value in values]
    all_values.extend(value for _, value in horizontal_lines if math.isfinite(value))
    if not all_values:
        all_values = [0.0, 1.0]
    y_min = min(0.0, min(all_values))
    y_max = max(all_values)
    if y_max <= y_min:
        y_max = y_min + 1.0
    y_pad = max((y_max - y_min) * 0.08, 1.0e-6)
    y_max += y_pad

    palette = ["#2563eb", "#dc2626", "#059669", "#9333ea", "#d97706", "#0891b2"]
    svg: list[str] = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        f'<text x="{left}" y="38" font-family="Segoe UI, sans-serif" font-size="26" font-weight="600">{_escape_xml(title)}</text>',
        f'<rect x="{left}" y="{top}" width="{plot_w}" height="{plot_h}" fill="#fafafa" stroke="#cbd5e1"/>',
    ]
    for tick in range(6):
        fraction = tick / 5.0
        y = top + plot_h * fraction
        value = y_max - (y_max - y_min) * fraction
        svg.append(f'<line x1="{left}" y1="{y:.2f}" x2="{left + plot_w}" y2="{y:.2f}" stroke="#e2e8f0"/>')
        svg.append(f'<text x="{left - 10}" y="{y + 5:.2f}" text-anchor="end" font-family="Consolas, monospace" font-size="14" fill="#475569">{value:.2f}</text>')
    if y_label:
        svg.append(f'<text x="20" y="{top + plot_h / 2:.2f}" transform="rotate(-90 20 {top + plot_h / 2:.2f})" text-anchor="middle" font-family="Segoe UI, sans-serif" font-size="16">{_escape_xml(y_label)}</text>')

    def map_y(value: float) -> float:
        return top + (y_max - value) / (y_max - y_min) * plot_h

    for label, value in horizontal_lines:
        y = map_y(value)
        svg.append(f'<line x1="{left}" y1="{y:.2f}" x2="{left + plot_w}" y2="{y:.2f}" stroke="#64748b" stroke-dasharray="8 7"/>')
        svg.append(f'<text x="{left + plot_w - 6}" y="{y - 6:.2f}" text-anchor="end" font-family="Segoe UI, sans-serif" font-size="13" fill="#334155">{_escape_xml(label)}</text>')

    legend_x = left
    for index, (name, values) in enumerate(sampled):
        color = palette[index % len(palette)]
        count = len(values)
        points: list[str] = []
        for point_index, value in enumerate(values):
            x = left if count <= 1 else left + point_index / (count - 1) * plot_w
            points.append(f"{x:.2f},{map_y(value):.2f}")
        svg.append(f'<polyline points="{" ".join(points)}" fill="none" stroke="{color}" stroke-width="2" vector-effect="non-scaling-stroke"/>')
        svg.append(f'<line x1="{legend_x}" y1="{height - 38}" x2="{legend_x + 28}" y2="{height - 38}" stroke="{color}" stroke-width="4"/>')
        svg.append(f'<text x="{legend_x + 36}" y="{height - 32}" font-family="Segoe UI, sans-serif" font-size="15">{_escape_xml(name)}</text>')
        legend_x += 36 + max(130, len(name) * 9)
    svg.append(f'<text x="{left + plot_w / 2:.2f}" y="{height - 12}" text-anchor="middle" font-family="Segoe UI, sans-serif" font-size="15">{_escape_xml(x_label)}</text>')
    svg.append("</svg>")
    path.write_text("\n".join(svg) + "\n", encoding="utf-8")


def _bar_svg(path: Path, title: str, values: Sequence[tuple[str, float]], y_label: str) -> None:
    width, height = 1280, 720
    left, top, right, bottom = 100, 72, 36, 170
    plot_w = width - left - right
    plot_h = height - top - bottom
    maximum = max([value for _, value in values] + [1.0e-6]) * 1.12
    palette = ["#2563eb", "#059669", "#d97706", "#9333ea", "#dc2626", "#0891b2", "#4f46e5", "#65a30d"]
    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        f'<text x="{left}" y="38" font-family="Segoe UI, sans-serif" font-size="26" font-weight="600">{_escape_xml(title)}</text>',
        f'<rect x="{left}" y="{top}" width="{plot_w}" height="{plot_h}" fill="#fafafa" stroke="#cbd5e1"/>',
    ]
    count = max(len(values), 1)
    slot = plot_w / count
    bar_w = slot * 0.62
    for index, (name, value) in enumerate(values):
        x = left + slot * index + (slot - bar_w) / 2
        bar_h = max(0.0, value / maximum * plot_h)
        y = top + plot_h - bar_h
        color = palette[index % len(palette)]
        svg.append(f'<rect x="{x:.2f}" y="{y:.2f}" width="{bar_w:.2f}" height="{bar_h:.2f}" fill="{color}"/>')
        svg.append(f'<text x="{x + bar_w / 2:.2f}" y="{y - 8:.2f}" text-anchor="middle" font-family="Consolas, monospace" font-size="14">{value:.3f}</text>')
        label_y = top + plot_h + 18
        svg.append(f'<text x="{x + bar_w / 2:.2f}" y="{label_y:.2f}" transform="rotate(35 {x + bar_w / 2:.2f} {label_y:.2f})" text-anchor="start" font-family="Segoe UI, sans-serif" font-size="13">{_escape_xml(name)}</text>')
    svg.append(f'<text x="22" y="{top + plot_h / 2:.2f}" transform="rotate(-90 22 {top + plot_h / 2:.2f})" text-anchor="middle" font-family="Segoe UI, sans-serif" font-size="16">{_escape_xml(y_label)}</text>')
    svg.append("</svg>")
    path.write_text("\n".join(svg) + "\n", encoding="utf-8")


def _sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _git_metadata(repo_root: Path) -> dict[str, Any]:
    executable = _find_git_executable()
    if executable is None:
        return {
            "gitAvailable": False,
            "gitExecutable": "",
            "branch": "",
            "commitBeforeDataCommit": "",
            "origin": "",
            "workingTreeDirty": None,
            "workingTreeChangeCount": None,
            "deliveryWarning": "Git was unavailable; diagnostic artifacts were still generated.",
        }
    branch_result = _git(repo_root, "branch", "--show-current")
    commit_result = _git(repo_root, "rev-parse", "HEAD")
    remote_result = _git(repo_root, "remote", "get-url", "origin")
    status_result = _git(repo_root, "status", "--short")
    status_lines = [
        line for line in status_result.stdout.splitlines()
        if line.strip()
    ] if status_result.returncode == 0 else []
    return {
        "gitAvailable": True,
        "gitExecutable": executable,
        "branch": branch_result.stdout.strip() if branch_result.returncode == 0 else "",
        "commitBeforeDataCommit": commit_result.stdout.strip() if commit_result.returncode == 0 else "",
        "origin": remote_result.stdout.strip() if remote_result.returncode == 0 else "",
        "workingTreeDirty": bool(status_lines) if status_result.returncode == 0 else None,
        "workingTreeChangeCount": len(status_lines) if status_result.returncode == 0 else None,
    }


def _auto_commit(repo_root: Path, run_dir: Path, run_name: str, push: bool) -> dict[str, Any]:
    allowed_prefix = "results/committed/local_debug/"
    try:
        relative = run_dir.resolve().relative_to(repo_root.resolve()).as_posix()
    except ValueError as exc:
        raise RuntimeError(
            f"Refusing to commit a directory outside the repository: {run_dir}"
        ) from exc
    if not relative.startswith(allowed_prefix):
        raise RuntimeError(f"Refusing to commit outside {allowed_prefix}: {relative}")

    if _find_git_executable() is None:
        return {
            "committed": False,
            "pushed": False,
            "pushRequested": push,
            "state": "skipped",
            "reason": "git_unavailable",
        }

    add = _git(repo_root, "add", "-f", "--", relative)
    if add.returncode != 0:
        raise RuntimeError(add.stdout.strip() or "git add failed")
    commit = _git(
        repo_root,
        "commit",
        "--only",
        "-m",
        f"data(local-debug): {run_name}",
        "--",
        relative,
    )
    if commit.returncode != 0:
        text = commit.stdout.strip()
        if "nothing to commit" not in text.lower():
            raise RuntimeError(text or "git commit failed")

    commit_sha = _git(repo_root, "rev-parse", "HEAD", check=True).stdout.strip()
    branch = _git(repo_root, "branch", "--show-current", check=True).stdout.strip()
    result: dict[str, Any] = {
        "committed": True,
        "commitSha": commit_sha,
        "branch": branch,
        "commitOutput": commit.stdout.strip(),
        "pushRequested": push,
        "pushed": False,
    }
    if push:
        if not branch:
            result["pushError"] = "Automatic push is disabled on detached HEAD."
            return result
        pushed = _git(repo_root, "push", "origin", f"HEAD:refs/heads/{branch}")
        result["pushOutput"] = pushed.stdout.strip()
        result["pushed"] = pushed.returncode == 0
        if pushed.returncode != 0:
            result["pushError"] = pushed.stdout.strip() or "git push failed"
    return result


def _recover_partial_outputs(run_dir: Path, run_name: str) -> dict[str, Any]:
    """Promote crash-safe partial streams after Visual Studio killed the Viewer."""

    partial_csv = run_dir / f"{run_name}_frames.partial.csv"
    final_csv = run_dir / f"{run_name}_frames.csv"
    partial_jsonl = run_dir / f"{run_name}_frames.partial.jsonl"
    final_jsonl = run_dir / f"{run_name}_frames.jsonl"
    partial_per_second = run_dir / f"{run_name}_per_second.partial.csv"
    final_per_second = run_dir / f"{run_name}_per_second.csv"

    promoted: list[str] = []
    recovered_row_count = 0
    if partial_csv.is_file():
        recovered_row_count = _sanitize_partial_csv(partial_csv, final_csv)
        promoted.append(final_csv.name)
    if partial_jsonl.is_file():
        _sanitize_partial_jsonl(partial_jsonl, final_jsonl)
        promoted.append(final_jsonl.name)

    # The per-second sidecar may be one flush behind at termination.  Regenerate
    # it from sanitized frame rows instead of promoting potentially stale data.
    partial_per_second.unlink(missing_ok=True)
    final_per_second.unlink(missing_ok=True)

    if not final_csv.is_file():
        raise FileNotFoundError(
            f"Interrupted run has no recoverable frame stream: {final_csv}"
        )
    if recovered_row_count <= 0:
        with final_csv.open("r", encoding="utf-8-sig", newline="") as handle:
            recovered_row_count = sum(1 for _ in csv.DictReader(handle))
    if recovered_row_count <= 0:
        raise RuntimeError(
            "The Viewer was stopped before the first post-warmup frame was captured."
        )

    state = _read_json(run_dir / "run_state.json")
    state.update({
        "schemaVersion": "hairgpu-local-debug-run-state-v1",
        "status": "INTERRUPTED",
        "exitMode": "visual_studio_stop_debugging",
        "formalEvidence": False,
        "partialDataRecovered": True,
        "capturedFrameCount": recovered_row_count,
        "recoveredAtUtc": datetime.now(timezone.utc).isoformat(),
        "promotedFiles": promoted,
    })
    _write_json(run_dir / "run_state.json", state)
    return state


@dataclass(frozen=True)
class ReportInputs:
    repo_root: Path
    run_dir: Path
    run_name: str
    viewer_exit_code: int
    auto_commit: bool
    push: bool


def build_report(inputs: ReportInputs) -> dict[str, Any]:
    run_dir = inputs.run_dir.resolve()
    run_dir.mkdir(parents=True, exist_ok=True)
    frames_path = run_dir / f"{inputs.run_name}_frames.csv"
    if not frames_path.is_file():
        raise FileNotFoundError(f"Telemetry CSV was not produced: {frames_path}")
    with frames_path.open("r", encoding="utf-8-sig", newline="") as handle:
        rows = list(csv.DictReader(handle))
    if not rows:
        raise RuntimeError("Telemetry CSV contains no captured rows.")

    per_second_path = run_dir / f"{inputs.run_name}_per_second.csv"
    if per_second_path.is_file():
        with per_second_path.open("r", encoding="utf-8-sig", newline="") as handle:
            per_second_rows = list(csv.DictReader(handle))
    else:
        per_second_rows = _write_per_second_csv(per_second_path, rows)

    frame_ms = _column(rows, "frameMilliseconds")
    sim_ms = _column(rows, "simulationMilliseconds")
    gpu_ms = _column(rows, "gpuHairMilliseconds")
    fps = _column(rows, "fps")
    total_hair_gpu = _column(rows, "totalHairGpuMilliseconds")
    static_cached = _column(rows, "staticCachedGroupCount")
    static_transformed = _column(rows, "staticTransformedGroupCount")
    dynamic = _column(rows, "dynamicGroupCount")
    repair = _column(rows, "repairRequiredGroupCount")
    boundary_error = _column(rows, "staticDynamicBoundaryMaxErrorMm")
    false_static_rate = _column(rows, "falseStaticRate")
    per_second_render_fps = _column(per_second_rows, "renderFps")
    per_second_simulation_hz = _column(per_second_rows, "simulationHz")
    per_second_gpu_throughput_fps = _column(per_second_rows, "meanGpuThroughputFps")
    per_second_dropped_ms = _column(per_second_rows, "droppedSimulationMillisecondsDelta")
    per_second_status_counts: dict[str, int] = {}
    for row in per_second_rows:
        status = str(row.get("realtimeStatus", "unknown"))
        per_second_status_counts[status] = per_second_status_counts.get(status, 0) + 1

    context = _read_json(run_dir / "capture_context.json")
    run_state = _read_json(run_dir / "run_state.json")
    native_summary = _read_json(run_dir / f"{inputs.run_name}_summary.json")
    native_settings = _read_json(run_dir / f"{inputs.run_name}_settings.json")
    git_info = _git_metadata(inputs.repo_root)

    gpu_columns = [
        ("block classify", "blockClassificationGpuMilliseconds"),
        ("group classify", "groupClassificationGpuMilliseconds"),
        ("compaction", "compactionGpuMilliseconds"),
        ("static transform", "staticTransformGpuMilliseconds"),
        ("dynamic solve", "dynamicSolveGpuMilliseconds"),
        ("boundary repair", "boundaryRepairGpuMilliseconds"),
        ("watchdog", "watchdogGpuMilliseconds"),
        ("follower reconstruction", "followerReconstructionGpuMilliseconds"),
        ("hair draw", "hairDrawGpuMilliseconds"),
    ]
    gpu_pass_means: list[tuple[str, float]] = []
    for label, column_name in gpu_columns:
        values = _column(rows, column_name)
        if values:
            gpu_pass_means.append((label, statistics.fmean(values)))

    nonzero_gpu_samples = sum(1 for value in gpu_ms if value > 0.0)
    summary: dict[str, Any] = {
        "schemaVersion": SCHEMA_VERSION,
        "evidenceClass": EVIDENCE_CLASS,
        "formalEvidence": False,
        "warning": "Visual Studio debugger capture is diagnostic and must not be used as formal paper evidence.",
        "runName": inputs.run_name,
        "viewerExitCode": inputs.viewer_exit_code,
        "capturedFrameCount": len(rows),
        "runState": run_state,
        "interrupted": run_state.get("status") == "INTERRUPTED",
        "partialDataRecovered": bool(run_state.get("partialDataRecovered", False)),
        "frameMilliseconds": _stats(frame_ms),
        "simulationMilliseconds": _stats(sim_ms),
        "gpuHairMilliseconds": _stats(gpu_ms),
        "totalHairGpuMilliseconds": _stats(total_hair_gpu),
        "fps": _stats(fps),
        "perSecondSampleCount": len(per_second_rows),
        "perSecondRenderFps": _stats(per_second_render_fps),
        "perSecondSimulationHz": _stats(per_second_simulation_hz),
        "perSecondGpuThroughputFps": _stats(per_second_gpu_throughput_fps),
        "perSecondDroppedSimulationMilliseconds": _stats(per_second_dropped_ms),
        "perSecondRealtimeStatusCounts": per_second_status_counts,
        "gpuTimingNonzeroSampleCount": nonzero_gpu_samples,
        "gpuTimingNonzeroCoverage": nonzero_gpu_samples / max(len(rows), 1),
        "staticCachedGroups": _stats(static_cached),
        "staticTransformedGroups": _stats(static_transformed),
        "dynamicGroups": _stats(dynamic),
        "repairRequiredGroups": _stats(repair),
        "maximumBoundaryErrorMm": max(boundary_error) if boundary_error else None,
        "maximumFalseStaticRate": max(false_static_rate) if false_static_rate else None,
        "novelty01Interpretation": {
            "preNoveltyComparable": str(rows[0].get("variant", "")).upper() in {"B0", "B1", "B2", "B3", "B4", "B5", "B6", "B7", "B8"},
            "proposalComparable": str(rows[0].get("variant", "")).upper() == "P",
            "pointLinePointGroupMetricsPresent": any(
                name in rows[0]
                for name in (
                    "staticCachedGroupCount",
                    "dynamicGroupCount",
                    "repairRequiredGroupCount",
                    "sharedPointConflictCount",
                )
            ),
        },
        "git": git_info,
        "captureContext": context,
        "nativeSummary": native_summary,
        "nativeSettings": native_settings,
    }

    _line_svg(
        run_dir / "frame_time.svg",
        "Frame and GPU time (Visual Studio diagnostic capture)",
        [("frame ms", frame_ms), ("simulation ms", sim_ms), ("GPU hair ms", gpu_ms)],
        horizontal_lines=(("60 FPS budget", 1000.0 / 60.0), ("24 FPS baseline budget", 1000.0 / 24.0)),
        y_label="milliseconds",
    )
    _line_svg(
        run_dir / "per_second_realtime.svg",
        "Per-second realtime status",
        [
            ("render FPS", per_second_render_fps),
            ("simulation Hz", per_second_simulation_hz),
            ("GPU throughput FPS", per_second_gpu_throughput_fps),
        ],
        horizontal_lines=(("60 Hz target", 60.0), ("24 FPS baseline gate", 24.0)),
        y_label="frames or ticks per second",
        x_label="second",
    )
    _line_svg(
        run_dir / "segment_group_state.svg",
        "Novelty 01 segment-group state counts",
        [
            ("static cached", static_cached),
            ("static transformed", static_transformed),
            ("dynamic", dynamic),
            ("repair required", repair),
        ],
        y_label="group count",
    )
    _bar_svg(
        run_dir / "gpu_pass_breakdown.svg",
        "Mean GPU pass cost",
        gpu_pass_means,
        "milliseconds",
    )

    with (run_dir / "summary_metrics.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["metric", "count", "minimum", "mean", "median", "p95", "p99", "maximum"])
        for label, key in (
            ("frameMilliseconds", "frameMilliseconds"),
            ("simulationMilliseconds", "simulationMilliseconds"),
            ("gpuHairMilliseconds", "gpuHairMilliseconds"),
            ("fps", "fps"),
            ("staticCachedGroups", "staticCachedGroups"),
            ("dynamicGroups", "dynamicGroups"),
            ("repairRequiredGroups", "repairRequiredGroups"),
        ):
            item = summary[key]
            writer.writerow([
                label,
                item["count"],
                item["minimum"],
                item["mean"],
                item["median"],
                item["p95"],
                item["p99"],
                item["maximum"],
            ])

    report = f"""# HairSystem local debugger capture

> **Evidence class:** `{EVIDENCE_CLASS}`  
> This F5/local-debugger result is diagnostic only. Formal paper evidence must be produced by `Run_Final_Runtime_Suite.bat` with the frozen asset and strict environment gate.

## Run identity

- Run: `{inputs.run_name}`
- Captured frames: {len(rows)}
- Viewer exit code: {inputs.viewer_exit_code}
- Branch: `{git_info.get('branch', '')}`
- Source commit before data commit: `{git_info.get('commitBeforeDataCommit', '')}`
- Git available: `{git_info.get('gitAvailable', False)}`
- Working tree dirty at report time: `{git_info.get('workingTreeDirty')}`

## Performance

| Metric | Mean | p95 | p99 | Maximum |
|---|---:|---:|---:|---:|
| Frame time (ms) | {_fmt(summary['frameMilliseconds']['mean'])} | {_fmt(summary['frameMilliseconds']['p95'])} | {_fmt(summary['frameMilliseconds']['p99'])} | {_fmt(summary['frameMilliseconds']['maximum'])} |
| Simulation (ms) | {_fmt(summary['simulationMilliseconds']['mean'])} | {_fmt(summary['simulationMilliseconds']['p95'])} | {_fmt(summary['simulationMilliseconds']['p99'])} | {_fmt(summary['simulationMilliseconds']['maximum'])} |
| GPU hair (ms) | {_fmt(summary['gpuHairMilliseconds']['mean'])} | {_fmt(summary['gpuHairMilliseconds']['p95'])} | {_fmt(summary['gpuHairMilliseconds']['p99'])} | {_fmt(summary['gpuHairMilliseconds']['maximum'])} |
| FPS | {_fmt(summary['fps']['mean'])} | {_fmt(summary['fps']['p95'])} | {_fmt(summary['fps']['p99'])} | {_fmt(summary['fps']['maximum'])} |

GPU timing non-zero coverage: `{summary['gpuTimingNonzeroCoverage']:.3%}`

## Per-second realtime evidence

| Metric | Mean | Minimum | p95 | Maximum |
|---|---:|---:|---:|---:|
| Render FPS | {_fmt(summary['perSecondRenderFps']['mean'])} | {_fmt(summary['perSecondRenderFps']['minimum'])} | {_fmt(summary['perSecondRenderFps']['p95'])} | {_fmt(summary['perSecondRenderFps']['maximum'])} |
| Simulation Hz | {_fmt(summary['perSecondSimulationHz']['mean'])} | {_fmt(summary['perSecondSimulationHz']['minimum'])} | {_fmt(summary['perSecondSimulationHz']['p95'])} | {_fmt(summary['perSecondSimulationHz']['maximum'])} |
| GPU throughput FPS | {_fmt(summary['perSecondGpuThroughputFps']['mean'])} | {_fmt(summary['perSecondGpuThroughputFps']['minimum'])} | {_fmt(summary['perSecondGpuThroughputFps']['p95'])} | {_fmt(summary['perSecondGpuThroughputFps']['maximum'])} |
| Dropped simulation ms | {_fmt(summary['perSecondDroppedSimulationMilliseconds']['mean'])} | {_fmt(summary['perSecondDroppedSimulationMilliseconds']['minimum'])} | {_fmt(summary['perSecondDroppedSimulationMilliseconds']['p95'])} | {_fmt(summary['perSecondDroppedSimulationMilliseconds']['maximum'])} |

Realtime-status counts: `{summary['perSecondRealtimeStatusCounts']}`

## Novelty 01 diagnostics

| Metric | Mean | Maximum |
|---|---:|---:|
| Static cached groups | {_fmt(summary['staticCachedGroups']['mean'], 1)} | {_fmt(summary['staticCachedGroups']['maximum'], 1)} |
| Static transformed groups | {_fmt(summary['staticTransformedGroups']['mean'], 1)} | {_fmt(summary['staticTransformedGroups']['maximum'], 1)} |
| Dynamic groups | {_fmt(summary['dynamicGroups']['mean'], 1)} | {_fmt(summary['dynamicGroups']['maximum'], 1)} |
| Repair-required groups | {_fmt(summary['repairRequiredGroups']['mean'], 1)} | {_fmt(summary['repairRequiredGroups']['maximum'], 1)} |
| Static/dynamic boundary max error (mm) | n/a | {_fmt(summary['maximumBoundaryErrorMm'])} |
| False-static rate | n/a | {_fmt(summary['maximumFalseStaticRate'], 6)} |

## Graphs

- [Frame and GPU time](frame_time.svg)
- [Per-second realtime status](per_second_realtime.svg)
- [Segment-group states](segment_group_state.svg)
- [GPU pass breakdown](gpu_pass_breakdown.svg)

## Interpretation

The proposed Novelty 01 unit is a point-line-point segment group. Static groups reuse their last valid solved result and are not recomputed until a head, collision, edit, boundary, or safety event wakes them. Dynamic groups are compacted and solved on the GPU. Shared intermediate points require directional/hemisphere ownership, and unsafe reuse must become repair or fallback rather than false-static acceptance.
"""
    (run_dir / "report.md").write_text(report, encoding="utf-8")
    _write_json(run_dir / "run_manifest.json", summary)

    _write_sha256_manifest(run_dir)

    return summary


def _write_sha256_manifest(run_dir: Path) -> None:
    """Hash every completed diagnostic artifact except the hash list itself."""
    files = sorted(
        path
        for path in Path(run_dir).iterdir()
        if path.is_file() and path.name != "files.sha256"
    )
    temporary = Path(run_dir) / "files.sha256.tmp"
    with temporary.open("w", encoding="utf-8", newline="\n") as handle:
        for path in files:
            if path == temporary:
                continue
            handle.write(f"{_sha256(path)}  {path.name}\n")
        handle.flush()
        os.fsync(handle.fileno())
    temporary.replace(Path(run_dir) / "files.sha256")


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--run-dir", type=Path, required=True)
    parser.add_argument("--run-name", required=True)
    parser.add_argument("--viewer-exit-code", type=int, default=0)
    parser.add_argument("--auto-commit", action="store_true")
    parser.add_argument("--push", action="store_true")
    parser.add_argument("--recover-partial", action="store_true")
    parser.add_argument("--watch-pid", type=int, default=0)
    parser.add_argument("--watch-timeout-seconds", type=float, default=0.0)
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)

    # HAIRGPU_F5_DETACHED_WATCHER_V1: this Python process is launched outside
    # the Viewer debugger job.  It waits for Visual Studio Stop Debugging, then
    # finalizes the partial streams without requiring another F5 launch.
    if int(args.watch_pid) > 0:
        if not _wait_for_process_exit(
            int(args.watch_pid),
            max(0.0, float(args.watch_timeout_seconds)),
        ):
            args.run_dir.mkdir(parents=True, exist_ok=True)
            _write_json(args.run_dir / "postprocess_status.json", {
                "schemaVersion": SCHEMA_VERSION,
                "success": False,
                "watcherTimedOut": True,
                "watchedProcessId": int(args.watch_pid),
            })
            return 4

        existing_status = _read_json(args.run_dir / "postprocess_status.json")
        existing_state = _read_json(args.run_dir / "run_state.json")
        if existing_status.get("success") is True or existing_state.get("status") == "COMPLETE":
            return 0

        partial_frames = args.run_dir / f"{args.run_name}_frames.partial.csv"
        final_frames = args.run_dir / f"{args.run_name}_frames.csv"
        if partial_frames.is_file():
            args.recover_partial = True
        elif final_frames.is_file():
            args.recover_partial = False
        else:
            existing_state.update({
                "schemaVersion": "hairgpu-local-debug-run-state-v1",
                "status": "INTERRUPTED",
                "exitMode": "visual_studio_stop_debugging_before_first_sample",
                "formalEvidence": False,
                "partialDataRecovered": False,
                "capturedFrameCount": 0,
            })
            _write_json(args.run_dir / "run_state.json", existing_state)
            _write_json(args.run_dir / "postprocess_status.json", {
                "schemaVersion": SCHEMA_VERSION,
                "success": False,
                "recoveredPartial": False,
                "error": "No post-warmup frame was captured before Visual Studio stopped the Viewer.",
                "watchedProcessId": int(args.watch_pid),
            })
            return 3

    inputs = ReportInputs(
        repo_root=args.repo_root.resolve(),
        run_dir=args.run_dir.resolve(),
        run_name=str(args.run_name),
        viewer_exit_code=int(args.viewer_exit_code),
        auto_commit=bool(args.auto_commit),
        push=bool(args.push),
    )
    try:
        if args.recover_partial:
            _recover_partial_outputs(inputs.run_dir, inputs.run_name)
        build_report(inputs)
        if not args.recover_partial:
            state = _read_json(inputs.run_dir / "run_state.json")
            state.update({
                "schemaVersion": "hairgpu-local-debug-run-state-v1",
                "status": "COMPLETE",
                "exitMode": "normal_viewer_return",
                "formalEvidence": False,
                "partialDataRecovered": False,
            })
            _write_json(inputs.run_dir / "run_state.json", state)
        git_available = _find_git_executable() is not None
        delivery: dict[str, Any] = {
            "requested": inputs.auto_commit,
            "pushRequested": inputs.push,
            "gitAvailable": git_available,
            "state": (
                "ready"
                if inputs.auto_commit and git_available
                else "skipped_git_unavailable"
                if inputs.auto_commit
                else "not_requested"
            ),
            "note": (
                "The final commit SHA is intentionally obtained from Git history; "
                "embedding a commit SHA inside the committed file would be self-referential."
            ),
        }
        # Only a successful, fully generated run directory reaches this point.  The
        # status file is written before commit so failed post-processing can never be
        # staged or pushed automatically.
        _write_json(inputs.run_dir / "postprocess_status.json", {
            "schemaVersion": SCHEMA_VERSION,
            "success": True,
            "recoveredPartial": bool(args.recover_partial),
            "delivery": delivery,
        })
        # The status is part of the committed evidence directory, so refresh the
        # integrity list after writing it and before any automatic commit.
        _write_sha256_manifest(inputs.run_dir)
        if inputs.auto_commit:
            result = _auto_commit(
                inputs.repo_root,
                inputs.run_dir,
                inputs.run_name,
                inputs.push,
            )
            print(json.dumps(result, ensure_ascii=False))
        return 0
    except Exception as exc:
        inputs.run_dir.mkdir(parents=True, exist_ok=True)
        message = f"{type(exc).__name__}: {exc}"
        (inputs.run_dir / "POSTPROCESS_ERROR.txt").write_text(message + "\n", encoding="utf-8")
        _write_json(inputs.run_dir / "postprocess_status.json", {
            "schemaVersion": SCHEMA_VERSION,
            "success": False,
            "error": message,
        })
        print(message, file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
