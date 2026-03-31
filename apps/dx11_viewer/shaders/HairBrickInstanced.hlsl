cbuffer RenderConstants : register(b0)
{
    row_major float4x4 ViewProjection;
    float4 CameraPosition;
    float4 LightDirection;
    float WidthScale;
    uint DebugColorMode;
    uint GuideStrandCount;
    uint _Padding0;
};

StructuredBuffer<float4> Positions : register(t0);
StructuredBuffer<float> Widths : register(t1);

struct VSInput
{
    float3 localPosition : POSITION;
    float3 localNormal : NORMAL;
    uint particleIndex : PARTICLE_INDEX;
    uint strandIndex : STRAND_INDEX;
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
    const float3 tipColor  = float3(0.76f, 0.55f, 0.16f);
    const float shadeShift = 0.86f + 0.14f * strandBand;
    const float3 naturalColor = lerp(tipColor, rootColor, 0.42f + 0.58f * widthN) * shadeShift;

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

    float3 p0 = Positions[input.particleIndex].xyz;
    float3 p1 = Positions[input.particleIndex + 1].xyz;
    float3 tangent = SafeNormalize(p1 - p0);
    float segLen = max(length(p1 - p0), 1.0e-5f);
    float3 center = lerp(p0, p1, 0.5f);

    float3 rootDir = float3(0.0f, 1.0f, 0.0f);
    if (dot(rootDir, rootDir) < 1.0e-6f)
    {
        rootDir = float3(0.0f, 1.0f, 0.0f);
    }
    float3 cameraDir = SafeNormalize(CameraPosition.xyz - center);
    float3 frameRef = SafeNormalize(lerp(rootDir, cameraDir, 0.18f));
    float3 axisX = SafeNormalize(cross(frameRef, tangent));
    if (dot(axisX, axisX) < 1.0e-6f)
    {
        axisX = SafeNormalize(cross(float3(0.0f, 1.0f, 0.0f), tangent));
    }
    float3 axisY = SafeNormalize(cross(tangent, axisX));

    float width = Widths[input.particleIndex] * max(WidthScale, 0.05f);
    float widthN = saturate((width - 0.0010f) / 0.0120f);
    float brickJitter = frac((float)(input.particleIndex * 13u + input.strandIndex * 17u) * 0.61803398875f) - 0.5f;
    float halfX = max(width * (1.45f + 0.20f * brickJitter), 0.0030f);
    float halfY = max(width * (1.10f - 0.10f * brickJitter), 0.0024f);
    float halfLen = max(segLen * 0.28f, halfX * 0.95f);

    float3 localBrick = float3(input.localPosition.x * halfX * 2.0f,
                               input.localPosition.y * halfY * 2.0f,
                               input.localPosition.z * halfLen * 2.0f);
    float3 worldPos = center + axisX * localBrick.x
                             + axisY * localBrick.y
                             + tangent * localBrick.z;
    float3 worldNormal = SafeNormalize(axisX * input.localNormal.x + axisY * input.localNormal.y + tangent * input.localNormal.z);

    output.position = mul(float4(worldPos, 1.0f), ViewProjection);
    output.worldPosition = worldPos;
    output.worldNormal = worldNormal;
    output.color = ComputeHairColor(input.strandIndex, widthN);
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    const float3 N = normalize(input.worldNormal);
    const float3 L = normalize(-LightDirection.xyz);
    const float3 V = normalize(CameraPosition.xyz - input.worldPosition);
    const float3 H = normalize(L + V);
    const float diffuse = saturate(dot(N, L));
    const float edge = saturate(1.0f - abs(dot(N, V)));
    const float specular = pow(saturate(dot(N, H)), 20.0f) * 0.10f;
    const float lighting = 0.24f + 0.76f * diffuse;
    const float3 shaded = input.color * lighting + specular.xxx + edge.xxx * 0.035f;
    return float4(shaded, 1.0f);
}
