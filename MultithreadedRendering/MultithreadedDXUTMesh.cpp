#include <DirectXMath.h>
#include <d3dUtils.h>
#include "MultithreadedDXUTMesh.h"

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CMultithreadedDXUTMesh::Create( ResourceUploadBatch* pUploadBatch, LPCWSTR szFileName, 
                                             MT_SDKMESH_CALLBACKS12* pCallbacks )
{
    if ( pCallbacks )
    {
        m_aRenderMeshCallback = pCallbacks->RenderMeshCallback;
    }
    else
    {
        m_aRenderMeshCallback = {nullptr, nullptr};
    }

    return CDXUTSDKMesh::Create( pUploadBatch, szFileName, pCallbacks );
}


//--------------------------------------------------------------------------------------
// Name: RenderMesh()
// Calls the RenderMesh of the base class.  We wrap this API because the base class
// version is protected and we need it to be public.
//--------------------------------------------------------------------------------------
_Use_decl_annotations_ void CMultithreadedDXUTMesh::RenderMesh(UINT iMesh,
                                                               bool bAdjacent,
                                                               ID3D12GraphicsCommandList *pd3dCommandList,
                                                               D3D12_GPU_DESCRIPTOR_HANDLE hDescriptorStart,
                                                               UINT iDiffuseSlot,
                                                               UINT iNormalSlot,
                                                               UINT iSpecularSlot) {
  CDXUTSDKMesh::RenderMesh(iMesh, bAdjacent, pd3dCommandList, hDescriptorStart, iDiffuseSlot, iNormalSlot,
                           iSpecularSlot);
}

//--------------------------------------------------------------------------------------
_Use_decl_annotations_ void CMultithreadedDXUTMesh::RenderFrame(UINT iFrame,
                                                                bool bAdjacent,
                                                                ID3D12GraphicsCommandList *pd3dCommandList,
                                                                D3D12_GPU_DESCRIPTOR_HANDLE hDescriptorStart,
                                                                UINT iDiffuseSlot,
                                                                UINT iNormalSlot,
                                                                UINT iSpecularSlot) {
  if (!m_pStaticMeshData || !m_pFrameArray)
    return;

  if (m_pFrameArray[iFrame].Mesh != INVALID_MESH) {
    if (!m_aRenderMeshCallback.pRenderMesh) {
      RenderMesh(m_pFrameArray[iFrame].Mesh, bAdjacent, pd3dCommandList, hDescriptorStart, iDiffuseSlot, iNormalSlot,
                 iSpecularSlot);
    } else {
      m_aRenderMeshCallback.pRenderMesh(this, m_pFrameArray[iFrame].Mesh, bAdjacent, pd3dCommandList, hDescriptorStart,
                                        iDiffuseSlot, iNormalSlot, iSpecularSlot,
                                        m_aRenderMeshCallback.pRenderUserContext);
    }
  }

  // Render our children
  if (m_pFrameArray[iFrame].ChildFrame != INVALID_FRAME)
    RenderFrame(m_pFrameArray[iFrame].ChildFrame, bAdjacent, pd3dCommandList, hDescriptorStart, iDiffuseSlot,
                iNormalSlot, iSpecularSlot);

  // Render our siblings
  if (m_pFrameArray[iFrame].SiblingFrame != INVALID_FRAME)
    RenderFrame(m_pFrameArray[iFrame].SiblingFrame, bAdjacent, pd3dCommandList, hDescriptorStart, iDiffuseSlot,
                iNormalSlot, iSpecularSlot);
}

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void CMultithreadedDXUTMesh::Render( ID3D12GraphicsCommandList* pd3dCommandList,
                                     D3D12_GPU_DESCRIPTOR_HANDLE hDescriptorStart,
                                     UINT iDiffuseSlot,
                                     UINT iNormalSlot,
                                     UINT iSpecularSlot )
{
    RenderFrame( 0, false, pd3dCommandList, hDescriptorStart, iDiffuseSlot, iNormalSlot, iSpecularSlot );
}
