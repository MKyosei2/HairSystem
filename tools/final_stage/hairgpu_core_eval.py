from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


def run(cmd: list[str]) -> None:
    subprocess.run(cmd, check=True)


def read_json(path: Path) -> dict:
    with path.open('r', encoding='utf-8') as f:
        return json.load(f)


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
    with (output_dir / 'core_eval_manifest.json').open('w', encoding='utf-8') as f:
        json.dump(manifest, f, indent=2)

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
    }
    with (output_dir / 'final_project_summary.json').open('w', encoding='utf-8') as f:
        json.dump(final_summary, f, indent=2)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
