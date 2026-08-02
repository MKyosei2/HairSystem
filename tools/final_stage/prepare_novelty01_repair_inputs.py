from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OLD = "37ff9749fe530e8ab08d849892c4c6326a2578bf"
NEW = "4f4c6210d7be40a0b5d522789d2684ade86d3de9"


def replace_all(relative: str, old: str, new: str) -> None:
    path = ROOT / relative
    text = path.read_text(encoding="utf-8")
    if old not in text:
        raise RuntimeError(f"expected marker missing in {relative}: {old}")
    path.write_text(text.replace(old, new), encoding="utf-8", newline="\n")


def main() -> int:
    replace_all(
        "files/tools/final_stage/Run_Novelty01_Readiness_Smoke.ps1",
        OLD,
        NEW,
    )
    replace_all(
        "files/tools/final_stage/Run_Novelty01_Readiness_Smoke.ps1",
        "Novelty01 69.9 baseline",
        "Novelty01 69.10 baseline",
    )
    replace_all(
        "files/tests/final_stage/test_novelty01_runtime_contract.py",
        OLD,
        NEW,
    )
    print("Prepared Novelty01 69.10 repair inputs.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
