

cbuffer cbPerObject: register(b0) {
  row_major float4x4  g_matWorldViewProj : packoffset(c0);
};

TextureCube g_txCubeMap : register(t0);

SamplerState g_samLinearWrap: register(s0);

struct SkyVSInput {
  float4 Pos : POSITION;
};

struct SkyVSOutput {
  float4 PosH: SV_POSITION;
  float3 Tex: TEXCOORD;
};

SkyVSOutput SkyVSMain(SkyVSInput vin) {
  SkyVSOutput vout;

  vout.PosH = vin.Pos;
  vout.Tex = normalize(mul(vin.Pos, g_matWorldViewProj)).xyz;
  return vout;
}

float4 SkyPSMain(SkyVSOutput pin): SV_TARGET {

  float4 color = g_txCubeMap.Sample(g_samLinearWrap, pin.Tex);
  return color;
}





