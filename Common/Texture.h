#pragma once
#include "d3dUtils.h"
#include <string>

struct Texture: public Unknown12 {
    Texture(const char *pszName = nullptr);
    ~Texture();

    HRESULT CreateTextureFromDDSFile(
        ID3D12Device *pd3dDevice,
        ID3D12GraphicsCommandList *pd3dCommandList,
        const wchar_t *pszFileName);

    HRESULT CreateTextureFromDDSMemory(
        ID3D12Device *pd3dDevice,
        ID3D12GraphicsCommandList *pd3dCommandList,
        const void *pData, size_t len);

    /// Call this when the texture is initalized by command list.
    VOID DisposeUploaders();

    std::string Name;

    ID3D12Resource *Resource;
    ID3D12Resource *UploadHeap;

    INT SrvHeapIndex;
};

