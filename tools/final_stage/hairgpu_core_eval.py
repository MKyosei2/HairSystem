from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path
from typing import Any


def run(cmd: list[str]) -> None:
    subprocess.run(cmd, check=True)


def read_json(path: Path) -> dict[str, Any]:
    with path.open('r', encoding='utf-8') as f:
        return json.load(f)


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding='utf-8')


def write_markdown(path: Path, final_summary: dict[str, Any], manifest: dict[str, Any]) -> None:
    lines = [
        '# HairGPU Core Evaluation Report',
        '',
        '## Summary',
        '',
        f"- Skeleton coverage percent: {final_summary.get('skeletonCoveragePercent', 0.0):.3f}",
        f"- Implemented pillars: {final_summary.get('implementedPillars', 0)}",
        f"- Compare variant count: {final_summary.get('compareVariantCount', 0)}",
        f"- Edit case count: {final_summary.get('editCaseCount', 0)}",
    ]
    runtime_suite = final_summary.get('runtimeSuite')
    if runtime_suite:
        lines += [
            '',
            '## Runtime suite overlay',
            '',
            f"- Avg p95 frame ms: {float(runtime_suite.get('avg_p95_frame_ms', 0.0)):.3f}",
            f"- Avg 60 FPS ratio: {float(runtime_suite.get('avg_sixty_fps_ratio', 0.0)):.3f}",
            f"- Avg edit locality: {float(runtime_suite.get('avg_edit_locality', 0.0)):.3f}",
            f"- Avg boundary naturalness: {float(runtime_suite.get('avg_boundary_naturalness', 0.0)):.3f}",
            f"- Avg edit-to-run latency ms: {float(runtime_suite.get('avg_edit_to_run_latency_ms', 0.0)):.3f}",
            f"- Avg penetration/sec: {float(runtime_suite.get('avg_penetration_per_sec', 0.0)):.3f}",
            f"- Run count: {runtime_suite.get('run_count', 0)}",
        ]
    hypothesis_rows = final_summary.get('runtimeHypothesisChecks', [])
    if hypothesis_rows:
        lines += ['', '## Runtime hypotheses', '', '| ID | Metric | Passed | Notes |', '|---|---|---|---|']
        for row in hypothesis_rows:
            lines.append(f"| {row.get('hypothesis_id','')} | {row.get('metric_name','')} | {row.get('passed','')} | {row.get('notes','')} |")
    coverage_rows = final_summary.get('runtimeScenarioCoverage', [])
    if coverage_rows:
        missing = [row for row in coverage_rows if row.get('present') != 'yes']
        lines += ['', '## Runtime scenario coverage', '', f"- Required checks: {len(coverage_rows)}", f"- Missing checks: {len(missing)}"]
    lines += ['', '## Output artifacts', '']
    for key, value in manifest.items():
        lines.append(f'- {key}: `{value}`')
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def main() -> int:
    parser = argparse.ArgumentParser(description='Run proposal-aligned hairgpu core evaluation pipeline')
    parser.add_argument('--cli', required=True)
    parser.add_argument('--input', required=True)
    parser.add_argument('--teacher-dir', required=True)
    parser.add_argument('--bundle-dir', required=True)
    parser.add_argument('--output-dir', required=True)
    parser.add_argument('--colliders')
    parser.add_argument('--max-active-clusters', type=int)
    parser.add_argument('--max-active-guides', type=int)
    parser.add_argument('--target-frame-ms', type=float)
    parser.add_argument('--summary-json', default='final_project_summary.json')
    parser.add_argument('--report-markdown', default='final_project_report.md')
    parser.add_argument('--runtime-suite-summary')
    args = parser.parse_args()

    cli = Path(args.cli)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    extra: list[str] = []
    if args.max_active_clusters is not None and args.max_active_guides is not None and args.target_frame_ms is not None:
        extra += [str(args.max_active_clusters), str(args.max_active_guides), str(args.target_frame_ms)]
    if args.colliders:
        extra += ['--colliders', args.colliders]

    run([str(cli), 'hybrid-compare', args.teacher_dir, args.bundle_dir, str(output_dir / 'compare'), *extra])
    run([str(cli), 'hybrid-ablate', args.teacher_dir, args.bundle_dir, str(output_dir / 'ablation'), *extra])
    run([str(cli), 'proposal-edit-suite-json', args.input, str(output_dir / 'edit_suite')])
    run([str(cli), 'proposal-claim-json', args.input, args.teacher_dir, args.bundle_dir, str(output_dir / 'proposal'), *extra])

    manifest = {
        'compareSummary': str(output_dir / 'compare' / 'hybrid_compare_summary.json'),
        'ablationSummary': str(output_dir / 'ablation' / 'hybrid_ablation_summary.json'),
        'editSuiteSummary': str(output_dir / 'edit_suite' / 'proposal_edit_suite_summary.json'),
        'claimSummary': str(output_dir / 'proposal' / 'proposal_claim_skeleton.json'),
    }
    if args.runtime_suite_summary:
        manifest['runtimeSuiteSummary'] = str(Path(args.runtime_suite_summary))
    write_json(output_dir / 'core_eval_manifest.json', manifest)

    claim = read_json(output_dir / 'proposal' / 'proposal_claim_skeleton.json')
    compare = read_json(output_dir / 'compare' / 'hybrid_compare_summary.json')
    edit_suite = read_json(output_dir / 'edit_suite' / 'proposal_edit_suite_summary.json')
    final_summary = {
        'skeletonCoveragePercent': claim.get('skeletonCoveragePercent', 0.0),
        'implementedPillars': claim.get('implementedPillars', 0),
        'compareVariantCount': len(compare.get('variants', [])),
        'editCaseCount': len(edit_suite.get('cases', [])),
        'claimSummary': manifest['claimSummary'],
        'editSuiteSummary': manifest['editSuiteSummary'],
        'compareSummary': manifest['compareSummary'],
        'ablationSummary': manifest['ablationSummary'],
    }
    if args.runtime_suite_summary:
        runtime_suite_summary = read_json(Path(args.runtime_suite_summary))
        summary_block = runtime_suite_summary.get('summary', runtime_suite_summary)
        final_summary['runtimeSuite'] = summary_block
        final_summary['runtimeHypothesisChecks'] = runtime_suite_summary.get('hypothesisChecks', [])
        final_summary['runtimeScenarioCoverage'] = runtime_suite_summary.get('scenarioCoverage', [])
    summary_path = output_dir / args.summary_json
    report_path = output_dir / args.report_markdown
    write_json(summary_path, final_summary)
    write_markdown(report_path, final_summary, manifest)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
