from __future__ import annotations

import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load module: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


class FormalAssetBindingContractTests(unittest.TestCase):
    def test_patcher_is_idempotent_and_emits_all_four_fields(self) -> None:
        patcher = load_module(
            "formal_asset_binding_patcher",
            ROOT
            / "tools/final_stage/apply_formal_asset_binding_patch.py",
        )
        fixture = '''\
from pathlib import Path
from typing import Any, Mapping
import re
SHA256_RE = re.compile(r"^[0-9a-fA-F]{64}$")
EXIT_CONFIGURATION_FAILURE = 5
class FormalRunError(RuntimeError):
    def __init__(self, message, code, issues=None):
        super().__init__(message)
def issue(*args, **kwargs):
    return {"args": args, **kwargs}
def sha256_tree(*args, **kwargs):
    return "d" * 64
def sha256_file(path):
    return "e" * 64
def git_value(*args):
    return "f" * 40
def resolve_path(value, repo_root):
    return Path(value) if value else None
def _build_environment(case: Mapping[str, Any], paths: Mapping[str, Path], repo_root: Path,
                       viewer_exe: Path, asset_manifest: Mapping[str, Any]) -> dict[str, str]:
    env = {}
    timeline_path = resolve_path(case.get("scenario_timeline"), repo_root)
    manifest_path = resolve_path(case.get("asset_manifest"), repo_root)
    shader_root = resolve_path(case.get("shader_root"), repo_root)
    method_settings_hash = "1" * 64
    timeline_hash = ""
    manifest_hash = ""
    shader_hash = sha256_tree([shader_root], base_path=repo_root) if shader_root and shader_root.exists() else ""
    values = {
        "HAIRGPU_ASSET_SHA256": asset_manifest.get("sourceAssetSha256", ""),
        "HAIRGPU_BUILD_CONFIGURATION": case.get("build_configuration", "Release"),
    }
    for key, value in values.items():
        if value not in (None, ""):
            env[key] = str(value)
    return env
'''
        patched, changed = patcher.patch_text(fixture)
        self.assertTrue(changed)
        second, changed_again = patcher.patch_text(patched)
        self.assertFalse(changed_again)
        self.assertEqual(second, patched)

        with tempfile.TemporaryDirectory() as temporary:
            base = Path(temporary)
            asset = base / "asset.hairbin"
            asset.write_bytes(b"asset")
            viewer = base / "viewer.exe"
            viewer.write_bytes(b"viewer")
            namespace: dict[str, Any] = {}
            exec(compile(patched, "fixture.py", "exec"), namespace)
            env = namespace["_build_environment"](
                {
                    "scenario_timeline": "",
                    "asset_manifest": "",
                    "shader_root": "",
                    "build_configuration": "Release",
                },
                {},
                base,
                viewer,
                {
                    "sourceAssetSha256": "a" * 64,
                    "generatedTopologySha256": "b" * 64,
                    "generatorSettingsSha256": "c" * 64,
                    "_resolvedMembers": {
                        "sourceAssetPath": str(asset.resolve()),
                    },
                },
            )
            self.assertEqual(
                env["HAIRGPU_FORMAL_ASSET_PATH"],
                str(asset.resolve()),
            )
            self.assertEqual(
                env["HAIRGPU_FORMAL_ASSET_SHA256"],
                "a" * 64,
            )
            self.assertEqual(
                env["HAIRGPU_FORMAL_TOPOLOGY_SHA256"],
                "b" * 64,
            )
            self.assertEqual(
                env["HAIRGPU_GENERATOR_SETTINGS_SHA256"],
                "c" * 64,
            )

    def test_repository_runner_is_patched_before_runtime_tests(self) -> None:
        runner = ROOT / "tools/final_stage/hairgpu_final_run.py"
        if not runner.is_file():
            self.skipTest("Target Main runner is present after package installation.")
        source = runner.read_text(encoding="utf-8-sig")
        for key in (
            "HAIRGPU_FORMAL_ASSET_PATH",
            "HAIRGPU_FORMAL_ASSET_SHA256",
            "HAIRGPU_FORMAL_TOPOLOGY_SHA256",
            "HAIRGPU_GENERATOR_SETTINGS_SHA256",
        ):
            self.assertIn(key, source)


if __name__ == "__main__":
    unittest.main()
