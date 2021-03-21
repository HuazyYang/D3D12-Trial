#include "MeshBuffer.h"
#include "Material.h"
#include "Texture.h"
#include "RenderItem.h"

using namespace DirectX;

RenderItem::RenderItem() {

    iNumFramesDirty = -1;

    XMStoreFloat4x4(&matWorld, XMMatrixIdentity());
    XMStoreFloat4x4(&matTexTransform, XMMatrixIdentity());

    uCBIndex = -1;
    uCbvHeapIndex = -1;

    pMeshBuffer = nullptr;
    pMaterial = nullptr;

    PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    uIndexCount = 0;
    uStartIndexLocation = 0;
    iBaseVertexLocation = 0;
}

RenderItem::RenderItem(const RenderItem &rhs) {
    memcpy(this, &rhs, sizeof(rhs));
    SAFE_ADDREF(pMeshBuffer);
    SAFE_ADDREF(pMaterial);
}

RenderItem::RenderItem(RenderItem &&rhs) {
    memcpy(this, &rhs, sizeof(rhs));
    rhs.pMaterial = nullptr;
    rhs.pMeshBuffer = nullptr;
}

RenderItem::~RenderItem() {
    SAFE_RELEASE(pMeshBuffer);
    SAFE_RELEASE(pMaterial);
}

RenderItem& RenderItem::operator=(const RenderItem &rhs) {
    if (this != &rhs) {
        memcpy(this, &rhs, sizeof(rhs));
        SAFE_ADDREF(pMaterial);
        SAFE_ADDREF(pMeshBuffer);
    }
    return *this;
}

RenderItem& RenderItem::operator=(RenderItem &&rhs) {
    if (this != &rhs) {
        memcpy(this, &rhs, sizeof(rhs));
        rhs.pMaterial = nullptr;
        rhs.pMeshBuffer = nullptr;
    }
    return *this;
}

void RenderItem::SetMeshBuffer(MeshBuffer *pMeshBuffer) {
    SAFE_RELEASE(this->pMeshBuffer);
    this->pMeshBuffer = pMeshBuffer;
    SAFE_ADDREF(this->pMeshBuffer);
}

void RenderItem::SetMaterial(Material *pMaterial) {
    SAFE_RELEASE(this->pMaterial);
    this->pMaterial = pMaterial;
    SAFE_ADDREF(this->pMaterial);
}

