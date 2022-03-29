
cbuffer cbPerFrame: register(b0) {
  float4x4 g_matViewProj;
}

Texture2D g_txDiffuse : register(t0);
SamplerState g_samLinear:register(s0);

struct VSInput {
  float3  PosW:    POSITION;
  float3  NormalW: NORMAL;
  float2  TexC:   TEXCOORD;
};

struct VSOutput {
  float4 PosH: SV_POSITION;
  float2 TexC: TEXCOORD;
};

typedef VSOutput  PSInput;

VSOutput VSSceneMain(VSInput vin) {
  VSOutput vout = (VSOutput)0;

  vout.PosH = mul(float4(vin.PosW, 1.0), g_matViewProj);
  vout.TexC = vin.TexC;
  return vout;
}

float4 PSSceneMain(PSInput pin): SV_TARGET {
  return g_txDiffuse.Sample(g_samLinear, pin.TexC);
  // return float4(1.0, 0.0, 0.0, 1.0);
}

float4 PSOccluder(PSInput pin): SV_TARGET {
  return float4(1.0, 0.0, 0.0, 0.5);
}