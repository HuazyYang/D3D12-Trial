
cbuffer cbPerframe: register(b0) {
    float4x4 g_matViewProj;
};

cbuffer cbPerObject: register(b1) {
    float4x4 g_matWorld;
}

float4 VS(float3 vPosL: POSITION) : SV_POSITION {
    float4 vPosW;
    float4 vPosH;

    vPosW = mul(float4(vPosL, 1.0f), g_matWorld);
    vPosH = mul(vPosW, g_matViewProj);
    return vPosH;
}

///
/// We do not need the PS.
///
void PS(void) {

}

struct DebugVSOut {
    float4 vPosH: SV_POSITION;
    float2 vTexC: TEXCOORD;
};

DebugVSOut ShadowDepthDebugVS(in uint vertexID: SV_VERTEXID) {

    float4 vPosH;
    float2 vTexC;
    DebugVSOut vout;

    vPosH.x = float(vertexID % 2);
    vPosH.y = float(1 - vertexID / 2);
    vPosH.z = 0.0f;
    vPosH.w = 1.0f;

    vTexC.x = vPosH.x;
    vTexC.y = 1.0 - vPosH.y;

    vPosH.y = vPosH.y - 1;

    vout.vPosH = vPosH;
    vout.vTexC = vTexC;

    return vout;
}

Texture2D g_txShadowDepthMap: register(t2);

/// Static sample states.
SamplerState g_samPointWrap        : register(s0);
SamplerState g_samPointClamp       : register(s1);
SamplerState g_samLinearWrap       : register(s2);
SamplerState g_samLinearClamp      : register(s3);
SamplerState g_samAnisotropicWrap  : register(s4);
SamplerState g_samAnisotropicClamp : register(s5);

float4 ShadowDepthDebugPS(DebugVSOut pin): SV_TARGET {
    return g_txShadowDepthMap.Sample(g_samPointWrap, pin.vTexC).rrrr;
    //return float4(1.0, 0.0, 0.0, 0.0);
}

