#include "DescriptorHeapAllocator.h"

DescriptorHeapAllocator::DescriptorHeapAllocator(UINT reservedNumDescriptor) {
  m_pDescriptorHeap = nullptr;
  Reset(reservedNumDescriptor);
}

DescriptorHeapAllocator::~DescriptorHeapAllocator() {
  SAFE_RELEASE(m_pDescriptorHeap);
}

VOID DescriptorHeapAllocator::Reset(UINT reservedNumDescriptor) {
  SAFE_RELEASE(m_pDescriptorHeap);
  m_aViewDescs.clear();
  m_aViewDescs.reserve(reservedNumDescriptor);
}

VOID DescriptorHeapAllocator::AddView(
  _In_ const D3D12_CONSTANT_BUFFER_VIEW_DESC &desc,
  _Out_opt_ D3D12_CPU_DESCRIPTOR_HANDLE *pCpuHandle,
  _Out_opt_ D3D12_GPU_DESCRIPTOR_HANDLE *pGpuHandle
) {
  HeapViewDesc descriptorDesc = {};
  descriptorDesc.Type = CBV;
  descriptorDesc.CbvDesc = desc;

  descriptorDesc.pCpuHandle = pCpuHandle;
  descriptorDesc.pGpuHandle = pGpuHandle;

  m_aViewDescs.push_back(descriptorDesc);
}

VOID DescriptorHeapAllocator::AddView(
  const D3D12_SHADER_RESOURCE_VIEW_DESC &desc,
  _In_ ID3D12Resource *pResource,
  _Out_opt_ D3D12_CPU_DESCRIPTOR_HANDLE *pCpuHandle,
  _Out_opt_ D3D12_GPU_DESCRIPTOR_HANDLE *pGpuHandle
) {
  HeapViewDesc descriptorDesc = {};
  descriptorDesc.Type = SRV;
  descriptorDesc.SrvDesc = desc;
  descriptorDesc.pResource = pResource;
  descriptorDesc.pCpuHandle = pCpuHandle;
  descriptorDesc.pGpuHandle = pGpuHandle;

  m_aViewDescs.push_back(descriptorDesc);
}

VOID DescriptorHeapAllocator::AddView(
  _In_ const D3D12_UNORDERED_ACCESS_VIEW_DESC &desc,
  _In_ ID3D12Resource *pResource,
  _Out_opt_ D3D12_CPU_DESCRIPTOR_HANDLE *pCpuHandle,
  _Out_opt_ D3D12_GPU_DESCRIPTOR_HANDLE *pGpuHandle
) {
  HeapViewDesc descriptorDesc = {};
  descriptorDesc.Type = UAV;
  descriptorDesc.UavDesc = desc;
  descriptorDesc.pResource = pResource;
  descriptorDesc.pCpuHandle = pCpuHandle;
  descriptorDesc.pGpuHandle = pGpuHandle;

  m_aViewDescs.push_back(descriptorDesc);
}

HRESULT DescriptorHeapAllocator::Allocate(
  _In_ ID3D12Device *pd3dDevice,
  _In_opt_ D3D12_DESCRIPTOR_HEAP_TYPE heapType,
  _In_opt_ D3D12_DESCRIPTOR_HEAP_FLAGS flags
) {

  HRESULT hr;

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc;
  heapDesc.Type = heapType;
  heapDesc.Flags = flags;
  heapDesc.NodeMask = 0;
  heapDesc.NumDescriptors = (UINT)m_aViewDescs.size();

  SAFE_RELEASE(m_pDescriptorHeap);
  V_RETURN(pd3dDevice->CreateDescriptorHeap(
    &heapDesc,
    IID_PPV_ARGS(&m_pDescriptorHeap)
  ));

  D3D12_CPU_DESCRIPTOR_HANDLE hCpuHandle = m_pDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
  D3D12_GPU_DESCRIPTOR_HANDLE hGpuHandle = m_pDescriptorHeap->GetGPUDescriptorHandleForHeapStart();

  UINT64 uDescriptorOffsetInBytes = pd3dDevice->GetDescriptorHandleIncrementSize(heapDesc.Type);

  for (auto &descriptorDesc : m_aViewDescs) {
    switch (descriptorDesc.Type) {
    case CBV:
      pd3dDevice->CreateConstantBufferView(&descriptorDesc.CbvDesc, hCpuHandle);
      break;
    case SRV:
      pd3dDevice->CreateShaderResourceView(descriptorDesc.pResource, &descriptorDesc.SrvDesc, hCpuHandle);
      break;
    case UAV:
      pd3dDevice->CreateUnorderedAccessView(descriptorDesc.pResource, nullptr, &descriptorDesc.UavDesc, hCpuHandle);
      break;
    }

    descriptorDesc.pCpuHandle ? (void)(*descriptorDesc.pCpuHandle = hCpuHandle) : (void)0;
    descriptorDesc.pGpuHandle ? (void)(*descriptorDesc.pGpuHandle = hGpuHandle) : (void)0;

    hCpuHandle.ptr += uDescriptorOffsetInBytes;
    hGpuHandle.ptr += uDescriptorOffsetInBytes;
  }

  /// Reset the descripor description buffer.
  m_aViewDescs.clear();

  return hr;
}

HRESULT DescriptorHeapAllocator::GetHeap(_Out_ ID3D12DescriptorHeap **ppHeap) {
  if (ppHeap) {
    _ASSERT(m_pDescriptorHeap && "Descriptor heap is not allocated yet!");
    SAFE_RELEASE(*ppHeap);
    *ppHeap = m_pDescriptorHeap;
    SAFE_ADDREF(*ppHeap);
  }
  return S_OK;
}

