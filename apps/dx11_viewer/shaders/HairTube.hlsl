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
    const float3 segment = p1 - p0;
    const float segmentLenSq = dot(segment, segment);
    if (segmentLenSq < 1.0e-8f)
    {
        return;
    }

    const float3 tangent = normalize(segment);
    const float3 mid = 0.5f * (p0 + p1);
    float3 toCamera = normalize(CameraPosition.xyz - mid);
    float3 axisX = cross(toCamera, tangent);
    if (dot(axisX, axisX) < 1.0e-8f)
    {
        axisX = cross(float3(0.0f, 1.0f, 0.0f), tangent);
    }
    if (dot(axisX, axisX) < 1.0e-8f)
    {
        axisX = cross(float3(1.0f, 0.0f, 0.0f), tangent);
    }
    axisX = normalize(axisX);
    const float3 axisY = normalize(cross(tangent, axisX));

    const float radius0 = max(input[0].width * 0.48f, 0.0025f);
    const float radius1 = max(input[1].width * 0.48f, 0.0020f);
    const uint sides = 6;
    const float kTau = 6.28318530718f;

    [unroll]
    for (uint side = 0; side < sides; ++side)
    {
        const float a0 = (float(side) / float(sides)) * kTau;
        const float a1 = (float(side + 1u) / float(sides)) * kTau;

        const float3 ringDir0 = normalize(axisX * cos(a0) + axisY * sin(a0));
        const float3 ringDir1 = normalize(axisX * cos(a1) + axisY * sin(a1));

        const float3 v00 = p0 + ringDir0 * radius0;
        const float3 v01 = p0 + ringDir1 * radius0;
        const float3 v10 = p1 + ringDir0 * radius1;
        const float3 v11 = p1 + ringDir1 * radius1;

        outStream.Append(MakeVertex(v00, ringDir0, input[0].color));
        outStream.Append(MakeVertex(v10, ringDir0, input[1].color));
        outStream.Append(MakeVertex(v01, ringDir1, input[0].color));

        outStream.Append(MakeVertex(v01, ringDir1, input[0].color));
        outStream.Append(MakeVertex(v10, ringDir0, input[1].color));
        outStream.Append(MakeVertex(v11, ringDir1, input[1].color));

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
    const float specular = pow(saturate(dot(N, H)), 24.0f) * 0.22f;
    const float lighting = 0.22f + 0.78f * diffuse;
    const float3 shaded = input.color * lighting + specular.xxx;
    return float4(shaded, 1.0f);
}
