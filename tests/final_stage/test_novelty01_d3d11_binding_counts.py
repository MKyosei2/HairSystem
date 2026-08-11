import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
RUNTIME = ROOT / "apps" / "dx11_viewer" / "src" / "GpuHairViewer_parts" / "16_segment_group_adaptive_payload_complete.inl"
HLSL = ROOT / "apps" / "dx11_viewer" / "shaders" / "HairSegmentGroupAdaptivePayloadRuntime.hlsl"


class Novelty01D3D11BindingCountTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.runtime = RUNTIME.read_text(encoding="utf-8")
        cls.hlsl = HLSL.read_text(encoding="utf-8")

    def _lambda_block(self, name: str, next_marker: str) -> str:
        begin = self.runtime.index(f"const auto {name}")
        end = self.runtime.index(next_marker, begin)
        return self.runtime[begin:end]

    def test_mode1_scatter_binds_exactly_three_srvs(self):
        block = self._lambda_block("bindMode1Scatter", "const auto runScan")
        self.assertIn("ID3D11ShaderResourceView* srvs[3]", block)
        self.assertIn("CSSetShaderResources(0u, 3u, srvs);", block)
        self.assertNotIn("CSSetShaderResources(0u, 4u, srvs);", block)
        self.assertIn("HAIRGPU_N01_BINDING_COUNT_GUARD_V1", block)

    def test_mode3_binds_all_four_declared_srvs(self):
        block = self._lambda_block("bindMode3", "ExecuteNovelty01DirtyRangePropagation();")
        self.assertIn("ID3D11ShaderResourceView* srvs[4]", block)
        self.assertIn("CSSetShaderResources(0u, 4u, srvs);", block)

    def test_hlsl_mode1_declares_only_t0_t1_t2(self):
        begin = self.hlsl.index("#if N01_BINDING_MODE == 1")
        end = self.hlsl.index("#endif // mode 1", begin)
        block = self.hlsl[begin:end]
        registers = sorted(set(re.findall(r"register\(t(\d+)\)", block)))
        self.assertEqual(registers, ["0", "1", "2"])

    def test_hlsl_mode3_declares_t0_through_t3(self):
        begin = self.hlsl.index("#if N01_BINDING_MODE == 3")
        end = self.hlsl.index("#endif // mode 3", begin)
        block = self.hlsl[begin:end]
        registers = sorted(set(re.findall(r"register\(t(\d+)\)", block)))
        self.assertEqual(registers, ["0", "1", "2", "3"])

    def test_local_d3d11_binding_counts_never_exceed_array_length(self):
        # Guard every local C-array in the SG-APT runtime against the
        # exact class of stack over-read that caused the frame-8 crash.
        for api, typename in (
            ("CSSetShaderResources", "ID3D11ShaderResourceView"),
            ("CSSetUnorderedAccessViews", "ID3D11UnorderedAccessView"),
        ):
            pattern = re.compile(
                rf"{re.escape(typename)}\*\s+(\w+)\[(\d+)\]\s*=.*?;",
                re.DOTALL,
            )
            checked = 0
            for match in pattern.finditer(self.runtime):
                name, declared = match.group(1), int(match.group(2))
                tail = self.runtime[match.end(): match.end() + 2200]
                call = re.search(
                    rf"{api}\(\s*0u\s*,\s*(\d+)u\s*,\s*{re.escape(name)}\b",
                    tail,
                )
                if not call:
                    continue
                requested = int(call.group(1))
                self.assertLessEqual(
                    requested,
                    declared,
                    f"{api} requests {requested} entries from {name}[{declared}]",
                )
                checked += 1
            self.assertGreater(checked, 0, f"No {api} local-array bindings checked")


if __name__ == "__main__":
    unittest.main()
