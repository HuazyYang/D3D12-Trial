#ifndef LIGHT_UTILS_H
#define LIGHT_UTILS_H

struct Light {
    float3 Strength;     /// Light color
    float  FalloffStart; /// Point Spot light only
    float3 Direction;   /// Directonal/Spot light only
    float  FalloffEnd;   /// Point/Spot light only
    float3 Position;    /// Point light only
    float  SpotPower;
};

struct Material {
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Shininess;
};

#pragma warning(disable: 4000)

#if 1
float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    // Linear falloff.
    return saturate((falloffEnd - d) / (falloffEnd - falloffStart));
}

// Schlick gives an approximation to Fresnel reflectance (see pg. 233 "Real-Time Rendering 3rd Ed.").
// R0 = ( (n-1)/(n+1) )^2, where n is the index of refraction.
float3 SchlickFresnel(float3 R0, float3 normal, float3 lightVec)
{
    float cosIncidentAngle = saturate(dot(normal, lightVec));

    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0f - R0)*(f0*f0*f0*f0*f0);

    return reflectPercent;
}

float3 BlinnPhong(float3 lightStrength, float3 lightVec, float3 normal, float3 toEye, Material mat)
{
    const float m = mat.Shininess * 256.0f;
    float3 halfVec = normalize(toEye + lightVec);

    float roughnessFactor = (m + 8.0f)*pow(max(dot(halfVec, normal), 0.0f), m) / 8.0f;
    float3 fresnelFactor = SchlickFresnel(mat.FresnelR0, halfVec, lightVec);

    float3 specAlbedo = fresnelFactor * roughnessFactor;

    // Our spec formula goes outside [0,1] range, but we are 
    // doing LDR rendering.  So scale it down a bit.
    specAlbedo = specAlbedo / (specAlbedo + 1.0f);

    return (mat.DiffuseAlbedo.rgb + specAlbedo) * lightStrength;
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for directional lights.
//---------------------------------------------------------------------------------------
float3 ComputeDirectionalLight(Light L, Material mat, float3 normal, float3 toEye)
{
    // The light vector aims opposite the direction the light rays travel.
    float3 lightVec = -L.Direction;

    // Scale light down by Lambert's cosine law.
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for point lights.
//---------------------------------------------------------------------------------------
float3 ComputePointLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye)
{
    // The vector from the surface to the light.
    float3 lightVec = L.Position - pos;

    // The distance from surface to light.
    float d = length(lightVec);

    // Range test.
    if (d > L.FalloffEnd)
        return 0.0f;

    // Normalize the light vector.
    lightVec /= d;

    // Scale light down by Lambert's cosine law.
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;

    // Attenuate light by distance.
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}

//---------------------------------------------------------------------------------------
// Evaluates the lighting equation for spot lights.
//---------------------------------------------------------------------------------------
float3 ComputeSpotLight(Light L, Material mat, float3 pos, float3 normal, float3 toEye)
{
    // The vector from the surface to the light.
    float3 lightVec = L.Position - pos;

    // The distance from surface to light.
    float d = length(lightVec);

    // Range test.
    if (d > L.FalloffEnd)
        return 0.0f;

    // Normalize the light vector.
    lightVec /= d;

    // Scale light down by Lambert's cosine law.
    float ndotl = max(dot(lightVec, normal), 0.0f);
    float3 lightStrength = L.Strength * ndotl;

    // Attenuate light by distance.
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    lightStrength *= att;

    // Scale by spotlight
    float spotFactor = pow(max(dot(-lightVec, L.Direction), 0.0f), L.SpotPower);
    lightStrength *= spotFactor;

    return BlinnPhong(lightStrength, lightVec, normal, toEye, mat);
}
#else

/// Linear attenuation factor, which apply to point lights and spot lights.
float CalcAttenuation(float d, float fFalloffStart, float fFalloffEnd) {
    /// Linear fallout.
    return saturate((fFalloffEnd - d) / (fFalloffEnd - fFalloffStart));
}

/// Schlick gives an approximation to Fresnel reflectence
float3 SchlickFresnel(float3 R0, float3 vNormal, float3 vLight) {
    float cosIncidentAngle = saturate(dot(vNormal, vLight));
    float f0 = 1.0f - cosIncidentAngle;
    float3 reflectPercent = R0 + (1.0f - R0) *(f0*f0*f0*f0*f0);
    return reflectPercent;
}

float3 BlinnPhong(
    float3 vLightStrength,
    float3 vLight,
    float3 vNormal,
    float3 vToEye,
    Material mat) {

    /// Dirive m from the shininess, which is derived from the roughness
    const float m = mat.Shininess * 256.0f;
    float3 vHalfVec = normalize(vToEye + vLight);
    float roughnessFactor = (m + 8.0f) * pow(max(dot(vHalfVec, vNormal), 0.0f), m) / 8.0f;
    float3 fresnelFactor = SchlickFresnel(mat.FresnelR0, vHalfVec, vLight);

    /// Out spec formaula goes outside [0, 1] range, but we are
    /// LDR rendering, so scale it down a bit.
    float3 specAlbedo = fresnelFactor * roughnessFactor;

    specAlbedo = specAlbedo / (specAlbedo + 1.0f);

    return (mat.DiffuseAlbedo.rgb + specAlbedo) * vLightStrength;
}

///
/// Evaluate the lighting equation for directional light
///
float3 ComputeDirectionalLight(Light L, Material mat, float3 vNormal, float3 vToEye) {
    /// The light vector aims opposite the direction the light rays travel.
    float3 vLight = -L.Direction;

    /// Scale light down by Lambert's cosine law.
    float ndotl = max(dot(vLight, vNormal), 0.0f);
    float3 vLightStrength = L.Strength * ndotl;

    return BlinnPhong(vLightStrength, vLight, vNormal, vToEye, mat);
}

float3 ComputePointLight(Light L, Material mat, float3 vPos, float3 vNormal, float3 vToEye) {
    float3 vLight = L.Position - vPos;
    float d = length(vLight);

    if (d > L.FalloffEnd)
        return float3(0.0f, 0.0f, 0.0f);

    vLight /= d;

    /// Scale light down by Lambert's cosine law.
    float ndotl = max(dot(vLight, vNormal), 0.0f);
    float3 vLightStrength = L.Strength * ndotl;

    /// Attenuate light by distance.
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    vLightStrength *= att;

    return BlinnPhong(vLightStrength, vLight, vNormal, vToEye, mat);
}

float3 ComputeSpotLight(Light L, Material mat, float3 vPos, float3 vNormal, float3 vToEye) {
    float3 vLight = L.Position - vPos;
    float d = length(vLight);

    if (d > L.FalloffEnd)
        return 0.0f;

    vLight /= d;

    /// scale light down by Lambert's cosine law.
    float ndotl = max(dot(vLight, vNormal), 0.0f);
    float3 vLightStrength = L.Strength * ndotl;

    /// Attenuate light by distance
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    vLightStrength *= att;

    /// Scale by spotlght.
    float spotFactor = pow(max(dot(-vLight, L.Direction), 0.0f), L.SpotPower);
    vLightStrength *= spotFactor;

    return BlinnPhong(vLightStrength, vLight, vNormal, vToEye, mat);
}
#endif

#ifndef NUM_DIR_LIGHTS
#define NUM_DIR_LIGHTS      4
#endif

#ifndef NUM_POINT_LIGHTS
#define NUM_POINT_LIGHTS    4
#endif

#ifndef NUM_SPOT_LIGHTS
#define NUM_SPOT_LIGHTS     4
#endif

#define NUM_LIGHTS  (NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS)

float4 ComputeLighting(
    Light g_aLights[NUM_LIGHTS],
    Material mat,
    float3 vPos,
    float3 vNormal,
    float3 vToEye,
    float fShadowFactor) {

    float3 results = 0.0f;
    int i;

    [unroll]
    for (i = 0; i < NUM_DIR_LIGHTS; ++i) {
        results += ComputeDirectionalLight(g_aLights[i], mat, vNormal, vToEye);
    }

    [unroll]
    for (; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; ++i) {
        results += ComputePointLight(g_aLights[i], mat, vPos, vNormal, vToEye);
    }

    [unroll]
    for (i = 0; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i) {
        results += ComputeSpotLight(g_aLights[i], mat, vPos, vNormal, vToEye);
    }

    results *= fShadowFactor;

    return float4(results, 0.0f);
}

#endif