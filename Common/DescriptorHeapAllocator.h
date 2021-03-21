#pragma once
#include "d3dUtils.h"
#include <vector>

class DescriptorHeapAllocator: public NonCopyable
{
public:
  DescriptorHeapAllocator(_In_ UINT reservedNumDescriptor = 4);
  ~DescriptorHeapAllocator();

  VOID Reset(_In_ UINT reservedNumDescriptor);

  VOID AddView(
    _In_ const D3D12_CONSTANT_BUFFER_VIEW_DESC &desc,
    _Out_opt_ D3D12_CPU_DESCRIPTOR_HANDLE *pCpuHandle,
    _Out_opt_ D3D12_GPU_DESCRIPTOR_HANDLE *pGpuHandle
    );

  VOID AddView(
    const D3D12_SHADER_RESOURCE_VIEW_DESC &desc,
    _In_ ID3D12Resource *pResource, /// No reference count operation.
    _Out_opt_ D3D12_CPU_DESCRIPTOR_HANDLE *pCpuHandle,
    _Out_opt_ D3D12_GPU_DESCRIPTOR_HANDLE *pGpuHandle
  );
  VOID AddView(
    _In_ const D3D12_UNORDERED_ACCESS_VIEW_DESC &desc,
    _In_ ID3D12Resource *pResource, /// No reference count operation.
    _Out_opt_ D3D12_CPU_DESCRIPTOR_HANDLE *pCpuHandle,
    _Out_opt_ D3D12_GPU_DESCRIPTOR_HANDLE *pGpuHandle
  );

  HRESULT Allocate(
    _In_ ID3D12Device *pd3dDevice, /// No reference count operation.
    _In_opt_ D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
    _In_opt_ D3D12_DESCRIPTOR_HEAP_FLAGS flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
  );

  HRESULT GetHeap(_Out_ ID3D12DescriptorHeap **ppHeap);

private:
  ID3D12DescriptorHeap *m_pDescriptorHeap;

  enum HEAP_VIEW_TYPE {
    CBV = 0,
    SRV = 1,
    UAV = 2
  };

  struct HeapViewDesc {
    HEAP_VIEW_TYPE Type;
    union {
      D3D12_CONSTANT_BUFFER_VIEW_DESC CbvDesc;
      D3D12_SHADER_RESOURCE_VIEW_DESC SrvDesc;
      D3D12_UNORDERED_ACCESS_VIEW_DESC UavDesc;
    };
    /// Resource used for binding to the view.
    union {
      ID3D12Resource *pResource;
    };

    D3D12_CPU_DESCRIPTOR_HANDLE *pCpuHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE *pGpuHandle;
  };

  std::vector<HeapViewDesc> m_aViewDescs;
};

