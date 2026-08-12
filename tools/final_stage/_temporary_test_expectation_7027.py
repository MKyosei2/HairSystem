from pathlib import Path

path = Path("tests/final_stage/test_thesis_formal_experiment_contract.py")
text = path.read_text(encoding="utf-8")
old = '            "actual-probe-sequence-warmup-fraction-v1",\n'
new = '            "actual-probe-sequence-warmup-fraction-loaded-v2",\n'
count = text.count(old)
if count != 1:
    raise RuntimeError(f"expected one legacy GPU health policy assertion, observed {count}")
path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")
print("updated existing GPU stability policy assertion")
