
cbuffer cbPerframe {
    float4x4 g_matViewProj;
    float4x4 g_matInvView;
};

struct ParticlePos {
    float3 Pos;
    float Mass;
    float3 Vel;
    float AccelScalar;
};

StructuredBuffer<ParticlePos> g_txPrevParticles: register(t0);
Texture2D g_txDiffuseMap: register(t1);

SamplerState g_samLinearMipLinear: register(s0);

struct VertexIn {
    float4 Color: COLOR;
    uint Id: SV_VertexID;
};

struct VertexOut {
    float3 Pos: POSITON;
    float4 Color: COLOR;
};

VertexOut VS(VertexIn vin) {
    VertexOut vout;
    ParticlePos P = g_txPrevParticles[vin.Id];

    vout.Pos = P.Pos;

    float mag = P.AccelScalar / 9.0f;
    vout.Color = lerp(float4(1.0f, 0.1f, 0.1f, 1.0f), vin.Color, mag);

    return vout;
}

struct GSOutput
{
	float4 PosH : SV_POSITION;
    float4 Color: COLOR;
    float2 Tex : TEXCOORD; /// z is the light tensity.
};

static const float3 g_vParticleRad = float3(10.0f, 10.0f, 0.0f);
static const float3 g_vPositionOffsets[] = {
    float3(-1.0f, 1.0f, .0f),
    float3(1.0f, 1.0f, .0f),
    float3(-1.0f, -1.0f, .0f),
    float3(1.0f, -1.0f, .0f)
};
static const float2 g_vTexOffsets[] = {
    float2(0.0f, .0f),
    float2(1.0f, .0f),
    float2(0.0f, 1.0f),
    float2(1.0f, 1.0f)
};

[maxvertexcount(4)]
void GS(
    point VertexOut gin[1],
	inout TriangleStream< GSOutput > output
){
    GSOutput gout;
    float3 posW;
    float3 offset;

    [unroll]
    for (int i = 0; i < 4; ++i) {
        offset = g_vParticleRad * g_vPositionOffsets[i];
        posW = gin[0].Pos + mul(float4(offset, 0.0f), g_matInvView).xyz;

        gout.PosH = mul(float4(posW, 1.0f), g_matViewProj);
        gout.Tex = g_vTexOffsets[i];
        gout.Color = gin[0].Color;

        output.Append(gout);
    }

    output.RestartStrip();
}

float4 PS(GSOutput pin) : SV_TARGET{
    float4 color;

    color = pin.Color * g_txDiffuseMap.Sample(g_samLinearMipLinear, pin.Tex);

    clip(color.a - 0.1f);

    return color;
}
