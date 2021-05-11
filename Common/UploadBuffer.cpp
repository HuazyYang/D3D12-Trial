#include "UploadBuffer.h"

UploadBuffer::~UploadBuffer() {
    SAFE_RELEASE(m_pUploadBuffer);
}

HRESULT UploadBuffer::CreateBuffer(
    ID3D12Device *pd3dDevice,
    UINT uElementCount,
    UINT cbElement,
    BOOL bIsConstant
) {
    UINT cbElementSride = cbElement;
    HRESULT hr;

    SAFE_RELEASE(m_pUploadBuffer);
    m_pMappedData = nullptr;

    // Constant buffer elements need to be multiples of 256 bytes.
    // This is because the hardware can only view constant data 
    // at m*256 byte offsets and of n*256 byte lengths. 
    // typedef struct D3D12_CONSTANT_BUFFER_VIEW_DESC {
    // UINT64 OffsetInBytes; // multiple of 256
    // UINT   SizeInBytes;   // multiple of 256
    // } D3D12_CONSTANT_BUFFER_VIEW_DESC;
    if (bIsConstant)
        cbElementSride = d3dUtils::CalcConstantBufferByteSize(cbElement);

    V_RETURN(pd3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(cbElementSride*uElementCount),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_pUploadBuffer)
    ));

    V(m_pUploadBuffer->Map(0, nullptr, (void **)&m_pMappedData));
    if (FAILED(hr)) {
        SAFE_RELEASE(m_pUploadBuffer);
        return hr;
    }

    m_cbPerElement = cbElement;
    m_cbElementStride = cbElementSride;
    m_uElementCount = uElementCount;

    // We do not need to unmap until we are done with the resource.  However, we must not write to
    // the resource while it is in use by the GPU (so we must use synchronization techniques).

    return S_OK;
}

VOID UploadBuffer::CopyData(const void *pBuffer, UINT cbBuffer, UINT iIndex) {
    HRESULT hr;
    V(cbBuffer >= m_cbPerElement && iIndex < m_uElementCount ? S_OK : E_INVALIDARG);
    if (cbBuffer >= m_cbPerElement && iIndex < m_uElementCount) {
        memcpy(m_pMappedData + iIndex * m_cbElementStride, pBuffer, m_cbPerElement);
    }
}

D3D12_GPU_VIRTUAL_ADDRESS UploadBuffer::GetConstBufferAddress() const {
  return m_pUploadBuffer ? m_pUploadBuffer->GetGPUVirtualAddress() : 0;
}

D3D12_GPU_VIRTUAL_ADDRESS UploadBuffer::GetConstBufferAddress(UINT uIndex) const {
  HRESULT hr;
  V(uIndex <= m_uElementCount ? S_OK : E_INVALIDARG);
  if (uIndex < m_uElementCount && m_pUploadBuffer) {
    return m_pUploadBuffer->GetGPUVirtualAddress() + uIndex * m_cbElementStride;
  }
  return 0;
}

BOOL UploadBuffer::IsValid() const {
  return !!m_pUploadBuffer;
}

UINT UploadBuffer::GetBufferSize() const {
    return m_cbElementStride * m_uElementCount;
}

UINT UploadBuffer::GetByteStride() const {
  return m_cbElementStride;
}


//
// UploadBufferStack implementation
//
UploadBufferStack::UploadBufferStack() {
  m_uBlockSize = 0;
  m_uReservedBlockCount = 0;
  m_uCurrBlock = -1;
  m_uCurrOffsetInBlock = 0;
  m_uCurrBufferSize = 0;
  InitializeCriticalSectionAndSpinCount(&m_csAlloc, 40000);
}

UploadBufferStack::~UploadBufferStack() {

  SAFE_RELEASE(m_pd3dDevice);
  Destroy();

  DeleteCriticalSection(&m_csAlloc);
}

void UploadBufferStack::Destroy() {

  LockAllocator();

  for(auto it = m_aBufferStorage.begin(); it != m_aBufferStorage.end(); ++it) {
    SAFE_RELEASE(it->UploadBuffer);
  }
  m_aBufferStorage.clear();

  UnlockAllocator();
}

void UploadBufferStack::LockAllocator() {
  EnterCriticalSection(&m_csAlloc);
}

void UploadBufferStack::UnlockAllocator() {
  LeaveCriticalSection(&m_csAlloc);
}

HRESULT UploadBufferStack::Initialize(ID3D12Device *pDevice, UINT BlockSize, UINT ReservedBlockCount) {

  if(pDevice == nullptr || BlockSize < 1)
    return E_INVALIDARG;

  SAFE_ADDREF(pDevice);
  m_pd3dDevice = pDevice;
  m_uBlockSize = d3dUtils::CalcConstantBufferByteSize(BlockSize);
  m_uReservedBlockCount = ReservedBlockCount;
  m_uCurrBlock = -1;
  m_uCurrOffsetInBlock = 0;
  m_uCurrBufferSize = 0;
  return S_OK;
}

HRESULT UploadBufferStack::Push(_In_ const void *pData, _In_ UINT uBufferSize, _Out_opt_ D3D12_CONSTANT_BUFFER_VIEW_DESC *pCBV) {

  void *pMappedData;
  HRESULT hr = Push(uBufferSize, &pMappedData, pCBV);
  if(SUCCEEDED(hr))
    memcpy(pMappedData, pData, uBufferSize);
  return hr;
}

HRESULT UploadBufferStack::Push(_In_ UINT uBufferSize, _Out_opt_ void **ppMappedData, _Out_opt_ D3D12_CONSTANT_BUFFER_VIEW_DESC *pCBV) {

  if(m_pd3dDevice == nullptr)
    return E_FAIL;

  HRESULT hr = S_OK;
  ID3D12Resource *pCurrBuffer = nullptr;
  UINT uNextOffset;
  UINT uEndOffset;
  void *pMappedData = nullptr;
  D3D12_GPU_VIRTUAL_ADDRESS pCurrBufferLocation;

  if(uBufferSize > m_uBlockSize) {
    DX_TRACE(L"UploadBufferStack: Block size(%u byte) is smaller than request block size(%u byte)!", m_uBlockSize,
              uBufferSize);
    V_RETURN(E_INVALIDARG);
  }

  LockAllocator();

  uNextOffset = m_uCurrOffsetInBlock + m_uCurrBufferSize;
  uBufferSize = d3dUtils::CalcConstantBufferByteSize(uBufferSize);
  uEndOffset = uNextOffset + uBufferSize;

  if (m_uCurrBlock == -1 || (m_uCurrBlock == (m_aBufferStorage.size() - 1) && uEndOffset > m_uBlockSize)) {
    // Allocate a new block
    hr = m_pd3dDevice->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
      D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Buffer(m_uBlockSize),
      D3D12_RESOURCE_STATE_GENERIC_READ,
      nullptr,
      IID_PPV_ARGS(&pCurrBuffer)
    );
    if(FAILED(hr)) {
      UnlockAllocator();
      V_RETURN(hr);
    }
    m_uCurrBlock += 1;
    pCurrBuffer->Map(0, nullptr, &pMappedData);
    m_aBufferStorage.push_back({ pCurrBuffer, m_uBlockSize, pMappedData });
    uNextOffset = 0;
  } else if (uEndOffset > m_uBlockSize) {
    m_uCurrBlock += 1;
    pCurrBuffer = m_aBufferStorage[m_uCurrBlock].UploadBuffer;
    pMappedData = m_aBufferStorage[m_uCurrBlock].pMappedData;
    uNextOffset = 0;
  } else {
    pCurrBuffer = m_aBufferStorage[m_uCurrBlock].UploadBuffer;
    pMappedData = m_aBufferStorage[m_uCurrBlock].pMappedData;
  }

  pCurrBufferLocation = pCurrBuffer->GetGPUVirtualAddress() + uNextOffset;

  m_uCurrOffsetInBlock = uNextOffset;
  m_uCurrBufferSize  = uBufferSize;

  UnlockAllocator();

  if(ppMappedData) *ppMappedData = (BYTE *)pMappedData + uNextOffset;
  if(pCBV) {
    pCBV->BufferLocation = pCurrBufferLocation;
    pCBV->SizeInBytes = uBufferSize;
  }

  return hr;
}

void UploadBufferStack::Clear() {

  LockAllocator();

  if(m_aBufferStorage.empty()) {
    m_uCurrBlock = -1;
  } else {
    m_uCurrBlock = 0;
  }
  
  m_uCurrOffsetInBlock = 0;
  m_uCurrBufferSize = 0;

  UnlockAllocator();
}

void UploadBufferStack::ClearCapacity() {

  LockAllocator();

  if(m_aBufferStorage.size() > m_uReservedBlockCount) {
    auto itFirst = std::next(m_aBufferStorage.begin(), (ptrdiff_t)m_uReservedBlockCount);

    for(auto it = itFirst; it != m_aBufferStorage.end(); ++it)
      SAFE_RELEASE(it->UploadBuffer);
    
    m_aBufferStorage.erase(itFirst, m_aBufferStorage.end());
  }

  if(m_aBufferStorage.empty()) {
    m_uCurrBlock = -1;
  } else {
    m_uCurrBlock = 0;
  }

  UnlockAllocator();
}
