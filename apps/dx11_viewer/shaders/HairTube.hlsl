cbuffer RenderConstants : register(b0)
{
    row_major float4x4 ViewProjection;
    float4 CameraPosition;
    float4 LightDirection;
    float WidthScale;
    float3 _Padding0;
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
    float4 position : POSITION0;
    float3 color : COLOR0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    const float3 worldPosition = Positions[input.particleIndex].xyz;
    output.position = mul(float4(worldPosition, 1.0f), ViewProjection);

    const float width = Widths[input.particleIndex] * max(WidthScale, 1.0f);
    const float widthN = saturate((width - 0.0010f) / 0.0080f);
    const float strandBand = frac((float)input.strandIndex * 0.61803398875f);
    const float3 rootColor = float3(1.00f, 0.88f, 0.38f);
    const float3 tipColor  = float3(0.78f, 0.58f, 0.18f);
    output.color = lerp(tipColor, rootColor, 0.35f + 0.65f * widthN) * (0.80f + 0.20f * strandBand);
    return output;
}

struct GSOutput
{
    float4 position : SV_Position;
    float3 color : COLOR0;
};

[maxvertexcount(2)]
void GSMain(line VSOutput input[2], inout LineStream<GSOutput> outStream)
{
    GSOutput o;
    o.position = input[0].position;
    o.color = input[0].color;
    outStream.Append(o);

    o.position = input[1].position;
    o.color = input[1].color;
    outStream.Append(o);

    outStream.RestartStrip();
}

float4 PSMain(GSOutput input) : SV_Target
{
    return float4(input.color, 1.0f);
}
