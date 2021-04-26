#pragma once
#include "d3dUtils.h"
#include <string>
#include <DirectXTex.h>
#include "D3D12MemAllocator.hpp"

namespace DirectX {

HRESULT __cdecl CreateTexture(_In_ ID3D12Device *pDevice, _In_ D3D12MAAllocator *pAllocator,
                              _In_ const TexMetadata &metadata, _Outptr_ D3D12MAResourceSPtr *ppResource) noexcept;

HRESULT __cdecl CreateTextureEx(_In_ ID3D12Device *pDevice, _In_ D3D12MAAllocator *pAllocator,
                                _In_ const TexMetadata &metadata, _In_ D3D12_RESOURCE_FLAGS resFlags,
                                _In_ bool forceSRGB, _Outptr_ D3D12MAResourceSPtr *ppResource) noexcept;
}; // namespace DirectX

struct Texture: public Unknown12 {
  Texture(const char *pszName = nullptr);
  ~Texture();

  HRESULT CreateTextureFromDDSFile(ID3D12Device *pd3dDevice, ID3D12GraphicsCommandList *pd3dCommandList,
                                   const wchar_t *pszFileName);

  /// Call this when the texture is initalized by command list.
  VOID DisposeUploaders();

  std::string Name;

  ID3D12Resource *Resource;
  ID3D12Resource *UploadHeap;

  INT SrvHeapIndex;
};
