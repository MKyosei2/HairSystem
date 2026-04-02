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

struct DebugLineEntry
{
    uint Type;
    uint IndexA;
    uint IndexB;
    uint AxisOrFlags;
    float4 Data0;
    float4 Data1;
    float4 Color;
};

StructuredBuffer<float4> Positions : register(t0);
StructuredBuffer<float4> ScalpPositions : register(t1);
StructuredBuffer<DebugLineEntry> DebugLines : register(t2);

struct VSInput
{
    float endpointId : TEXCOORD0;
    uint instanceId : SV_InstanceID;
};

struct VSOutput
{
    float4 position : SV_Position;
    float4 color : COLOR0;
};

float3 ResolveDebugLinePosition(DebugLineEntry entry, bool useEnd)
{
    if (entry.Type == 0u)
    {
        return useEnd ? entry.Data1.xyz : entry.Data0.xyz;
    }
    if (entry.Type == 1u)
    {
        return useEnd ? Positions[entry.IndexB].xyz : Positions[entry.IndexA].xyz;
    }
    if (entry.Type == 2u)
    {
        return useEnd ? ScalpPositions[entry.IndexB].xyz : ScalpPositions[entry.IndexA].xyz;
    }
    if (entry.Type == 3u)
    {
        const float3 base = Positions[entry.IndexA].xyz;
        float3 axis = float3(1.0f, 0.0f, 0.0f);
        if (entry.AxisOrFlags == 1u) axis = float3(0.0f, 1.0f, 0.0f);
        if (entry.AxisOrFlags == 2u) axis = float3(0.0f, 0.0f, 1.0f);
        const float signValue = useEnd ? 1.0f : -1.0f;
        return base + axis * entry.Data0.w * signValue;
    }
    return float3(0.0f, 0.0f, 0.0f);
}

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    DebugLineEntry entry = DebugLines[input.instanceId];
    const bool useEnd = input.endpointId > 0.5f;
    const float3 worldPosition = ResolveDebugLinePosition(entry, useEnd);
    output.position = mul(float4(worldPosition, 1.0f), ViewProjection);
    output.color = entry.Color;
    return output;
}

float4 PSMain(VSOutput input) : SV_Target
{
    return input.color;
}
