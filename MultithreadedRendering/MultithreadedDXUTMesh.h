#pragma once

#include <SDKMesh.h>

class CMultithreadedDXUTMesh;
typedef void (*LPRENDERMESH12)(CMultithreadedDXUTMesh *pMesh,
                               UINT iMesh,
                               bool bAdjacent,
                               ID3D12GraphicsCommandList *pd3dCommandList,
                               D3D12_GPU_DESCRIPTOR_HANDLE hDescriptorStart,
                               UINT iDiffuseSlot,
                               UINT iNormalSlot,
                               UINT iSpecularSlot,
                               void *pUserContext);

struct MT_SDKMESH_RENDER_CALLBACK12 {
    LPRENDERMESH12 pRenderMesh;
    void *pRenderUserContext;
};

struct MT_SDKMESH_CALLBACKS12 : public SDKMESH_CALLBACKS12
{
    MT_SDKMESH_RENDER_CALLBACK12 RenderMeshCallback;
};

// Class to override CDXUTSDKMesh in order to allow farming out different Draw
// calls to different DeviceContexts.  Instead of calling RenderMesh directly,
// this class passes the call through a user-supplied callback.
//
// Note it is crucial for the multithreading sample that this class not use
// pd3dDeviceContext in the implementation, other than to pass it through to 
// the callback and to DXUT.  Any other use will not be reflected in the auxiliary
// device contexts used by the sample.
class CMultithreadedDXUTMesh : public CDXUTSDKMesh
{

public:
    virtual HRESULT Create( _In_ ResourceUploadBatch* pUploadBatch,
                            _In_z_ LPCWSTR szFileName,
                            _In_opt_ MT_SDKMESH_CALLBACKS12* pLoaderCallbacks = nullptr) ;

    virtual void Render( _In_ ID3D12GraphicsCommandList* pd3dCommandList,
                         _In_ D3D12_GPU_DESCRIPTOR_HANDLE hDescriptorStart,
                         _In_ UINT iDiffuseSlot = INVALID_SAMPLER_SLOT,
                         _In_ UINT iNormalSlot = INVALID_SAMPLER_SLOT,
                         _In_ UINT iSpecularSlot = INVALID_SAMPLER_SLOT ) override;

    void RenderMesh( _In_ UINT iMesh,
                     _In_ bool bAdjacent,
                     _In_ ID3D12GraphicsCommandList* pd3dCommandList,
                     _In_ D3D12_GPU_DESCRIPTOR_HANDLE hDescriptorStart,
                     _In_ UINT iDiffuseSlot,
                     _In_ UINT iNormalSlot,
                     _In_ UINT iSpecularSlot );
    void RenderFrame( _In_ UINT iFrame,
                      _In_ bool bAdjacent,
                      _In_ ID3D12GraphicsCommandList* pd3dCommandList,
                      _In_ D3D12_GPU_DESCRIPTOR_HANDLE hDescriptorStart,
                      _In_ UINT iDiffuseSlot,
                      _In_ UINT iNormalSlot,
                      _In_ UINT iSpecularSlot );

protected:
  MT_SDKMESH_RENDER_CALLBACK12 m_aRenderMeshCallback;

private:
  using CDXUTSDKMesh::Create;
};