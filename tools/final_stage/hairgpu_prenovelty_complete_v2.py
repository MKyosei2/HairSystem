#!/usr/bin/env python3
"""Strict P15 pre-novelty acceptance, analysis, and evidence packaging."""
from __future__ import annotations

import argparse, hashlib, html, json, os, sqlite3, subprocess, tempfile, time, zipfile
from pathlib import Path
from typing import Any, Mapping

SCHEMA_VERSION = "hairgpu-prenovelty-complete-v2"
PHASES = ("smoke", "startup20", "coverage18", "determinism", "soak30")
STANDARD_PHASES = ("smoke", "coverage18", "determinism", "soak30")
BAD_MARKERS = ("*_ERROR.json", "*_INCOMPLETE.json", "*_RUNNING.json", "*.run.lock")


def now() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8-sig"))
    if not isinstance(value, dict):
        raise ValueError(f"Expected JSON object: {path}")
    return value


def write(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, name = tempfile.mkstemp(prefix=path.name + ".tmp.", dir=path.parent)
    tmp = Path(name)
    try:
        with os.fdopen(fd, "w", encoding="utf-8", newline="") as stream:
            stream.write(text); stream.flush(); os.fsync(stream.fileno())
        os.replace(tmp, path)
    finally:
        tmp.unlink(missing_ok=True)


def write_json(path: Path, value: Mapping[str, Any]) -> None:
    write(path, json.dumps(dict(value), ensure_ascii=False, indent=2, allow_nan=False) + "\n")


def canonical(value: Any) -> str:
    raw = json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()
    return hashlib.sha256(raw).hexdigest()


def git(root: Path, *args: str) -> str:
    try:
        run = subprocess.run(["git", *args], cwd=root, capture_output=True, text=True, timeout=20, check=False)
    except (FileNotFoundError, subprocess.SubprocessError):
        return ""
    return run.stdout.strip() if run.returncode == 0 else ""


def resolve(config_path: Path, raw: Any) -> Path:
    path = Path(str(raw))
    return (path if path.is_absolute() else config_path.parent / path).resolve()


def record(path: Path, root: Path) -> dict[str, Any]:
    try:
        name = path.resolve().relative_to(root.resolve()).as_posix()
    except ValueError:
        name = str(path.resolve())
    return {"path": name, "sizeBytes": path.stat().st_size, "sha256": sha(path)}


def expected_counts(config: Mapping[str, Any], coverage_seed: int) -> dict[str, int]:
    _ = coverage_seed
    methods = [x for x in config.get("methods", []) if isinstance(x, dict)]
    scenarios = [x for x in config.get("scenarios", []) if isinstance(x, dict)]
    return {
        "smoke": 12,
        "startup20": 20,
        "coverage18": len(methods) * len(scenarios),
        "determinism": sum(int(x.get("repetitions", 1)) for x in config.get("determinism_replays", []) if isinstance(x, dict)),
        "soak30": len([x for x in config.get("long_runs", []) if isinstance(x, dict)]),
    }


def phase_files(directory: Path) -> list[Path]:
    return [directory / name for name in (
        "suite_preflight.json", "suite_dry_run.json", "suite_metrics.csv", "suite_metrics.json",
        "final_runtime_suite.sqlite", "suite_post_gates.json", "suite_summary.json",
        "final_runtime_suite_artifacts.zip",
    )]


def coverage_issues(rows: list[dict[str, Any]], config: Mapping[str, Any], seed: int) -> list[dict[str, Any]]:
    methods = {str(x.get("code", "")) for x in config.get("methods", []) if isinstance(x, dict)}
    scenarios = {str(x.get("code", "")) for x in config.get("scenarios", []) if isinstance(x, dict)}
    expected = {(m, s) for m in methods for s in scenarios}
    observed: set[tuple[str, str]] = set(); problems: list[dict[str, Any]] = []
    for row in rows:
        observed.add((str(row.get("method", "")), str(row.get("scenario", ""))))
        if int(row.get("seed", -1)) != seed or int(row.get("valid", 0) or 0) != 1 or int(row.get("quality_gate_passed", 0) or 0) != 1:
            problems.append({"code": "coverage18_run_failed", "run": row.get("run_name"), "seed": row.get("seed")})
    if observed != expected:
        problems.append({"code": "coverage18_matrix_mismatch", "missing": sorted(expected - observed), "unexpected": sorted(observed - expected)})
    if len(rows) != len(expected):
        problems.append({"code": "coverage18_row_count", "expected": len(expected), "observed": len(rows)})
    return problems


def validate_phase(root: Path, phase: str, config: Mapping[str, Any], counts: Mapping[str, int], seed: int) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    directory = root / phase; issues: list[dict[str, Any]] = []
    if not directory.is_dir():
        return {"phase": phase, "passed": False}, [{"code": "phase_missing", "phase": phase}]
    if phase == "startup20":
        path = directory / "startup20_summary.json"
        if not path.is_file():
            return {"phase": phase, "passed": False}, [{"code": "startup20_missing"}]
        data = read(path); runs = data.get("runs") if isinstance(data.get("runs"), list) else []
        expected = counts[phase]
        passed = bool(data.get("passed")) and int(data.get("requestedRepetitions", 0)) == expected and int(data.get("completedRepetitions", 0)) == expected and len(runs) == expected and all(isinstance(x, dict) and x.get("passed") for x in runs)
        if not passed: issues.append({"code": "startup20_failed"})
        return {"phase": phase, "passed": passed, "expectedRunCount": expected, "files": [record(path, root)]}, issues
    required = phase_files(directory); missing = [str(x) for x in required if not x.is_file()]
    if missing:
        return {"phase": phase, "passed": False}, [{"code": "phase_files_missing", "paths": missing}]
    pre, post, summary, metrics = read(directory / "suite_preflight.json"), read(directory / "suite_post_gates.json"), read(directory / "suite_summary.json"), read(directory / "suite_metrics.json")
    rows = [dict(x) for x in metrics.get("rows", []) if isinstance(x, dict)]
    expected = counts[phase]; markers = list(directory.glob("*_COMPLETE.json"))
    if phase == "coverage18": issues.extend(coverage_issues(rows, config, seed))
    values = {
        "selectedRunCount": int(summary.get("selectedRunCount", 0) or 0),
        "completedRowCount": int(summary.get("completedRowCount", 0) or 0),
        "validRunCount": int(summary.get("validRunCount", 0) or 0),
        "qualityPassCount": int(summary.get("qualityPassCount", 0) or 0),
        "metricRowCount": len(rows), "completeMarkerCount": len(markers),
    }
    passed = not pre.get("problems", []) and bool(post.get("passed")) and not post.get("issues", []) and bool(summary.get("postSuiteGatesPassed")) and int(summary.get("worstExitCode", 999)) == 0 and all(value == expected for value in values.values()) and not issues
    if not passed: issues.append({"code": "phase_failed", "phase": phase, "expected": expected, **values})
    return {"phase": phase, "passed": passed, "expectedRunCount": expected, **values, "runtimeBranch": summary.get("runtimeBranch", ""), "runtimeCommitSha": summary.get("runtimeCommitSha", ""), "files": [record(x, root) for x in required]}, issues


def run_paths(directory: Path, run: str) -> dict[str, Path]:
    return {
        "summary": directory / f"{run}_summary.json", "frames": directory / f"{run}_frames.csv",
        "jsonl": directory / f"{run}_telemetry.jsonl", "settings": directory / f"{run}_settings.json",
        "environment": directory / f"{run}_environment.json", "validation": directory / f"{run}_validation.json",
        "manifest": directory / f"{run}_artifact_manifest.json",
    }


def pick(summary: Mapping[str, Any], settings: Mapping[str, Any], key: str) -> Any:
    if summary.get(key) not in (None, ""): return summary[key]
    for section in ("observed", "applied", "requested"):
        value = settings.get(section)
        if isinstance(value, Mapping) and value.get(key) not in (None, ""): return value[key]
    return ""


def build_detailed_outputs(root: Path, config_path: Path, seed: int) -> dict[str, Any]:
    from hairgpu_final_db import FinalStageDatabase, RuntimeRunMetadata
    from hairgpu_final_excel import export_workbook
    config = read(config_path); analysis = root / "analysis"; analysis.mkdir(parents=True, exist_ok=True)
    database_path = analysis / "validated_evidence.sqlite"; database_path.unlink(missing_ok=True)
    schema = resolve(config_path, config.get("source", {}).get("schema")); imported = 0
    with FinalStageDatabase(database_path) as database:
        database.initialize(schema)
        for phase in STANDARD_PHASES:
            directory = root / phase
            for marker in sorted(directory.glob("*_COMPLETE.json")):
                raw = marker.name[:-len("_COMPLETE.json")]; paths = run_paths(directory, raw)
                if any(not path.is_file() for path in paths.values()): raise RuntimeError(f"Run artifacts missing: {phase}/{raw}")
                summary, settings = read(paths["summary"]), read(paths["settings"])
                method = str(pick(summary, settings, "observedMethodCode") or pick(summary, settings, "methodCode") or summary.get("appliedMethodCode") or summary.get("requestedMethodCode") or "")
                scenario = str(summary.get("scenario") or pick(summary, settings, "scenario") or "")
                asset = str(summary.get("formalAssetPath") or settings.get("formalAssetPath") or "")
                metadata = RuntimeRunMetadata(str(config.get("experiment_name") or "Novelty01_PreNovelty_Complete"), method, scenario, Path(asset).name if asset else "frozen_100k_asset", f"{phase}__{raw}", operator_name="prenovelty-complete-v2", notes=f"phase={phase}")
                database.import_runtime_run(metadata, paths["summary"], paths["frames"], frame_jsonl_path=paths["jsonl"], settings_json_path=paths["settings"], environment_json_path=paths["environment"], validation_json_path=paths["validation"], manifest_json_path=paths["manifest"])
                imported += 1
        rows = [dict(x) for x in read(root / "coverage18" / "suite_metrics.json").get("rows", []) if isinstance(x, dict)]
        present = {(str(x.get("scenario", "")), str(x.get("method", ""))) for x in rows if int(x.get("seed", -1)) == seed and int(x.get("valid", 0) or 0) == 1 and int(x.get("quality_gate_passed", 0) or 0) == 1}
        suite = str(config.get("suite_name") or "prenovelty_complete_v2"); database.clear_suite_rows(suite)
        for scenario in [str(x.get("code", "")) for x in config.get("scenarios", []) if isinstance(x, dict)]:
            for method in [str(x.get("code", "")) for x in config.get("methods", []) if isinstance(x, dict)]:
                database.add_scenario_coverage(suite, scenario, method, (scenario, method) in present, category="coverage18", notes=f"seed={seed}")
    excel = analysis / "formal_results.xlsx"; export_workbook(database_path, excel)
    graphs = make_graphs(database_path, analysis / "graphs")
    report = analysis / "formal_report.md"
    write(report, f"# Pre-novelty complete formal report\n\n- Generated: {now()}\n- Imported formal runs: {imported}\n- Coverage: B0/B6/B8 across all 18 scenarios at seed {seed}.\n- GTX 1660 status: theoretical projection only; physical target-hardware execution is future work.\n")
    files = [database_path, excel, report, *graphs]
    manifest = {"schemaVersion": "hairgpu-formal-postprocess-v1", "generatedUtc": now(), "importedRunCount": imported, "artifacts": [record(x, root) for x in files]}
    manifest["manifestSha256"] = canonical(manifest); post = analysis / "postprocess_manifest.json"; write_json(post, manifest); files.append(post)
    return {"passed": True, "importedRunCount": imported, "files": [record(x, root) for x in files]}


def bar(path: Path, title: str, rows: list[tuple[str, float]], subtitle: str) -> None:
    if not rows: raise RuntimeError(f"No rows for graph: {title}")
    width, left, right, top, bh, gap = 1280, 260, 100, 80, 28, 12; height = top + 60 + len(rows) * (bh + gap); maximum = max(max(v for _, v in rows), 1e-9); plot = width-left-right
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">', '<rect width="100%" height="100%" fill="white"/>', f'<text x="40" y="42" font-family="Segoe UI" font-size="28" font-weight="700">{html.escape(title)}</text>', f'<text x="40" y="66" font-family="Segoe UI" font-size="14">{html.escape(subtitle)}</text>']
    for i, (name, value) in enumerate(rows):
        y = top + i*(bh+gap); w = max(1.0, plot*value/maximum); out += [f'<text x="{left-12}" y="{y+20}" text-anchor="end" font-family="Segoe UI" font-size="14">{html.escape(name)}</text>', f'<rect x="{left}" y="{y}" width="{w:.2f}" height="{bh}" rx="4" fill="#4e79a7"/>', f'<text x="{left+w+8:.2f}" y="{y+20}" font-family="Segoe UI" font-size="14">{value:.3f}</text>']
    out.append('</svg>'); write(path, "\n".join(out)+"\n")


def make_graphs(database: Path, directory: Path) -> list[Path]:
    directory.mkdir(parents=True, exist_ok=True); connection = sqlite3.connect(database)
    specs = [
        ("method_average_fps.svg", "Average FPS by method", "Higher is better", "SELECT method_code, AVG(avg_fps) FROM runtime_runs WHERE is_valid=1 GROUP BY method_code ORDER BY method_code"),
        ("scenario_p99_frame_ms.svg", "P99 frame time by scenario", "Milliseconds; lower is better", "SELECT scenario_code, AVG(p99_frame_ms) FROM runtime_runs WHERE is_valid=1 GROUP BY scenario_code ORDER BY scenario_code"),
        ("method_average_hair_gpu_ms.svg", "Average hair GPU time by method", "Milliseconds; lower is better", "SELECT method_code, AVG(avg_hair_gpu_ms) FROM runtime_runs WHERE is_valid=1 GROUP BY method_code ORDER BY method_code"),
    ]; output: list[Path] = []
    try:
        for name, title, subtitle, sql in specs:
            rows = [(str(a), float(b)) for a, b in connection.execute(sql).fetchall() if b is not None]; path = directory/name; bar(path, title, rows, subtitle); output.append(path)
    finally: connection.close()
    return output


def frozen_snapshot(root: Path, bundle: Path) -> dict[str, Any]:
    files = [x for x in sorted(bundle.rglob("*")) if x.is_file() and ".tmp." not in x.name and x.name not in {"frozen_asset_bundle_snapshot.zip", "frozen_asset_bundle_snapshot.zip.sha256"}]
    if not files: raise RuntimeError("Frozen bundle is empty")
    archive, sidecar, tmp = root/"frozen_asset_bundle_snapshot.zip", root/"frozen_asset_bundle_snapshot.zip.sha256", root/"frozen_asset_bundle_snapshot.zip.tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for path in files: z.write(path, "frozen_asset_bundle/"+path.relative_to(bundle).as_posix())
    os.replace(tmp, archive); digest = sha(archive); write(sidecar, f"{digest}  {archive.name}\n")
    records = [record(x, bundle) for x in files]
    return {"fileCount": len(files), "bundleTreeSha256": canonical(records), "snapshotSha256": digest}


def evidence_archive(root: Path, config: Path, manifest: Path) -> tuple[Path, Path]:
    archive, sidecar, tmp = root/"prenovelty_complete_evidence.zip", root/"prenovelty_complete_evidence.zip.sha256", root/"prenovelty_complete_evidence.zip.tmp"
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        z.write(config, "configuration/"+config.name); z.write(manifest, "complete_manifest.json")
        for path in sorted(root.rglob("*")):
            if path.is_file() and path not in {archive, sidecar, tmp, manifest} and ".tmp." not in path.name: z.write(path, "results/"+path.relative_to(root).as_posix())
    os.replace(tmp, archive); write(sidecar, f"{sha(archive)}  {archive.name}\n"); return archive, sidecar


def aggregate(root: Path, config_path: Path, repo: Path, seed: int) -> tuple[dict[str, Any], int]:
    root, config_path, repo = root.resolve(), config_path.resolve(), repo.resolve(); root.mkdir(parents=True, exist_ok=True); config = read(config_path); counts = expected_counts(config, seed); issues: list[dict[str, Any]] = []; phases = []
    for phase in PHASES:
        result, phase_issues = validate_phase(root, phase, config, counts, seed); phases.append(result); issues.extend(phase_issues)
    bad = sorted({x for pattern in BAD_MARKERS for x in root.rglob(pattern) if x.is_file()})
    if bad: issues.append({"code": "bad_markers", "paths": [x.relative_to(root).as_posix() for x in bad]})
    branch, commit, dirty = git(repo, "branch", "--show-current"), git(repo, "rev-parse", "HEAD"), git(repo, "status", "--porcelain", "--untracked-files=normal")
    if not branch: issues.append({"code": "branch_unavailable"})
    if not commit: issues.append({"code": "commit_unavailable"})
    if dirty: issues.append({"code": "dirty_worktree", "paths": dirty.splitlines()[:20]})
    manifest_path = resolve(config_path, config.get("source", {}).get("asset_manifest")); bundle, lock = manifest_path.parent, manifest_path.parent/"frozen_bundle_lock.json"
    if not manifest_path.is_file(): issues.append({"code": "frozen_manifest_missing"})
    if not lock.is_file(): issues.append({"code": "frozen_lock_missing"})
    post: dict[str, Any] = {"passed": False}; frozen: dict[str, Any] = {}
    if not issues:
        try: post = build_detailed_outputs(root, config_path, seed); frozen = frozen_snapshot(root, bundle)
        except Exception as exc: issues.append({"code": "postprocess_failed", "message": str(exc)})
    passed = not issues and all(x.get("passed") for x in phases) and post.get("passed") is True
    result: dict[str, Any] = {"schemaVersion": SCHEMA_VERSION, "generatedUtc": now(), "status": "COMPLETE" if passed else "INCOMPLETE", "passed": passed, "repository": {"branch": branch, "commitSha": commit, "cleanWorktree": not bool(dirty)}, "configuration": {"path": str(config_path), "sha256": sha(config_path), "coverageSeed": seed}, "frozenAsset": {"manifestPath": str(manifest_path), "manifestSha256": sha(manifest_path) if manifest_path.is_file() else "", "lockPath": str(lock), "lockSha256": sha(lock) if lock.is_file() else "", "snapshot": frozen, "regenerationPolicy": "explicit-refreeze-only"}, "phases": phases, "postprocess": post, "gtx1660ClaimPolicy": {"status": "theoretical_projection_only", "formalEvidence": False, "futureWork": "Physical GTX 1660-class execution using the same frozen workload."}, "issues": issues, "archiveFileName": "prenovelty_complete_evidence.zip", "archiveSha256Sidecar": "prenovelty_complete_evidence.zip.sha256"}
    result["manifestSha256"] = canonical(result); complete = root/"complete_manifest.json"; write_json(complete, result)
    if passed: evidence_archive(root, config_path, complete); return result, 0
    for path in (root/"prenovelty_complete_evidence.zip", root/"prenovelty_complete_evidence.zip.sha256"): path.unlink(missing_ok=True)
    return result, 2


def self_test() -> int:
    config = {"methods": [{"code": x} for x in ("B0", "B6", "B8")], "scenarios": [{"code": f"S{i:02d}"} for i in range(1,19)], "determinism_replays": [{"repetitions": 5}]*3, "long_runs": [{}]*4}
    if expected_counts(config, 1103) != {"smoke":12,"startup20":20,"coverage18":54,"determinism":15,"soak30":4}: return 1
    rows = [{"method":m,"scenario":f"S{i:02d}","seed":1103,"valid":1,"quality_gate_passed":1} for m in ("B0","B6","B8") for i in range(1,19)]
    if coverage_issues(rows, config, 1103): return 2
    print("hairgpu_prenovelty_complete_v2 self-test: PASS"); return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__); parser.add_argument("--root", type=Path); parser.add_argument("--config", type=Path); parser.add_argument("--repo-root", type=Path); parser.add_argument("--coverage-seed", type=int, default=1103); parser.add_argument("--self-test", action="store_true"); args = parser.parse_args()
    if args.self_test: return self_test()
    if not args.root or not args.config or not args.repo_root: parser.error("--root, --config, and --repo-root are required")
    try: result, code = aggregate(args.root, args.config, args.repo_root, args.coverage_seed)
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as exc: print(f"Pre-novelty complete v2 failed: {exc}"); return 3
    print(json.dumps(result, ensure_ascii=False, indent=2)); return code


if __name__ == "__main__": raise SystemExit(main())
