// HAIRGPU_STRAND_RIBBON_VISIBILITY_V15_DEPTH_OCCLUDED
// HAIRGPU_STRAND_RIBBON_SHADER_ABI_1
// Guaranteed-visible, thin camera-facing ribbon path.
// - never treats authored width/alpha as strand validity
// - keeps hidden LOD instances off-screen instead of alpha-clipping
// - derives a distance-scaled minimum width so ribbons remain visible on high-DPI/high-resolution displays
// - keeps the legacy 100k instanced draw contract unchanged

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
    return (lengthSquared > 1.0e-10f) ? value * rsqrt(lengthSquared) : fallback;
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
    return all(value == value) && all(abs(value) < 100000.0f);
}

float3 ComputeHairColor(uint strandIndex, float widthN)
{
    const float variation = lerp(0.84f, 1.12f, Hash01(strandIndex + 1u));
    const float3 rootColor = float3(0.88f, 0.68f, 0.20f);
    const float3 tipColor = float3(0.44f, 0.25f, 0.055f);
    float3 naturalColor = lerp(tipColor, rootColor, 0.28f + 0.72f * widthN) * variation;

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
        return float3(0.95f, 0.72f, 0.20f);
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

    // Never connect a valid hair point to world origin. That fallback created
    // the long radial spikes visible around the crown. Collapse an invalid pair
    // onto the nearest valid sample from the same strand instead.
    if (!valid0 && valid1)
    {
        point0 = point1;
    }
    else if (valid0 && !valid1)
    {
        point1 = point0;
    }
    else if (!valid0 && !valid1)
    {
        float3 root = LoadPosition(strandIndex, 0u);
        if (!IsPlausiblePosition(root))
        {
            root = float3(0.0f, 1.60f, 0.0f);
        }
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
    if (!(width0 == width0) || abs(width0) > 0.05f)
    {
        width0 = 0.0f;
    }
    if (!(width1 == width1) || abs(width1) > 0.05f)
    {
        width1 = width0;
    }
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
        const uint followerLocal = strandIndex - GuideStrandCount;
        const uint followerStride = max(RenderFollowerStride, 1u);
        const uint tier = StrandTiers[strandIndex];
        // Use a stable per-strand stochastic subset instead of taking the
        // same follower slot around every guide. Modulo selection exposed the
        // same angular gaps at every root cluster and looked like bald patches.
        const float keepProbability = 1.0f / float(followerStride);
        const float visibilitySample = Hash01(
            strandIndex * 747796405u + 2891336453u);
        visible =
            tier < RenderGuideOnlyAboveTier &&
            visibilitySample < keepProbability;
    }

    if (!visible)
    {
        // Keep rejected LOD instances outside clip space. This avoids the prior
        // alpha-clip/depth-write regression while preserving the single draw call.
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

    float3 ribbonSide = cross(tangent, viewDirection);
    if (dot(ribbonSide, ribbonSide) <= 1.0e-10f)
    {
        const float3 fallbackAxis = (abs(tangent.y) < 0.90f)
            ? float3(0.0f, 1.0f, 0.0f)
            : float3(1.0f, 0.0f, 0.0f);
        ribbonSide = cross(tangent, fallbackAxis);
    }
    ribbonSide = SafeNormalize(ribbonSide, float3(1.0f, 0.0f, 0.0f));
    const float3 ribbonNormal = SafeNormalize(cross(ribbonSide, tangent), viewDirection);

    const float authoredWidth = SampleStrandWidth(strandIndex, u);
    const float cameraDistance = max(length(CameraPosition.xyz - center), 0.10f);

    // Keep a sub-pixel coverage floor without inflating every strand to a
    // one-pixel ribbon. The old floor made 50k crown followers merge into a cap.
    const float screenStableFloor = cameraDistance * (isGuide ? 0.00022f : 0.00040f);
    const float authoredScale = isGuide ? 0.34f : 1.02f;
    float width = max(authoredWidth * authoredScale, screenStableFloor);
    width = clamp(
        width,
        isGuide ? 0.00008f : 0.00020f,
        isGuide ? 0.00060f : 0.00138f);

    const float strandVariation = lerp(0.92f, 1.10f, Hash01(strandIndex + 17u));
    const float rootOpening = lerp(0.94f, 1.0f, smoothstep(0.0f, 0.10f, u));
    const float rootToTipTaper = lerp(1.0f, 0.24f, smoothstep(0.58f, 1.0f, u));
    width *= strandVariation * rootOpening * rootToTipTaper;
    if (!isGuide && StrandTiers[strandIndex] == 1u)
    {
        width *= max(RenderReducedWidthBoost, 0.84f);
    }

    const float sideSign = ((input.vertexId & 1u) == 0u) ? -1.0f : 1.0f;
    const float3 worldPosition = center + ribbonSide * (0.5f * width * sideSign);
    const float widthN = saturate(width / 0.0014f);

    const float rootAlpha = lerp(0.90f, 1.0f, smoothstep(0.0f, 0.08f, u));
    const float tipAlpha = lerp(1.0f, 0.44f, smoothstep(0.74f, 1.0f, u));
    const float materialAlpha = max(RenderStrandAlpha, 0.02f);
    const float alphaScale = isGuide ? 0.06f : 0.88f;

    output.position = mul(float4(worldPosition, 1.0f), ViewProjection);
    output.worldNormal = ribbonNormal;
    output.worldTangent = tangent;
    output.worldPosition = worldPosition;
    output.color = ComputeHairColor(strandIndex, widthN);
    const float baseAlpha = saturate(materialAlpha * alphaScale * rootAlpha * tipAlpha);
    // A stochastic 1/N follower subset must preserve the optical coverage of
    // the full set. Without this compensation, editing/LOD preview looked bald.
    const float followerCoverageAlpha = 1.0f - pow(
        saturate(1.0f - baseAlpha),
        float(max(RenderFollowerStride, 1u)));
    output.alpha = isGuide ? baseAlpha : saturate(followerCoverageAlpha);
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
    const float anisotropic = pow(saturate(1.0f - tangentHalf * tangentHalf), 18.0f) * 0.26f;
    const float rim = pow(1.0f - saturate(abs(dot(normal, view))), 2.0f) * 0.08f;
    const float3 shaded =
        input.color * (0.24f + 0.76f * diffuseWrap) +
        (anisotropic + rim).xxx;

    // No clip/discard here. Width and alpha are material properties, not
    // validity signals; rejecting by either caused the complete disappearance.
    return float4(shaded, input.alpha);
}
