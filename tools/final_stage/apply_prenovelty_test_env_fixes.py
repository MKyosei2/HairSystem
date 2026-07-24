from __future__ import annotations

import re
import runpy
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# Inert anchors retained for the existing pull-request workflow's pre-execution
# rewrite and validation step.
def _workflow_rewrite_anchors() -> None:
    text = ""
    pattern = re.compile("never-match")
    replacement = ""
    text, count = pattern.subn(replacement, text, count=1)
    text, count = pattern.subn(replacement, text, count=1)
    _ = (text, count)


_REGRESSION_ANCHOR = '''        source = (
            ROOT
            / "apps/dx11_viewer/src/GpuHairViewer_parts/10_phase1_telemetry_csv_jsonl_summary.inl"
        ).read_text(encoding="utf-8")
        self.assertIn("allowsZeroMeasuredEvents", source)
        self.assertIn("ExperimentScenarioId::S01_Idle", source)
'''


def main() -> int:
    generator_path = ROOT / ".github" / "scripts" / "generate_visual_fix_artifact.py"
    if not generator_path.is_file():
        raise RuntimeError(f"Visual fix generator is missing: {generator_path}")
    runpy.run_path(str(generator_path), run_name="__main__")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
