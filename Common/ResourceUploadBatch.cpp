#include "D3D12MemAllocator.hpp"
#include "ResourceUploadBatch.hpp"
#include "SyncFence.hpp"

using Microsoft::WRL::ComPtr;

class ResourceUploadBatch::Impl {
  friend class ResourceUploadBatch;

private:
  struct FrameResource {
    ComPtr<ID3D12CommandAllocator> CmdAllocator;
    ComPtr<SyncFence> SyncFence;
    UINT64 SyncPoint;
    std::vector<D3D12MAResourceSPtr> UploadBuffers;
  };

public:
  Impl(_In_ ID3D12Device *pd3dDevice, _In_ D3D12MAAllocator *pAllocator) {
    m_pd3dDevice = pd3dDevice;
    m_pAllocator = pAllocator;
    m_uInternalState = 0x0;
  }

  D3D12MAAllocator *GetAllocator() const {
    return m_pAllocator;
  }

  ID3D12Device *GetDevice() const {
    return m_pd3dDevice.Get();
  }

  HRESULT Begin(D3D12_COMMAND_LIST_TYPE cmdListType) {

    HRESULT hr;

    // Sanity check
    if (m_uInternalState != 0) {
      // Already have a once.
      V_RETURN2("ResourceUploadBatch: resources are in use!", E_FAIL);
    } else if (cmdListType != D3D12_COMMAND_LIST_TYPE_DIRECT &&
               cmdListType != D3D12_COMMAND_LIST_TYPE_COPY &&
               cmdListType != D3D12_COMMAND_LIST_TYPE_COMPUTE) {
      V_RETURN2("Command list type must be direct, copy or compute!", E_INVALIDARG);
    }

    V_RETURN(m_pd3dDevice->CreateCommandAllocator(
        cmdListType, IID_PPV_ARGS(m_pd3dCmdAlloc.ReleaseAndGetAddressOf())));
    DX_SetDebugName(m_pd3dCmdAlloc.Get(), "ResourceUploadBatch::ComandAllocator");
    V_RETURN(
        m_pd3dDevice->CreateCommandList(0, cmdListType, m_pd3dCmdAlloc.Get(), nullptr,
                                        IID_PPV_ARGS(m_pd3dCommandList.ReleaseAndGetAddressOf())));
    DX_SetDebugName(m_pd3dCommandList.Get(), "ResourceUploadBatch::CommandList");

    m_pd3dCommandList->Close();
    m_pd3dCommandList->Reset(m_pd3dCmdAlloc.Get(), nullptr);

    m_uInternalState = 0x1;
    return hr;
  }

  HRESULT End(_In_ ID3D12CommandQueue *commitQueue, _Out_ std::future<HRESULT> *waitable) {
    HRESULT hr;

    // Sanity check
    if (!commitQueue)
      V_RETURN2("Commit command queue must not be null!", E_INVALIDARG);
    if (!(m_uInternalState & 0x1))
      V_RETURN2("Call \"End\" before call \"Begin\" is not allowed!", E_FAIL);

    V_RETURN(m_pd3dCommandList->Close());
    commitQueue->ExecuteCommandLists(1, CommandListCast(m_pd3dCommandList.GetAddressOf()));

    if (waitable) {

      UINT64 syncPoint;
      FrameResource *pFrameResource;

      if(!m_pSyncFence) {
        V_RETURN(CreateSyncFence(m_pSyncFence.GetAddressOf()));
        V_RETURN(m_pSyncFence->Initialize(m_pd3dDevice.Get()));
      }

      V_RETURN(m_pSyncFence->Signal(commitQueue, &syncPoint));

      pFrameResource = new FrameResource;
      pFrameResource->CmdAllocator = m_pd3dCmdAlloc;
      pFrameResource->SyncFence = m_pSyncFence;
      pFrameResource->SyncPoint = syncPoint;
      pFrameResource->UploadBuffers = std::move(m_aUploadBuffers);

      *waitable = std::async(std::launch::async, [pFrameResource]() -> HRESULT {
        HRESULT hr;
        hr = pFrameResource->SyncFence->WaitForSyncPoint(pFrameResource->SyncPoint);
        delete pFrameResource;

        V_RETURN2("ResourceUploadBatch: sync error!", hr);
        return hr;
      });
    }

    // Release attached resource for further reuse.
    m_pd3dCmdAlloc = nullptr;
    m_pd3dCommandList = nullptr;
    m_aUploadBuffers.clear();
    m_uInternalState = 0x0;

    return hr;
  }

  HRESULT Enqueue(
  _In_ ID3D12Resource *pDestResource,
  _In_range_(0,D3D12_REQ_SUBRESOURCES) UINT FirstSubresource,
  _In_range_(0,D3D12_REQ_SUBRESOURCES-FirstSubresource) UINT NumSubresources,
  _In_reads_(NumSubresources) const D3D12_SUBRESOURCE_DATA* pSrcData) {
    HRESULT hr;

    if (!(m_uInternalState & 0x1))
      V_RETURN2("ResourceUploadBatch: call \"Begin\" first!", E_FAIL);

    UINT64 uploadSize =
        GetRequiredIntermediateSize(pDestResource, FirstSubresource, NumSubresources);

    D3D12MA_ALLOCATION_DESC allocDesc = {};
    allocDesc.Flags = D3D12MA::ALLOCATION_FLAG_NONE;
    allocDesc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

    D3D12MAResourceSPtr scratchResource;

    V_RETURN((*m_pAllocator)
                 ->CreateResource(&allocDesc, &CD3DX12_RESOURCE_DESC::Buffer(uploadSize),
                                  D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                  D3D12MA_IID_PPV_ARGS(&scratchResource)));

    UpdateSubresources(m_pd3dCommandList.Get(), pDestResource, scratchResource.Get(), 0,
                       FirstSubresource, NumSubresources, pSrcData);

    m_aUploadBuffers.push_back(std::move(scratchResource));

    return hr;
  }

  HRESULT Enqueue(_In_ ID3D12Resource *pResourceDefault,
                  _In_ const D3D12MAResourceSPtr *uploadBuffer) {
    HRESULT hr = S_OK;

    if (!(m_uInternalState & 0x1))
      V_RETURN2("ResourceUploadBatch: call \"Begin\" first!", E_FAIL);

    if (pResourceDefault == nullptr || uploadBuffer == nullptr)
      V_RETURN(E_INVALIDARG);

    m_pd3dCommandList->CopyResource(pResourceDefault, uploadBuffer->Get());

    m_aUploadBuffers.push_back(*uploadBuffer);

    return hr;
  }

  void ResourceBarrier(_In_ uint32_t numBarriers, _In_ const D3D12_RESOURCE_BARRIER *pBarriers) {

    HRESULT hr;
    if(!(m_uInternalState & 0x1))
      V2("ResourceUploadBatch: call \"Begin\" first!", E_FAIL);

    m_pd3dCommandList->ResourceBarrier(numBarriers, pBarriers);
  }

private:
  unsigned int m_uInternalState;
  ComPtr<ID3D12Device> m_pd3dDevice;
  ComPtr<ID3D12CommandAllocator> m_pd3dCmdAlloc;
  ComPtr<ID3D12GraphicsCommandList> m_pd3dCommandList;
  ComPtr<SyncFence> m_pSyncFence;
  D3D12MAAllocator *m_pAllocator;
  std::vector<D3D12MAResourceSPtr> m_aUploadBuffers;
};

ResourceUploadBatch::ResourceUploadBatch(_In_ ID3D12Device *pDevice, _In_ D3D12MAAllocator *pAllocator) {
  m_pImpl = std::make_unique<Impl>(pDevice, pAllocator);
}

ResourceUploadBatch::~ResourceUploadBatch() {}

HRESULT ResourceUploadBatch::Begin(D3D12_COMMAND_LIST_TYPE commandListType) {
  return m_pImpl->Begin(commandListType);
}

HRESULT ResourceUploadBatch::End(_In_ ID3D12CommandQueue *commitQueue, _Out_opt_ std::future<HRESULT> *waitable) {
  return m_pImpl->End(commitQueue, waitable);
}

HRESULT ResourceUploadBatch::Enqueue(
  _In_ ID3D12Resource *pDestResource,
  _In_range_(0,D3D12_REQ_SUBRESOURCES) UINT FirstSubresource,
  _In_range_(0,D3D12_REQ_SUBRESOURCES-FirstSubresource) UINT NumSubresources,
  _In_reads_(NumSubresources) const D3D12_SUBRESOURCE_DATA* pSrcData) {
  return m_pImpl->Enqueue(pDestResource, FirstSubresource, NumSubresources, pSrcData);
}

HRESULT ResourceUploadBatch::Enqueue(_In_ ID3D12Resource *pResourceDefault, _In_ const D3D12MAResourceSPtr *uploadBuffer) {
  return m_pImpl->Enqueue(pResourceDefault, uploadBuffer);
}

void ResourceUploadBatch::ResourceBarrier(_In_ uint32_t numBarriers, _In_ const D3D12_RESOURCE_BARRIER *pBarriers) {
  return m_pImpl->ResourceBarrier(numBarriers, pBarriers);
}

D3D12MAAllocator *ResourceUploadBatch::GetAllocator() const {
  return m_pImpl->GetAllocator();
}

ID3D12Device *ResourceUploadBatch::GetDevice() const {
  return m_pImpl->GetDevice();
}