#include "Material.h"
#include "Texture.h"

using namespace DirectX;

Material::Material()
{
    CBIndex = -1;
    CbvHeapIndex = -1;

    NumFrameDirty = -1;
    DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };

    FresnelR0 = { 0.01f, 0.01f, 0.01f };
    Roughness = 0.25f;

    XMMATRIX I = XMMatrixIdentity();
    XMStoreFloat4x4(&MatTransform, I);

    DiffuseMap = nullptr;
    NormalMap = nullptr;
    HeightMap = nullptr;
    DiffuseMapHeapIndex = -1;
}


Material::~Material() {
    SAFE_RELEASE(DiffuseMap);
    SAFE_RELEASE(NormalMap);
    SAFE_RELEASE(HeightMap);
}

void Material::SetDiffuseMap(Texture *pDiffuseMap) {
    SAFE_RELEASE(DiffuseMap);
    DiffuseMap = pDiffuseMap;
    SAFE_ADDREF(DiffuseMap);
}

void Material::SetNormalMap(Texture *pNormalMap) {
    SAFE_RELEASE(NormalMap);
    NormalMap = pNormalMap;
    SAFE_ADDREF(NormalMap);
}

void Material::SetHeightMap(Texture *pHeightMap) {
    SAFE_RELEASE(HeightMap);
    HeightMap = pHeightMap;
    SAFE_ADDREF(HeightMap);
}
