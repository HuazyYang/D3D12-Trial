/// Make sure cbv b0, b1 is aligned seamly with Shapes.hlsl.
#include "LightUtils.hlsl"

cbuffer cbPerPass: register(b0) {
    float4x4 g_matViewProj;
    float3 g_vEyePosW;
}

cbuffer cbPerObject : register(b1) {
    float4x4 g_matWorld;
}

TextureCube g_txCubeMap : register(t1);

/// Static sample states.
SamplerState g_samPointWrap        : register(s0);
SamplerState g_samPointClamp       : register(s1);
SamplerState g_samLinearWrap       : register(s2);
SamplerState g_samLinearClamp      : register(s3);
SamplerState g_samAnisotropicWrap  : register(s4);
SamplerState g_samAnisotropicClamp : register(s5);

struct VertexIn {
    float3 vPosL: POSITION;
};


struct VertexOut {
    float4 vPosH: SV_POSITION;
    float3 vTexC: TEXCOORD;
};


VertexOut VS(VertexIn vin) {
    VertexOut vout;
    float4 vPosW;

    vPosW = mul(float4(vin.vPosL, 1.0f), g_matWorld);

    vPosW.xyz += g_vEyePosW;

    vout.vPosH = mul(vPosW, g_matViewProj).xyww;

    vout.vTexC = vin.vPosL;
    return vout;
}

float4 PS(VertexOut pin): SV_TARGET {

    float4 vLitColor;

    vLitColor = g_txCubeMap.Sample(g_samLinearWrap, pin.vTexC);

    return vLitColor;
}
