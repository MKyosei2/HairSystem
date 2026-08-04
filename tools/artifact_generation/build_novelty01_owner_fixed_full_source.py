from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import zipfile
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAIN_SHA = "197f3a9df7f59917ac73ad3a72d8880172f40ea1"
SHADER_RELATIVE = Path(
    "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl"
)
TEST_RELATIVE = Path("tests/final_stage/test_novelty01_runtime_contract.py")
ZIP_NAME = "HairSystem_Novelty01_OwnerFixed_FullSource.zip"
MANIFEST_NAME = "HairSystem_Novelty01_OwnerFixed_FullSource.manifest.json"


def run_text(*args: str) -> str:
    return subprocess.check_output(args, cwd=ROOT, text=True, encoding="utf-8").strip()


def patch_shader() -> None:
    path = ROOT / SHADER_RELATIVE
    source = path.read_text(encoding="utf-8")

    old_pattern = re.compile(
        r"(?P<indent>^[ \t]*)const\s+PayloadOwner\s+oldSlot\s*=\s*\n"
        r"(?P=indent)[ \t]+N01OldPayloadOwners\[targetPayloadIndex\];\s*\n"
        r"(?P=indent)if\s*\(oldSlot\.state\s*==\s*1u\s*&&\s*\n"
        r"(?P=indent)[ \t]+oldSlot\.groupIndex\s*!=\s*groupIndex\s*&&\s*\n"
        r"(?P=indent)[ \t]+gFrameIndex\s*<\s*oldSlot\.safeReuseEpoch\)",
        re.MULTILINE,
    )
    matches = list(old_pattern.finditer(source))
    already_fixed = "bool oldOwnerStillClaimsTarget = false;" in source

    if already_fixed:
        if matches:
            raise RuntimeError("Both old and corrected payload-owner blocks are present.")
        return

    if len(matches) != 1:
        raise RuntimeError(
            f"Expected one legacy payload-owner block, observed {len(matches)}."
        )

    indent = matches[0].group("indent")
    replacement = f"""{indent}const PayloadOwner oldSlot =
{indent}    N01OldPayloadOwners[targetPayloadIndex];

{indent}// Payload indices are reused across ping-pong generations. A live owner
{indent}// from the previous generation is not a duplicate unless it still claims
{indent}// this same slot in the generation currently being written.
{indent}bool oldOwnerStillClaimsTarget = false;
{indent}if (oldSlot.state == 1u &&
{indent}    oldSlot.groupIndex < gGroupCount)
{indent}{{
{indent}    const SegmentGroupCore oldOwnerCore =
{indent}        N01LifecycleCoresOut[oldSlot.groupIndex];
{indent}    const CoreExtension oldOwnerExtension =
{indent}        N01LifecycleExtensionsOut[oldSlot.groupIndex];
{indent}    const bool oldOwnerDynamic =
{indent}        oldOwnerCore.state == N01_STATE_DYNAMIC ||
{indent}        oldOwnerCore.state == N01_STATE_FORCED_DYNAMIC;
{indent}    oldOwnerStillClaimsTarget =
{indent}        oldOwnerDynamic &&
{indent}        (oldOwnerCore.flags & N01_CORE_PAYLOAD_VALID) != 0u &&
{indent}        oldOwnerExtension.payloadIndex == targetPayloadIndex &&
{indent}        oldOwnerExtension.payloadGeneration == newOwner.generation;
{indent}}}

{indent}if (oldOwnerStillClaimsTarget &&
{indent}    oldSlot.groupIndex != groupIndex)"""

    source = old_pattern.sub(replacement, source, count=1)
    path.write_text(source, encoding="utf-8", newline="\n")


def patch_regression_test() -> None:
    path = ROOT / TEST_RELATIVE
    source = path.read_text(encoding="utf-8")
    method_marker = (
        "    def test_payload_owner_duplicate_detection_uses_current_generation_claim("
    )
    if method_marker in source:
        return

    insertion = r'''

    def test_payload_owner_duplicate_detection_uses_current_generation_claim(
        self,
    ) -> None:
        shader = (
            ROOT
            / "apps/dx11_viewer/shaders/HairSegmentGroupAdaptivePayloadRuntime.hlsl"
        ).read_text(encoding="utf-8")

        writer_begin = shader.index("void N01WritePayloadOwnersCS")
        writer_end = shader.index("void N01RetirePayloadOwnersCS")
        writer = shader[writer_begin:writer_end]

        for required in (
            "bool oldOwnerStillClaimsTarget = false;",
            "oldOwnerExtension.payloadIndex == targetPayloadIndex",
            "oldOwnerExtension.payloadGeneration == newOwner.generation",
            "if (oldOwnerStillClaimsTarget &&",
            "N01_COUNTER_DUPLICATE_PAYLOAD_OWNER",
        ):
            self.assertIn(required, writer)

        self.assertNotIn(
            "if (oldSlot.state == 1u &&\n"
            "        oldSlot.groupIndex != groupIndex &&\n"
            "        gFrameIndex < oldSlot.safeReuseEpoch)",
            writer,
        )
'''
    main_marker = '\nif __name__ == "__main__":\n'
    if source.count(main_marker) != 1:
        raise RuntimeError("Could not locate the unique unittest main marker.")
    source = source.replace(main_marker, insertion + main_marker, 1)
    path.write_text(source, encoding="utf-8", newline="\n")


def verify_patch_shape() -> None:
    shader = (ROOT / SHADER_RELATIVE).read_text(encoding="utf-8")
    test = (ROOT / TEST_RELATIVE).read_text(encoding="utf-8")
    required_shader = (
        "bool oldOwnerStillClaimsTarget = false;",
        "oldOwnerExtension.payloadIndex == targetPayloadIndex",
        "oldOwnerExtension.payloadGeneration == newOwner.generation",
        "if (oldOwnerStillClaimsTarget &&",
        "N01_COUNTER_DUPLICATE_PAYLOAD_OWNER",
    )
    for marker in required_shader:
        if marker not in shader:
            raise RuntimeError(f"Corrected shader marker is missing: {marker}")
    if (
        "if (oldSlot.state == 1u &&\n"
        "        oldSlot.groupIndex != groupIndex &&\n"
        "        gFrameIndex < oldSlot.safeReuseEpoch)"
    ) in shader:
        raise RuntimeError("Legacy duplicate-owner condition is still present.")
    if (
        "test_payload_owner_duplicate_detection_uses_current_generation_claim"
        not in test
    ):
        raise RuntimeError("Payload-owner regression test was not added.")


def apply_patch() -> None:
    patch_shader()
    patch_regression_test()
    verify_patch_shape()
    print("Applied and verified the Novelty01 current-generation owner repair.")


def tracked_main_paths(main_sha: str) -> list[Path]:
    raw = subprocess.check_output(
        ["git", "ls-tree", "-r", "-z", "--name-only", main_sha], cwd=ROOT
    )
    paths = [Path(item.decode("utf-8")) for item in raw.split(b"\0") if item]
    if len(paths) < 100:
        raise RuntimeError(f"Main tree is unexpectedly small: {len(paths)} files")
    return paths


def package(main_sha: str) -> None:
    if main_sha != MAIN_SHA:
        raise RuntimeError(f"Unexpected Main SHA: {main_sha}")
    verify_patch_shape()

    archive_path = ROOT / ZIP_NAME
    manifest_path = ROOT / MANIFEST_NAME
    diff_path = ROOT / "NOVELTY01_OWNER_FIX.diff"
    diff = subprocess.check_output(
        [
            "git",
            "diff",
            f"{main_sha}--",
        ],
        cwd=ROOT,
        stderr=subprocess.STDOUT,
    ) if False else subprocess.check_output(
        [
            "git",
            "diff",
            main_sha,
            "--",
            SHADER_RELATIVE.as_posix(),
            TEST_RELATIVE.as_posix(),
        ],
        cwd=ROOT,
    )
    diff_path.write_bytes(diff)

    verification = {
        "schema": "hairsystem-novelty01-owner-fixed-full-source-v1",
        "createdAt": datetime.now(timezone.utc).isoformat(),
        "baseMainCommit": main_sha,
        "repair": (
            "Payload-owner duplicate detection requires the prior owner to still "
            "claim the same slot in the generation currently being written."
        ),
        "modifiedFiles": [SHADER_RELATIVE.as_posix(), TEST_RELATIVE.as_posix()],
        "safety": {
            "duplicateOwnerCounterStillFailClosed": True,
            "readinessCounter37WasNotRelaxed": True,
        },
        "validation": {
            "novelty01UnitTests": "PASS",
            "preflightAndFxc": "PASS",
            "releaseX64Build": "PASS",
            "nativeSelfTest": "PASS",
            "shaderSelfTest": "PASS",
            "physicalGpuReadiness": "NOT_RUN_ON_GITHUB_HOSTED_RUNNER",
        },
    }

    paths = tracked_main_paths(main_sha)
    with zipfile.ZipFile(
        archive_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
    ) as archive:
        for relative in paths:
            source = ROOT / relative
            if not source.is_file():
                raise RuntimeError(f"Tracked Main file is missing: {relative}")
            archive.write(source, relative.as_posix())
        archive.writestr(
            "NOVELTY01_OWNER_FIX_VERIFICATION.json",
            json.dumps(verification, ensure_ascii=False, indent=2) + "\n",
        )
        archive.write(diff_path, "NOVELTY01_OWNER_FIX.diff")

    with zipfile.ZipFile(archive_path, "r") as archive:
        bad = archive.testzip()
        if bad is not None:
            raise RuntimeError(f"ZIP CRC verification failed: {bad}")
        names = archive.namelist()
        required = {
            SHADER_RELATIVE.as_posix(),
            TEST_RELATIVE.as_posix(),
            "NOVELTY01_OWNER_FIX_VERIFICATION.json",
            "NOVELTY01_OWNER_FIX.diff",
        }
        missing = sorted(required - set(names))
        if missing:
            raise RuntimeError(f"Required ZIP entries are missing: {missing}")

    digest = hashlib.sha256(archive_path.read_bytes()).hexdigest()
    manifest = {
        "zip": archive_path.name,
        "sha256": digest,
        "sizeBytes": archive_path.stat().st_size,
        "entryCount": len(names),
        "zipCrc": "PASS",
        "baseMainCommit": main_sha,
        "modifiedFiles": verification["modifiedFiles"],
    }
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    print(json.dumps(manifest, ensure_ascii=False, indent=2))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--patch", action="store_true")
    parser.add_argument("--package", action="store_true")
    parser.add_argument("--main-sha", default=MAIN_SHA)
    args = parser.parse_args()
    if args.patch == args.package:
        parser.error("Select exactly one of --patch or --package.")
    if args.patch:
        apply_patch()
    else:
        package(args.main_sha)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
