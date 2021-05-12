//--------------------------------------------------------------------------------------
// File: SDKMesh.cpp
//
// The SDK Mesh format (.sdkmesh) is not a recommended file format for games.  
// It was designed to meet the specific needs of the SDK samples.  Any real-world 
// applications should avoid this file format in favor of a destination format that 
// meets the specific needs of the application.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkId=320437
//--------------------------------------------------------------------------------------
#include <d3dUtils.h>
#include <DirectXMath.h>
#include "SDKmesh.h"
#include <ResourceUploadBatch.hpp>
#include <Texture.h>

using namespace DirectX;

HRESULT CDXUTSDKMesh::CreateTextureFromFile(_In_ ResourceUploadBatch *pUploadBatch, _In_z_ LPCSTR pSrcFile,
                                   _Outptr_ ID3D12Resource** ppOutputRV, _In_ bool bSRGB, INT *pAllocHeapIndex) {
  WCHAR szSrcFile[MAX_PATH];
  MultiByteToWideChar(CP_ACP, 0, pSrcFile, -1, szSrcFile, MAX_PATH);
  szSrcFile[MAX_PATH - 1] = 0;

  return CreateTextureFromFile(pUploadBatch, szSrcFile, ppOutputRV, bSRGB, pAllocHeapIndex);
}

HRESULT CDXUTSDKMesh::CreateTextureFromFile(_In_ ResourceUploadBatch *pUploadBatch, _In_z_ LPCWSTR pSrcFile,
                                   _Outptr_ ID3D12Resource** ppOutputRV, _In_ bool bSRGB, INT *pAllocHeapIndex) {

  if (!ppOutputRV)
    return E_INVALIDARG;

  *ppOutputRV = nullptr;

  for (auto it = m_TextureCache.cbegin(); it != m_TextureCache.cend(); ++it) {
    if (!wcscmp(it->wszSource, pSrcFile) && it->bSRGB == bSRGB && it->pSRV12) {
      it->pSRV12->AddRef();
      *ppOutputRV = it->pSRV12;
      if(pAllocHeapIndex) *pAllocHeapIndex = it->uDescriptorHeapIndex;
      return S_OK;
    }
  }

  WCHAR ext[_MAX_EXT];
  _wsplitpath_s(pSrcFile, nullptr, 0, nullptr, 0, nullptr, 0, ext, _MAX_EXT);

  HRESULT hr;
  TexMetadata texMetaData;
  ScratchImage scratchImage;
  std::vector<D3D12_SUBRESOURCE_DATA> subres;

  if (_wcsicmp(ext, L".dds") == 0) {

        hr = (DirectX::LoadFromDDSFile(pSrcFile, DDS_FLAGS_NONE, &texMetaData, scratchImage));
        if(SUCCEEDED(hr)) {

          V_RETURN(DirectX::PrepareUpload(pUploadBatch->GetDevice(), scratchImage.GetImages(),
                                          scratchImage.GetImageCount(), texMetaData, subres));

          if (bSRGB) {
            texMetaData.format = MakeSRGB(texMetaData.format);
          }
          V_RETURN(DirectX::CreateTexture(pUploadBatch->GetDevice(), texMetaData, ppOutputRV));

          V_RETURN(pUploadBatch->Enqueue(*ppOutputRV, 0, (UINT)subres.size(), subres.data()));
          pUploadBatch->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(*ppOutputRV,
                                                                                 D3D12_RESOURCE_STATE_COPY_DEST,
                                                                                 D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|
                                                                                 D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
        }
    } else {
        hr = (DirectX::LoadFromWICFile(pSrcFile, bSRGB ? WIC_FLAGS_FORCE_SRGB : WIC_FLAGS_NONE, &texMetaData, scratchImage));
        if(SUCCEEDED(hr)) {
            V_RETURN(DirectX::PrepareUpload(pUploadBatch->GetDevice(), scratchImage.GetImages(), scratchImage.GetImageCount(),
                                            texMetaData, subres));
            V_RETURN(DirectX::CreateTexture(pUploadBatch->GetDevice(), texMetaData,
                                            ppOutputRV));
            
            V_RETURN(pUploadBatch->Enqueue(*ppOutputRV, 0, (UINT)subres.size(), subres.data()));
            pUploadBatch->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(*ppOutputRV, D3D12_RESOURCE_STATE_COPY_DEST, 
                                                                                  D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE|
                                                                                  D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));
        }
    }

  if (FAILED(hr))
    return hr;

  SDKMESH_TEXTURE_CACHE_ENTRY entry;
  wcscpy_s(entry.wszSource, MAX_PATH, pSrcFile);
  entry.bSRGB = bSRGB;
  entry.pSRV12 = *ppOutputRV;
  entry.pSRV12->AddRef();
  entry.uDescriptorHeapIndex = (INT)m_TextureCache.size();
  if(pAllocHeapIndex) *pAllocHeapIndex = entry.uDescriptorHeapIndex;
  m_TextureCache.push_back(entry);

  return hr;
}

HRESULT CDXUTSDKMesh::GetResourceDescriptorHeap(_In_ ID3D12Device* pDev12, BOOL bShaderVisible, _Out_ ID3D12DescriptorHeap **ppHeap) const {

    HRESULT hr = S_OK;
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Flags = bShaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = (INT)m_TextureCache.size();
    UINT uCbvSrvUavIncrementSize;

    if(pDev12 == nullptr || ppHeap == nullptr)
        return E_INVALIDARG;

    V_RETURN(pDev12->CreateDescriptorHeap(
        &heapDesc,
        IID_PPV_ARGS(ppHeap)
    ));

    uCbvSrvUavIncrementSize = pDev12->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CD3DX12_CPU_DESCRIPTOR_HANDLE handle0, handle;
    handle0 = (*ppHeap)->GetCPUDescriptorHandleForHeapStart();

    for(auto it = m_TextureCache.begin(); it != m_TextureCache.end(); ++it) {

        handle.InitOffsetted(handle0, it->uDescriptorHeapIndex, uCbvSrvUavIncrementSize);
        pDev12->CreateShaderResourceView(it->pSRV12, nullptr, handle);
    }

    return hr;
}

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void CDXUTSDKMesh::LoadMaterials( ResourceUploadBatch* pUploadBatch, SDKMESH_MATERIAL* pMaterials, UINT numMaterials,
                                  SDKMESH_CALLBACKS12* pLoaderCallbacks )
{
    char strPath[MAX_PATH];

    if( pLoaderCallbacks && pLoaderCallbacks->pCreateTextureFromFile )
    {
        for( UINT m = 0; m < numMaterials; m++ )
        {
            pMaterials[m].pDiffuseTexture12 = nullptr;
            pMaterials[m].pNormalTexture12 = nullptr;
            pMaterials[m].pSpecularTexture12 = nullptr;
            pMaterials[m].DiffuseHeapIndex = -1;
            pMaterials[m].NormalHeapIndex = -1;
            pMaterials[m].SpecularHeapIndex = -1;

            // load textures
            if( pMaterials[m].DiffuseTexture[0] != 0 )
            {
                pLoaderCallbacks->pCreateTextureFromFile( pUploadBatch,
                                                          pMaterials[m].DiffuseTexture, &pMaterials[m].pDiffuseTexture12,
                                                          &pMaterials[m].DiffuseHeapIndex,
                                                          pLoaderCallbacks->pContext );
            }
            if( pMaterials[m].NormalTexture[0] != 0 )
            {
                pLoaderCallbacks->pCreateTextureFromFile( pUploadBatch,
                                                          pMaterials[m].NormalTexture, &pMaterials[m].pNormalTexture12,
                                                          &pMaterials[m].NormalHeapIndex,
                                                          pLoaderCallbacks->pContext );
            }
            if( pMaterials[m].SpecularTexture[0] != 0 )
            {
                pLoaderCallbacks->pCreateTextureFromFile( pUploadBatch,
                                                          pMaterials[m].SpecularTexture, &pMaterials[m].pSpecularTexture12,
                                                          &pMaterials[m].SpecularHeapIndex,
                                                          pLoaderCallbacks->pContext );
            }
        }
    }
    else
    {
        for( UINT m = 0; m < numMaterials; m++ )
        {
            pMaterials[m].pDiffuseTexture12 = nullptr;
            pMaterials[m].pNormalTexture12 = nullptr;
            pMaterials[m].pSpecularTexture12 = nullptr;
            pMaterials[m].DiffuseHeapIndex = -1;
            pMaterials[m].NormalHeapIndex = -1;
            pMaterials[m].SpecularHeapIndex = -1;

            // load textures
            if( pMaterials[m].DiffuseTexture[0] != 0 )
            {
                sprintf_s( strPath, MAX_PATH, "%s%s", m_strPath, pMaterials[m].DiffuseTexture );
                if( FAILED( CreateTextureFromFile( pUploadBatch,
                                                   strPath, &pMaterials[m].pDiffuseTexture12,
                                                   true, &pMaterials[m].DiffuseHeapIndex) ) )
                    pMaterials[m].pDiffuseTexture12 = ( ID3D12Resource* )ERROR_RESOURCE_VALUE;

            }
            if( pMaterials[m].NormalTexture[0] != 0 )
            {
                sprintf_s( strPath, MAX_PATH, "%s%s", m_strPath, pMaterials[m].NormalTexture );
                if( FAILED( CreateTextureFromFile( pUploadBatch,
                                                   strPath,
                                                   &pMaterials[m].pNormalTexture12, false, &pMaterials[m].NormalHeapIndex ) ) )
                    pMaterials[m].pNormalTexture12 = ( ID3D12Resource* )ERROR_RESOURCE_VALUE;
            }
            if( pMaterials[m].SpecularTexture[0] != 0 )
            {
                sprintf_s( strPath, MAX_PATH, "%s%s", m_strPath, pMaterials[m].SpecularTexture );
                if( FAILED( CreateTextureFromFile( pUploadBatch,
                                                    strPath,
                                                    &pMaterials[m].pSpecularTexture12, false, &pMaterials[m].SpecularHeapIndex ) ) )
                    pMaterials[m].pSpecularTexture12 = ( ID3D12Resource* )ERROR_RESOURCE_VALUE;
            }
        }
    }
}

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CDXUTSDKMesh::CreateVertexBuffer( ResourceUploadBatch *pUploadBatch, SDKMESH_VERTEX_BUFFER_HEADER* pHeader,
                                          void* pVertices, SDKMESH_CALLBACKS12* pLoaderCallbacks )
{
    HRESULT hr = S_OK;
    pHeader->DataOffset = 0;
    //Vertex Buffer
    CD3DX12_RESOURCE_DESC vbDesc = CD3DX12_RESOURCE_DESC::Buffer(pHeader->SizeBytes);

    if( pLoaderCallbacks && pLoaderCallbacks->pCreateVertexBuffer )
    {
        pLoaderCallbacks->pCreateVertexBuffer( pUploadBatch, &pHeader->pVB12, vbDesc, pVertices,
                                               pLoaderCallbacks->pContext );
    }
    else
    {
        hr = pUploadBatch->GetDevice()->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &vbDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&pHeader->pVB12)
        );
        if(SUCCEEDED(hr)) {
            DX_SetDebugName(pHeader->pVB12, "CDXUTSDKMesh");

            D3D12_SUBRESOURCE_DATA InitData;
            InitData.pData = pVertices;
            InitData.RowPitch = vbDesc.Width;
            InitData.SlicePitch = vbDesc.Width;

            pUploadBatch->Enqueue(pHeader->pVB12, 0, 1, &InitData);
            pUploadBatch->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                pHeader->pVB12, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
        }
    }

    return hr;
}


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CDXUTSDKMesh::CreateIndexBuffer( ResourceUploadBatch* pUploadBatch, SDKMESH_INDEX_BUFFER_HEADER* pHeader,
                                         void* pIndices, SDKMESH_CALLBACKS12* pLoaderCallbacks )
{
    HRESULT hr = S_OK;
    pHeader->DataOffset = 0;
    //Index Buffer
    CD3DX12_RESOURCE_DESC ibDesc = CD3DX12_RESOURCE_DESC::Buffer(pHeader->SizeBytes);

    if( pLoaderCallbacks && pLoaderCallbacks->pCreateIndexBuffer )
    {
        pLoaderCallbacks->pCreateIndexBuffer( pUploadBatch, &pHeader->pIB12, ibDesc, pIndices,
                                              pLoaderCallbacks->pContext );
    }
    else
    {
        hr = pUploadBatch->GetDevice()->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &ibDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&pHeader->pIB12)
        );
        if(SUCCEEDED(hr)) {
            DX_SetDebugName(pHeader->pIB12, "CDXUTSDKMesh");

            D3D12_SUBRESOURCE_DATA InitData;
            InitData.pData = pIndices;
            InitData.RowPitch = ibDesc.Width;
            InitData.SlicePitch = ibDesc.Width;

            pUploadBatch->Enqueue(pHeader->pIB12, 0, 1, &InitData);
            pUploadBatch->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                pHeader->pIB12, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_INDEX_BUFFER));
        }
    }

    return hr;
}


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CDXUTSDKMesh::CreateFromFile( ResourceUploadBatch* pUploadBatch,
                                      LPCWSTR szFileName,
                                      SDKMESH_CALLBACKS12* pLoaderCallbacks12 )
{
    HRESULT hr = S_OK;

    // Find the path for the file
    V_RETURN( FindDemoMediaFileAbsPath( szFileName, std::size(m_strPathW), m_strPathW) == 0 ? S_OK : E_FAIL );

    // Open the file
    m_hFile = CreateFile( m_strPathW, FILE_READ_DATA, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN,
                          nullptr );
    if( INVALID_HANDLE_VALUE == m_hFile )
        return HRESULT_FROM_WIN32(GetLastError());

    // Change the path to just the directory
    WCHAR* pLastBSlash = wcsrchr( m_strPathW, L'\\' );
    if( pLastBSlash )
        *( pLastBSlash + 1 ) = L'\0';
    else
        *m_strPathW = L'\0';

    WideCharToMultiByte( CP_ACP, 0, m_strPathW, -1, m_strPath, MAX_PATH, nullptr, FALSE );

    // Get the file size
    LARGE_INTEGER FileSize;
    GetFileSizeEx( m_hFile, &FileSize );
    UINT cBytes = FileSize.LowPart;

    // Allocate memory
    m_pStaticMeshData = new (std::nothrow) BYTE[ cBytes ];
    if( !m_pStaticMeshData )
    {
        CloseHandle( m_hFile );
        return E_OUTOFMEMORY;
    }

    // Read in the file
    DWORD dwBytesRead;
    if( !ReadFile( m_hFile, m_pStaticMeshData, cBytes, &dwBytesRead, nullptr ) )
        hr = E_FAIL;

    CloseHandle( m_hFile );

    if( SUCCEEDED( hr ) )
    {
        hr = CreateFromMemory( pUploadBatch,
                               m_pStaticMeshData,
                               cBytes,
                               false,
                               pLoaderCallbacks12 );
        if( FAILED( hr ) )
            delete []m_pStaticMeshData;
    }

    return hr;
}

_Use_decl_annotations_
HRESULT CDXUTSDKMesh::CreateFromMemory( ResourceUploadBatch* pUploadBatch,
                                        BYTE* pData,
                                        size_t DataBytes,
                                        bool bCopyStatic,
                                        SDKMESH_CALLBACKS12* pLoaderCallbacks12 )
{
    XMFLOAT3 lower; 
    XMFLOAT3 upper; 
    
    m_pDev12 = pUploadBatch->GetDevice();

    if ( DataBytes < sizeof(SDKMESH_HEADER) )
        return E_FAIL;

    // Set outstanding resources to zero
    m_NumOutstandingResources = 0;

    if( bCopyStatic )
    {
        auto pHeader = reinterpret_cast<SDKMESH_HEADER*>( pData );

        SIZE_T StaticSize = ( SIZE_T )( pHeader->HeaderSize + pHeader->NonBufferDataSize );
        if ( DataBytes < StaticSize )
            return E_FAIL;

        m_pHeapData = new (std::nothrow) BYTE[ StaticSize ];
        if( !m_pHeapData )
            return E_OUTOFMEMORY;

        m_pStaticMeshData = m_pHeapData;

        memcpy( m_pStaticMeshData, pData, StaticSize );
    }
    else
    {
        m_pHeapData = pData;
        m_pStaticMeshData = pData;
    }

    // Pointer fixup
    m_pMeshHeader = reinterpret_cast<SDKMESH_HEADER*>( m_pStaticMeshData );

    m_pVertexBufferArray = ( SDKMESH_VERTEX_BUFFER_HEADER* )( m_pStaticMeshData +
                                                              m_pMeshHeader->VertexStreamHeadersOffset );
    m_pIndexBufferArray = ( SDKMESH_INDEX_BUFFER_HEADER* )( m_pStaticMeshData +
                                                            m_pMeshHeader->IndexStreamHeadersOffset );
    m_pMeshArray = ( SDKMESH_MESH* )( m_pStaticMeshData + m_pMeshHeader->MeshDataOffset );
    m_pSubsetArray = ( SDKMESH_SUBSET* )( m_pStaticMeshData + m_pMeshHeader->SubsetDataOffset );
    m_pFrameArray = ( SDKMESH_FRAME* )( m_pStaticMeshData + m_pMeshHeader->FrameDataOffset );
    m_pMaterialArray = ( SDKMESH_MATERIAL* )( m_pStaticMeshData + m_pMeshHeader->MaterialDataOffset );

    // Setup subsets
    for( UINT i = 0; i < m_pMeshHeader->NumMeshes; i++ )
    {
        m_pMeshArray[i].pSubsets = ( UINT* )( m_pStaticMeshData + m_pMeshArray[i].SubsetOffset );
        m_pMeshArray[i].pFrameInfluences = ( UINT* )( m_pStaticMeshData + m_pMeshArray[i].FrameInfluenceOffset );
    }

    // error condition
    if( m_pMeshHeader->Version != SDKMESH_FILE_VERSION )
    {
        return E_NOINTERFACE;
    }

    // Setup buffer data pointer
    BYTE* pBufferData = pData + m_pMeshHeader->HeaderSize + m_pMeshHeader->NonBufferDataSize;

    // Get the start of the buffer data
    UINT64 BufferDataStart = m_pMeshHeader->HeaderSize + m_pMeshHeader->NonBufferDataSize;

    // Create VBs
    m_ppVertices = new (std::nothrow) BYTE*[m_pMeshHeader->NumVertexBuffers];
    if ( !m_ppVertices )
    {
        return E_OUTOFMEMORY;
    }
    for( UINT i = 0; i < m_pMeshHeader->NumVertexBuffers; i++ )
    {
        BYTE* pVertices = nullptr;
        pVertices = ( BYTE* )( pBufferData + ( m_pVertexBufferArray[i].DataOffset - BufferDataStart ) );

        if( pUploadBatch )
            CreateVertexBuffer( pUploadBatch, &m_pVertexBufferArray[i], pVertices, pLoaderCallbacks12 );

        m_ppVertices[i] = pVertices;
    }

    // Create IBs
    m_ppIndices = new (std::nothrow) BYTE*[m_pMeshHeader->NumIndexBuffers];
    if ( !m_ppIndices )
    {
        return E_OUTOFMEMORY;
    }

    for( UINT i = 0; i < m_pMeshHeader->NumIndexBuffers; i++ )
    {
        BYTE* pIndices = nullptr;
        pIndices = ( BYTE* )( pBufferData + ( m_pIndexBufferArray[i].DataOffset - BufferDataStart ) );

        if( pUploadBatch )
            CreateIndexBuffer( pUploadBatch, &m_pIndexBufferArray[i], pIndices, pLoaderCallbacks12 );

        m_ppIndices[i] = pIndices;
    }

    // Load Materials
    if( pUploadBatch )
        LoadMaterials( pUploadBatch, m_pMaterialArray, m_pMeshHeader->NumMaterials, pLoaderCallbacks12 );

    // Create a place to store our bind pose frame matrices
    m_pBindPoseFrameMatrices = new (std::nothrow) XMFLOAT4X4[ m_pMeshHeader->NumFrames ];
    if( !m_pBindPoseFrameMatrices )
    {
        return E_OUTOFMEMORY;
    }

    // Create a place to store our transformed frame matrices
    m_pTransformedFrameMatrices = new (std::nothrow) XMFLOAT4X4[ m_pMeshHeader->NumFrames ];
    if( !m_pTransformedFrameMatrices )
    {
        return E_OUTOFMEMORY;
    }

    m_pWorldPoseFrameMatrices = new (std::nothrow) XMFLOAT4X4[ m_pMeshHeader->NumFrames ];
    if( !m_pWorldPoseFrameMatrices )
    {
        return E_OUTOFMEMORY;
    }

    SDKMESH_SUBSET* pSubset = nullptr;
    D3D12_PRIMITIVE_TOPOLOGY PrimType;

    // update bounding volume 
    SDKMESH_MESH* currentMesh = &m_pMeshArray[0];
    int tris = 0;
    for (UINT meshi=0; meshi < m_pMeshHeader->NumMeshes; ++meshi) {
        lower.x = FLT_MAX; lower.y = FLT_MAX; lower.z = FLT_MAX;
        upper.x = -FLT_MAX; upper.y = -FLT_MAX; upper.z = -FLT_MAX;
        currentMesh = GetMesh( meshi );
        INT indsize;
        if (m_pIndexBufferArray[currentMesh->IndexBuffer].IndexType == IT_16BIT ) {
            indsize = 2;
        }else {
            indsize = 4;        
        }

        for( UINT subset = 0; subset < currentMesh->NumSubsets; subset++ )
        {
            pSubset = GetSubset( meshi, subset ); //&m_pSubsetArray[ currentMesh->pSubsets[subset] ];

            PrimType = GetPrimitiveType12( ( SDKMESH_PRIMITIVE_TYPE )pSubset->PrimitiveType );
            assert( PrimType ==  D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);// only triangle lists are handled.

            UINT IndexCount = ( UINT )pSubset->IndexCount;
            UINT IndexStart = ( UINT )pSubset->IndexStart;

            /*if( bAdjacent )
            {
                IndexCount *= 2;
                IndexStart *= 2;
            }*/
     
        //BYTE* pIndices = nullptr;
            //m_ppIndices[i]
            UINT *ind = ( UINT * )m_ppIndices[currentMesh->IndexBuffer];
            float *verts =  ( float* )m_ppVertices[currentMesh->VertexBuffers[0]];
            UINT stride = (UINT)m_pVertexBufferArray[currentMesh->VertexBuffers[0]].StrideBytes;
            assert (stride % 4 == 0);
            stride /=4;
            for (UINT vertind = IndexStart; vertind < IndexStart + IndexCount; ++vertind) {
                UINT current_ind=0;
                if (indsize == 2) {
                    UINT ind_div2 = vertind / 2;
                    current_ind = ind[ind_div2];
                    if (vertind %2 ==0) {
                        current_ind = current_ind << 16;
                        current_ind = current_ind >> 16;
                    }else {
                        current_ind = current_ind >> 16;
                    }
                }else {
                    current_ind = ind[vertind];
                }
                tris++;
                XMFLOAT3 *pt = (XMFLOAT3*)&(verts[stride * current_ind]);
                if (pt->x < lower.x) {
                    lower.x = pt->x;
                }
                if (pt->y < lower.y) {
                    lower.y = pt->y;
                }
                if (pt->z < lower.z) {
                    lower.z = pt->z;
                }
                if (pt->x > upper.x) {
                    upper.x = pt->x;
                }
                if (pt->y > upper.y) {
                    upper.y = pt->y;
                }
                if (pt->z > upper.z) {
                    upper.z = pt->z;
                }
                //BYTE** m_ppVertices;
                //BYTE** m_ppIndices;
            }
            //pd3dDeviceContext->DrawIndexed( IndexCount, IndexStart, VertexStart );
        }

        XMFLOAT3 half( ( upper.x - lower.x ) * 0.5f,
                       ( upper.y - lower.y ) * 0.5f,
                       ( upper.z - lower.z ) * 0.5f );

        currentMesh->BoundingBoxCenter.x = lower.x + half.x;
        currentMesh->BoundingBoxCenter.y = lower.y + half.y;
        currentMesh->BoundingBoxCenter.z = lower.z + half.z;

        currentMesh->BoundingBoxExtents = half;

    }
    // Update 
        
    return S_OK;
}


//--------------------------------------------------------------------------------------
// transform bind pose frame using a recursive traversal
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void CDXUTSDKMesh::TransformBindPoseFrame( UINT iFrame, CXMMATRIX parentWorld )
{
    if( !m_pBindPoseFrameMatrices )
        return;

    // Transform ourselves
    XMMATRIX m = XMLoadFloat4x4( &m_pFrameArray[iFrame].Matrix );
    XMMATRIX mLocalWorld = XMMatrixMultiply( m, parentWorld );
    XMStoreFloat4x4( &m_pBindPoseFrameMatrices[iFrame], mLocalWorld );

    // Transform our siblings
    if( m_pFrameArray[iFrame].SiblingFrame != INVALID_FRAME )
    {
        TransformBindPoseFrame( m_pFrameArray[iFrame].SiblingFrame, parentWorld );
    }

    // Transform our children
    if( m_pFrameArray[iFrame].ChildFrame != INVALID_FRAME )
    {
        TransformBindPoseFrame( m_pFrameArray[iFrame].ChildFrame, mLocalWorld );
    }
}


//--------------------------------------------------------------------------------------
// transform frame using a recursive traversal
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void CDXUTSDKMesh::TransformFrame( UINT iFrame, CXMMATRIX parentWorld, double fTime )
{
    // Get the tick data
    XMMATRIX mLocalTransform;

    UINT iTick = GetAnimationKeyFromTime( fTime );

    if( INVALID_ANIMATION_DATA != m_pFrameArray[iFrame].AnimationDataIndex )
    {
        auto pFrameData = &m_pAnimationFrameData[ m_pFrameArray[iFrame].AnimationDataIndex ];
        auto pData = &pFrameData->pAnimationData[ iTick ];

        // turn it into a matrix (Ignore scaling for now)
        XMFLOAT3 parentPos = pData->Translation;
        XMMATRIX mTranslate = XMMatrixTranslation( parentPos.x, parentPos.y, parentPos.z );

        XMVECTOR quat = XMVectorSet( pData->Orientation.x, pData->Orientation.y, pData->Orientation.z, pData->Orientation.w );
        if ( XMVector4Equal( quat, g_XMZero ) )
            quat = XMQuaternionIdentity();
        quat = XMQuaternionNormalize( quat );
        XMMATRIX mQuat = XMMatrixRotationQuaternion( quat );
        mLocalTransform = ( mQuat * mTranslate );
    }
    else
    {
        mLocalTransform = XMLoadFloat4x4( &m_pFrameArray[iFrame].Matrix );
    }

    // Transform ourselves
    XMMATRIX mLocalWorld = XMMatrixMultiply( mLocalTransform, parentWorld );
    XMStoreFloat4x4( &m_pTransformedFrameMatrices[iFrame], mLocalWorld );
    XMStoreFloat4x4( &m_pWorldPoseFrameMatrices[iFrame], mLocalWorld );

    // Transform our siblings
    if( m_pFrameArray[iFrame].SiblingFrame != INVALID_FRAME )
    {
        TransformFrame( m_pFrameArray[iFrame].SiblingFrame, parentWorld, fTime );
    }

    // Transform our children
    if( m_pFrameArray[iFrame].ChildFrame != INVALID_FRAME )
    {
        TransformFrame( m_pFrameArray[iFrame].ChildFrame, mLocalWorld, fTime );
    }
}


//--------------------------------------------------------------------------------------
// transform frame assuming that it is an absolute transformation
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void CDXUTSDKMesh::TransformFrameAbsolute( UINT iFrame, double fTime )
{
    UINT iTick = GetAnimationKeyFromTime( fTime );

    if( INVALID_ANIMATION_DATA != m_pFrameArray[iFrame].AnimationDataIndex )
    {
        auto pFrameData = &m_pAnimationFrameData[ m_pFrameArray[iFrame].AnimationDataIndex ];
        auto pData = &pFrameData->pAnimationData[ iTick ];
        auto pDataOrig = &pFrameData->pAnimationData[ 0 ];

        XMMATRIX mTrans1 = XMMatrixTranslation( -pDataOrig->Translation.x, -pDataOrig->Translation.y, -pDataOrig->Translation.z );
        XMMATRIX mTrans2 = XMMatrixTranslation( pData->Translation.x, pData->Translation.y, pData->Translation.z );

        XMVECTOR quat1 = XMVectorSet( pDataOrig->Orientation.x, pDataOrig->Orientation.y, pDataOrig->Orientation.z, pDataOrig->Orientation.w );
        quat1 = XMQuaternionInverse( quat1 );
        XMMATRIX mRot1 = XMMatrixRotationQuaternion( quat1 );
        XMMATRIX mInvTo = mTrans1 * mRot1;

        XMVECTOR quat2 = XMVectorSet( pData->Orientation.x, pData->Orientation.y, pData->Orientation.z, pData->Orientation.w );
        XMMATRIX mRot2 = XMMatrixRotationQuaternion( quat2 );
        XMMATRIX mFrom = mRot2 * mTrans2;

        XMMATRIX mOutput = mInvTo * mFrom;
        XMStoreFloat4x4( &m_pTransformedFrameMatrices[iFrame], mOutput );
    }
}

#define MAX_D3D12_VERTEX_STREAMS D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void CDXUTSDKMesh::RenderMesh( UINT iMesh,
                               bool bAdjacent,
                               ID3D12GraphicsCommandList* pd3dCommandList,
                               D3D12_GPU_DESCRIPTOR_HANDLE hDescriptorStart,
                               UINT iDiffuseSlot,
                               UINT iNormalSlot,
                               UINT iSpecularSlot )
{
    if( 0 < GetOutstandingBufferResources() )
        return;

    auto pMesh = &m_pMeshArray[iMesh];

    D3D12_VERTEX_BUFFER_VIEW VBV[MAX_D3D12_VERTEX_STREAMS];
    D3D12_INDEX_BUFFER_VIEW IBV;

    if( pMesh->NumVertexBuffers > MAX_D3D12_VERTEX_STREAMS )
        return;

    for( UINT64 i = 0; i < pMesh->NumVertexBuffers; i++ )
    {
        VBV[i].BufferLocation = m_pVertexBufferArray[ pMesh->VertexBuffers[i] ].pVB12->GetGPUVirtualAddress();
        VBV[i].StrideInBytes = ( UINT )m_pVertexBufferArray[ pMesh->VertexBuffers[i] ].StrideBytes;
        VBV[i].SizeInBytes = (UINT)m_pVertexBufferArray[ pMesh->VertexBuffers[i] ].SizeBytes;
    }

    SDKMESH_INDEX_BUFFER_HEADER* pIndexBufferArray;
    if( bAdjacent )
        pIndexBufferArray = m_pAdjacencyIndexBufferArray;
    else
        pIndexBufferArray = m_pIndexBufferArray;

    auto pIB = pIndexBufferArray[ pMesh->IndexBuffer ].pIB12;
    DXGI_FORMAT ibFormat = DXGI_FORMAT_R16_UINT;
    switch( pIndexBufferArray[ pMesh->IndexBuffer ].IndexType )
    {
    case IT_16BIT:
        ibFormat = DXGI_FORMAT_R16_UINT;
        break;
    case IT_32BIT:
        ibFormat = DXGI_FORMAT_R32_UINT;
        break;
    };
    IBV.BufferLocation = pIB->GetGPUVirtualAddress();
    IBV.Format = ibFormat;
    IBV.SizeInBytes = (UINT)pIndexBufferArray[ pMesh->IndexBuffer ].SizeBytes;

    pd3dCommandList->IASetVertexBuffers( 0, pMesh->NumVertexBuffers, VBV );
    pd3dCommandList->IASetIndexBuffer( &IBV );

    SDKMESH_SUBSET* pSubset = nullptr;
    SDKMESH_MATERIAL* pMat = nullptr;
    D3D12_PRIMITIVE_TOPOLOGY PrimType;

    ID3D12Device *pd3dDevice;
    UINT uCbvSrvUavIncrementSize;
    CD3DX12_GPU_DESCRIPTOR_HANDLE handle0, handle;

    pd3dCommandList->GetDevice(IID_PPV_ARGS(&pd3dDevice));
    uCbvSrvUavIncrementSize = pd3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    SAFE_RELEASE(pd3dDevice);
    handle0 = hDescriptorStart;

    for( UINT subset = 0; subset < pMesh->NumSubsets; subset++ )
    {
        pSubset = &m_pSubsetArray[ pMesh->pSubsets[subset] ];

        PrimType = GetPrimitiveType12( ( SDKMESH_PRIMITIVE_TYPE )pSubset->PrimitiveType );
        if( bAdjacent )
        {
            switch( PrimType )
            {
            case D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST:
                PrimType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
                break;
            case D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP:
                PrimType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
                break;
            case D3D_PRIMITIVE_TOPOLOGY_LINELIST:
                PrimType = D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
                break;
            case D3D_PRIMITIVE_TOPOLOGY_LINESTRIP:
                PrimType = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;
                break;
            }
        }

        pd3dCommandList->IASetPrimitiveTopology( PrimType );

        pMat = &m_pMaterialArray[ pSubset->MaterialID ];
        if( iDiffuseSlot != INVALID_SAMPLER_SLOT && !IsErrorResource( pMat->pDiffuseTexture12 ) ) {
            handle.InitOffsetted(handle0, pMat->DiffuseHeapIndex, uCbvSrvUavIncrementSize);
            pd3dCommandList->SetGraphicsRootDescriptorTable( iDiffuseSlot,  handle);
        }
        if( iNormalSlot != INVALID_SAMPLER_SLOT && !IsErrorResource( pMat->pNormalTexture12 ) ) {
            handle.InitOffsetted(handle0, pMat->NormalHeapIndex, uCbvSrvUavIncrementSize);
            pd3dCommandList->SetGraphicsRootDescriptorTable( iNormalSlot, handle );
        }
        if( iSpecularSlot != INVALID_SAMPLER_SLOT && !IsErrorResource( pMat->pSpecularTexture12 ) ) {
            handle.InitOffsetted(handle0, pMat->SpecularHeapIndex, uCbvSrvUavIncrementSize);
            pd3dCommandList->SetGraphicsRootDescriptorTable( iSpecularSlot, handle );
        }

        UINT IndexCount = ( UINT )pSubset->IndexCount;
        UINT IndexStart = ( UINT )pSubset->IndexStart;
        UINT VertexStart = ( UINT )pSubset->VertexStart;
        if( bAdjacent )
        {
            IndexCount *= 2;
            IndexStart *= 2;
        }

        pd3dCommandList->DrawIndexedInstanced( IndexCount, 1, IndexStart, VertexStart, 0 );
    }
}

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void CDXUTSDKMesh::RenderFrame( UINT iFrame,
                                bool bAdjacent,
                                ID3D12GraphicsCommandList* pd3dCommandList,
                                D3D12_GPU_DESCRIPTOR_HANDLE hDescriptorStart,
                                UINT iDiffuseSlot,
                                UINT iNormalSlot,
                                UINT iSpecularSlot )
{
    if( !m_pStaticMeshData || !m_pFrameArray )
        return;

    if( m_pFrameArray[iFrame].Mesh != INVALID_MESH )
    {
        RenderMesh( m_pFrameArray[iFrame].Mesh,
                    bAdjacent,
                    pd3dCommandList,
                    hDescriptorStart,
                    iDiffuseSlot,
                    iNormalSlot,
                    iSpecularSlot );
    }

    // Render our children
    if( m_pFrameArray[iFrame].ChildFrame != INVALID_FRAME )
        RenderFrame( m_pFrameArray[iFrame].ChildFrame, bAdjacent, pd3dCommandList, hDescriptorStart, iDiffuseSlot, 
                     iNormalSlot, iSpecularSlot );

    // Render our siblings
    if( m_pFrameArray[iFrame].SiblingFrame != INVALID_FRAME )
        RenderFrame( m_pFrameArray[iFrame].SiblingFrame, bAdjacent, pd3dCommandList, hDescriptorStart, iDiffuseSlot, 
                     iNormalSlot, iSpecularSlot );
}

//--------------------------------------------------------------------------------------
CDXUTSDKMesh::CDXUTSDKMesh() noexcept :
    m_NumOutstandingResources(0),
    m_bLoading(false),
    m_hFile(0),
    m_hFileMappingObject(0),
    m_pDev12(nullptr),
    m_pd3dCommandList(nullptr),
    m_pStaticMeshData(nullptr),
    m_pHeapData(nullptr),
    m_pAnimationData(nullptr),
    m_ppVertices(nullptr),
    m_ppIndices(nullptr),
    m_strPathW{},
    m_strPath{},
    m_pMeshHeader(nullptr),
    m_pVertexBufferArray(nullptr),
    m_pIndexBufferArray(nullptr),
    m_pMeshArray(nullptr),
    m_pSubsetArray(nullptr),
    m_pFrameArray(nullptr),
    m_pMaterialArray(nullptr),
    m_pAdjacencyIndexBufferArray(nullptr),
    m_pAnimationHeader(nullptr),
    m_pAnimationFrameData(nullptr),
    m_pBindPoseFrameMatrices(nullptr),
    m_pTransformedFrameMatrices(nullptr),
    m_pWorldPoseFrameMatrices(nullptr)
{
}


//--------------------------------------------------------------------------------------
CDXUTSDKMesh::~CDXUTSDKMesh()
{
    Destroy();
}

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CDXUTSDKMesh::Create( ResourceUploadBatch* pUploadBatch, LPCWSTR szFileName, SDKMESH_CALLBACKS12* pLoaderCallbacks )
{
    return CreateFromFile( pUploadBatch, szFileName, pLoaderCallbacks );
}

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
HRESULT CDXUTSDKMesh::Create( ResourceUploadBatch* pUploadBatch, BYTE* pData, size_t DataBytes, bool bCopyStatic, SDKMESH_CALLBACKS12* pLoaderCallbacks )
{
    return CreateFromMemory( pUploadBatch, pData, DataBytes, bCopyStatic, pLoaderCallbacks );
}

//--------------------------------------------------------------------------------------
HRESULT CDXUTSDKMesh::LoadAnimation( _In_z_ const WCHAR* szFileName )
{
    HRESULT hr = E_FAIL;
    DWORD dwBytesRead = 0;
    LARGE_INTEGER liMove;
    WCHAR strPath[MAX_PATH];

    // Find the path for the file
    V_RETURN( FindDemoMediaFileAbsPath( szFileName, MAX_PATH, strPath) == 0 ? S_OK : E_FAIL );

    // Open the file
    HANDLE hFile = CreateFile( strPath, FILE_READ_DATA, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_FLAG_SEQUENTIAL_SCAN, nullptr );
    if( INVALID_HANDLE_VALUE == hFile )
        return HRESULT_FROM_WIN32(GetLastError());

    /////////////////////////
    // Header
    SDKANIMATION_FILE_HEADER fileheader;
    if( !ReadFile( hFile, &fileheader, sizeof( SDKANIMATION_FILE_HEADER ), &dwBytesRead, nullptr ) )
    {
        CloseHandle(hFile);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    //allocate
    m_pAnimationData = new (std::nothrow) BYTE[ ( size_t )( sizeof( SDKANIMATION_FILE_HEADER ) + fileheader.AnimationDataSize ) ];
    if( !m_pAnimationData )
    {
        CloseHandle(hFile);
        return E_OUTOFMEMORY;
    }

    // read it all in
    liMove.QuadPart = 0;
    if( !SetFilePointerEx( hFile, liMove, nullptr, FILE_BEGIN ) )
    {
        CloseHandle(hFile);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if( !ReadFile( hFile, m_pAnimationData, ( DWORD )( sizeof( SDKANIMATION_FILE_HEADER ) +
                                                       fileheader.AnimationDataSize ), &dwBytesRead, nullptr ) )
    {
        CloseHandle(hFile);
        return HRESULT_FROM_WIN32(GetLastError());
    }

    // pointer fixup
    m_pAnimationHeader = ( SDKANIMATION_FILE_HEADER* )m_pAnimationData;
    m_pAnimationFrameData = ( SDKANIMATION_FRAME_DATA* )( m_pAnimationData + m_pAnimationHeader->AnimationDataOffset );

    UINT64 BaseOffset = sizeof( SDKANIMATION_FILE_HEADER );
    for( UINT i = 0; i < m_pAnimationHeader->NumFrames; i++ )
    {
        m_pAnimationFrameData[i].pAnimationData = ( SDKANIMATION_DATA* )( m_pAnimationData +
                                                                          m_pAnimationFrameData[i].DataOffset +
                                                                          BaseOffset );
        auto pFrame = FindFrame( m_pAnimationFrameData[i].FrameName );
        if( pFrame )
        {
            pFrame->AnimationDataIndex = i;
        }
    }

    return S_OK;
}

//--------------------------------------------------------------------------------------
void CDXUTSDKMesh::Destroy()
{

    for(auto it = m_TextureCache.begin(); it != m_TextureCache.end(); ++it) {
        SAFE_RELEASE(it->pSRV12);
    }

    if( !CheckLoadDone() )
        return;

    if( m_pStaticMeshData )
    {
        if( m_pMaterialArray )
        {
            for( UINT64 m = 0; m < m_pMeshHeader->NumMaterials; m++ )
            {
                if( m_pDev12 )
                {
                    if( m_pMaterialArray[m].pDiffuseTexture12 && !IsErrorResource( m_pMaterialArray[m].pDiffuseTexture12 ) )
                    {
                        //m_pMaterialArray[m].pDiffuseRV12->GetResource( &pRes );
                        //SAFE_RELEASE( pRes );

                        SAFE_RELEASE( m_pMaterialArray[m].pDiffuseTexture12 );
                    }
                    if( m_pMaterialArray[m].pNormalTexture12 && !IsErrorResource( m_pMaterialArray[m].pNormalTexture12 ) )
                    {
                        //m_pMaterialArray[m].pNormalRV12->GetResource( &pRes );
                        //SAFE_RELEASE( pRes );

                        SAFE_RELEASE( m_pMaterialArray[m].pNormalTexture12 );
                    }
                    if( m_pMaterialArray[m].pSpecularTexture12 && !IsErrorResource( m_pMaterialArray[m].pSpecularTexture12 ) )
                    {
                        //m_pMaterialArray[m].pSpecularRV12->GetResource( &pRes );
                        //SAFE_RELEASE( pRes );

                        SAFE_RELEASE( m_pMaterialArray[m].pSpecularTexture12 );
                    } 
                }
            }
        }
        for( UINT64 i = 0; i < m_pMeshHeader->NumVertexBuffers; i++ )
        {
            SAFE_RELEASE( m_pVertexBufferArray[i].pVB12 );
        }

        for( UINT64 i = 0; i < m_pMeshHeader->NumIndexBuffers; i++ )
        {
            SAFE_RELEASE( m_pIndexBufferArray[i].pIB12 );
        }
    }

    if( m_pAdjacencyIndexBufferArray )
    {
        for( UINT64 i = 0; i < m_pMeshHeader->NumIndexBuffers; i++ )
        {
            SAFE_RELEASE( m_pAdjacencyIndexBufferArray[i].pIB12 );
        }
    }
    SAFE_DELETE_ARRAY( m_pAdjacencyIndexBufferArray );

    SAFE_DELETE_ARRAY( m_pHeapData );
    m_pStaticMeshData = nullptr;
    SAFE_DELETE_ARRAY( m_pAnimationData );
    SAFE_DELETE_ARRAY( m_pBindPoseFrameMatrices );
    SAFE_DELETE_ARRAY( m_pTransformedFrameMatrices );
    SAFE_DELETE_ARRAY( m_pWorldPoseFrameMatrices );

    SAFE_DELETE_ARRAY( m_ppVertices );
    SAFE_DELETE_ARRAY( m_ppIndices );

    m_pMeshHeader = nullptr;
    m_pVertexBufferArray = nullptr;
    m_pIndexBufferArray = nullptr;
    m_pMeshArray = nullptr;
    m_pSubsetArray = nullptr;
    m_pFrameArray = nullptr;
    m_pMaterialArray = nullptr;

    m_pAnimationHeader = nullptr;
    m_pAnimationFrameData = nullptr;

}


//--------------------------------------------------------------------------------------
// transform the mesh frames according to the animation for time fTime
//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void CDXUTSDKMesh::TransformMesh( CXMMATRIX world, double fTime )
{
    if( !m_pAnimationHeader || FTT_RELATIVE == m_pAnimationHeader->FrameTransformType )
    {
        TransformFrame( 0, world, fTime );

        // For each frame, move the transform to the bind pose, then
        // move it to the final position
        for( UINT i = 0; i < m_pMeshHeader->NumFrames; i++ )
        {
            XMMATRIX m = XMLoadFloat4x4( &m_pBindPoseFrameMatrices[i] );
            XMMATRIX mInvBindPose = XMMatrixInverse( nullptr, m );
            m = XMLoadFloat4x4( &m_pTransformedFrameMatrices[i] );
            XMMATRIX mFinal = mInvBindPose * m;
            XMStoreFloat4x4( &m_pTransformedFrameMatrices[i], mFinal );
        }
    }
    else if( FTT_ABSOLUTE == m_pAnimationHeader->FrameTransformType )
    {
        for( UINT i = 0; i < m_pAnimationHeader->NumFrames; i++ )
            TransformFrameAbsolute( i, fTime );
    }
}


//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void CDXUTSDKMesh::Render( ID3D12GraphicsCommandList* pd3dCommandList,
                           D3D12_GPU_DESCRIPTOR_HANDLE hDescriptorStart,
                           UINT iDiffuseSlot,
                           UINT iNormalSlot,
                           UINT iSpecularSlot )
{
    RenderFrame( 0, false, pd3dCommandList, hDescriptorStart, iDiffuseSlot, iNormalSlot, iSpecularSlot );
}

//--------------------------------------------------------------------------------------
_Use_decl_annotations_
void CDXUTSDKMesh::RenderAdjacent( ID3D12GraphicsCommandList* pd3dCommandList,
                                   D3D12_GPU_DESCRIPTOR_HANDLE hDescriptorStart,
                                   UINT iDiffuseSlot,
                                   UINT iNormalSlot,
                                   UINT iSpecularSlot )
{
    RenderFrame( 0, true, pd3dCommandList, hDescriptorStart, iDiffuseSlot, iNormalSlot, iSpecularSlot );
}


//--------------------------------------------------------------------------------------
D3D12_PRIMITIVE_TOPOLOGY CDXUTSDKMesh::GetPrimitiveType12( _In_ SDKMESH_PRIMITIVE_TYPE PrimType )
{
    D3D12_PRIMITIVE_TOPOLOGY retType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    switch( PrimType )
    {
        case PT_TRIANGLE_LIST:
            retType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            break;
        case PT_TRIANGLE_STRIP:
            retType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            break;
        case PT_LINE_LIST:
            retType = D3D_PRIMITIVE_TOPOLOGY_LINELIST;
            break;
        case PT_LINE_STRIP:
            retType = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
            break;
        case PT_POINT_LIST:
            retType = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
            break;
        case PT_TRIANGLE_LIST_ADJ:
            retType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST_ADJ;
            break;
        case PT_TRIANGLE_STRIP_ADJ:
            retType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP_ADJ;
            break;
        case PT_LINE_LIST_ADJ:
            retType = D3D_PRIMITIVE_TOPOLOGY_LINELIST_ADJ;
            break;
        case PT_LINE_STRIP_ADJ:
            retType = D3D_PRIMITIVE_TOPOLOGY_LINESTRIP_ADJ;
            break;
    };

    return retType;
}

//--------------------------------------------------------------------------------------
DXGI_FORMAT CDXUTSDKMesh::GetIBFormat12( _In_ UINT iMesh ) const
{
    switch( m_pIndexBufferArray[ m_pMeshArray[ iMesh ].IndexBuffer ].IndexType )
    {
        case IT_16BIT:
            return DXGI_FORMAT_R16_UINT;
        case IT_32BIT:
            return DXGI_FORMAT_R32_UINT;
    };
    return DXGI_FORMAT_R16_UINT;
}

//--------------------------------------------------------------------------------------
ID3D12Resource* CDXUTSDKMesh::GetVB12( _In_ UINT iMesh, _In_ UINT iVB ) const
{
    return m_pVertexBufferArray[ m_pMeshArray[ iMesh ].VertexBuffers[iVB] ].pVB12;
}

//--------------------------------------------------------------------------------------
ID3D12Resource* CDXUTSDKMesh::GetIB12( _In_ UINT iMesh ) const
{
    return m_pIndexBufferArray[ m_pMeshArray[ iMesh ].IndexBuffer ].pIB12;
}
SDKMESH_INDEX_TYPE CDXUTSDKMesh::GetIndexType( _In_ UINT iMesh ) const
{
    return ( SDKMESH_INDEX_TYPE ) m_pIndexBufferArray[m_pMeshArray[ iMesh ].IndexBuffer].IndexType;
}
//--------------------------------------------------------------------------------------
ID3D12Resource* CDXUTSDKMesh::GetAdjIB12( _In_ UINT iMesh ) const
{
    return m_pAdjacencyIndexBufferArray[ m_pMeshArray[ iMesh ].IndexBuffer ].pIB12;
}

//--------------------------------------------------------------------------------------
const char* CDXUTSDKMesh::GetMeshPathA() const
{
    return m_strPath;
}

//--------------------------------------------------------------------------------------
const WCHAR* CDXUTSDKMesh::GetMeshPathW() const
{
    return m_strPathW;
}

//--------------------------------------------------------------------------------------
UINT CDXUTSDKMesh::GetNumMeshes() const
{
    if( !m_pMeshHeader )
        return 0;
    return m_pMeshHeader->NumMeshes;
}

//--------------------------------------------------------------------------------------
UINT CDXUTSDKMesh::GetNumMaterials() const
{
    if( !m_pMeshHeader )
        return 0;
    return m_pMeshHeader->NumMaterials;
}

//--------------------------------------------------------------------------------------
UINT CDXUTSDKMesh::GetNumVBs() const
{
    if( !m_pMeshHeader )
        return 0;
    return m_pMeshHeader->NumVertexBuffers;
}

//--------------------------------------------------------------------------------------
UINT CDXUTSDKMesh::GetNumIBs() const
{
    if( !m_pMeshHeader )
        return 0;
    return m_pMeshHeader->NumIndexBuffers;
}

//--------------------------------------------------------------------------------------
ID3D12Resource* CDXUTSDKMesh::GetVB12At( _In_ UINT iVB ) const
{
    return m_pVertexBufferArray[ iVB ].pVB12;
}

//--------------------------------------------------------------------------------------
ID3D12Resource* CDXUTSDKMesh::GetIB12At( _In_ UINT iIB ) const
{
    return m_pIndexBufferArray[ iIB ].pIB12;
}

//--------------------------------------------------------------------------------------
BYTE* CDXUTSDKMesh::GetRawVerticesAt( _In_ UINT iVB ) const
{
    return m_ppVertices[iVB];
}

//--------------------------------------------------------------------------------------
BYTE* CDXUTSDKMesh::GetRawIndicesAt( _In_ UINT iIB ) const
{
    return m_ppIndices[iIB];
}

//--------------------------------------------------------------------------------------
SDKMESH_MATERIAL* CDXUTSDKMesh::GetMaterial( _In_ UINT iMaterial ) const
{
    return &m_pMaterialArray[ iMaterial ];
}

//--------------------------------------------------------------------------------------
SDKMESH_MESH* CDXUTSDKMesh::GetMesh( _In_ UINT iMesh ) const
{
    return &m_pMeshArray[ iMesh ];
}

//--------------------------------------------------------------------------------------
UINT CDXUTSDKMesh::GetNumSubsets( _In_ UINT iMesh ) const
{
    return m_pMeshArray[ iMesh ].NumSubsets;
}

//--------------------------------------------------------------------------------------
SDKMESH_SUBSET* CDXUTSDKMesh::GetSubset( _In_ UINT iMesh, _In_ UINT iSubset ) const
{
    return &m_pSubsetArray[ m_pMeshArray[ iMesh ].pSubsets[iSubset] ];
}

//--------------------------------------------------------------------------------------
UINT CDXUTSDKMesh::GetVertexStride( _In_ UINT iMesh, _In_ UINT iVB ) const
{
    return ( UINT )m_pVertexBufferArray[ m_pMeshArray[ iMesh ].VertexBuffers[iVB] ].StrideBytes;
}

//--------------------------------------------------------------------------------------
UINT CDXUTSDKMesh::GetNumFrames() const
{
    return m_pMeshHeader->NumFrames;
}

//--------------------------------------------------------------------------------------
SDKMESH_FRAME* CDXUTSDKMesh::GetFrame( _In_ UINT iFrame ) const
{
    assert( iFrame < m_pMeshHeader->NumFrames );
    return &m_pFrameArray[ iFrame ];
}

//--------------------------------------------------------------------------------------
SDKMESH_FRAME* CDXUTSDKMesh::FindFrame( _In_z_ const char* pszName ) const
{
    for( UINT i = 0; i < m_pMeshHeader->NumFrames; i++ )
    {
        if( _stricmp( m_pFrameArray[i].Name, pszName ) == 0 )
        {
            return &m_pFrameArray[i];
        }
    }
    return nullptr;
}

//--------------------------------------------------------------------------------------
UINT64 CDXUTSDKMesh::GetNumVertices( _In_ UINT iMesh, _In_ UINT iVB ) const
{
    return m_pVertexBufferArray[ m_pMeshArray[ iMesh ].VertexBuffers[iVB] ].NumVertices;
}

//--------------------------------------------------------------------------------------
UINT64 CDXUTSDKMesh::GetNumIndices( _In_ UINT iMesh ) const
{
    return m_pIndexBufferArray[ m_pMeshArray[ iMesh ].IndexBuffer ].NumIndices;
}

//--------------------------------------------------------------------------------------
XMVECTOR CDXUTSDKMesh::GetMeshBBoxCenter( _In_ UINT iMesh ) const
{
    return XMLoadFloat3( &m_pMeshArray[iMesh].BoundingBoxCenter );
}

//--------------------------------------------------------------------------------------
XMVECTOR CDXUTSDKMesh::GetMeshBBoxExtents( _In_ UINT iMesh ) const
{
    return XMLoadFloat3( &m_pMeshArray[iMesh].BoundingBoxExtents );
}

//--------------------------------------------------------------------------------------
UINT CDXUTSDKMesh::GetOutstandingResources() const
{
    UINT outstandingResources = 0;
    if( !m_pMeshHeader )
        return 1;

    outstandingResources += GetOutstandingBufferResources();

    if( m_pDev12 )
    {
        for( UINT i = 0; i < m_pMeshHeader->NumMaterials; i++ )
        {
            if( m_pMaterialArray[i].DiffuseTexture[0] != 0 )
            {
                if( !m_pMaterialArray[i].pDiffuseTexture12 && !IsErrorResource( m_pMaterialArray[i].pDiffuseTexture12 ) )
                    outstandingResources ++;
            }

            if( m_pMaterialArray[i].NormalTexture[0] != 0 )
            {
                if( !m_pMaterialArray[i].pNormalTexture12 && !IsErrorResource( m_pMaterialArray[i].pNormalTexture12 ) )
                    outstandingResources ++;
            }

            if( m_pMaterialArray[i].SpecularTexture[0] != 0 )
            {
                if( !m_pMaterialArray[i].pSpecularTexture12 && !IsErrorResource( m_pMaterialArray[i].pSpecularTexture12 ) )
                    outstandingResources ++;
            }
        }
    }

    return outstandingResources;
}

//--------------------------------------------------------------------------------------
UINT CDXUTSDKMesh::GetOutstandingBufferResources() const
{
    UINT outstandingResources = 0;
    if( !m_pMeshHeader )
        return 1;

    return outstandingResources;
}

//--------------------------------------------------------------------------------------
bool CDXUTSDKMesh::CheckLoadDone()
{
    if( 0 == GetOutstandingResources() )
    {
        m_bLoading = false;
        return true;
    }

    return false;
}

//--------------------------------------------------------------------------------------
bool CDXUTSDKMesh::IsLoaded() const
{
    if( m_pStaticMeshData && !m_bLoading )
    {
        return true;
    }

    return false;
}

//--------------------------------------------------------------------------------------
bool CDXUTSDKMesh::IsLoading() const
{
    return m_bLoading;
}

//--------------------------------------------------------------------------------------
void CDXUTSDKMesh::SetLoading( _In_ bool bLoading )
{
    m_bLoading = bLoading;
}

//--------------------------------------------------------------------------------------
BOOL CDXUTSDKMesh::HadLoadingError() const
{
    return FALSE;
}

//--------------------------------------------------------------------------------------
UINT CDXUTSDKMesh::GetNumInfluences( _In_ UINT iMesh ) const
{
    return m_pMeshArray[iMesh].NumFrameInfluences;
}

//--------------------------------------------------------------------------------------
XMMATRIX CDXUTSDKMesh::GetMeshInfluenceMatrix( _In_ UINT iMesh, _In_ UINT iInfluence ) const
{
    UINT iFrame = m_pMeshArray[iMesh].pFrameInfluences[ iInfluence ];
    return XMLoadFloat4x4( &m_pTransformedFrameMatrices[iFrame] );
}

XMMATRIX CDXUTSDKMesh::GetWorldMatrix( _In_ UINT iFrameIndex ) const
{
    return XMLoadFloat4x4( &m_pWorldPoseFrameMatrices[iFrameIndex] );
}

XMMATRIX CDXUTSDKMesh::GetInfluenceMatrix( _In_ UINT iFrameIndex ) const
{
    return XMLoadFloat4x4( &m_pTransformedFrameMatrices[iFrameIndex] );
}


//--------------------------------------------------------------------------------------
UINT CDXUTSDKMesh::GetAnimationKeyFromTime( _In_ double fTime ) const
{
    if( !m_pAnimationHeader )
    {
        return 0;
    }

    UINT iTick = ( UINT )( m_pAnimationHeader->AnimationFPS * fTime );

    iTick = iTick % ( m_pAnimationHeader->NumAnimationKeys - 1 );
    iTick ++;

    return iTick;
}

_Use_decl_annotations_
bool CDXUTSDKMesh::GetAnimationProperties( UINT* pNumKeys, float* pFrameTime ) const
{
    if( !m_pAnimationHeader )
    {
        *pNumKeys = 0;
        *pFrameTime = 0;
        return false;
    }

    *pNumKeys = m_pAnimationHeader->NumAnimationKeys;
    *pFrameTime = 1.0f / (float)m_pAnimationHeader->AnimationFPS;

    return true;
}
