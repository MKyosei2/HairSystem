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
    uint _RenderPadding0;
    uint _RenderPadding1;
};

StructuredBuffer<float4> Positions : register(t0);
StructuredBuffer<float> Widths : register(t1);

struct VSInput
{
    float strandU : TEXCOORD0;
    float angle : TEXCOORD1;
    uint instanceId : SV_InstanceID;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 worldNormal : NORMAL0;
    float3 worldPosition : TEXCOORD0;
    float3 color : COLOR0;
};

float3 SafeNormalize(float3 v)
{
    float lenSq = dot(v, v);
    return (lenSq > 1.0e-8f) ? (v * rsqrt(lenSq)) : float3(0.0f, 1.0f, 0.0f);
}

float3 ComputeHairColor(uint strandIndex, float widthN)
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
    return naturalColor;
}

float3 SampleStrandPosition(uint strandIndex, float u)
{
    const uint strandBase = strandIndex * ParticlesPerStrand;
    const float scaled = saturate(u) * max((float)ParticlesPerStrand - 1.0f, 0.0f);
    const uint local0 = min((uint)floor(scaled), max(ParticlesPerStrand - 1u, 0u));
    const uint local1 = min(local0 + 1u, max(ParticlesPerStrand - 1u, 0u));
    const float t = scaled - (float)local0;
    const float3 p0 = Positions[strandBase + local0].xyz;
    const float3 p1 = Positions[strandBase + local1].xyz;
    return lerp(p0, p1, t);
}

float SampleStrandWidth(uint strandIndex, float u)
{
    const uint strandBase = strandIndex * ParticlesPerStrand;
    const float scaled = saturate(u) * max((float)ParticlesPerStrand - 1.0f, 0.0f);
    const uint local0 = min((uint)floor(scaled), max(ParticlesPerStrand - 1u, 0u));
    const uint local1 = min(local0 + 1u, max(ParticlesPerStrand - 1u, 0u));
    const float t = scaled - (float)local0;
    return lerp(Widths[strandBase + local0], Widths[strandBase + local1], t) * max(WidthScale, 0.05f);
}

VSOutput VSMain(VSInput input)
{
    VSOutput output;

    const uint strandIndex = min(input.instanceId, max(TotalStrandCount, 1u) - 1u);
    const float u = saturate(input.strandU);
    const float prevU = max(u - (1.0f / max((float)ParticlesPerStrand - 1.0f, 1.0f)), 0.0f);
    const float nextU = min(u + (1.0f / max((float)ParticlesPerStrand - 1.0f, 1.0f)), 1.0f);

    const float3 center = SampleStrandPosition(strandIndex, u);
    const float3 prevP = SampleStrandPosition(strandIndex, prevU);
    const float3 nextP = SampleStrandPosition(strandIndex, nextU);
    const float3 tangent = SafeNormalize(nextP - prevP);

    const float3 root = SampleStrandPosition(strandIndex, 0.0f);
    float3 rootDir = SafeNormalize(center - root);
    if (abs(dot(rootDir, tangent)) > 0.96f)
    {
        rootDir = float3(0.0f, 1.0f, 0.0f);
    }
    float3 viewDir = SafeNormalize(CameraPosition.xyz - center);
    float3 baseNormal = SafeNormalize(cross(rootDir, tangent));
    float3 viewNormal = SafeNormalize(cross(viewDir, tangent));
    float3 normal = SafeNormalize(lerp(baseNormal, viewNormal, 0.28f));
    if (dot(normal, normal) < 1.0e-6f)
    {
        float3 upRef = abs(dot(float3(0.0f, 1.0f, 0.0f), tangent)) > 0.92f ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 1.0f, 0.0f);
        normal = SafeNormalize(cross(tangent, upRef));
    }
    float3 binormal = SafeNormalize(cross(tangent, normal));
    normal = SafeNormalize(cross(binormal, tangent));

    const float width = max(SampleStrandWidth(strandIndex, u), 0.0015f);
    const float radius = max(width * 0.48f, 0.0018f);
    const float3 radialDir = cos(input.angle) * normal + sin(input.angle) * binormal;
    const float3 worldPosition = center + radialDir * radius;

    const float widthN = saturate((width - 0.0010f) / 0.0120f);
    output.position = mul(float4(worldPosition, 1.0f), ViewProjection);
    output.worldNormal = radialDir;
    output.worldPosition = worldPosition;
    output.color = ComputeHairColor(strandIndex, widthN);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    const float3 N = SafeNormalize(input.worldNormal);
    const float3 L = SafeNormalize(-LightDirection.xyz);
    const float3 V = SafeNormalize(CameraPosition.xyz - input.worldPosition);
    const float3 H = SafeNormalize(L + V);
    const float diffuse = saturate(dot(N, L));
    const float specular = pow(saturate(dot(N, H)), 24.0f) * 0.18f;
    const float lighting = 0.24f + 0.76f * diffuse;
    return float4(input.color * lighting + specular.xxx, 0.98f);
}
