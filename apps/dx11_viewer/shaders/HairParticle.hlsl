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

struct VSOutput
{
    float3 center : TEXCOORD0;
    float3 tangent : TEXCOORD1;
    float3 color : COLOR0;
    float radius : TEXCOORD2;
    float strandT : TEXCOORD3;
};

struct GSOutput
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 tangent : TEXCOORD1;
    float3 color : COLOR0;
    float2 uv : TEXCOORD2;
    float strandT : TEXCOORD3;
};

float3 SafeNormalize(float3 v)
{
    float lenSq = dot(v, v);
    return (lenSq > 1.0e-8f) ? (v * rsqrt(lenSq)) : float3(0.0f, 1.0f, 0.0f);
}

float3 ComputeHairColor(uint strandIndex, float widthN, float strandT)
{
    const float strandBand = frac((float)strandIndex * 0.61803398875f);
    const float3 rootColor = float3(1.00f, 0.88f, 0.38f);
    const float3 tipColor  = float3(0.72f, 0.50f, 0.16f);
    const float3 naturalColor = lerp(rootColor, tipColor, strandT) * (0.84f + 0.16f * strandBand);

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

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;

    const uint safeParticlesPerStrand = max(ParticlesPerStrand, 1u);
    const uint strandIndex = min(vertexId / safeParticlesPerStrand, max(TotalStrandCount, 1u) - 1u);
    const uint localIndex = min(vertexId % safeParticlesPerStrand, safeParticlesPerStrand - 1u);
    const uint strandBase = strandIndex * safeParticlesPerStrand;
    const uint prevIndex = strandBase + ((localIndex > 0u) ? (localIndex - 1u) : localIndex);
    const uint nextIndex = strandBase + min(localIndex + 1u, safeParticlesPerStrand - 1u);

    output.center = Positions[vertexId].xyz;
    output.tangent = SafeNormalize(Positions[nextIndex].xyz - Positions[prevIndex].xyz);
    const float width = Widths[vertexId] * max(WidthScale, 0.05f);
    output.radius = max(width * 0.60f, 0.0022f);
    output.strandT = (safeParticlesPerStrand > 1u) ? (float(localIndex) / float(safeParticlesPerStrand - 1u)) : 0.0f;
    const float widthN = saturate((width - 0.0010f) / 0.0120f);
    output.color = ComputeHairColor(strandIndex, widthN, output.strandT);
    return output;
}

[maxvertexcount(4)]
void GSPointMain(point VSOutput input[1], inout TriangleStream<GSOutput> stream)
{
    VSOutput v = input[0];
    float3 viewDir = SafeNormalize(CameraPosition.xyz - v.center);
    float3 upRef = (abs(viewDir.y) > 0.96f) ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 1.0f, 0.0f);
    float3 right = SafeNormalize(cross(viewDir, upRef));
    float3 up = SafeNormalize(cross(right, viewDir));
    float size = v.radius * 1.15f;

    const float2 uv[4] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f, -1.0f),
        float2( 1.0f,  1.0f)
    };

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        GSOutput o;
        float3 offset = right * (uv[i].x * size) + up * (uv[i].y * size);
        o.worldPosition = v.center + offset;
        o.position = mul(float4(o.worldPosition, 1.0f), ViewProjection);
        o.tangent = v.tangent;
        o.color = v.color;
        o.uv = uv[i];
        o.strandT = v.strandT;
        stream.Append(o);
    }
    stream.RestartStrip();
}

[maxvertexcount(4)]
void GSBillboardMain(point VSOutput input[1], inout TriangleStream<GSOutput> stream)
{
    VSOutput v = input[0];
    float3 viewDir = SafeNormalize(CameraPosition.xyz - v.center);
    float3 tangent = SafeNormalize(v.tangent);
    float3 side = SafeNormalize(cross(viewDir, tangent));
    if (dot(side, side) < 1.0e-5f)
    {
        float3 upRef = (abs(viewDir.y) > 0.96f) ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 1.0f, 0.0f);
        side = SafeNormalize(cross(viewDir, upRef));
    }

    float alongScale = lerp(0.9f, 1.9f, v.strandT);
    float sideScale = lerp(1.0f, 0.55f, v.strandT);
    float3 along = tangent * (v.radius * alongScale);
    float3 lateral = side * (v.radius * sideScale);

    const float2 uv[4] = {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f, -1.0f),
        float2( 1.0f,  1.0f)
    };

    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        GSOutput o;
        float3 offset = lateral * uv[i].x + along * uv[i].y;
        o.worldPosition = v.center + offset;
        o.position = mul(float4(o.worldPosition, 1.0f), ViewProjection);
        o.tangent = tangent;
        o.color = v.color;
        o.uv = uv[i];
        o.strandT = v.strandT;
        stream.Append(o);
    }
    stream.RestartStrip();
}

float4 PSMain(GSOutput input) : SV_Target
{
    float2 uv = input.uv;
    float r2 = dot(uv, uv);
    if (r2 > 1.0f)
    {
        discard;
    }

    float alpha = saturate(1.0f - r2);
    alpha = alpha * alpha;
    alpha *= lerp(0.95f, 0.55f, input.strandT);

    float3 viewDir = SafeNormalize(CameraPosition.xyz - input.worldPosition);
    float3 tangent = SafeNormalize(input.tangent);
    float3 fakeNormal = SafeNormalize(viewDir + tangent * uv.y * 0.55f + float3(uv.x, 0.0f, 0.0f) * 0.15f);
    float3 L = SafeNormalize(-LightDirection.xyz);
    float diffuse = 0.28f + 0.72f * saturate(dot(fakeNormal, L));
    float rim = pow(1.0f - saturate(dot(fakeNormal, viewDir)), 2.2f) * 0.22f;
    float3 color = input.color * diffuse + rim.xxx * 0.12f;
    return float4(color, alpha);
}
