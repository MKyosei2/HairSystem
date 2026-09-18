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

struct VSInput
{
    uint particleIndex : PARTICLE_INDEX;
    uint strandIndex : STRAND_INDEX;
};

struct VSOutput
{
    float3 worldPosition : POSITION0;
    float3 worldNext : POSITION1;
    float width : WIDTH0;
    float3 color : COLOR0;
};

float3 ComputeHairColor(uint strandIndex, float widthN)
{
    const float strandBand = frac((float)strandIndex * 0.61803398875f);
    const float3 rootColor = float3(1.00f, 0.88f, 0.38f);
    const float3 tipColor  = float3(0.78f, 0.58f, 0.18f);
    const float3 naturalColor = lerp(tipColor, rootColor, 0.35f + 0.65f * widthN) * (0.82f + 0.18f * strandBand);

    if (DebugColorMode == 1)
    {
        const bool isGuide = strandIndex < GuideStrandCount;
        return isGuide ? float3(1.00f, 0.48f, 0.10f) : float3(0.10f, 0.82f, 0.88f);
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
    output.worldNext = Positions[input.particleIndex + 1].xyz;

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
    float3 worldPosition : TEXCOORD0;
    float3 color : COLOR0;
};

GSOutput MakeVertex(float3 worldPosition, float3 worldNormal, float3 color)
{
    GSOutput o;
    o.position = mul(float4(worldPosition, 1.0f), ViewProjection);
    o.worldNormal = worldNormal;
    o.worldPosition = worldPosition;
    o.color = color;
    return o;
}

[maxvertexcount(36)]
void GSMain(line VSOutput input[2], inout TriangleStream<GSOutput> outStream)
{
    const float3 p0 = input[0].worldPosition;
    const float3 p1 = input[1].worldPosition;
    float3 tangent = p1 - p0;
    float segmentLen = length(tangent);
    if (segmentLen < 1.0e-6f)
    {
        return;
    }
    tangent /= segmentLen;

    float3 up = abs(tangent.y) < 0.92f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 axisX = normalize(cross(up, tangent));
    float3 axisY = normalize(cross(tangent, axisX));
    float radius = max(input[0].width * 1.10f, 0.0032f);
    float halfLen = min(segmentLen * 0.28f, radius * 1.15f);
    float3 center = lerp(p0, p1, 0.5f);

    float3 corners[8];
    corners[0] = center - axisX * radius - axisY * radius - tangent * halfLen;
    corners[1] = center + axisX * radius - axisY * radius - tangent * halfLen;
    corners[2] = center + axisX * radius + axisY * radius - tangent * halfLen;
    corners[3] = center - axisX * radius + axisY * radius - tangent * halfLen;
    corners[4] = center - axisX * radius - axisY * radius + tangent * halfLen;
    corners[5] = center + axisX * radius - axisY * radius + tangent * halfLen;
    corners[6] = center + axisX * radius + axisY * radius + tangent * halfLen;
    corners[7] = center - axisX * radius + axisY * radius + tangent * halfLen;

    uint3 tris[12] = {
        uint3(0, 1, 2), uint3(0, 2, 3),
        uint3(4, 6, 5), uint3(4, 7, 6),
        uint3(0, 4, 5), uint3(0, 5, 1),
        uint3(1, 5, 6), uint3(1, 6, 2),
        uint3(2, 6, 7), uint3(2, 7, 3),
        uint3(3, 7, 4), uint3(3, 4, 0)
    };

    [unroll]
    for (uint i = 0; i < 12; ++i)
    {
        float3 a = corners[tris[i].x];
        float3 b = corners[tris[i].y];
        float3 c = corners[tris[i].z];
        float3 n = normalize(cross(b - a, c - a));
        outStream.Append(MakeVertex(a, n, input[0].color));
        outStream.Append(MakeVertex(b, n, input[0].color));
        outStream.Append(MakeVertex(c, n, input[0].color));
        outStream.RestartStrip();
    }
}

float4 PSMain(GSOutput input) : SV_Target
{
    const float3 N = normalize(input.worldNormal);
    const float3 L = normalize(-LightDirection.xyz);
    const float3 V = normalize(CameraPosition.xyz - input.worldPosition);
    const float3 H = normalize(L + V);
    const float diffuse = saturate(dot(N, L));
    const float specular = pow(saturate(dot(N, H)), 18.0f) * 0.18f;
    const float lighting = 0.26f + 0.74f * diffuse;
    const float3 shaded = input.color * lighting + specular.xxx;
    return float4(shaded, 1.0f);
}
