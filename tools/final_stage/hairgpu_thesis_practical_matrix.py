from __future__ import annotations
import argparse, json, os, subprocess, sys
from pathlib import Path
from typing import Any

CASE_SCHEMA = "hairgpu-thesis-practical-case-v1"

def load_cases(repo: Path) -> dict[str, dict[str, Any]]:
    root = repo / "benchmarks" / "thesis_practical"
    if not root.is_dir(): raise FileNotFoundError(root)
    out = {}
    for path in sorted(root.glob("T*.json")):
        payload = json.loads(path.read_text(encoding="utf-8"))
        if payload.get("schema") != CASE_SCHEMA: raise ValueError(f"{path}: unsupported schema")
        cid = str(payload.get("id", "")).upper()
        if not cid or cid in out: raise ValueError(f"{path}: invalid or duplicate case id")
        out[cid] = payload
    if not out: raise RuntimeError("no thesis practical cases found")
    return out

def parse_cases(text: str, cases: dict[str, dict[str, Any]]) -> list[str]:
    if text.strip().lower() == "all": return list(cases)
    out = []
    for raw in text.split(","):
        cid = raw.strip().upper()
        if not cid: continue
        if cid not in cases: raise ValueError(f"unknown thesis practical case: {cid}")
        if cid not in out: out.append(cid)
    if not out: raise ValueError("at least one case is required")
    return out

def run_case(args, cid, case):
    output = args.output.resolve() / cid
    output.mkdir(parents=True, exist_ok=True)
    matrix = args.repo.resolve() / "tools/final_stage/hairgpu_novelty01_matrix.py"
    if not matrix.is_file(): raise FileNotFoundError(matrix)
    guide_count = max(int(case.get("guideCount", 1)), 1)
    tip_mm = float(case.get("tipOffsetMm", 3.0) or 3.0)
    force = bool(case.get("forceFallback", False))
    env = os.environ.copy()
    env.update({
        "HAIRGPU_THESIS_EDIT_EVAL": "1",
        "HAIRGPU_THESIS_CASE": cid,
        "HAIRGPU_THESIS_EDIT_GUIDE_COUNT": str(guide_count),
        "HAIRGPU_THESIS_EDIT_TIP_MM": f"{tip_mm:.9g}",
        "HAIRGPU_THESIS_FORCE_FULL_FALLBACK": "1" if force else "0",
    })
    cmd = [sys.executable, str(matrix), "--repo", str(args.repo.resolve()),
           "--viewer", str(args.viewer.resolve()), "--template", str(args.template.resolve()),
           "--output", str(output), "--profile", args.profile, "--variants", args.variants,
           "--scenarios", str(case["scenario"]), "--seeds", args.seeds,
           "--schedule", args.schedule]
    if args.repetitions is not None: cmd += ["--repetitions", str(args.repetitions)]
    if args.dry_run: cmd.append("--dry-run")
    if args.continue_on_error: cmd.append("--continue-on-error")
    if args.require_fxc: cmd.append("--require-fxc")
    cp = subprocess.run(cmd, env=env, cwd=args.repo.resolve(), text=True,
                        capture_output=True, check=False)
    (output / "thesis_wrapper_stdout.txt").write_text(cp.stdout, encoding="utf-8")
    (output / "thesis_wrapper_stderr.txt").write_text(cp.stderr, encoding="utf-8")
    return {"case": cid, "scenario": case["scenario"], "guideCount": guide_count,
            "tipMm": tip_mm, "forceFallback": force, "profile": args.profile,
            "returnCode": cp.returncode, "ok": cp.returncode == 0, "output": str(output)}

def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("--repo", required=True, type=Path); p.add_argument("--viewer", required=True, type=Path)
    p.add_argument("--template", required=True, type=Path); p.add_argument("--output", required=True, type=Path)
    p.add_argument("--profile", default="pilot"); p.add_argument("--variants", default="B0,P")
    p.add_argument("--seeds", default="1,2"); p.add_argument("--repetitions", type=int)
    p.add_argument("--schedule", default="counterbalanced")
    p.add_argument("--cases", default="T01,T02,T03,T04,T05,T06,T07")
    p.add_argument("--dry-run", action="store_true"); p.add_argument("--continue-on-error", action="store_true")
    p.add_argument("--require-fxc", action="store_true")
    args = p.parse_args(); cases = load_cases(args.repo.resolve()); selected = parse_cases(args.cases, cases)
    args.output.mkdir(parents=True, exist_ok=True)
    rows = []
    for cid in selected:
        row = run_case(args, cid, cases[cid]); rows.append(row)
        if not row["ok"] and not args.continue_on_error: break
    summary = {"schema": "hairgpu-thesis-practical-matrix-v3", "caseSchema": CASE_SCHEMA,
               "profile": args.profile, "selectedCases": selected,
               "passed": bool(rows) and all(r["ok"] for r in rows), "results": rows}
    (args.output / "thesis_practical_matrix_summary.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if summary["passed"] else 1

if __name__ == "__main__": raise SystemExit(main())
