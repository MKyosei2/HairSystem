from __future__ import annotations

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
TOUCHED: list[str] = []


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    (ROOT / path).write_text(text, encoding="utf-8", newline="\n")
    TOUCHED.append(path)


# 1) Register the final inextensibility compute shader.
path = "apps/dx11_viewer/src/GpuHairViewer.h"
text = read(path)
old = "        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_distanceCS;\n        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_bendCS;\n"
new = "        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_distanceCS;\n        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_stretchGuardCS;\n        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_bendCS;\n"
if old not in text:
    raise RuntimeError("GpuHairViewer stretch-guard member anchor missing")
write(path, text.replace(old, new, 1))


# 2) Compile the shader and dispatch it after the final collision pass, before
# followers consume the guide positions.
path = "apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl"
text = read(path)
old = '''            !CompileShaderFromFile(L"HairSim.hlsl", "DistanceCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_distanceCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "BendCS", "cs_5_0", &csBlob, errorText) ||
'''
new = '''            !CompileShaderFromFile(L"HairSim.hlsl", "DistanceCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_distanceCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "StretchGuardCS", "cs_5_0", &csBlob, errorText) ||
            FAILED(m_device->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &m_stretchGuardCS)) ||
            !CompileShaderFromFile(L"HairSim.hlsl", "BendCS", "cs_5_0", &csBlob, errorText) ||
'''
if old not in text:
    raise RuntimeError("LoadShaders distance/bend anchor missing")
text = text.replace(old, new, 1)

old = '''        // Followers consume only the final solved guide state. Running this inside every
        // solver iteration produced the same final dependency but repeated 96k x 128 work.
        bindPrimaryUavs();
        const uint32_t followerPointDispatchCount = followerDispatchCount * particlesPerStrand;
'''
new = '''        // Collision is the final operation inside each solver iteration, so it can
        // reintroduce segment extension after DistanceCS. Apply one deterministic,
        // root-to-tip inextensibility pass after the final collision and before the
        // followers consume the guide positions. This preserves each authored rest
        // length instead of shortening the benchmark asset.
        bindPrimaryUavs();
        dispatchTiled(m_stretchGuardCS.Get(), guideDispatchCount, kHairThreadsPerGroup);

        // Followers consume only the final solved guide state. Running this inside every
        // solver iteration produced the same final dependency but repeated 96k x 128 work.
        bindPrimaryUavs();
        const uint32_t followerPointDispatchCount = followerDispatchCount * particlesPerStrand;
'''
if old not in text:
    raise RuntimeError("final solver/follower anchor missing")
write(path, text.replace(old, new, 1))


# 3) Add the hard extension guard. It only corrects segments that exceed the
# authored rest length by more than 0.1%; compression and bend remain untouched.
path = "apps/dx11_viewer/shaders/HairSim.hlsl"
text = read(path)
anchor = '''[numthreads(64, 1, 1)]
void BendCS(uint3 id : SV_DispatchThreadID)
'''
shader = r'''
[numthreads(64, 1, 1)]
void StretchGuardCS(uint3 id : SV_DispatchThreadID)
{
    uint dispatchIndex = DispatchBaseIndex + id.x;
    uint strandIndex = ResolveGuideStrandIndex(dispatchIndex);
    if (strandIndex == 0xFFFFFFFFu || strandIndex >= GuideStrandCount)
    {
        return;
    }

    uint start = strandIndex * ParticlesPerStrand;
    static const float kMaximumStretchRatio = 1.001f;

    // One thread owns one guide, so this root-to-tip pass has no inter-thread
    // write conflict. Each child is moved while its parent remains fixed; once
    // a segment is corrected, a later segment cannot stretch it again.
    [loop]
    for (uint i = 1u; i < ParticlesPerStrand; ++i)
    {
        uint previousIndex = start + i - 1u;
        uint currentIndex = start + i;

        float3 parentPosition = OutputPositions[previousIndex].xyz;
        float3 childPosition = OutputPositions[currentIndex].xyz;
        float3 restParent = RestPositions[previousIndex].xyz;
        float3 restChild = RestPositions[currentIndex].xyz;
        float targetLength = max(length(restChild - restParent), 1.0e-5f);

        float3 segment = childPosition - parentPosition;
        float currentLength = length(segment);
        if (currentLength <= targetLength * kMaximumStretchRatio)
        {
            continue;
        }

        float3 direction = (currentLength > 1.0e-6f)
            ? segment / currentLength
            : SafeNormalize(restChild - restParent);
        float3 correctedPosition = parentPosition + direction * targetLength;
        float3 correction = correctedPosition - childPosition;

        OutputPositions[currentIndex] = float4(correctedPosition, 1.0f);

        // Move the previous-position sample by the same correction so the guard
        // does not inject artificial velocity into the next Verlet integration.
        float3 previousPosition = OutputPreviousPositions[currentIndex].xyz;
        OutputPreviousPositions[currentIndex] = float4(previousPosition + correction, 1.0f);
    }
}

'''
if "void StretchGuardCS" in text:
    raise RuntimeError("StretchGuardCS already exists")
if anchor not in text:
    raise RuntimeError("HairSim BendCS anchor missing")
write(path, text.replace(anchor, shader + anchor, 1))

EXPECTED = [
    "apps/dx11_viewer/src/GpuHairViewer.h",
    "apps/dx11_viewer/src/GpuHairViewer_parts/03_simulation_reset_and_frame_step.inl",
    "apps/dx11_viewer/shaders/HairSim.hlsl",
]
if sorted(set(TOUCHED)) != sorted(EXPECTED):
    raise RuntimeError(f"unexpected modified-file set: {sorted(set(TOUCHED))}")
(ROOT / "inextensible_stretch_guard_modified_files.txt").write_text(
    "\n".join(EXPECTED) + "\n",
    encoding="utf-8",
)
print("Patched inextensible stretch guard:")
print("\n".join(EXPECTED))
