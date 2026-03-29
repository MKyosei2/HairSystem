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
    uint particleIndex : PARTICLE_INDEX;
    uint strandIndex : STRAND_INDEX;
};

struct VSOutput
{
    float3 worldPosition : POSITION0;
    float width : WIDTH0;
    float3 color : COLOR0;
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
    output.worldPosition = Positions[input.particleIndex].xyz;

    const float width = Widths[input.particleIndex] * max(WidthScale, 0.05f);
    const float widthN = saturate((width - 0.0010f) / 0.0120f);
    output.width = max(width, 0.0015f);
    output.color = ComputeHairColor(input.strandIndex, widthN);
    return output;
}

struct GSOutput
{
    float4 position : SV_Position;
    float3 worldNormal : NORMAL0;
    float3 color : COLOR0;
};

[maxvertexcount(4)]
void GSMain(line VSOutput input[2], inout TriangleStream<GSOutput> outStream)
{
    const float3 p0 = input[0].worldPosition;
    const float3 p1 = input[1].worldPosition;
    const float3 segment = p1 - p0;
    const float segmentLenSq = dot(segment, segment);
    if (segmentLenSq < 1.0e-8f)
    {
        return;
    }

    const float3 tangent = normalize(segment);
    const float3 mid = 0.5f * (p0 + p1);
    float3 viewDir = normalize(CameraPosition.xyz - mid);
    float3 side = cross(viewDir, tangent);
    if (dot(side, side) < 1.0e-8f)
    {
        side = cross(float3(0.0f, 1.0f, 0.0f), tangent);
    }
    if (dot(side, side) < 1.0e-8f)
    {
        side = cross(float3(1.0f, 0.0f, 0.0f), tangent);
    }
    side = normalize(side);

    const float halfWidth0 = max(input[0].width * 0.55f, 0.0025f);
    const float halfWidth1 = max(input[1].width * 0.55f, 0.0020f);
    const float3 normal = normalize(cross(tangent, side));

    GSOutput o;

    o.worldNormal = normal;
    o.color = input[0].color;
    o.position = mul(float4(p0 - side * halfWidth0, 1.0f), ViewProjection);
    outStream.Append(o);

    o.worldNormal = normal;
    o.color = input[0].color;
    o.position = mul(float4(p0 + side * halfWidth0, 1.0f), ViewProjection);
    outStream.Append(o);

    o.worldNormal = normal;
    o.color = input[1].color;
    o.position = mul(float4(p1 - side * halfWidth1, 1.0f), ViewProjection);
    outStream.Append(o);

    o.worldNormal = normal;
    o.color = input[1].color;
    o.position = mul(float4(p1 + side * halfWidth1, 1.0f), ViewProjection);
    outStream.Append(o);

    outStream.RestartStrip();
}

float4 PSMain(GSOutput input) : SV_Target
{
    const float3 normal = normalize(input.worldNormal);
    const float3 lightDir = normalize(-LightDirection.xyz);
    const float ndl = saturate(dot(normal, lightDir));
    const float lighting = 0.28f + 0.72f * ndl;
    return float4(input.color * lighting, 0.97f);
}
