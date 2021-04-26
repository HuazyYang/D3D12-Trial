#include "Texture.h"

//-------------------------------------------------------------------------------------
// Create a texture resource
//-------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT DirectX::CreateTexture(ID3D12Device* pDevice, D3D12MAAllocator* pAllocator, const TexMetadata& metadata,
                               D3D12MAResourceSPtr* ppResource) noexcept {
  return CreateTextureEx(pDevice, pAllocator, metadata, D3D12_RESOURCE_FLAG_NONE, false, ppResource);
}

_Use_decl_annotations_
HRESULT DirectX::CreateTextureEx(ID3D12Device* pDevice, D3D12MAAllocator* pAllocator, const TexMetadata& metadata,
                                 D3D12_RESOURCE_FLAGS resFlags, bool forceSRGB,
                                 D3D12MAResourceSPtr* ppResource) noexcept {
  if (!pDevice || !ppResource)
    return E_INVALIDARG;

  *ppResource = nullptr;

  if (!metadata.mipLevels || !metadata.arraySize)
    return E_INVALIDARG;

  if ((metadata.width > UINT32_MAX) || (metadata.height > UINT32_MAX) || (metadata.mipLevels > UINT16_MAX) ||
      (metadata.arraySize > UINT16_MAX))
    return E_INVALIDARG;

  DXGI_FORMAT format = metadata.format;
  if (forceSRGB) {
    format = MakeSRGB(format);
  }

  D3D12_RESOURCE_DESC desc = {};
  desc.Width               = static_cast<UINT>(metadata.width);
  desc.Height              = static_cast<UINT>(metadata.height);
  desc.MipLevels           = static_cast<UINT16>(metadata.mipLevels);
  desc.DepthOrArraySize    = (metadata.dimension == TEX_DIMENSION_TEXTURE3D) ? static_cast<UINT16>(metadata.depth)
                                                                             : static_cast<UINT16>(metadata.arraySize);
  desc.Format              = format;
  desc.Flags               = resFlags;
  desc.SampleDesc.Count    = 1;
  desc.Dimension           = static_cast<D3D12_RESOURCE_DIMENSION>(metadata.dimension);

  D3D12MA_ALLOCATION_DESC allocDesc = {};
  allocDesc.Flags                   = D3D12MA::ALLOCATION_FLAG_NONE;
  allocDesc.HeapType                = D3D12_HEAP_TYPE_DEFAULT;

  HRESULT hr = (*pAllocator)
                   ->CreateResource(&allocDesc, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                    D3D12MA_IID_PPV_ARGS(ppResource));

  return hr;
}

Texture::Texture(const char* pszName) {
  if (pszName)
    Name = pszName;

  Resource     = nullptr;
  UploadHeap   = nullptr;
  SrvHeapIndex = -1;
}

Texture::~Texture() {
  SAFE_RELEASE(Resource);
  SAFE_RELEASE(UploadHeap);
}

VOID Texture::DisposeUploaders() {
  SAFE_RELEASE(UploadHeap);
}

HRESULT Texture::CreateTextureFromDDSFile(ID3D12Device* pd3dDevice, ID3D12GraphicsCommandList* pd3dCommandList,
                                          const wchar_t* pszFileName) {
  HRESULT hr;

  DirectX::TexMetadata metaData;
  DirectX::ScratchImage scratchImage;
  WCHAR szFilePath[MAX_PATH];
  std::vector<D3D12_SUBRESOURCE_DATA> subresources;

  SAFE_RELEASE(Resource);
  SAFE_RELEASE(UploadHeap);

  V_RETURN(FindDemoMediaFileAbsPath(pszFileName, _countof(szFilePath), szFilePath));

  V_RETURN(DirectX::LoadFromDDSFile(szFilePath, DirectX::DDS_FLAGS_NONE, &metaData, scratchImage));

  V_RETURN(DirectX::CreateTexture(pd3dDevice, metaData, &Resource));

  V_RETURN(DirectX::PrepareUpload(pd3dDevice, scratchImage.GetImages(), scratchImage.GetImageCount(), metaData,
                                  subresources));

  UINT64 uploadBufferSize = GetRequiredIntermediateSize(Resource, 0, (UINT)subresources.size());

  V(pd3dDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
                                        &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&UploadHeap)));
  if (FAILED(hr)) {
    SAFE_RELEASE(Resource);
    return hr;
  }

  /// The default buffer state is D3D12_RESOURCE_STATE_COPY_DEST, so no barrier is need here.
  // pd3dCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(Resource,
  //    D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));

  UpdateSubresources(pd3dCommandList, Resource, UploadHeap, 0, 0, (UINT)subresources.size(), subresources.data());

  pd3dCommandList->ResourceBarrier(
      1, &CD3DX12_RESOURCE_BARRIER::Transition(Resource, D3D12_RESOURCE_STATE_COPY_DEST,
                                               D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE |
                                                   D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));

  return hr;
}
