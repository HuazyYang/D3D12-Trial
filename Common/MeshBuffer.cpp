#include "MeshBuffer.h"


HRESULT CreateMeshBuffer(MeshBuffer **ppMeshBuffer) {

  if (ppMeshBuffer) {
    *ppMeshBuffer = new MeshBuffer;
  }

  return S_OK;
}

///
/// MeshBuffer Implementation
///
MeshBuffer::MeshBuffer()
  : m_pVertexBufferUploader(nullptr)
  , m_pVertexBufferGPU(nullptr)
  , m_pIndexBufferUploader(nullptr)
  , m_pIndexBufferGPU(nullptr)
  , m_uVertexCount(0)
  , m_uVertexBufferByteSize(0)
  , m_uVertexByteStride(0)
  , m_uIndexCount(0)
  , m_uIndexBufferByteSize(0)
  , m_uIndexByteStride(0)
  , m_eIndexFormat(DXGI_FORMAT_UNKNOWN) {
}

MeshBuffer::~MeshBuffer() {
  SAFE_RELEASE(m_pVertexBufferUploader);
  SAFE_RELEASE(m_pIndexBufferUploader);
  SAFE_RELEASE(m_pVertexBufferGPU);
  SAFE_RELEASE(m_pIndexBufferGPU);
}

void MeshBuffer::DisposeUploaders() {
  SAFE_RELEASE(m_pVertexBufferUploader);
  SAFE_RELEASE(m_pIndexBufferUploader);
}

HRESULT MeshBuffer::CreateVertexBuffer(
  _In_ ID3D12Device *pd3dDevice,
  _In_ ID3D12GraphicsCommandList *pd3dCommandList,
  _In_ const void *pData,
  _In_ UINT uVertexCount,
  _In_ UINT uVertexStride,
  _In_opt_ DirectX::BoundingBox *pAABB
) {
  HRESULT hr;
  ID3D12Resource *pBufferGPU = nullptr, *pBufferUploader = nullptr;
  UINT uBufferSize = uVertexCount * uVertexStride;

  V_RETURN(d3dUtils::CreateDefaultBuffer(pd3dDevice, pd3dCommandList,
    pData, uBufferSize, &pBufferUploader, &pBufferGPU));

  m_pVertexBufferUploader = pBufferUploader;
  m_pVertexBufferGPU = pBufferGPU;
  m_uVertexCount = uVertexCount;
  m_uVertexBufferByteSize = uBufferSize;
  m_uVertexByteStride = uVertexStride;

  if (pAABB) {
    m_aAABB = *pAABB;
  }

  return hr;
}

HRESULT MeshBuffer::CreateIndexBuffer(
  _In_ ID3D12Device *pd3dDevice,
  _In_ ID3D12GraphicsCommandList *pd3dCommandList,
  _In_ const void *pData,
  _In_ UINT uIndexCount,
  _In_ UINT uIndexStride
) {
  HRESULT hr;
  ID3D12Resource *pBufferGPU = nullptr, *pBufferUploader = nullptr;
  UINT uBufferSize = uIndexCount * uIndexStride;

  V_RETURN(d3dUtils::CreateDefaultBuffer(pd3dDevice, pd3dCommandList,
    pData, uBufferSize, &pBufferUploader, &pBufferGPU));

  m_pIndexBufferUploader = pBufferUploader;
  m_pIndexBufferGPU = pBufferGPU;
  m_uIndexCount = uIndexCount;
  m_uIndexBufferByteSize = uBufferSize;
  m_uIndexByteStride = uIndexStride;

  switch (uIndexStride) {
    case 2:
      m_eIndexFormat = DXGI_FORMAT_R16_UINT; break;
    case 4:
      m_eIndexFormat = DXGI_FORMAT_R32_UINT; break;
    default:
      m_eIndexFormat = DXGI_FORMAT_UNKNOWN; break;
  }

  return hr;
}

HRESULT MeshBuffer::UploadVertexBuffer(
  _In_ ID3D12GraphicsCommandList *pd3dCommandList,
  _In_ D3D12_RESOURCE_STATES prevState,
  _In_ D3D12_RESOURCE_STATES currState,
  _In_ const void *pData,
  _In_ UINT uByteSize
) {

  HRESULT hr;

  if (pData && uByteSize > m_uVertexBufferByteSize)
    return E_INVALIDARG;

  hr = S_OK;
  if (pData) {
    D3D12_SUBRESOURCE_DATA initData;
    initData.pData = pData;
    initData.RowPitch = uByteSize;
    initData.SlicePitch = uByteSize;

    pd3dCommandList->ResourceBarrier(1,
      &CD3DX12_RESOURCE_BARRIER::Transition(m_pVertexBufferGPU, prevState, D3D12_RESOURCE_STATE_COPY_DEST));

    UpdateSubresources<1>(pd3dCommandList, m_pVertexBufferGPU, m_pVertexBufferUploader, 0, 0, 1, &initData);

    pd3dCommandList->ResourceBarrier(1,
      &CD3DX12_RESOURCE_BARRIER::Transition(m_pVertexBufferGPU, D3D12_RESOURCE_STATE_COPY_DEST, currState));
  }

  return hr;
}

D3D12_VERTEX_BUFFER_VIEW MeshBuffer::VertexBufferView() const {
  D3D12_VERTEX_BUFFER_VIEW vbv = {};
  if (m_pVertexBufferGPU) {
    vbv.BufferLocation = m_pVertexBufferGPU->GetGPUVirtualAddress();
    vbv.SizeInBytes = m_uVertexBufferByteSize;
    vbv.StrideInBytes = m_uVertexByteStride;
  }
  return vbv;
}

D3D12_INDEX_BUFFER_VIEW MeshBuffer::IndexBufferView() const {
  D3D12_INDEX_BUFFER_VIEW ibv = {};
  if (m_pIndexBufferGPU) {
    ibv.BufferLocation = m_pIndexBufferGPU->GetGPUVirtualAddress();
    ibv.SizeInBytes = m_uIndexBufferByteSize;
    ibv.Format = m_eIndexFormat;
  }
  return ibv;
}

DirectX::BoundingBox MeshBuffer::GetBoundingBox() const {
  return m_aAABB;
}