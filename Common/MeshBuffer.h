#pragma once
#include "d3dUtils.h"
#include <string>
#include <DirectXMath.h>
#include <DirectXCollision.h>

class MeshBuffer;

extern HRESULT CreateMeshBuffer(MeshBuffer **ppMeshBuffer);

class MeshBuffer : public Unknown12 {
public:
  HRESULT CreateVertexBuffer(
    _In_ ID3D12Device *pd3dDevice,
    _In_ ID3D12GraphicsCommandList *pd3dCommandList,
    _In_ const void *pData,
    _In_ UINT uVertexCount,
    _In_ UINT uVertexStride,
    _In_opt_ DirectX::BoundingBox *pAABB = nullptr
  );

  HRESULT CreateIndexBuffer(
    _In_ ID3D12Device *pd3dDevice,
    _In_ ID3D12GraphicsCommandList *pd3dCommandList,
    _In_ const void *pData,
    _In_ UINT uIndexCount,
    _In_ UINT uIndexStride
  );

  HRESULT UploadVertexBuffer(
    _In_ ID3D12GraphicsCommandList *pd3dCommandList,
    _In_ D3D12_RESOURCE_STATES prevState,
    _In_ D3D12_RESOURCE_STATES currState,
    _In_ const void *pData,
    _In_ UINT uByteSize
  );

  void DisposeUploaders();

  BOOL IsVertexBufferValid() const;
  BOOL IsIndexBufferValid() const;

  VOID GetVertexBufferInfo(
    _Out_opt_ ID3D12Resource **ppVertexBuffer,
    _Out_opt_ UINT *vertexCount,
    _Out_opt_ UINT *vertexStride
  ) const;

  VOID GetIndexBufferInfo(
    _Out_opt_ ID3D12Resource **ppIndexBuffer,
    _Out_opt_ UINT *indexCount,
    _Out_opt_ UINT *indexStride,
    _Out_opt_ DXGI_FORMAT *indexFormat
  ) const;

  /// Vertex buffer view.
  D3D12_VERTEX_BUFFER_VIEW VertexBufferView() const;
  /// Index buffer view.
  D3D12_INDEX_BUFFER_VIEW IndexBufferView() const;

  /// Retrive AABB.
  DirectX::BoundingBox GetBoundingBox() const;

protected:

  friend HRESULT CreateMeshBuffer(MeshBuffer **);

  MeshBuffer();
  ~MeshBuffer();

  ID3D12Resource *m_pVertexBufferUploader;
  ID3D12Resource *m_pIndexBufferUploader;
  ID3D12Resource *m_pVertexBufferGPU;
  ID3D12Resource *m_pIndexBufferGPU;

  UINT m_uVertexCount;
  UINT m_uVertexBufferByteSize;
  UINT m_uVertexByteStride;

  UINT m_uIndexCount;
  UINT m_uIndexBufferByteSize;
  UINT m_uIndexByteStride;
  DXGI_FORMAT m_eIndexFormat;

  DirectX::BoundingBox m_aAABB;
};


/// Inline implementation
inline BOOL MeshBuffer::IsVertexBufferValid() const {
  return !!m_pVertexBufferGPU;
}

inline BOOL MeshBuffer::IsIndexBufferValid() const {
  return !!m_pIndexBufferGPU;
}

inline VOID MeshBuffer::GetVertexBufferInfo(
  _Out_opt_ ID3D12Resource **ppVertexBuffer,
  _Out_opt_ UINT *vertexCount,
  _Out_opt_ UINT *vertexStride
) const {
  ppVertexBuffer ? *ppVertexBuffer = m_pVertexBufferGPU : nullptr;
  SAFE_ADDREF(*ppVertexBuffer);
  vertexCount ? *vertexCount = m_uVertexCount : 0;
  vertexStride ? *vertexStride = m_uVertexByteStride : 0;
}

inline VOID MeshBuffer::GetIndexBufferInfo(
  _Out_opt_ ID3D12Resource **ppIndexBuffer,
  _Out_opt_ UINT *indexCount,
  _Out_opt_ UINT *indexStride,
  _Out_opt_ DXGI_FORMAT *indexFormat
) const {
  ppIndexBuffer ? *ppIndexBuffer = m_pIndexBufferGPU : nullptr;
  SAFE_ADDREF(*ppIndexBuffer);
  indexCount ? *indexCount = m_uIndexCount : 0;
  indexStride ? *indexStride = m_uIndexByteStride : 0;
  indexFormat ? *indexFormat = m_eIndexFormat : DXGI_FORMAT_UNKNOWN;
}

