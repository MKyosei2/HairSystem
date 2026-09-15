from __future__ import annotations

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


class RepositoryDedupContractTests(unittest.TestCase):
    def test_old_thesis_payload_is_removed(self) -> None:
        self.assertFalse((ROOT / "payload").exists())

    def test_one_time_patchers_are_removed(self) -> None:
        obsolete = [
            ROOT / "Apply-ThesisPracticalEvaluation.ps1",
            ROOT / "scripts/apply_thesis_practical_evaluation.py",
            ROOT / "scripts/apply_novelty_61_37_final_fix.py",
            ROOT / "scripts/apply_novelty_61_39_final_fix.py",
            ROOT / "scripts/apply_novelty_metrics_fix.py",
        ]
        leftovers = [str(path.relative_to(ROOT)) for path in obsolete if path.exists()]
        self.assertEqual([], leftovers, f"obsolete one-time patchers remain: {leftovers}")

    def test_thesis_case_definition_has_single_owner(self) -> None:
        matrix = (
            ROOT / "tools/final_stage/hairgpu_thesis_practical_matrix.py"
        ).read_text(encoding="utf-8")
        self.assertIn('glob("T*.json")', matrix)
        self.assertNotIn('"T01": {', matrix)
        self.assertNotIn('"T07": {', matrix)


if __name__ == "__main__":
    unittest.main()
