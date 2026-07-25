from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
USER_PROJECT = ROOT / "HairSystem.vcxproj.user"
MSBUILD_NS = {"m": "http://schemas.microsoft.com/developer/msbuild/2003"}


class F5LocalCaptureProjectContractTests(unittest.TestCase):
    def test_debug_and_release_x64_enable_local_capture(self) -> None:
        self.assertTrue(USER_PROJECT.is_file())
        root = ET.parse(USER_PROJECT).getroot()
        groups = root.findall("m:PropertyGroup", MSBUILD_NS)
        observed = {}
        for group in groups:
            condition = group.attrib.get("Condition", "")
            arguments = group.find("m:LocalDebuggerCommandArguments", MSBUILD_NS)
            if arguments is not None:
                observed[condition] = arguments.text or ""

        for configuration in ("Debug", "Release"):
            condition = f"'$(Configuration)|$(Platform)'=='{configuration}|x64'"
            self.assertIn(condition, observed)
            arguments = observed[condition]
            self.assertIn("--local-capture", arguments)
            self.assertIn("--local-capture-warmup 120", arguments)
            self.assertIn("--local-capture-frames 1800", arguments)
            self.assertNotIn("--local-capture-autocommit", arguments)
            self.assertNotIn("--local-capture-push", arguments)


if __name__ == "__main__":
    unittest.main()
