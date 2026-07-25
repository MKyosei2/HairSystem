# Hair Strand Shader ABI Fix

## Problem

The V12 `HairStrandInstanced.hlsl` source was valid, but the runtime loader still
required the V11 visual-version marker. Every existing candidate was therefore
rejected as stale before `D3DCompileFromFile` was called, producing
`HRESULT=0x80070002`.

## Fix

- Add the stable marker `HAIRGPU_STRAND_RIBBON_SHADER_ABI_1` to the shader.
- Validate this ABI marker in the runtime loader.
- Keep the visual marker (`V12_DENSE_COVERAGE`) independent from the ABI.
- Add a regression test so later V13/V14 visual tuning does not repeat the same
  startup failure.
