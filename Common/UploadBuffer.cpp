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

