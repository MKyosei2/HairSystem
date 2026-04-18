cbuffer CoverageConstants : register(b0)
{
    row_major float4x4 ViewProjection;
    float4 HairRootTintColor;
    float4 ScalpBaseColor;
    float ScalpCoverageAlpha;
    float _Pad0;
    float _Pad1;
    float _Pad2;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

float WrapSignedUnitDelta(float v)
{
    while (v > 0.5f) v -= 1.0f;
    while (v < -0.5f) v += 1.0f;
    return v;
}

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    float3 worldPos = input.position + input.normal * 0.0008f;
    o.position = mul(float4(worldPos, 1.0f), ViewProjection);
    o.uv = input.uv;
    return o;
}

float4 PSMain(VSOutput input) : SV_Target
{
    float dx = WrapSignedUnitDelta(input.uv.x - 0.5f);
    float dy = input.uv.y - 0.20f;

    float crown = exp(-(dx * dx * 2.0f + dy * dy * 1.2f) / 0.030f);
    float partLine = exp(-(dx * dx) / 0.0008f) * exp(-(dy * dy) / 0.050f);
    float shoulderL = exp(-((dx + 0.028f) * (dx + 0.028f)) / 0.0018f) * exp(-(dy * dy) / 0.060f);
    float shoulderR = exp(-((dx - 0.028f) * (dx - 0.028f)) / 0.0018f) * exp(-(dy * dy) / 0.060f);

    float coverage = 0.15f + 0.55f * crown - 0.35f * partLine + 0.25f * (shoulderL + shoulderR);
    coverage = saturate(coverage * ScalpCoverageAlpha);
    float3 color = lerp(ScalpBaseColor.xyz, HairRootTintColor.xyz, 0.80f);
    return float4(color, coverage);
}
