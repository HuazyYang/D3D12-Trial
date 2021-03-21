
cbuffer cbUpdateSettings: register(b0) {
    float g_fK0;
    float g_fK1;
    float g_fK2;

    float g_fDisturbMag;
    int2 g_iDisturbIndex;
}

Texture2D<float> g_PrevSolInput: register(t0);
Texture2D<float> g_CurrSolInput : register(t1);
RWTexture2D<float> g_NextSolOutput: register(u0);

[numthreads(16, 16, 1)]
void UpdateWavesCS( uint3 DTid : SV_DispatchThreadID )
{
    int x = DTid.x;
    int y = DTid.y;

    g_NextSolOutput[int2(x, y)] =
        g_fK0 * g_PrevSolInput[int2(x, y)].r +
        g_fK1 * g_CurrSolInput[int2(x, y)].r +
        g_fK2 * (
            g_CurrSolInput[int2(x,y-1)].r +
            g_CurrSolInput[int2(x,y+1)].r +
            g_CurrSolInput[int2(x-1,y)].r +
            g_CurrSolInput[int2(x+1,y)].r
            );
}

[numthreads(1, 1, 1)]
void DisturbWavesCS() {
    int x = g_iDisturbIndex.x;
    int y = g_iDisturbIndex.y;

    float halfMag = 0.5f * g_fDisturbMag;

    g_NextSolOutput[int2(x,y)] += g_fDisturbMag;
    g_NextSolOutput[int2(x-1,y)] += halfMag;
    g_NextSolOutput[int2(x+1,y)] += halfMag;
    g_NextSolOutput[int2(x,y-1)] += halfMag;
    g_NextSolOutput[int2(x,y+1)] += halfMag;

}
