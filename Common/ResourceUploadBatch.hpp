#pragma once
#include "d3dUtils.h"
#include <future>

class SyncFence;

class ResourceUploadBatch {
public:
  ResourceUploadBatch(_In_ ID3D12Device *pDevice, _In_ D3D12MAAllocator *pAllocator);
  ResourceUploadBatch(ResourceUploadBatch &&) = default;
  ResourceUploadBatch& operator = (ResourceUploadBatch &&) = default;
  ~ResourceUploadBatch();

  D3D12MAAllocator *GetAllocator() const;
  ID3D12Device *GetDevice() const; // Borrowed reference

  HRESULT Begin(D3D12_COMMAND_LIST_TYPE commandListType = D3D12_COMMAND_LIST_TYPE_DIRECT);

  HRESULT End(_In_ ID3D12CommandQueue *commitQueue, _Out_opt_ std::future<HRESULT> *waitable);

  HRESULT Enqueue(
    _In_ ID3D12Resource *pDestResource,
    _In_range_(0,D3D12_REQ_SUBRESOURCES) UINT FirstSubresource,
    _In_range_(0,D3D12_REQ_SUBRESOURCES-FirstSubresource) UINT NumSubresources,
    _In_reads_(NumSubresources) const D3D12_SUBRESOURCE_DATA* pSrcData);

  HRESULT Enqueue(
    _In_ ID3D12Resource *pResourceDefault,
    _In_ const D3D12MAResourceSPtr *uploadBuffer
  );

  void ResourceBarrier(
    _In_ uint32_t numBarriers,
    _In_ const D3D12_RESOURCE_BARRIER *pBarriers
  );

private:
  ResourceUploadBatch(const ResourceUploadBatch &) = delete;
  ResourceUploadBatch& operator = (const ResourceUploadBatch &) = delete;

  class Impl;
  std::unique_ptr<Impl> m_pImpl;
};