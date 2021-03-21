///
/// ComputeNBodyGravity.hlsl
///

#define BLOCKSIZE           128
#define BLOCKSIZE_LOG2      7


struct ParticlePos {
    float3 Pos;
    float Mass;
    float3 Vel;
    float AccelScalar;
};

cbuffer cbSettings: register(b0) {
    int2 g_vParam;  /// MAX_PARTICALS
                    /// dimx
    float2 g_vParamf; /// Delta time
                      /// Dumping
};

static const float g_fSofteningSquared = 0.0012500000*0.0012500000;
static const float g_fG = 6.67300e+1f;

StructuredBuffer<ParticlePos> g_txPrevParticles: register(t0);
RWStructuredBuffer< ParticlePos> g_txNextParticles: register(u0);

groupshared float4 g_vCachedParticles[BLOCKSIZE];

float3 BodyGravityInteraction(in float3 pos1, in float4 pos2Mass2) {
    float3 r = pos2Mass2.xyz - pos1;
    float distSqr = dot(r, r);

    distSqr += g_fSofteningSquared;
    float invDist = 1.0 / sqrt(distSqr);

    return g_fG * pos2Mass2.w * invDist * invDist * invDist * r;
}

[numthreads(BLOCKSIZE, 1, 1)]
void NBodyGravityCS(uint3 DTid: SV_DispatchThreadId, uint3 GTid: SV_GroupThreadId,
    uint GI: SV_GroupIndex) {

    ParticlePos P1, P2;
    float4 vCached;
    float3 vAccel = 0;

    P1 = g_txPrevParticles[DTid.x];

    [loop]
    for (int i = 0; i < g_vParam.y; ++i) {
        P2 = g_txPrevParticles[(i << BLOCKSIZE_LOG2) + GI];
        g_vCachedParticles[GI] = float4(P2.Pos, P2.Mass);

        GroupMemoryBarrierWithGroupSync();

        [unroll]
        for (int j = 0; j < BLOCKSIZE; ) {
            vAccel += BodyGravityInteraction(P1.Pos, g_vCachedParticles[j++]);
            vAccel += BodyGravityInteraction(P1.Pos, g_vCachedParticles[j++]);
            vAccel += BodyGravityInteraction(P1.Pos, g_vCachedParticles[j++]);
            vAccel += BodyGravityInteraction(P1.Pos, g_vCachedParticles[j++]);
            vAccel += BodyGravityInteraction(P1.Pos, g_vCachedParticles[j++]);
            vAccel += BodyGravityInteraction(P1.Pos, g_vCachedParticles[j++]);
            vAccel += BodyGravityInteraction(P1.Pos, g_vCachedParticles[j++]);
            vAccel += BodyGravityInteraction(P1.Pos, g_vCachedParticles[j++]);
        }

        GroupMemoryBarrierWithGroupSync();
    }

    /// Since when the index of the particles out of bound, the index operator will return
    /// the massive of zero, so we are done computation.

    P1.Vel += vAccel * g_vParamf.x;
    P1.Vel *= g_vParamf.y;
    P1.Pos += P1.Vel * g_vParamf.x;
    P1.AccelScalar = length(vAccel);

    g_txNextParticles[DTid.x] = P1;
}

