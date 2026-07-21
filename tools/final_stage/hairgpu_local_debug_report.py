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
import statistics
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Mapping, Sequence

EVIDENCE_CLASS = "diagnostic_debugger"
SCHEMA_VERSION = "hairgpu-local-debug-evidence-v1"


def _run(command: Sequence[str], cwd: Path, *, check: bool = False) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        list(command),
        cwd=str(cwd),
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=check,
    )


def _git(repo_root: Path, *args: str, check: bool = False) -> subprocess.CompletedProcess[str]:
    return _run(["git", *args], repo_root, check=check)


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
    svg.append(f'<text x="{left + plot_w / 2:.2f}" y="{height - 12}" text-anchor="middle" font-family="Segoe UI, sans-serif" font-size="15">captured frame</text>')
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
    branch = _git(repo_root, "branch", "--show-current").stdout.strip()
    commit = _git(repo_root, "rev-parse", "HEAD").stdout.strip()
    remote = _git(repo_root, "remote", "get-url", "origin").stdout.strip()
    status_lines = [line for line in _git(repo_root, "status", "--short").stdout.splitlines() if line.strip()]
    return {
        "branch": branch,
        "commitBeforeDataCommit": commit,
        "origin": remote,
        "workingTreeDirty": bool(status_lines),
        "workingTreeChangeCount": len(status_lines),
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
            raise RuntimeError("Automatic push is disabled on detached HEAD.")
        pushed = _git(repo_root, "push", "origin", f"HEAD:refs/heads/{branch}")
        result["pushOutput"] = pushed.stdout.strip()
        result["pushed"] = pushed.returncode == 0
        if pushed.returncode != 0:
            raise RuntimeError(pushed.stdout.strip() or "git push failed")
    return result


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

    context = _read_json(run_dir / "capture_context.json")
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
        "frameMilliseconds": _stats(frame_ms),
        "simulationMilliseconds": _stats(sim_ms),
        "gpuHairMilliseconds": _stats(gpu_ms),
        "totalHairGpuMilliseconds": _stats(total_hair_gpu),
        "fps": _stats(fps),
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
- Working tree dirty at report time: `{git_info.get('workingTreeDirty', False)}`

## Performance

| Metric | Mean | p95 | p99 | Maximum |
|---|---:|---:|---:|---:|
| Frame time (ms) | {_fmt(summary['frameMilliseconds']['mean'])} | {_fmt(summary['frameMilliseconds']['p95'])} | {_fmt(summary['frameMilliseconds']['p99'])} | {_fmt(summary['frameMilliseconds']['maximum'])} |
| Simulation (ms) | {_fmt(summary['simulationMilliseconds']['mean'])} | {_fmt(summary['simulationMilliseconds']['p95'])} | {_fmt(summary['simulationMilliseconds']['p99'])} | {_fmt(summary['simulationMilliseconds']['maximum'])} |
| GPU hair (ms) | {_fmt(summary['gpuHairMilliseconds']['mean'])} | {_fmt(summary['gpuHairMilliseconds']['p95'])} | {_fmt(summary['gpuHairMilliseconds']['p99'])} | {_fmt(summary['gpuHairMilliseconds']['maximum'])} |
| FPS | {_fmt(summary['fps']['mean'])} | {_fmt(summary['fps']['p95'])} | {_fmt(summary['fps']['p99'])} | {_fmt(summary['fps']['maximum'])} |

GPU timing non-zero coverage: `{summary['gpuTimingNonzeroCoverage']:.3%}`

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
- [Segment-group states](segment_group_state.svg)
- [GPU pass breakdown](gpu_pass_breakdown.svg)

## Interpretation

The proposed Novelty 01 unit is a point-line-point segment group. Static groups reuse their last valid solved result and are not recomputed until a head, collision, edit, boundary, or safety event wakes them. Dynamic groups are compacted and solved on the GPU. Shared intermediate points require directional/hemisphere ownership, and unsafe reuse must become repair or fallback rather than false-static acceptance.
"""
    (run_dir / "report.md").write_text(report, encoding="utf-8")
    _write_json(run_dir / "run_manifest.json", summary)

    files = sorted(
        path
        for path in run_dir.iterdir()
        if path.is_file() and path.name != "files.sha256"
    )
    with (run_dir / "files.sha256").open("w", encoding="utf-8", newline="\n") as handle:
        for path in files:
            handle.write(f"{_sha256(path)}  {path.name}\n")

    return summary


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo-root", type=Path, required=True)
    parser.add_argument("--run-dir", type=Path, required=True)
    parser.add_argument("--run-name", required=True)
    parser.add_argument("--viewer-exit-code", type=int, default=0)
    parser.add_argument("--auto-commit", action="store_true")
    parser.add_argument("--push", action="store_true")
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    inputs = ReportInputs(
        repo_root=args.repo_root.resolve(),
        run_dir=args.run_dir.resolve(),
        run_name=str(args.run_name),
        viewer_exit_code=int(args.viewer_exit_code),
        auto_commit=bool(args.auto_commit),
        push=bool(args.push),
    )
    try:
        build_report(inputs)
        # Write the status before committing so the committed run directory is
        # self-contained. The resulting commit SHA can be queried from Git history.
        commit_result: dict[str, Any] = {
            "requested": inputs.auto_commit,
            "pushRequested": inputs.push,
            "state": "pending" if inputs.auto_commit else "not_requested",
        }
        _write_json(inputs.run_dir / "postprocess_status.json", {
            "schemaVersion": SCHEMA_VERSION,
            "success": True,
            "commit": commit_result,
        })
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
