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


def patch_preflight_entry_map() -> None:
    path = ROOT / "files/tools/final_stage/hairgpu_novelty01_preflight.py"
    text = path.read_text(encoding="utf-8")
    marker = '    "N01InjectDesignScenarioCS": 12,\n}'
    replacement = (
        '    "N01InjectDesignScenarioCS": 12,\n'
        '    "N01InitializeRenderCacheCS": 13,\n'
        '    "N01UpdateGuideRenderCacheCS": 13,\n'
        '    "N01UpdateFollowerRenderCacheCS": 13,\n'
        '}'
    )
    if marker not in text:
        raise RuntimeError("preflight entry-point map insertion marker missing")
    text = text.replace(marker, replacement, 1)
    if "for mode in range(13):" not in text:
        raise RuntimeError("preflight binding-mode loop marker missing")
    text = text.replace("for mode in range(13):", "for mode in range(14):", 1)
    path.write_text(text, encoding="utf-8", newline="\n")


def patch_packager_document_ending() -> None:
    path = ROOT / "tools/final_stage/build_novelty01_69_10_complete_repair_package.py"
    text = path.read_text(encoding="utf-8")
    old = '        dtext = dtext.rstrip() + status + "\\n"'
    new = '        dtext = dtext.rstrip() + status.rstrip() + "\\n"'
    if old not in text:
        raise RuntimeError("packager design-document ending marker missing")
    path.write_text(text.replace(old, new, 1), encoding="utf-8", newline="\n")


def patch_hlsl_fxc_compatibility() -> None:
    path = (
        ROOT
        / "replacement/apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl"
    )
    text = path.read_text(encoding="utf-8")

    # `triangle` is parsed as a reserved primitive token by legacy FXC.
    triangle_declarations = text.count("const TriangleData triangle")
    if triangle_declarations < 3:
        raise RuntimeError(
            "unexpected TriangleData declaration count: "
            f"{triangle_declarations}"
        )
    text = text.replace("const TriangleData triangle", "const TriangleData triIndices")
    text = text.replace("triangle.a", "triIndices.a")
    text = text.replace("triangle.b", "triIndices.b")
    text = text.replace("triangle.c", "triIndices.c")

    # FXC cannot select a user-defined struct with the conditional operator.
    payload_old = (
        "        DynamicPayload payload = extension.payloadIndex < gPayloadCapacity\n"
        "            ? N01Payloads[extension.payloadIndex]\n"
        "            : (DynamicPayload)0;"
    )
    payload_new = (
        "        DynamicPayload payload = (DynamicPayload)0;\n"
        "        if (extension.payloadIndex < gPayloadCapacity)\n"
        "        {\n"
        "            payload = N01Payloads[extension.payloadIndex];\n"
        "        }"
    )
    payload_count = text.count(payload_old)
    if payload_count != 1:
        raise RuntimeError(
            "unexpected DynamicPayload conditional count: "
            f"{payload_count}"
        )
    text = text.replace(payload_old, payload_new, 1)

    candidate_old = (
        "    SharedPointCandidate winner =\n"
        "        N01FeatureEnabled(N01_FEATURE_STABLE_SHARED_POINT)\n"
        "        ? (N01CandidateBetter(child, owner) ? child : owner)\n"
        "        : owner;"
    )
    candidate_new = (
        "    SharedPointCandidate winner = owner;\n"
        "    if (N01FeatureEnabled(N01_FEATURE_STABLE_SHARED_POINT) &&\n"
        "        N01CandidateBetter(child, owner))\n"
        "    {\n"
        "        winner = child;\n"
        "    }"
    )
    candidate_count = text.count(candidate_old)
    if candidate_count != 1:
        raise RuntimeError(
            "unexpected SharedPointCandidate conditional count: "
            f"{candidate_count}"
        )
    text = text.replace(candidate_old, candidate_new, 1)

    path.write_text(text, encoding="utf-8", newline="\n")


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
    patch_preflight_entry_map()
    patch_packager_document_ending()
    patch_hlsl_fxc_compatibility()
    print("Prepared Novelty01 69.10 repair inputs.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
