#pragma once
#include "d3dUtils.h"
#include <DirectXMath.h>
#include "UploadBuffer.h"
#include <vector>

class MeshBuffer;
class RenderableItem;

/// Renderable Item creator.
HRESULT CreateRenderableItem(RenderableItem **ppRenderableItem);

///
/// Abstract renderable item.
///
class RenderableItem: public Unknown12
{
public:
  void SetMeshBuffer(_In_ MeshBuffer *pMeshBuffer);
  void SetWorldMatrix(_In_ const DirectX::XMFLOAT4X4 &matWorld);
  void SetWorldMatrix(_In_ DirectX::CXMMATRIX matWorld);

  VOID SetShaderBindingTable(
    _In_ const d3dUtils::SHADER_BINDING_TABLE *pSBT
  );

  VOID SetDrawIndexedInstanced(
    _In_ D3D12_PRIMITIVE_TOPOLOGY ePrimiveType,
    _In_ UINT IndexCountPerInstance,
    _In_ UINT InstanceCount,
    _In_ UINT StartIndexLocation,
    _In_ INT  BaseVertexLocation,
    _In_ UINT StartInstanceLocation,
    _In_ BOOL bMappInstanceBufferToLastInputSlot = TRUE
  );

  DirectX::XMMATRIX GetWorldMatrix() const;

  /// @param bConstant Constant buffer will be aligned on the 256 boundary per instance data element.
  HRESULT CreateInstanceUploadBuffer(
    _In_ ID3D12Device *pd3dDevice,
    _In_ UINT uInstanceCount,
    _In_ UINT uInstanceByteStride,
    _In_ BOOL bConstant,
    _In_opt_ const void *pInitData
  );

  HRESULT CopyInstanceData(
    _In_ const void *pData,
    _In_ UINT cbBuffer,
    _In_ UINT uIndex
  );

  VOID EnqueueDrawCommand(
    _In_ ID3D12GraphicsCommandList *pCommandList
  );

protected:
  friend HRESULT CreateRenderableItem(RenderableItem **ppRenderableItem);

  RenderableItem();
  ~RenderableItem();

  void ClearShaderBindingTable();

  DirectX::XMFLOAT4X4 m_matWorld;
  /// Mesh buffer.
  MeshBuffer *m_pMeshBuffer;

  /// Shader binding table.
  std::vector<d3dUtils::SHADER_BINDING_ENTRY> m_aSBT;

  /// Instance buffer is frequently update per frame.
  /// For now, only one instance buffer is supported, multiple instance
  /// buffer need multiple input slot.
  UploadBuffer m_aInstanceBuffer;

  /// Wether mapp the instance buffer as a input slot of input layout.
  /// Default is true.
  BOOL m_bMappInstanceBufferToLastInputSlot;

  /// Primitive type.
  D3D12_PRIMITIVE_TOPOLOGY m_ePrimitiveType;
  /// Index count per-instance for instance drawing calls.
  UINT m_uIndexCountPerInstance;
  /// Number of instances to draw.
  UINT m_uInstanceCount;
  /// The location of the first index read by the GPU from the index buffer.
  UINT m_uStartIndexLocation;
  /// The offset add to each index to read per-vertex data from vertex buffer.
  UINT m_uBaseVertexLocation;
  /// The offset add to each instance index to read per-instance data from Instance Buffer(Vertex Buffer).
  UINT m_uStartInstanceLocation;
};

