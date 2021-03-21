#include "LightUtils.hlsl"

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

Texture2DArray g_txDiffuseMapArray: register(t1);

/// Static sample states.
SamplerState g_samPointWrap        : register(s0);
SamplerState g_samPointClamp       : register(s1);
SamplerState g_samLinearWrap       : register(s2);
SamplerState g_samLinearClamp      : register(s3);
SamplerState g_samAnisotropicWrap  : register(s4);
SamplerState g_samAnisotropicClamp : register(s5);

struct VertexIn {
    float3 vPosL:       POSITION;
    float2 vSize:       SIZE;
};

struct VertexOut {
    float3 vPosW:   POSITION;
    float2 vSize:   SIZE;
};

struct GeoOut {
    float4 vPosH: SV_POSITION;
    float3 vPosW: POSITION;
    float3 vNormalW: NORMAL;
    float2 vTexC: TEXCOORD;
    uint uPrimID: SV_PRIMITIVEID;
};

VertexOut VS(in VertexIn vin) {
    VertexOut vout;

    vout.vPosW = mul(float4(vin.vPosL, 1.0f), g_matWorld).xyz;
    vout.vSize = vin.vSize;

    return vout;
}

cbuffer cbSettings {
    static const float2 g_vBillBoardTexC[] = {
        float2(1.0, 0.0),
        float2(0.0, 0.0),
        float2(1.0, 1.0),
        float2(0.0, 1.0)
    };
};

[maxvertexcount(4)]
void GS(point VertexOut gin[1], uint primID: SV_PrimitiveID,
    inout TriangleStream<GeoOut> quad) {

    float3 vToEye = g_vEyePosW - gin[0].vPosW;
    float3 vUp = float3(0.0f, 1.0f, 0.0f);
    float3 vRight;
    float4x4 matViewToWorld;
    int i;
    float3 v[4];
    float halfWidth, halfHeight;
    GeoOut gout;

    vToEye = normalize(vToEye);
    vRight = cross(vUp, vToEye);

    halfWidth = 0.5*gin[0].vSize.x;
    halfHeight = 0.5*gin[0].vSize.y;

    v[0] =  halfWidth * vRight + halfHeight * vUp;
    v[1] = -halfWidth * vRight + halfHeight * vUp;
    v[2] =  halfWidth * vRight - halfHeight * vUp;
    v[3] = -halfWidth * vRight - halfHeight * vUp;

    [unroll]
    for (i = 0; i < 4; ++i) {
        gout.vPosW = gin[0].vPosW + v[i];
        gout.vPosH = mul(float4(gout.vPosW, 1.0f), g_matViewProj);
        gout.vNormalW = vToEye;
        gout.vTexC = g_vBillBoardTexC[i];
        gout.uPrimID = primID;

        quad.Append(gout);
    }
}


float4 PS(GeoOut pin) : SV_TARGET{

    float4 vLitColor;
    float3 vToEye;
    float4 ambient;
    float4 diffuseAlbedo;

    diffuseAlbedo = g_txDiffuseMapArray.Sample(g_samAnisotropicWrap,
        float3(pin.vTexC, pin.uPrimID%3)) * g_vDiffuseAlbedo;

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
