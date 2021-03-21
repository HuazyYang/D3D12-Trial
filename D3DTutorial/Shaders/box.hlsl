
cbuffer CbPerframe: register(b0) {
    float4x4 g_matWorldViewProj;
    float4x4 g_matTexTransform;
};

Texture2D g_txDiffuseMap    : register(t0);
Texture2D g_txMaskMap       : register(t1);
SamplerState g_samLinearMipPointWrap    : register(s0);
SamplerState g_samLinearMipPointClmap   : register(s1);

struct VertexIn {
    float3 vPos: POSITION;
    float2 vTexC: TEXCOORD;
};

struct VertexOut {
    float4 vPosH: SV_POSITION;
    float2 vTexC: TEXCOORD;
};

VertexOut BoxVS(VertexIn vin) {
    VertexOut vout;

    vout.vPosH = mul(float4(vin.vPos, 1.0), g_matWorldViewProj);
    vout.vTexC = mul(float4(vin.vTexC, 0.0f, 1.0f), g_matTexTransform).xy;
    return vout;
}

float4 BoxPS(
    VertexOut pin
) : SV_TARGET{

    float4 vColor;

    vColor = g_txDiffuseMap.Sample(g_samLinearMipPointWrap, pin.vTexC) *
        g_txMaskMap.Sample(g_samLinearMipPointClmap, pin.vTexC);

    return vColor;
}

