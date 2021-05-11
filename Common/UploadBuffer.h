#pragma once
#include "d3dUtils.h"
#include <vector>

class UploadBuffer
{
public:
    ~UploadBuffer();

    HRESULT CreateBuffer(ID3D12Device *pd3dDevice,
        UINT uElementCount,
        UINT cbElement,
        BOOL bIsConstant
    );

    VOID CopyData(const void *pBuffer, UINT cbBuffer, UINT iIndex);

    D3D12_GPU_VIRTUAL_ADDRESS GetConstBufferAddress() const;
    D3D12_GPU_VIRTUAL_ADDRESS GetConstBufferAddress(UINT uIndex) const;

    /// Check if wrapped buffer is valid.
    BOOL IsValid() const;
    UINT GetBufferSize() const;
    UINT GetByteStride() const;

protected:
    ID3D12Resource  *m_pUploadBuffer = nullptr;
    BYTE            *m_pMappedData = nullptr;
    UINT            m_cbPerElement = 0;
    UINT            m_uElementCount = 0;
    UINT            m_cbElementStride = 0;
};



class UploadBufferStack {
public:
  UploadBufferStack();
  ~UploadBufferStack();
  HRESULT Initialize(ID3D12Device *pDevice, UINT BlockSize, UINT ReservedBlockCount);
  HRESULT Push(_In_ const void *pData, _In_ UINT uBufferSize, _Out_opt_ D3D12_CONSTANT_BUFFER_VIEW_DESC *pCBV = nullptr);
  HRESULT Push(_In_ UINT uBufferSize, _Out_opt_ void **ppMappedData, _Out_opt_ D3D12_CONSTANT_BUFFER_VIEW_DESC *pCBV = nullptr);
  void Clear();
  void ClearCapacity();
private:
  void Destroy();
  void LockAllocator();
  void UnlockAllocator();

  struct BufferStorage {
    ID3D12Resource *UploadBuffer;
    UINT BufferSize;
    void *pMappedData;
  };

  ID3D12Device *m_pd3dDevice;
  std::vector<BufferStorage> m_aBufferStorage;
  UINT m_uBlockSize;
  UINT m_uReservedBlockCount;
  UINT m_uCurrBlock;
  UINT m_uCurrOffsetInBlock;
  UINT m_uCurrBufferSize;

  // Multithread support
  CRITICAL_SECTION m_csAlloc;
};
