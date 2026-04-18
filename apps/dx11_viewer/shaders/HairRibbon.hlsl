cbuffer RenderConstants : register(b0)
{
    row_major float4x4 ViewProjection;
    float4 CameraPosition;
    float4 LightDirection;
    float WidthScale;
    uint DebugColorMode;
    uint GuideStrandCount;
    uint ParticlesPerStrand;
    uint TotalStrandCount;
    uint TubeRingSides;
    uint RenderFollowerStride;
    uint RenderGuideOnlyAboveTier;
    float RenderReducedWidthBoost;
    float RenderHiddenWidthThreshold;
    float RenderStrandAlpha;
    float _RenderPadding0;
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
    float3 worldPosition : POSITION0;
    float width : WIDTH0;
    float3 color : COLOR0;
    float alpha : TEXCOORD0;
};

float3 EditabilityColor(uint category)
{
    if (category == 0u) return float3(0.12f, 0.95f, 0.48f);   // editable
    if (category == 2u) return float3(1.00f, 0.18f, 0.86f);   // selected strand
    if (category == 3u) return float3(1.00f, 0.72f, 0.20f);   // related guide/family
    if (category == 4u) return float3(1.00f, 0.92f, 0.18f);   // boundary band
    return float3(0.30f, 0.32f, 0.36f);                       // locked
}

float EditabilityAlphaScale(uint category)
{
    if (category == 0u) return 1.00f;
    if (category == 2u) return 1.00f;
    if (category == 3u) return 0.95f;
    if (category == 4u) return 0.88f;
    return 0.24f;
}

float EditabilityWidthScale(uint category)
{
    if (category == 2u) return 2.20f;
    if (category == 3u) return 1.35f;
    if (category == 4u) return 1.15f;
    if (category == 0u) return 1.08f;
    return 0.70f;
}

float3 ComputeHairColor(uint strandIndex, float widthN, uint strandTier)
{
    const float strandBand = frac((float)strandIndex * 0.61803398875f);
    const float3 rootColor = float3(1.00f, 0.88f, 0.38f);
    const float3 tipColor  = float3(0.78f, 0.58f, 0.18f);
    const float3 naturalColor = lerp(tipColor, rootColor, 0.35f + 0.65f * widthN) * (0.82f + 0.18f * strandBand);

    if (DebugColorMode == 1)
    {
        const bool isGuide = strandIndex < GuideStrandCount;
        return isGuide ? float3(0.18f, 0.95f, 0.85f) : float3(0.98f, 0.52f, 0.18f);
    }
    if (DebugColorMode == 2)
    {
        return lerp(float3(0.12f, 0.35f, 1.0f), float3(1.0f, 0.20f, 0.10f), widthN);
    }
    if (DebugColorMode == 3)
    {
        return EditabilityColor(strandTier);
    }
    return naturalColor;
}

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.worldPosition = Positions[input.particleIndex].xyz;

    const uint strandTier = StrandTiers[input.strandIndex];
    const uint safeParticlesPerStrand = max(ParticlesPerStrand, 1u);
    const uint localIndex = input.particleIndex % safeParticlesPerStrand;
    const float strandT = (safeParticlesPerStrand > 1u) ? (float(localIndex) / float(safeParticlesPerStrand - 1u)) : 0.0f;
    const float rootMask = exp(-(strandT * strandT) / 0.015f);
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

    width *= (1.0f + 0.28f * rootMask);
    if (DebugColorMode == 3u)
    {
        width *= EditabilityWidthScale(strandTier);
    }
    const float widthN = saturate((width - 0.0010f) / 0.0120f);
    output.width = width;
    const float alphaBoost = 1.0f + 0.18f * rootMask;
    output.alpha = (width > RenderHiddenWidthThreshold) ? (RenderStrandAlpha * alphaBoost * ((DebugColorMode == 3u) ? EditabilityAlphaScale(strandTier) : 1.0f)) : 0.0f;
    output.color = ComputeHairColor(input.strandIndex, widthN, strandTier);
    return output;
}

struct GSOutput
{
    float4 position : SV_Position;
    float3 worldNormal : NORMAL0;
    float3 worldPosition : TEXCOORD0;
    float3 color : COLOR0;
    float alpha : TEXCOORD1;
};

GSOutput MakeVertex(float3 worldPosition, float3 worldNormal, float3 color, float alpha)
{
    GSOutput o;
    o.position = mul(float4(worldPosition, 1.0f), ViewProjection);
    o.worldNormal = worldNormal;
    o.worldPosition = worldPosition;
    o.color = color;
    o.alpha = alpha;
    return o;
}

float3 SafeNormalize(float3 v, float3 fallback)
{
    const float lenSq = dot(v, v);
    return (lenSq > 1.0e-8f) ? (v * rsqrt(lenSq)) : fallback;
}

void BuildStableBasis(float3 tangent, out float3 axisX, out float3 axisY)
{
    const float3 refA = float3(0.0f, 1.0f, 0.0f);
    const float3 refB = float3(1.0f, 0.0f, 0.0f);
    const float3 ref = (abs(tangent.y) < 0.85f) ? refA : refB;
    axisX = SafeNormalize(cross(ref, tangent), float3(1.0f, 0.0f, 0.0f));
    axisY = SafeNormalize(cross(tangent, axisX), float3(0.0f, 0.0f, 1.0f));
}

[maxvertexcount(48)]
void GSMain(line VSOutput input[2], inout TriangleStream<GSOutput> outStream)
{
    if ((input[0].alpha <= 0.0f) && (input[1].alpha <= 0.0f))
    {
        return;
    }

    const float3 p0 = input[0].worldPosition;
    const float3 p1 = input[1].worldPosition;
    const float3 segment = p1 - p0;
    const float segmentLenSq = dot(segment, segment);
    if (segmentLenSq < 1.0e-8f)
    {
        return;
    }

    const float3 tangent = normalize(segment);
    float3 axisX;
    float3 axisY;
    BuildStableBasis(tangent, axisX, axisY);

    const float radius0 = max(input[0].width * 0.52f, 0.0020f);
    const float radius1 = max(input[1].width * 0.52f, 0.0018f);
    const uint sides = min(max(TubeRingSides, 4u), 8u);
    const float kTau = 6.28318530718f;

    [loop]
    for (uint side = 0u; side < sides; ++side)
    {
        const float a0 = (float(side) / float(sides)) * kTau;
        const float a1 = (float(side + 1u) / float(sides)) * kTau;

        const float3 ringDir0 = normalize(axisX * cos(a0) + axisY * sin(a0));
        const float3 ringDir1 = normalize(axisX * cos(a1) + axisY * sin(a1));

        const float3 v00 = p0 + ringDir0 * radius0;
        const float3 v01 = p0 + ringDir1 * radius0;
        const float3 v10 = p1 + ringDir0 * radius1;
        const float3 v11 = p1 + ringDir1 * radius1;

        outStream.Append(MakeVertex(v00, ringDir0, input[0].color, input[0].alpha));
        outStream.Append(MakeVertex(v10, ringDir0, input[1].color, input[1].alpha));
        outStream.Append(MakeVertex(v01, ringDir1, input[0].color, input[0].alpha));

        outStream.Append(MakeVertex(v01, ringDir1, input[0].color, input[0].alpha));
        outStream.Append(MakeVertex(v10, ringDir0, input[1].color, input[1].alpha));
        outStream.Append(MakeVertex(v11, ringDir1, input[1].color, input[1].alpha));

        outStream.RestartStrip();
    }
}

float4 PSMain(GSOutput input) : SV_Target
{
    const float3 N = normalize(input.worldNormal);
    const float3 L = normalize(-LightDirection.xyz);
    const float3 V = normalize(CameraPosition.xyz - input.worldPosition);
    const float3 H = normalize(L + V);

    const float diffuseFront = saturate(dot(N, L));
    const float diffuseWrap = saturate(dot(N, L) * 0.5f + 0.5f);
    const float specular = pow(saturate(dot(N, H)), 20.0f) * 0.16f;
    const float rim = pow(1.0f - saturate(abs(dot(N, V))), 2.0f) * 0.12f;
    const float lighting = 0.16f + 0.52f * diffuseFront + 0.20f * diffuseWrap;
    const float3 shaded = input.color * lighting + (specular + rim).xxx;
    return float4(shaded, input.alpha);
}
