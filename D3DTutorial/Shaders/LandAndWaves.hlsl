#include "LightUtils.hlsl"

//#define _WAVES_DISPLACEMENT_MAP

cbuffer cbPerPass: register(b0) {
    float4x4 g_matViewProj;
    float3 g_vEyePosW;
    float4 g_vAmbientStrength;
    Light g_aLights[NUM_LIGHTS];
};

cbuffer cbPerObject : register(b1) {
    float4x4 g_matWorld;
    float4x4 g_matWorldInvTranspose;
    float4x4 g_matTexTransform;
    float g_fWavesSpatialStep;
};

cbuffer cbMaterial : register(b2) {
    float4 g_vDiffuseAlbedo;
    float3 g_vFresnelR0;
    float g_fRoughness;
    float4x4 g_matMatTransform;
};

Texture2D<float> g_txDisplacementMap: register(t0);
Texture2D g_txDiffuseMap: register(t1);

/// Static sample states.
SamplerState g_samPointWrap        : register(s0);
SamplerState g_samPointClamp       : register(s1);
SamplerState g_samLinearWrap       : register(s2);
SamplerState g_samLinearClamp      : register(s3);
SamplerState g_samAnisotropicWrap  : register(s4);
SamplerState g_samAnisotropicClamp : register(s5);

struct VertexIn {
    float3 vPosL : POSITION;
    float3 vNormalL: NORMAL;
    float2 vTexC : TEXCOORD;
};

struct VertexOut {
    float4 vPosH: SV_POSITION;
    float3 vPosW: POSITION;
    float3 vNormalW: NORMAL;
    float2 vTexC: TEXCOORD;
};

VertexOut VS(VertexIn vin) {
    float4 vPosW;
    VertexOut vout;

#ifdef _WAVES_DISPLACEMENT_MAP
    float l, r, b, t;

    vin.vPosL.y += g_txDisplacementMap.SampleLevel(g_samLinearWrap, vin.vTexC, 1.0f).r;

    l = g_txDisplacementMap.SampleLevel(g_samLinearWrap, vin.vTexC, 0.0f, int2(-1, 0)).r;
    r = g_txDisplacementMap.SampleLevel(g_samLinearWrap, vin.vTexC, 0.0f, int2(1, 0)).r;
    b = g_txDisplacementMap.SampleLevel(g_samLinearWrap, vin.vTexC, 0.0f, int2(0, 1)).r;
    t = g_txDisplacementMap.SampleLevel(g_samLinearWrap, vin.vTexC, 0.0f, int2(0, -1)).r;

    vin.vNormalL = normalize(float3(-r+l, 2.0f * g_fWavesSpatialStep, b-t));
#endif
    vPosW = mul(float4(vin.vPosL, 1.0f), g_matWorld);

    vout.vPosW = vPosW.xyz;
    vout.vPosH = mul(vPosW, g_matViewProj);

    vout.vNormalW = mul(float4(vin.vNormalL, 0.0f), g_matWorldInvTranspose).xyz;
    vout.vTexC = mul(float4(vin.vTexC, 0.0f, 1.0f), g_matTexTransform).xy;

    return vout;
}

float4 PS(VertexOut pin) : SV_TARGET {

    float4 vLitColor;
    float3 vToEye;
    float4 ambient;
    float4 diffuseAlbedo;

    diffuseAlbedo = g_txDiffuseMap.Sample(g_samAnisotropicWrap, pin.vTexC) * g_vDiffuseAlbedo;

    clip(diffuseAlbedo.a - 0.1f);

    Material mat = { diffuseAlbedo, g_vFresnelR0, 1.0f - g_fRoughness };

    /// Interpolation normal can unnormalize it, so renormalize it.
    pin.vNormalW = normalize(pin.vNormalW);

    vToEye = g_vEyePosW - pin.vPosW;
    vToEye = normalize(vToEye);

    ambient = g_vAmbientStrength * diffuseAlbedo;

    vLitColor = ambient + ComputeLighting(
        g_aLights,
        mat,
        pin.vPosW,
        pin.vNormalW,
        vToEye,
        1.0f
    );

    /// Common to take alpha from the diffuse material.
    vLitColor.a = diffuseAlbedo.a;

    return vLitColor;
}



