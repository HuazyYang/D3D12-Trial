


cbuffer cbPerObject: register(b0) {
  float4x4 g_matWorld;
  float g_fTessFactor;
}

cbuffer cbPerframe: register(b1) {
  float3 g_vEyePosW;
  float4x4 g_matViewProj;
};

StructuredBuffer<float3> g_txPosExtra;

struct VS_INPUT {
  float3 PosL: POSITION;
  float4 Color: COLOR;
};

struct VS_OUTPUT {
  float3 PosL: POSITION;
  float4 Color: COLOR;
};

struct HS_CONSTANT_OUTPUT {
  float EdgeTess[4]: SV_TessFactor;
  float InsideTess: SV_InsideTessFactor;

  float3 PosExtraW: EXTRA_POSITION;
};

struct HS_CONTROL_OUTPUT {
  float3 PosL: POSITION;
  float4 Color: COLOR;
};

struct DS_OUTPUT {
  float4 PosH: SV_POSITION;
  float4 Color: COLOR;
};

VS_OUTPUT VSMain(VS_INPUT i) {
  VS_OUTPUT output;

  output.PosL = i.PosL;
  output.Color = i.Color;

  return output;
}

HS_CONSTANT_OUTPUT ConstantHS(InputPatch<VS_OUTPUT, 3> ip, uint patchID: SV_PrimitiveID) {

  HS_CONSTANT_OUTPUT output;

  output.EdgeTess[0] = output.EdgeTess[1] = output.EdgeTess[2] = g_fTessFactor;
  output.InsideTess = g_fTessFactor;

  output.PosExtraW = g_txPosExtra[patchID];

  return output;
}

HS_CONTROL_OUTPUT ControlHS(InputPatch<VS_OUTPUT, 3> ip, uint i: SV_OutputControlPointID) {
  HS_CONTROL_OUTPUT output;

  output.PosL = ip[i].PosL;
  output.Color = ip[i].Color;

  return output;
}

[domain("tri")]
DS_OUTPUT DSMain(HS_CONSTANT_OUTPUT input, InputPatch<HS_CONTROL_OUTPUT, 3> bezpath, float3 UVW: SV_DOMAINLOCATION) {

  DS_OUTPUT output;

}

