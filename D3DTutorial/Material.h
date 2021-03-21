#pragma once
#include "d3dUtils.h"
#include <DirectXMath.h>
#include <string>

struct Texture;

struct Material: public Unknown12
{
    Material();
    ~Material();

    void SetDiffuseMap(Texture *pTexture);
    void SetNormalMap(Texture *pNormalMap);
    void SetHeightMap(Texture *pHeightMap);

    std::string Name;

    int CBIndex;
    int CbvHeapIndex;

    int NumFrameDirty;

    DirectX::XMFLOAT4 DiffuseAlbedo;

    DirectX::XMFLOAT3 FresnelR0;
    FLOAT Roughness;

    DirectX::XMFLOAT4X4 MatTransform;

    Texture *DiffuseMap;
    Texture *NormalMap;
    Texture *HeightMap;
    int DiffuseMapHeapIndex;
};

