#pragma once
#include "d3dUtils.h"
#include <DirectXCollision.h>
#include <map>
#include <memory>

class MeshBuffer;
struct Material;
struct Texture;

struct RenderItem {
    RenderItem();
    RenderItem(const RenderItem &rhs);
    RenderItem(RenderItem &&rhs);
    ~RenderItem();

    RenderItem& operator = (const RenderItem &rhs);
    RenderItem& operator = (RenderItem &&rhs);

    void SetMeshBuffer(MeshBuffer *pMeshBuffer);
    void SetMaterial(Material *pMaterial);

    // Dirty flag indicating the object data has changed and we need to update the constant buffer.
    // Because we have an object cbuffer for each FrameResource, we have to apply the
    // update to each FrameResource.  Thus, when we modify obect data we should set 
    // NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
    INT iNumFramesDirty;

    DirectX::XMFLOAT4X4 matWorld;
    DirectX::XMFLOAT4X4 matTexTransform;

    UINT uCBIndex;
    UINT uCbvHeapIndex;

    MeshBuffer *pMeshBuffer;
    Material *pMaterial;

    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType;

    UINT uIndexCount;
    UINT uStartIndexLocation;
    INT iBaseVertexLocation;

    DirectX::BoundingBox Bounds;
};


