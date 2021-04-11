///
/// Hello DXR simple ray tracing shaders
///


struct HitInfo {
  float4 ColorAndDistance;
};

struct ShadowHitInfo {
  bool Hit;
};

struct Attributes {
  float2 bary;
};


cbuffer cbPerframe: register(b0, space0) {
  float4x4 g_matInvProjection;
  float3 g_vEyePosW;
  float4x4 g_matViewInv;
}

// Raytracing output, accessed as UAV.
RWTexture2D<float4> g_txOutput: register(u0);

/// Raytracing Acceleration Structure, accessed as SRV.
RaytracingAccelerationStructure g_aSceneBVH : register(t0);

///
/// Ray Generation Shader
///
[shader("raygeneration")]
void GenerateRay() {
  // Initialize the ray payload.
  HitInfo payload;
  payload.ColorAndDistance = float4(.0f, .0f, .0f, .0f);

  // Get the location with the dispatched 2D grid of work items
  // (often maps to pixels, so this could represent a pixel coordinate).
  uint2 launchIndex = DispatchRaysIndex().xy;
  float2 dims = float2(DispatchRaysDimensions().xy);
  float2 d = (((launchIndex.xy + 0.5f) / dims.xy) * 2.f - 1.f);

  d.y *= -1.0f;
  float4 r = mul(float4(d.x, d.y, 1.0f, 1.0f), g_matInvProjection);

  r /= r.w;
  r.w = .0f;

  /// Transform into world space.
  r = mul(r, g_matViewInv);
  r = normalize(r);

  // Define a ray, consisting of origin, direction, and the min-max distance
  // values
  RayDesc ray;
  ray.Origin = g_vEyePosW;
  ray.Direction = r.xyz;
  ray.TMin = 0;
  ray.TMax = 100000;

  // Trace the ray
  TraceRay(
    // Parameter name: AccelerationStructure
    // Acceleration structure
    g_aSceneBVH,

    // Parameter name: RayFlags
    // Flags can be used to specify the behavior upon hitting a surface
    RAY_FLAG_NONE,

    // Parameter name: InstanceInclusionMask
    // Instance inclusion mask, which can be used to mask out some geometry to
    // this ray by and-ing the mask with a geometry mask. The 0xFF flag then
    // indicates no geometry will be masked
    0xFF,

    // Parameter name: RayContributionToHitGroupIndex
    // Depending on the type of ray, a given object can have several hit
    // groups attached (ie. what to do when hitting to compute regular
    // shading, and what to do when hitting to compute shadows). Those hit
    // groups are specified sequentially in the SBT, so the value below
    // indicates which offset (on 4 bits) to apply to the hit groups for this
    // ray. In this sample we only have one hit group per object, hence an
    // offset of 0.
    0,

    // Parameter name: MultiplierForGeometryContributionToHitGroupIndex
    // The offsets in the SBT can be computed from the object ID, its instance
    // ID, but also simply by the order the objects have been pushed in the
    // acceleration structure. This allows the application to group shaders in
    // the SBT in the same order as they are added in the AS, in which case
    // the value below represents the stride (4 bits representing the number
    // of hit groups) between two consecutive objects.
    0,

    // Parameter name: MissShaderIndex
    // Index of the miss shader to use in case several consecutive miss
    // shaders are present in the SBT. This allows to change the behavior of
    // the program when no geometry have been hit, for example one to return a
    // sky color for regular rendering, and another returning a full
    // visibility value for shadow rays. This sample has only one miss shader,
    // hence an index 0
    0,

    // Parameter name: Ray
    // Ray information to trace
    ray,

    // Parameter name: Payload
    // Payload associated to the ray, which will be used to communicate
    // between the hit/miss shaders and the raygen
    payload);

  g_txOutput[launchIndex] = float4(payload.ColorAndDistance.rgb, 1.0f);
}


///
/// Miss Shader.
///
[shader("miss")]
void Miss(inout HitInfo payload : SV_RayPayload) {
  uint2 launchIndex = DispatchRaysIndex().xy;
  float2 dims = float2(DispatchRaysDimensions().xy);
  payload.ColorAndDistance = float4(0.2f, 0.2f,  -0.6f * launchIndex.y / dims.y + 1.0f, -1.0f);
}

struct STriVertex {
  float3 Pos;
  float3 Normal;
  float4 Color;
};

cbuffer cbDirectionalLight: register(b0, space1) {
  float3 g_vLightPosW;
  float Padding;
  float4 g_vLightStength;
};

/// Used for calculate normal calculation in world space.
StructuredBuffer<float4x4> g_txWorldInvTranspose: register(t0, space1);

StructuredBuffer<STriVertex> g_txTriangleVertices: register(t1, space1);
StructuredBuffer<uint> g_txTriangleIndices: register(t2, space1);

///
/// Closest Hit Shader
///
[shader("closesthit")]
void ClosestHit(inout HitInfo payload, Attributes attrib) {

  /// Trace shadow ray.
  float3 posW = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
  float3 lightDir = g_vLightPosW - posW;

  lightDir = normalize(lightDir);

  RayDesc ray;
  ray.Origin = posW;
  ray.Direction = lightDir;
  ray.TMin = 0.01;
  ray.TMax = 100000;

  ShadowHitInfo shadowPayload = { true };

  TraceRay(
    g_aSceneBVH,
    RAY_FLAG_NONE,
    0xFF,
    1,
    0,
    1,
    ray,
    shadowPayload);

  float shadowFactor = shadowPayload.Hit ? 0.3f : 1.0f;

  uint vid = 3 * PrimitiveIndex();
  float3 barycentrics = float3(1.0f - attrib.bary.x - attrib.bary.y, attrib.bary.x, attrib.bary.y);

  uint v0 = g_txTriangleIndices[vid];
  uint v1 = g_txTriangleIndices[vid + 1];
  uint v2 = g_txTriangleIndices[vid + 2];

  STriVertex V0, V1, V2;

  V0 = g_txTriangleVertices[v0];
  V1 = g_txTriangleVertices[v1];
  V2 = g_txTriangleVertices[v2];

  float4 diffuseTexColor = V0.Color * barycentrics.x + V1.Color * barycentrics.y + V2.Color * barycentrics.z;
  
  float3 normalW = V0.Normal * barycentrics.x + V1.Normal * barycentrics.y + V2.Normal * barycentrics.z;

  normalW = mul(normalW, (float3x3)g_txWorldInvTranspose[InstanceID()]);

  normalW = normalize(normalW);

  float4 diffuse = diffuseTexColor * g_vLightStength * shadowFactor * max(dot(normalW, lightDir), 0.0f);

  payload.ColorAndDistance = diffuse;
}

//[shader("closesthit")]
//void ShadowClosestHit(inout ShadowHitInfo payload : SV_RayPayload, Attributes attrib) {
//  payload.Hit = true;
//}

[shader("miss")]
void ShadowMiss(inout ShadowHitInfo payload : SV_RayPayload) {
  payload.Hit = false;
}
