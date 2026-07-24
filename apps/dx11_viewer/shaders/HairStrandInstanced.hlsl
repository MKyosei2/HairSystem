// HAIRGPU_STRAND_RIBBON_VISIBILITY_V17_SEPARATE_RENDER_AND_EDIT
// HAIRGPU_STRAND_RIBBON_SHADER_ABI_1
// Camera-facing instanced ribbon path.
// Rendering tiers and edit categories use independent SRVs.
// Authored width and simulation tier never decide strand validity.

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
StructuredBuffer<uint> RenderTiers : register(t2);
StructuredBuffer<uint> EditCategories : register(t3);

struct VSInput
{
    uint vertexId : SV_VertexID;
    uint instanceId : SV_InstanceID;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 worldNormal : NORMAL0;
    float3 worldTangent : TANGENT0;
    float3 worldPosition : TEXCOORD0;
    float3 color : COLOR0;
    float alpha : TEXCOORD1;
};

float3 SafeNormalize(float3 value, float3 fallback)
{
    const float lengthSquared = dot(value, value);
    return (lengthSquared > 1.0e-10f)
        ? value * rsqrt(lengthSquared)
        : fallback;
}

float Hash01(uint value)
{
    value ^= value >> 16u;
    value *= 0x7feb352du;
    value ^= value >> 15u;
    value *= 0x846ca68bu;
    value ^= value >> 16u;
    return float(value & 0x00ffffffu) / 16777215.0f;
}

bool IsPlausiblePosition(float3 value)
{
    return all(value == value) && all(abs(value) < 1000.0f);
}

float3 ComputeHairColor(uint strandIndex, float widthN, uint editCategory)
{
    const float variation = lerp(0.88f, 1.08f, Hash01(strandIndex + 1u));
    const float3 rootColor = float3(0.32f, 0.20f, 0.085f);
    const float3 tipColor = float3(0.075f, 0.045f, 0.020f);
    const float3 naturalColor = lerp(tipColor, rootColor, 0.30f + 0.70f * widthN) * variation;

    if (DebugColorMode == 1u)
    {
        return (strandIndex < GuideStrandCount)
            ? float3(0.18f, 0.95f, 0.85f)
            : float3(0.98f, 0.52f, 0.18f);
    }
    if (DebugColorMode == 2u)
    {
        return lerp(float3(0.12f, 0.35f, 1.0f), float3(1.0f, 0.20f, 0.10f), widthN);
    }
    if (DebugColorMode == 3u)
    {
        if (editCategory == 0u) return float3(0.12f, 0.95f, 0.48f);
        if (editCategory == 2u) return float3(1.00f, 0.18f, 0.86f);
        if (editCategory == 3u) return float3(1.00f, 0.72f, 0.20f);
        if (editCategory == 4u) return float3(1.00f, 0.92f, 0.18f);
        return float3(0.38f, 0.42f, 0.48f);
    }
    return naturalColor;
}

float3 LoadPosition(uint strandIndex, uint pointIndex)
{
    const uint safePointCount = max(ParticlesPerStrand, 1u);
    const uint safeStrandCount = max(TotalStrandCount, 1u);
    const uint safeStrand = min(strandIndex, safeStrandCount - 1u);
    const uint safePoint = min(pointIndex, safePointCount - 1u);
    return Positions[safeStrand * safePointCount + safePoint].xyz;
}

float3 SampleStrandPosition(uint strandIndex, float u)
{
    const uint safePointCount = max(ParticlesPerStrand, 1u);
    const float scaled = saturate(u) * max(float(safePointCount - 1u), 0.0f);
    const uint local0 = min((uint)floor(scaled), safePointCount - 1u);
    const uint local1 = min(local0 + 1u, safePointCount - 1u);
    const float interpolation = scaled - float(local0);

    float3 point0 = LoadPosition(strandIndex, local0);
    float3 point1 = LoadPosition(strandIndex, local1);
    const bool valid0 = IsPlausiblePosition(point0);
    const bool valid1 = IsPlausiblePosition(point1);
    if (!valid0 && valid1) point0 = point1;
    else if (valid0 && !valid1) point1 = point0;
    else if (!valid0 && !valid1)
    {
        float3 root = LoadPosition(strandIndex, 0u);
        if (!IsPlausiblePosition(root)) root = float3(0.0f, 1.60f, 0.0f);
        point0 = root;
        point1 = root;
    }
    return lerp(point0, point1, interpolation);
}

float SampleStrandWidth(uint strandIndex, float u)
{
    const uint safePointCount = max(ParticlesPerStrand, 1u);
    const uint safeStrandCount = max(TotalStrandCount, 1u);
    const uint safeStrand = min(strandIndex, safeStrandCount - 1u);
    const float scaled = saturate(u) * max(float(safePointCount - 1u), 0.0f);
    const uint local0 = min((uint)floor(scaled), safePointCount - 1u);
    const uint local1 = min(local0 + 1u, safePointCount - 1u);
    const float interpolation = scaled - float(local0);
    const uint strandBase = safeStrand * safePointCount;

    float width0 = Widths[strandBase + local0];
    float width1 = Widths[strandBase + local1];
    if (!(width0 == width0) || width0 < 0.0f || width0 > 0.05f) width0 = 0.0f;
    if (!(width1 == width1) || width1 < 0.0f || width1 > 0.05f) width1 = width0;
    return max(lerp(width0, width1, interpolation), 0.0f) * max(WidthScale, 0.05f);
}

VSOutput VSMain(VSInput input)
{
    VSOutput output;

    const uint safeStrandCount = max(TotalStrandCount, 1u);
    const uint safePointCount = max(ParticlesPerStrand, 1u);
    const uint strandIndex = min(input.instanceId, safeStrandCount - 1u);
    const uint pointIndex = min(input.vertexId >> 1u, safePointCount - 1u);
    const float u = (safePointCount > 1u)
        ? float(pointIndex) / float(safePointCount - 1u)
        : 0.0f;
    const float du = 1.0f / max(float(safePointCount - 1u), 1.0f);

    const bool isGuide = strandIndex < GuideStrandCount;
    bool visible = true;
    if (!isGuide)
    {
        // Decimate only when CPU explicitly requests a transient interaction
        // stride. Simulation tier and edit category never hide a strand.
        const uint followerStride = max(RenderFollowerStride, 1u);
        if (followerStride > 1u)
        {
            const float keepProbability = 1.0f / float(followerStride);
            visible = Hash01(strandIndex * 747796405u + 2891336453u) < keepProbability;
        }
    }

    if (!visible)
    {
        output.position = float4(0.0f, 0.0f, 2.0f, 1.0f);
        output.worldNormal = float3(0.0f, 0.0f, 1.0f);
        output.worldTangent = float3(0.0f, 1.0f, 0.0f);
        output.worldPosition = float3(0.0f, 0.0f, 0.0f);
        output.color = float3(0.0f, 0.0f, 0.0f);
        output.alpha = 0.0f;
        return output;
    }

    const float3 center = SampleStrandPosition(strandIndex, u);
    const float3 previousPoint = SampleStrandPosition(strandIndex, max(u - du, 0.0f));
    const float3 nextPoint = SampleStrandPosition(strandIndex, min(u + du, 1.0f));
    const float3 tangent = SafeNormalize(nextPoint - previousPoint, float3(0.0f, -1.0f, 0.0f));
    const float3 viewDirection = SafeNormalize(CameraPosition.xyz - center, float3(0.0f, 0.0f, 1.0f));

    float3 ribbonSide = cross(viewDirection, tangent);
    if (dot(ribbonSide, ribbonSide) <= 1.0e-10f)
    {
        const float3 fallbackAxis = (abs(tangent.y) < 0.90f)
            ? float3(0.0f, 1.0f, 0.0f)
            : float3(1.0f, 0.0f, 0.0f);
        ribbonSide = cross(fallbackAxis, tangent);
    }
    ribbonSide = SafeNormalize(ribbonSide, float3(1.0f, 0.0f, 0.0f));
    const float3 ribbonNormal = SafeNormalize(cross(tangent, ribbonSide), viewDirection);

    const float authoredWidth = SampleStrandWidth(strandIndex, u);
    const float cameraDistance = max(length(CameraPosition.xyz - center), 0.10f);
    // Approximately two pixels at a 60-degree vertical FOV, independent of
    // camera distance. This avoids point/dash rendering after auto framing.
    const float distanceScaledHalfWidth = cameraDistance * 0.00125f;
    const float authoredHalfWidth = authoredWidth * (isGuide ? 0.22f : 0.50f);
    float halfWidth = max(authoredHalfWidth, distanceScaledHalfWidth);
    halfWidth = min(halfWidth, cameraDistance * 0.0030f);

    const float rootMask = 1.0f - smoothstep(0.0f, 0.10f, u);
    const float tipTaper = lerp(1.0f, 0.34f, smoothstep(0.70f, 1.0f, u));
    halfWidth *= lerp(1.12f, 1.0f, 1.0f - rootMask) * tipTaper;

    const float sideSign = ((input.vertexId & 1u) == 0u) ? -1.0f : 1.0f;
    const float3 worldPosition = center + ribbonSide * (halfWidth * sideSign);
    const float widthN = saturate(authoredWidth / 0.008f);
    const uint editCategory = EditCategories[strandIndex];

    output.position = mul(float4(worldPosition, 1.0f), ViewProjection);
    output.worldNormal = ribbonNormal;
    output.worldTangent = tangent;
    output.worldPosition = worldPosition;
    output.color = ComputeHairColor(strandIndex, widthN, editCategory);
    // Width is a material property, never a validity signal. Keep a stable
    // positive alpha even when authored tip width approaches zero.
    const float tipAlpha = lerp(1.0f, 0.70f, smoothstep(0.82f, 1.0f, u));
    output.alpha = saturate(max(RenderStrandAlpha, 0.20f) * tipAlpha);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    const float3 normal = SafeNormalize(input.worldNormal, float3(0.0f, 0.0f, 1.0f));
    const float3 tangent = SafeNormalize(input.worldTangent, float3(0.0f, 1.0f, 0.0f));
    const float3 light = SafeNormalize(-LightDirection.xyz, float3(0.0f, 1.0f, 0.0f));
    const float3 view = SafeNormalize(CameraPosition.xyz - input.worldPosition, float3(0.0f, 0.0f, 1.0f));
    const float3 halfVector = SafeNormalize(light + view, view);

    const float diffuseWrap = saturate(dot(normal, light) * 0.42f + 0.58f);
    const float tangentHalf = saturate(abs(dot(tangent, halfVector)));
    const float anisotropic = pow(saturate(1.0f - tangentHalf * tangentHalf), 18.0f) * 0.20f;
    const float rim = pow(1.0f - saturate(abs(dot(normal, view))), 2.0f) * 0.06f;
    const float3 shaded = input.color * (0.34f + 0.66f * diffuseWrap) + (anisotropic + rim).xxx;
    return float4(shaded, input.alpha);
}
