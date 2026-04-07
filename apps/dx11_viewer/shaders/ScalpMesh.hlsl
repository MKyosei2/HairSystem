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

StructuredBuffer<float4> ScalpPositions : register(t0);

struct VSInput
{
    uint scalpIndex : TEXCOORD0;
    float3 normal : NORMAL;
    float3 color : COLOR0;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 worldNormal : NORMAL0;
    float3 color : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    const float3 position = ScalpPositions[input.scalpIndex].xyz;
    output.position = mul(float4(position, 1.0f), ViewProjection);
    output.worldPosition = position;
    output.worldNormal = normalize(input.normal);
    output.color = input.color;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    const float3 N = normalize(input.worldNormal);
    const float3 L = normalize(-LightDirection.xyz);
    const float3 V = normalize(CameraPosition.xyz - input.worldPosition);
    const float3 H = normalize(L + V);

    const float diffuse = saturate(dot(N, L));
    const float fresnel = pow(1.0f - saturate(dot(N, V)), 3.0f);
    const float specular = pow(saturate(dot(N, H)), 20.0f) * 0.18f;

    const float3 baseColor = input.color;
    const float3 rimColor = float3(0.90f, 0.82f, 0.78f);
    const float3 shaded = baseColor * (0.22f + 0.78f * diffuse) + rimColor * (fresnel * 0.08f) + specular.xxx;
    return float4(shaded, 1.0f);
}
