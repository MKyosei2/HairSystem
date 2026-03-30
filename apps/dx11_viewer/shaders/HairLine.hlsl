cbuffer RenderConstants : register(b0)
{
    row_major float4x4 ViewProjection;
    float4 CameraPosition;
    float4 LightDirection;
    float WidthScale;
    uint DebugColorMode;
    uint GuideStrandCount;
    uint RenderFollowerStride;
    uint RenderGuideOnlyAboveTier;
    uint RenderTubeSides;
    float RenderReducedWidthBoost;
    float RenderHiddenWidthThreshold;
};

StructuredBuffer<float4> Positions : register(t0);
StructuredBuffer<float> Widths : register(t1);
StructuredBuffer<uint> StrandTiers : register(t2);

struct VSInput
{
    uint particleIndex : PARTICLE_INDEX;
    uint strandIndex : STRAND_INDEX;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

float3 ComputeHairColor(uint strandIndex, float widthN)
{
    const float strandBand = frac((float)strandIndex * 0.61803398875f);
    const float3 rootColor = float3(1.00f, 0.88f, 0.38f);
    const float3 tipColor  = float3(0.78f, 0.58f, 0.18f);
    const float3 naturalColor = lerp(tipColor, rootColor, 0.35f + 0.65f * widthN) * (0.80f + 0.20f * strandBand);

    if (DebugColorMode == 1)
    {
        const bool isGuide = strandIndex < GuideStrandCount;
        return isGuide ? float3(0.18f, 0.95f, 0.85f) : float3(0.98f, 0.52f, 0.18f);
    }
    if (DebugColorMode == 2)
    {
        return lerp(float3(0.12f, 0.35f, 1.0f), float3(1.0f, 0.20f, 0.10f), widthN);
    }
    return naturalColor;
}

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    const float3 worldPosition = Positions[input.particleIndex].xyz;
    const uint strandTier = StrandTiers[input.strandIndex];

    float width = Widths[input.particleIndex] * max(WidthScale, 0.05f);
    const bool isGuide = input.strandIndex < GuideStrandCount;
    if (!isGuide)
    {
        const uint followerLocal = input.strandIndex - GuideStrandCount;
        const uint followerStride = max(RenderFollowerStride, 1u);
        if ((strandTier >= RenderGuideOnlyAboveTier) || ((followerLocal % followerStride) != 0u))
        {
            width = 0.0f;
        }
    }

    if (strandTier == 1u)
    {
        width *= RenderReducedWidthBoost;
    }

    const float widthN = saturate((width - 0.0010f) / 0.0120f);
    output.position = mul(float4(worldPosition, 1.0f), ViewProjection);
    float alpha = (width > RenderHiddenWidthThreshold) ? 1.0f : 0.0f;
    output.color = float4(ComputeHairColor(input.strandIndex, widthN), alpha);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    return input.color;
}
