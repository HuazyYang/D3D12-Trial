#pragma once
#include "d3dUtils.h"
#include <vector>

class UploadBufferPool {
public:

private:
    std::vector<ID3D12Resource> m_aUploadBuffers;
};

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

