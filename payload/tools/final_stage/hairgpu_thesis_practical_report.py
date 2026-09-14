from __future__ import annotations

"""Join thesis edit episodes with existing frame/Novelty01 telemetry.

No synthetic performance values are generated. Missing source columns remain
missing and are reported as evidence gaps.
"""

import argparse
import csv
import json
import math
from pathlib import Path
from typing import Any


def read_csv(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8-sig", newline="") as handle:
        return list(csv.DictReader(handle))


def fnum(row: dict[str, str], *names: str) -> float | None:
    for name in names:
        raw = row.get(name, "")
        if raw in ("", None):
            continue
        try:
            value = float(raw)
        except (TypeError, ValueError):
            continue
        if math.isfinite(value):
            return value
    return None


def inum(row: dict[str, str], *names: str) -> int | None:
    value = fnum(row, *names)
    return None if value is None else int(value)


def find_frame_csv(edit_csv: Path) -> Path | None:
    stem = edit_csv.name.replace("_thesis_edits.csv", "")
    exact = edit_csv.with_name(stem + "_frames.csv")
    if exact.is_file():
        return exact
    candidates = sorted(edit_csv.parent.glob("*_frames.csv"))
    return candidates[0] if len(candidates) == 1 else None


def find_novelty_csv(edit_csv: Path) -> Path | None:
    stem = edit_csv.name.replace("_thesis_edits.csv", "")
    exact = edit_csv.with_name(stem + "_novelty01.csv")
    if exact.is_file():
        return exact
    candidates = sorted(edit_csv.parent.glob("*_novelty01.csv"))
    return candidates[0] if len(candidates) == 1 else None


def nearest_rows(
    rows: list[dict[str, str]], start_frame: int, window: int
) -> list[dict[str, str]]:
    selected = []
    for row in rows:
        frame = inum(
            row,
            "globalFrameIndex",
            "measuredFrameIndex",
            "frame",
            "source_frame",
        )
        if frame is None:
            continue
        if start_frame <= frame <= start_frame + window:
            selected.append(row)
    return selected


def derive_execution_count(
    method: str,
    total_groups: int,
    frame_rows: list[dict[str, str]],
    novelty_rows: list[dict[str, str]],
) -> tuple[int | None, str]:
    if method == "B0":
        return total_groups, "B0_full_dynamic_contract"

    candidates: list[int] = []
    for row in frame_rows:
        for names in (
            ("dirtyGroupCount", "dirty_group_count"),
            ("dynamicGroupCount", "dynamic_group_count"),
            ("repairRequiredGroupCount", "repair_required_group_count"),
        ):
            value = inum(row, *names)
            if value is not None:
                candidates.append(value)

    # Novelty01 counter 9 is Counter_DirtyGroups.
    for row in novelty_rows:
        value = inum(row, "counter_9")
        if value is not None:
            candidates.append(value)

    if not candidates:
        return None, "missing_dirty_group_evidence"
    return min(max(candidates), total_groups), "measured_dirty_group_evidence"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--window", type=int, default=4)
    args = parser.parse_args()

    edit_files = sorted(args.input.rglob("*_thesis_edits.csv"))
    args.output.mkdir(parents=True, exist_ok=True)

    rows_out: list[dict[str, Any]] = []
    gaps: list[dict[str, Any]] = []

    for edit_file in edit_files:
        frame_file = find_frame_csv(edit_file)
        novelty_file = find_novelty_csv(edit_file)
        frame_rows = read_csv(frame_file) if frame_file else []
        novelty_rows = read_csv(novelty_file) if novelty_file else []

        for edit in read_csv(edit_file):
            frame = inum(edit, "frame") or 0
            method = edit.get("method_label", "OTHER")
            total_groups = inum(edit, "total_group_count") or 0
            direct_groups = inum(edit, "direct_group_count") or 0

            local_frames = nearest_rows(frame_rows, frame, args.window)
            local_n01 = nearest_rows(novelty_rows, frame, args.window)
            executed, source = derive_execution_count(
                method,
                total_groups,
                local_frames,
                local_n01,
            )

            execution_ratio = (
                None
                if executed is None or total_groups <= 0
                else executed / total_groups
            )
            reuse_ratio = (
                None if execution_ratio is None else 1.0 - execution_ratio
            )
            amplification = (
                None
                if executed is None or direct_groups <= 0
                else executed / direct_groups
            )

            frame_ms = [
                v
                for row in local_frames
                for v in [
                    fnum(
                        row,
                        "frameIntervalMilliseconds",
                        "renderFrameMilliseconds",
                        "frameMilliseconds",
                    )
                ]
                if v is not None
            ]
            post_edit_p95 = None
            if frame_ms:
                ordered = sorted(frame_ms)
                index = min(
                    len(ordered) - 1,
                    max(0, math.ceil(len(ordered) * 0.95) - 1),
                )
                post_edit_p95 = ordered[index]

            record = {
                **edit,
                "executed_group_count": executed,
                "execution_source": source,
                "execution_ratio": execution_ratio,
                "reuse_ratio": reuse_ratio,
                "update_amplification": amplification,
                "post_edit_frame_p95_ms": post_edit_p95,
                "frame_csv": str(frame_file) if frame_file else "",
                "novelty_csv": str(novelty_file) if novelty_file else "",
            }
            rows_out.append(record)

            if executed is None:
                gaps.append(
                    {
                        "editFile": str(edit_file),
                        "episode": edit.get("episode"),
                        "reason": source,
                    }
                )

    columns = sorted(
        {key for row in rows_out for key in row.keys()}
    )
    csv_path = args.output / "thesis_practical_episode_summary.csv"
    with csv_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=columns)
        writer.writeheader()
        writer.writerows(rows_out)

    summary = {
        "schema": "hairgpu-thesis-practical-report-v1",
        "episodeCount": len(rows_out),
        "evidenceGapCount": len(gaps),
        "passedEvidenceCompleteness": len(rows_out) > 0 and not gaps,
        "evidenceGaps": gaps,
        "episodeCsv": str(csv_path),
    }
    (args.output / "thesis_practical_report.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0 if summary["passedEvidenceCompleteness"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
