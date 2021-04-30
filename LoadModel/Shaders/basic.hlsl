
cbuffer cbPerframe: register(b0) {
  float4x4 ViewProj;
  float3 EyePosW;
};

Texture2D DiffuseMap:register(t0);

SamplerState BilinearSampler:register(s0);

struct VS_INPUT {
  float3 PosL: POSITION;
  float3 NormalL: NORMAL;
  float2 Texcoord: TEXCOORD;
};

struct VS_OUTPUT {
  float4 PosH: SV_POSITION;
  float3 PosW: POSITION;
  float3 NormalW: NORMAL;
  float2 Texcoord: TEXCOORD;
};

VS_OUTPUT VSMain(VS_INPUT i) {
  VS_OUTPUT o = (VS_OUTPUT)0;

  o.PosW  = i.PosL;
  o.PosH = mul(float4(i.PosL, 1.0), ViewProj);
  o.NormalW = i.NormalL;
  o.Texcoord = i.Texcoord;

  return o;
}

float4 PSMain(VS_OUTPUT i): SV_TARGET {

  float4 color = DiffuseMap.Sample(BilinearSampler, i.Texcoord);
  return float4(color.xyz, 1.0);
}
