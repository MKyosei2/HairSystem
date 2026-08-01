from __future__ import annotations

import csv
import importlib.util
import json
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MODULE_PATH = ROOT / "tools/final_stage/hairgpu_b0_readiness_smoke.py"
SPEC = importlib.util.spec_from_file_location("hairgpu_b0_readiness_smoke", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
smoke = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(smoke)


class B0ReadinessSmokeTests(unittest.TestCase):
    def _write_outputs(
        self,
        root: Path,
        run_name: str,
        frame_count: int = 600,
        fatal_row: int | None = None,
    ) -> None:
        csv_path = root / f"{run_name}_frames.csv"
        jsonl_path = root / f"{run_name}_telemetry.jsonl"
        fieldnames = [
            "sourceFrameIndex",
            "totalStrandCount",
            "nanStateCount",
            "infiniteStateCount",
            "fatalFailure",
            "syntheticMetricCount",
            "sameFrameReadbackCount",
            "frameIntervalMilliseconds",
        ]
        with csv_path.open("w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=fieldnames)
            writer.writeheader()
            for index in range(frame_count):
                writer.writerow(
                    {
                        "sourceFrameIndex": index,
                        "totalStrandCount": 100000,
                        "nanStateCount": 0,
                        "infiniteStateCount": 0,
                        "fatalFailure": 1 if fatal_row == index else 0,
                        "syntheticMetricCount": 0,
                        "sameFrameReadbackCount": 0,
                        "frameIntervalMilliseconds": 16.0,
                    }
                )
        with jsonl_path.open("w", encoding="utf-8", newline="\n") as handle:
            for index in range(frame_count):
                handle.write(
                    json.dumps(
                        {
                            "sourceFrameIndex": index,
                            "totalStrandCount": 100000,
                            "nanStateCount": 0,
                            "infiniteStateCount": 0,
                            "fatalFailure": 1 if fatal_row == index else 0,
                        }
                    )
                    + "\n"
                )

    def test_validator_accepts_exact_sane_600_frame_capture(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            run_name = "readiness"
            self._write_outputs(root, run_name)
            result = smoke.validate_readiness_outputs(root, run_name, 600)
            self.assertEqual(result["status"], "PASS")
            self.assertEqual(result["csvRows"], 600)
            self.assertEqual(result["jsonlRows"], 600)
            self.assertEqual(result["totalStrandCount"], 100000)
            self.assertEqual(result["timingField"], "frameIntervalMilliseconds")
            self.assertFalse(result["formalEvidence"])

    def test_validator_rejects_short_capture(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            run_name = "short"
            self._write_outputs(root, run_name, frame_count=599)
            with self.assertRaisesRegex(smoke.ReadinessError, "CSV frame count mismatch"):
                smoke.validate_readiness_outputs(root, run_name, 600)

    def test_validator_rejects_fatal_runtime_state(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            run_name = "fatal"
            self._write_outputs(root, run_name, fatal_row=17)
            with self.assertRaisesRegex(smoke.ReadinessError, "Invalid runtime state"):
                smoke.validate_readiness_outputs(root, run_name, 600)

    def test_case_is_fixed_to_b0_s01_and_100k(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            manifest = Path(temporary) / "manifest.json"
            manifest.write_text("{}", encoding="utf-8")
            case = smoke.build_case(ROOT, "case", manifest, 120, 600)
            self.assertEqual(case["method"], "B0")
            self.assertEqual(case["scenario"], "S01_Idle")
            self.assertEqual(case["display_strand_count"], 100000)
            self.assertEqual(case["warmup_frames"], 120)
            self.assertEqual(case["measurement_frames"], 600)
            self.assertFalse(case["hardware_profile_strict"])
            self.assertFalse(case["strict_visual_validation"])


if __name__ == "__main__":
    unittest.main()
