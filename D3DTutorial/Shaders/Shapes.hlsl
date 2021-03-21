#include "LightUtils.hlsl"


#ifndef _USE_DIFFUSE_ENV_MAP
#define _USE_DIFFUSE_ENV_MAP    1
#endif

struct MaterialData {
    float4    DiffuseAlbedo;
    float3    FresnelR0;
    float     Roughness;
    float4x4  MatTransform;

    uint      DiffuseMapIndex;
    uint      NormalMapIndex;
    uint      Padding0;
    uint      Padding1;
};

cbuffer cbPerPass: register(b0) {
    float4x4 g_matViewProj;
    float3 g_vEyePosW;
    float g_fHeightScale; /// Used for height map.
    float g_fMinTessDistance; /// Used for height map.
    float g_fMaxTessDistance;
    float g_fMinTessFactor;
    float g_fMaxTessFactor;
    ///
    float4x4 g_matLightVPT;
    float4 g_vAmbientStrength;
    Light g_aLights[NUM_LIGHTS];
}

cbuffer cbPerObject : register(b1) {
    float4x4 g_matWorld; /// First entry must be world matrix
    float4x4 g_matWorldInvTranspose;
    float4x4 g_matTexTransform;

    uint     g_iMaterialIndex;
}

StructuredBuffer<MaterialData> g_aMaterialDatas: register(t0);
#if _USE_DIFFUSE_ENV_MAP
TextureCube g_txCubeMap :     register(t1);
#endif
Texture2D g_txShadowDepthMap: register(t2);

Texture2D g_txDiffuseMap[8]: register(t0, space1);

/// Static sample states.
SamplerState g_samPointWrap        : register(s0);
SamplerState g_samPointClamp       : register(s1);
SamplerState g_samLinearWrap       : register(s2);
SamplerState g_samLinearClamp      : register(s3);
SamplerState g_samAnisotropicWrap  : register(s4);
SamplerState g_samAnisotropicClamp : register(s5);

SamplerComparisonState g_samShadowPointBias  : register(s6);

struct VertexIn {
    float3 vPosL    : POSITION;
    float3 vNormalL : NORMAL;
    float3 vTangentU: TANGENTU;
    float2 vTexC    : TEXCOORD;
};

struct VertexOut {
    float4 vPosH    : SV_POSITION;
    float3 vPosW    : POSITION;
    float3 vNormalW : NORMAL;
    float3 vTangentW: TANGENTU;
    float2 vTexC    : TEXCOORD;
    float fTessFactor : TESSFACTOR;
};

VertexOut VS(in VertexIn vin) {
    VertexOut vout;
    float4 vPosW;

    vPosW = mul(float4(vin.vPosL, 1.0f), g_matWorld);
    vout.vPosW = vPosW.xyz;

    vout.vPosH = mul(vPosW, g_matViewProj);

    vout.vNormalW = mul(float4(vin.vNormalL, 0.0f), g_matWorldInvTranspose).xyz;

    vout.vTangentW = mul(float4(vin.vTangentU, 0.0f), g_matWorldInvTranspose).xyz;

    vout.vTexC = mul(float4(vin.vTexC, 0.0f, 1.0f), g_matTexTransform).xy;

    float t = saturate((g_fMaxTessDistance - distance(vout.vPosW, g_vEyePosW)) /
        (g_fMaxTessDistance - g_fMinTessDistance));

    vout.fTessFactor = g_fMinTessFactor + t * (g_fMaxTessFactor - g_fMinTessFactor);

    return vout;
}

struct PatchTess {
    float EdgeTess[3] : SV_TESSFACTOR;
    float InsideTess : SV_INSIDETESSFACTOR;
};

PatchTess PatchHS(InputPatch<VertexOut, 3> patch) {
    PatchTess pt;

    pt.EdgeTess[0] = 0.5f*(patch[1].fTessFactor + patch[2].fTessFactor);
    pt.EdgeTess[1] = 0.5f*(patch[2].fTessFactor + patch[0].fTessFactor);
    pt.EdgeTess[2] = 0.5f*(patch[0].fTessFactor + patch[1].fTessFactor);
    pt.InsideTess = pt.EdgeTess[0];

    return pt;
}

struct HullOut {
    float3 vPosW    : POSITION;
    float3 vNormalW : NORMAL;
    float3 vTangentW: TANGENTU;
    float2 vTexC    : TEXCOORD;
};

[domain("tri")]
[partitioning("fractional_odd")]
[outputtopology("triangle_cw")]
[outputcontrolpoints(3)]
[patchconstantfunc("PatchHS")]
HullOut HS(InputPatch<VertexOut, 3> p,
    uint i: SV_OutputControlPointID,
    uint patchId : SV_PrimitiveID) {
    HullOut hout;

    hout.vPosW = p[i].vPosW;
    hout.vNormalW = p[i].vNormalW;
    hout.vTangentW = p[i].vTangentW;
    hout.vTexC = p[i].vTexC;

    return hout;
}

struct DomainOut {
    float4 vPosH: SV_POSITION;
    float3 vPosW    : POSITION;
    float3 vNormalW : NORMAL;
    float3 vTangentW: TANGENTU;
    float2 vTexC    : TEXCOORD;
};

[domain("tri")]
DomainOut DS(PatchTess patchTess,
    float3 bary: SV_DomainLocation,
    const OutputPatch<HullOut, 3> tri) {

    DomainOut dout;

    dout.vPosW = tri[0].vPosW * bary.x + tri[1].vPosW * bary.y +
        tri[2].vPosW * bary.z;
    dout.vNormalW = tri[0].vNormalW * bary.x + tri[1].vNormalW * bary.y +
        tri[2].vNormalW * bary.z;
    dout.vTangentW = tri[0].vTangentW * bary.x + tri[1].vTangentW * bary.y +
        tri[2].vTangentW * bary.z;
    dout.vTexC = tri[0].vTexC * bary.x + tri[1].vTexC * bary.y +
        tri[2].vTexC * bary.z;

    /// Interpolating can unnormalize it, so normaize it again.
    dout.vNormalW = normalize(dout.vNormalW);
    dout.vTangentW = normalize(dout.vTangentW);

    int mapIndex = g_aMaterialDatas[g_iMaterialIndex].NormalMapIndex;
    const float MipLevelInterval = 20.0f;
    float mipLevel = clamp((distance(g_vEyePosW, dout.vPosW) - MipLevelInterval) / MipLevelInterval,
        0.0f, 6.0f);
    float dh = g_txDiffuseMap[mapIndex].SampleLevel(g_samLinearWrap, dout.vTexC, mipLevel).a;

    dout.vPosW += g_fHeightScale * (dh - 1.0f) * dout.vNormalW;

    /// Project to homogeneous clip space.
    dout.vPosH = mul(float4(dout.vPosW, 1.0f), g_matViewProj);

    return dout;
}


float3 NormalSampleToWorldSpace(
    in float3 normalMapSample,
    in float3 normalW,
    in float3 tangentW
) {
    float3 normalT = 2.0f*normalMapSample - 1.0f;
    float3 T, N, B;

    N = normalW;
    T = tangentW;
    T = T - dot(N, T)*N;
    B = cross(N, T);

    float3x3 TBN = float3x3(T, B, N);

    float3 bumpedNormalW = mul(normalT, TBN);

    return bumpedNormalW;
}

float ComputeShadowFactor(
    in float3 vPosW
) {
    float4 vPosH;
    float fShadowFactor = .0f;
    float dx, dy;

    vPosH = mul(float4(vPosW, 1.0f), g_matLightVPT);
    vPosH /= vPosH.w;

    uint width, height, numMips;
    g_txShadowDepthMap.GetDimensions(0, width, height, numMips);
    dx = 1.0f / width;
    dy = 1.0f / height;

    const float2 offset[] = {
        float2(.0f, .0f), float2(-dx, .0f), float2(dx, .0f),
        float2(.0f, -dy), float2(-dx, -dy), float2(dx, -dy),
        float2(.0f, dy), float2(-dx, dy), float2(dx, dy)
    };

    [unroll]
    for (uint i = 0; i < 9; ++i) {
        fShadowFactor += g_txShadowDepthMap.SampleCmpLevelZero(
            g_samShadowPointBias, vPosH.xy, vPosH.z).r;
    }

        fShadowFactor /= 9.0f;

    return fShadowFactor;
}

float4 PS(in DomainOut pin) : SV_TARGET{

    float3 vNormalW;
    float3 normalMapSample;
    MaterialData matData = g_aMaterialDatas[g_iMaterialIndex];
    float4 vLitColor;
    float3 vToEye;
    float4 ambient;
    float4 diffuseAlbedo;
    float fShadowFactor;

    diffuseAlbedo = g_txDiffuseMap[matData.DiffuseMapIndex].Sample(g_samAnisotropicWrap, pin.vTexC)
        * matData.DiffuseAlbedo;

    Material mat = { diffuseAlbedo, matData.FresnelR0, 1.0f - matData.Roughness };

    /// Interpolation normal can unnormalize it, so renormalize it.
    pin.vNormalW = normalize(pin.vNormalW);
    pin.vTangentW = normalize(pin.vTangentW);

    normalMapSample = g_txDiffuseMap[matData.NormalMapIndex].Sample(g_samLinearWrap, pin.vTexC).xyz;
    vNormalW = NormalSampleToWorldSpace(normalMapSample, pin.vNormalW, pin.vTangentW);
    //vNormalW = pin.vNormalW;

    vToEye = g_vEyePosW - pin.vPosW;
    vToEye = normalize(vToEye);

    ambient = g_vAmbientStrength * diffuseAlbedo;

    fShadowFactor = ComputeShadowFactor(pin.vPosW);

    vLitColor = ambient + ComputeLighting(
        g_aLights,
        mat,
        pin.vPosW,
        vNormalW,
        vToEye,
        fShadowFactor
    );

    /// Add in specular reflections.
#if _USE_DIFFUSE_ENV_MAP
    float3 r = reflect(-vToEye, vNormalW);
    float4 reflectionColor = g_txCubeMap.Sample(g_samLinearWrap, r);
    float3 fresnelFactor = SchlickFresnel(matData.FresnelR0, vNormalW, r);
    vLitColor.rgb += mat.Shininess * fresnelFactor * reflectionColor.rgb;
#endif

    /// Common to take alpha from the diffuse material.
    vLitColor.a = diffuseAlbedo.a;

    return vLitColor;
}



