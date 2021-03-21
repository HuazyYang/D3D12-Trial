#include "RenderableItem.h"
#include "MeshBuffer.h"

using namespace d3dUtils;

HRESULT CreateRenderableItem(RenderableItem **ppRenderable) {
  HRESULT hr = S_OK;

  if (ppRenderable) {
    *ppRenderable = new RenderableItem;
  }

  return hr;
}

RenderableItem::RenderableItem() {
  m_pMeshBuffer = nullptr;
  m_bMappInstanceBufferToLastInputSlot = TRUE;
  m_ePrimitiveType = D3D_PRIMITIVE_TOPOLOGY_UNDEFINED;
  m_uIndexCountPerInstance = 0;
  m_uInstanceCount = 0;
  m_uStartIndexLocation = 0;
  m_uBaseVertexLocation = 0;
  m_uStartInstanceLocation = 0;
}

RenderableItem::~RenderableItem() {
  SAFE_RELEASE(m_pMeshBuffer);
  ClearShaderBindingTable();
}

VOID RenderableItem::SetMeshBuffer(_In_ MeshBuffer *pMeshBuffer) {
  SAFE_RELEASE(m_pMeshBuffer);
  m_pMeshBuffer = m_pMeshBuffer;
  SAFE_ADDREF(m_pMeshBuffer);
}

void RenderableItem::SetWorldMatrix(_In_ const DirectX::XMFLOAT4X4 &matWorld) {
  m_matWorld = matWorld;
}

void RenderableItem::SetWorldMatrix(_In_ DirectX::CXMMATRIX matWorld) {
  XMStoreFloat4x4(&m_matWorld, matWorld);
}

DirectX::XMMATRIX RenderableItem::GetWorldMatrix() const {
  DirectX::XMMATRIX W = XMLoadFloat4x4(&m_matWorld);
  return W;
}

VOID RenderableItem::SetShaderBindingTable(
  _In_ const SHADER_BINDING_TABLE *pSBT
) {
  UINT i;
  const SHADER_BINDING_ENTRY *pEntry;

  ClearShaderBindingTable();

  if (pSBT) {
    m_aSBT.resize(pSBT->NumBindingEntry);
    i = 0;
    pEntry = pSBT->pBindingEntries;
    for (auto it = m_aSBT.begin(); it != m_aSBT.end(); ++it, ++pEntry) {
      *it = *pEntry;
      it->RootConstants.p32BitValues = nullptr;
      if (pEntry->Type == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS &&
        pEntry->RootConstants.Num32BitValues > 0) {
        it->RootConstants.p32BitValues = new UINT32[pEntry->RootConstants.Num32BitValues];
        memcpy((void *)it->RootConstants.p32BitValues, pEntry->RootConstants.p32BitValues,
          sizeof(UINT32)*pEntry->RootConstants.Num32BitValues);
      }
    }
  }
}

VOID RenderableItem::ClearShaderBindingTable() {

  for (auto it = m_aSBT.begin(); it != m_aSBT.end(); ++it) {
    if (it->Type == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS && it->RootConstants.p32BitValues) {
      delete[] const_cast<UINT32*>(reinterpret_cast<const UINT32 *>(it->RootConstants.p32BitValues));
      it->RootConstants.p32BitValues = nullptr;
    }
  }
  m_aSBT.clear();
}

HRESULT RenderableItem::CreateInstanceUploadBuffer(
  _In_ ID3D12Device *pd3dDevice,
  _In_ UINT uInstanceCount,
  _In_ UINT uInstanceByteStride,
  _In_ BOOL bConstant,
  _In_opt_ const void *pInitData
) {
  HRESULT hr;

  V_RETURN(m_aInstanceBuffer.CreateBuffer(
    pd3dDevice,
    uInstanceCount,
    uInstanceByteStride,
    bConstant
  ));

  UINT i;
  const char *pbInitData = reinterpret_cast<const char *>(pInitData);

  for (i = 0; i < uInstanceCount; ++i) {
    m_aInstanceBuffer.CopyData(pbInitData += uInstanceByteStride, uInstanceByteStride, i);
  }

  return hr;
}

HRESULT RenderableItem::CopyInstanceData(
  _In_ const void *pData,
  _In_ UINT cbBuffer,
  _In_ UINT uIndex
) {
  HRESULT hr = S_OK;
  m_aInstanceBuffer.CopyData(pData, cbBuffer, uIndex);
  return hr;
}

VOID RenderableItem::SetDrawIndexedInstanced(
  _In_ D3D12_PRIMITIVE_TOPOLOGY ePrimiveType,
  _In_ UINT IndexCountPerInstance,
  _In_ UINT InstanceCount,
  _In_ UINT StartIndexLocation,
  _In_ INT  BaseVertexLocation,
  _In_ UINT StartInstanceLocation,
  _In_ BOOL bMappInstanceBufferToLastInputSlot
) {
  m_ePrimitiveType = ePrimiveType;
  m_uIndexCountPerInstance = IndexCountPerInstance;
  m_uInstanceCount = InstanceCount;
  m_uStartIndexLocation = StartIndexLocation;
  m_uBaseVertexLocation = BaseVertexLocation;
  m_uStartInstanceLocation = StartInstanceLocation;
  m_bMappInstanceBufferToLastInputSlot = bMappInstanceBufferToLastInputSlot;
}

VOID RenderableItem::EnqueueDrawCommand(
  ID3D12GraphicsCommandList *pCommandList
) {
  /// Sanity check.
  _ASSERT(pCommandList && m_pMeshBuffer);
  _ASSERT(!m_uInstanceCount || m_aInstanceBuffer.IsValid());

  if (pCommandList && m_pMeshBuffer) {

    for (auto itSBE = m_aSBT.begin(); itSBE != m_aSBT.end(); ++itSBE) {
      switch (itSBE->Type) {
      case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
        pCommandList->SetGraphicsRoot32BitConstants(itSBE->RootParameterIndex, itSBE->RootConstants.Num32BitValues,
          itSBE->RootConstants.p32BitValues, itSBE->RootConstants.Dest32BitOffset);
        break;
      case D3D12_ROOT_PARAMETER_TYPE_CBV:
        pCommandList->SetGraphicsRootConstantBufferView(itSBE->RootParameterIndex,
          itSBE->RootDescriptor.Address);
        break;
      case D3D12_ROOT_PARAMETER_TYPE_SRV:
        pCommandList->SetGraphicsRootShaderResourceView(itSBE->RootParameterIndex,
          itSBE->RootDescriptor.Address);
        break;
      case D3D12_ROOT_PARAMETER_TYPE_UAV:
        pCommandList->SetGraphicsRootUnorderedAccessView(itSBE->RootParameterIndex,
          itSBE->RootDescriptor.Address);
        break;
      case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
        pCommandList->SetGraphicsRootDescriptorTable(itSBE->RootParameterIndex,
          itSBE->DescriptorTableEntry.DescriptorHandle);
        break;
      }
    }

    D3D12_VERTEX_BUFFER_VIEW vbvs[2];
    UINT vbvCount = 1;

    vbvs[0] = m_pMeshBuffer->VertexBufferView();
    if (m_uInstanceCount > 0 && m_bMappInstanceBufferToLastInputSlot && m_aInstanceBuffer.IsValid()) {
      vbvs[1].BufferLocation = m_aInstanceBuffer.GetConstBufferAddress();
      vbvs[1].SizeInBytes = m_aInstanceBuffer.GetBufferSize();
      vbvs[1].StrideInBytes = m_aInstanceBuffer.GetByteStride();
      vbvCount = 2;
    }

    pCommandList->IASetPrimitiveTopology(m_ePrimitiveType);
    pCommandList->IASetVertexBuffers(0, vbvCount, vbvs);
    pCommandList->IASetIndexBuffer(&m_pMeshBuffer->IndexBufferView());

    pCommandList->DrawIndexedInstanced(
      m_uIndexCountPerInstance,
      m_uInstanceCount,
      m_uStartIndexLocation,
      m_uBaseVertexLocation,
      m_uStartInstanceLocation
    );
  }
}