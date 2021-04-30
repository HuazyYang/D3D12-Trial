//--------------------------------------------------------------------------------------
// File: SDKMesh.h
//
// Disclaimer:  
//   The SDK Mesh format (.sdkmesh) is not a recommended file format for shipping titles.  
//   It was designed to meet the specific needs of the SDK samples.  Any real-world 
//   applications should avoid this file format in favor of a destination format that 
//   meets the specific needs of the application.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.
//
// http://go.microsoft.com/fwlink/?LinkId=320437
//--------------------------------------------------------------------------------------
#pragma once

//--------------------------------------------------------------------------------------
// Hard Defines for the various structures
//--------------------------------------------------------------------------------------
#define SDKMESH_FILE_VERSION 101
#define MAX_VERTEX_ELEMENTS 32
#define MAX_VERTEX_STREAMS 16
#define MAX_FRAME_NAME 100
#define MAX_MESH_NAME 100
#define MAX_SUBSET_NAME 100
#define MAX_MATERIAL_NAME 100
#define MAX_TEXTURE_NAME MAX_PATH
#define MAX_MATERIAL_PATH MAX_PATH
#define INVALID_FRAME ((UINT)-1)
#define INVALID_MESH ((UINT)-1)
#define INVALID_MATERIAL ((UINT)-1)
#define INVALID_SUBSET ((UINT)-1)
#define INVALID_ANIMATION_DATA ((UINT)-1)
#define INVALID_SAMPLER_SLOT ((UINT)-1)
#define ERROR_RESOURCE_VALUE 1

template<typename TYPE> BOOL IsErrorResource( TYPE data )
{
    if( ( TYPE )ERROR_RESOURCE_VALUE == data )
        return TRUE;
    return FALSE;
}
//--------------------------------------------------------------------------------------
// Enumerated Types.
//--------------------------------------------------------------------------------------
enum SDKMESH_PRIMITIVE_TYPE
{
    PT_TRIANGLE_LIST = 0,
    PT_TRIANGLE_STRIP,
    PT_LINE_LIST,
    PT_LINE_STRIP,
    PT_POINT_LIST,
    PT_TRIANGLE_LIST_ADJ,
    PT_TRIANGLE_STRIP_ADJ,
    PT_LINE_LIST_ADJ,
    PT_LINE_STRIP_ADJ,
    PT_QUAD_PATCH_LIST,
    PT_TRIANGLE_PATCH_LIST,
};

enum SDKMESH_INDEX_TYPE
{
    IT_16BIT = 0,
    IT_32BIT,
};

enum FRAME_TRANSFORM_TYPE
{
    FTT_RELATIVE = 0,
    FTT_ABSOLUTE,		//This is not currently used but is here to support absolute transformations in the future
};

//--------------------------------------------------------------------------------------
// Structures.  Unions with pointers are forced to 64bit.
//--------------------------------------------------------------------------------------
#pragma pack(push,8)

typedef struct _D3DVERTEXELEMENT9
{
    WORD    Stream;     // Stream index
    WORD    Offset;     // Offset in the stream in bytes
    BYTE    Type;       // Data type
    BYTE    Method;     // Processing method
    BYTE    Usage;      // Semantics
    BYTE    UsageIndex; // Semantic index
} D3DVERTEXELEMENT9, *LPD3DVERTEXELEMENT9;

struct SDKMESH_HEADER
{
    //Basic Info and sizes
    UINT Version;
    BYTE IsBigEndian;
    UINT64 HeaderSize;
    UINT64 NonBufferDataSize;
    UINT64 BufferDataSize;

    //Stats
    UINT NumVertexBuffers;
    UINT NumIndexBuffers;
    UINT NumMeshes;
    UINT NumTotalSubsets;
    UINT NumFrames;
    UINT NumMaterials;

    //Offsets to Data
    UINT64 VertexStreamHeadersOffset;
    UINT64 IndexStreamHeadersOffset;
    UINT64 MeshDataOffset;
    UINT64 SubsetDataOffset;
    UINT64 FrameDataOffset;
    UINT64 MaterialDataOffset;
};

struct SDKMESH_VERTEX_BUFFER_HEADER
{
    UINT64 NumVertices;
    UINT64 SizeBytes;
    UINT64 StrideBytes;
    D3DVERTEXELEMENT9 Decl[MAX_VERTEX_ELEMENTS];
    union
    {
        UINT64 DataOffset;				//(This also forces the union to 64bits)
        ID3D12Resource* pVB12;
    };
};

struct SDKMESH_INDEX_BUFFER_HEADER
{
    UINT64 NumIndices;
    UINT64 SizeBytes;
    UINT IndexType;
    union
    {
        UINT64 DataOffset;				//(This also forces the union to 64bits)
        ID3D12Resource* pIB12;
    };
};

struct SDKMESH_MESH
{
    char Name[MAX_MESH_NAME];
    BYTE NumVertexBuffers;
    UINT VertexBuffers[MAX_VERTEX_STREAMS];
    UINT IndexBuffer;
    UINT NumSubsets;
    UINT NumFrameInfluences; //aka bones

    DirectX::XMFLOAT3 BoundingBoxCenter;
    DirectX::XMFLOAT3 BoundingBoxExtents;

    union
    {
        UINT64 SubsetOffset;	//Offset to list of subsets (This also forces the union to 64bits)
        UINT* pSubsets;	    //Pointer to list of subsets
    };
    union
    {
        UINT64 FrameInfluenceOffset;  //Offset to list of frame influences (This also forces the union to 64bits)
        UINT* pFrameInfluences;      //Pointer to list of frame influences
    };
};

struct SDKMESH_SUBSET
{
    char Name[MAX_SUBSET_NAME];
    UINT MaterialID;
    UINT PrimitiveType;
    UINT64 IndexStart;
    UINT64 IndexCount;
    UINT64 VertexStart;
    UINT64 VertexCount;
};

struct SDKMESH_FRAME
{
    char Name[MAX_FRAME_NAME];
    UINT Mesh;
    UINT ParentFrame;
    UINT ChildFrame;
    UINT SiblingFrame;
    DirectX::XMFLOAT4X4 Matrix;
    UINT AnimationDataIndex;		//Used to index which set of keyframes transforms this frame
};

struct SDKMESH_MATERIAL
{
    char    Name[MAX_MATERIAL_NAME];

    // Use MaterialInstancePath
    char    MaterialInstancePath[MAX_MATERIAL_PATH];

    // Or fall back to d3d8-type materials
    char    DiffuseTexture[MAX_TEXTURE_NAME];
    char    NormalTexture[MAX_TEXTURE_NAME];
    char    SpecularTexture[MAX_TEXTURE_NAME];

    DirectX::XMFLOAT4 Diffuse;
    DirectX::XMFLOAT4 Ambient;
    DirectX::XMFLOAT4 Specular;
    DirectX::XMFLOAT4 Emissive;
    float Power;

    union
    {
        UINT64 Force64_1;			//Force the union to 64bits
        ID3D12Resource* pDiffuseTexture12;
    };
    union
    {
        UINT64 Force64_2;			//Force the union to 64bits
        ID3D12Resource* pNormalTexture12;
    };
    union
    {
        UINT64 Force64_3;			//Force the union to 64bits
        ID3D12Resource* pSpecularTexture12;
    };

    union
    {
        UINT64 Force64_4;			//Force the union to 64bits
        INT DiffuseHeapIndex;
    };
    union
    {
        UINT64 Force64_5;		    //Force the union to 64bits
        INT NormalHeapIndex;
    };
    union
    {
        UINT64 Force64_6;			//Force the union to 64bits
        INT SpecularHeapIndex;
    };

};

struct SDKANIMATION_FILE_HEADER
{
    UINT Version;
    BYTE IsBigEndian;
    UINT FrameTransformType;
    UINT NumFrames;
    UINT NumAnimationKeys;
    UINT AnimationFPS;
    UINT64 AnimationDataSize;
    UINT64 AnimationDataOffset;
};

struct SDKANIMATION_DATA
{
    DirectX::XMFLOAT3 Translation;
    DirectX::XMFLOAT4 Orientation;
    DirectX::XMFLOAT3 Scaling;
};

struct SDKANIMATION_FRAME_DATA
{
    char FrameName[MAX_FRAME_NAME];
    union
    {
        UINT64 DataOffset;
        SDKANIMATION_DATA* pAnimationData;
    };
};

#pragma pack(pop)

static_assert( sizeof(D3DVERTEXELEMENT9) == 8, "Direct3D9 Decl structure size incorrect" );
static_assert( sizeof(SDKMESH_HEADER)== 104, "SDK Mesh structure size incorrect" );
static_assert( sizeof(SDKMESH_VERTEX_BUFFER_HEADER) == 288, "SDK Mesh structure size incorrect" );
static_assert( sizeof(SDKMESH_INDEX_BUFFER_HEADER) == 32, "SDK Mesh structure size incorrect" );
static_assert( sizeof(SDKMESH_MESH) == 224, "SDK Mesh structure size incorrect" );
static_assert( sizeof(SDKMESH_SUBSET) == 144, "SDK Mesh structure size incorrect" );
static_assert( sizeof(SDKMESH_FRAME) == 184, "SDK Mesh structure size incorrect" );
static_assert( sizeof(SDKMESH_MATERIAL) == 1256, "SDK Mesh structure size incorrect" );
static_assert( sizeof(SDKANIMATION_FILE_HEADER) == 40, "SDK Mesh structure size incorrect" );
static_assert( sizeof(SDKANIMATION_DATA) == 40, "SDK Mesh structure size incorrect" );
static_assert( sizeof(SDKANIMATION_FRAME_DATA) == 112, "SDK Mesh structure size incorrect" );

#ifndef _CONVERTER_APP_

#include <forward_list>

class ResourceUploadBatch;

//--------------------------------------------------------------------------------------
// AsyncLoading callbacks
//--------------------------------------------------------------------------------------
typedef void ( CALLBACK*LPCREATETEXTUREFROMFILE12 )( _In_ ResourceUploadBatch *pUploadBatch, _In_z_ char* szFileName,
                                                     _Outptr_ ID3D12Resource** ppRV, _Out_ INT *pAllocHeapIndex, _In_opt_ void* pContext );
typedef void ( CALLBACK*LPCREATEVERTEXBUFFER12 )( _In_ ResourceUploadBatch *pUploadBatch, _Outptr_ ID3D12Resource** ppBuffer,
                                                  _In_ D3D12_RESOURCE_DESC BufferDesc,_In_ void* pData, _In_opt_ void* pContext );
typedef void ( CALLBACK*LPCREATEINDEXBUFFER12 )( _In_ ResourceUploadBatch *pUploadBatch, _Outptr_ ID3D12Resource** ppBuffer,
                                                 _In_ D3D12_RESOURCE_DESC BufferDesc, _In_ void* pData, _In_opt_ void* pContext );
struct SDKMESH_CALLBACKS12
{
    LPCREATETEXTUREFROMFILE12 pCreateTextureFromFile;
    LPCREATEVERTEXBUFFER12 pCreateVertexBuffer;
    LPCREATEINDEXBUFFER12 pCreateIndexBuffer;
    void* pContext;
};

struct SDKMESH_TEXTURE_CACHE_ENTRY {
    WCHAR   wszSource[MAX_PATH];
    bool    bSRGB;
    INT uDescriptorHeapIndex; // For better data alignment.
    ID3D12Resource* pSRV12;
    SDKMESH_TEXTURE_CACHE_ENTRY() {
        wszSource[0] = 0;
        bSRGB = false;
        pSRV12 = nullptr;
        uDescriptorHeapIndex = -1;
    }
};

//--------------------------------------------------------------------------------------
// CDXUTSDKMesh class.  This class reads the sdkmesh file format for use by the samples
//--------------------------------------------------------------------------------------
class CDXUTSDKMesh
{
private:
    UINT m_NumOutstandingResources;
    bool m_bLoading;
    //BYTE*                         m_pBufferData;
    HANDLE m_hFile;
    HANDLE m_hFileMappingObject;
    std::vector<BYTE*> m_MappedPointers;
    ID3D12Device* m_pDev12;
    ID3D12GraphicsCommandList* m_pd3dCommandList;

    std::vector<SDKMESH_TEXTURE_CACHE_ENTRY> m_TextureCache;

protected:
    //These are the pointers to the two chunks of data loaded in from the mesh file
    BYTE* m_pStaticMeshData;
    BYTE* m_pHeapData;
    BYTE* m_pAnimationData;
    BYTE** m_ppVertices;
    BYTE** m_ppIndices;

    //Keep track of the path
    WCHAR                           m_strPathW[MAX_PATH];
    char                            m_strPath[MAX_PATH];

    //General mesh info
    SDKMESH_HEADER* m_pMeshHeader;
    SDKMESH_VERTEX_BUFFER_HEADER* m_pVertexBufferArray;
    SDKMESH_INDEX_BUFFER_HEADER* m_pIndexBufferArray;
    SDKMESH_MESH* m_pMeshArray;
    SDKMESH_SUBSET* m_pSubsetArray;
    SDKMESH_FRAME* m_pFrameArray;
    SDKMESH_MATERIAL* m_pMaterialArray;

    // Adjacency information (not part of the m_pStaticMeshData, so it must be created and destroyed separately )
    SDKMESH_INDEX_BUFFER_HEADER* m_pAdjacencyIndexBufferArray;

    //Animation
    SDKANIMATION_FILE_HEADER* m_pAnimationHeader;
    SDKANIMATION_FRAME_DATA* m_pAnimationFrameData;
    DirectX::XMFLOAT4X4* m_pBindPoseFrameMatrices;
    DirectX::XMFLOAT4X4* m_pTransformedFrameMatrices;
    DirectX::XMFLOAT4X4* m_pWorldPoseFrameMatrices;

protected:
    HRESULT CreateTextureFromFile(_In_ ResourceUploadBatch *pUploadBatch, _In_z_ LPCWSTR pSrcFile,
                                   _Outptr_ ID3D12Resource** ppOutputRV, _In_ bool bSRGB=false, _Out_opt_ INT *pAllocHeapIndex = nullptr);
    HRESULT CreateTextureFromFile(_In_ ResourceUploadBatch *pUploadBatch, _In_z_ LPCSTR pSrcFile,
                                  _Outptr_ ID3D12Resource **ppOutputRV, _In_ bool bSRGB = false, _Out_opt_ INT *pAllocHeapIndex = nullptr);
    void LoadMaterials( _In_ ResourceUploadBatch* pUploadBatch, _In_reads_(NumMaterials) SDKMESH_MATERIAL* pMaterials,
                        _In_ UINT NumMaterials, _In_opt_ SDKMESH_CALLBACKS12* pLoaderCallbacks = nullptr );

    HRESULT CreateVertexBuffer( _In_ ResourceUploadBatch* pUploadBatch,
                                _In_ SDKMESH_VERTEX_BUFFER_HEADER* pHeader, _In_reads_(pHeader->SizeBytes) void* pVertices,
                                _In_opt_ SDKMESH_CALLBACKS12* pLoaderCallbacks = nullptr );

    HRESULT CreateIndexBuffer( _In_ ResourceUploadBatch* pUploadBatch,
                               _In_ SDKMESH_INDEX_BUFFER_HEADER* pHeader, _In_reads_(pHeader->SizeBytes) void* pIndices,
                               _In_opt_ SDKMESH_CALLBACKS12* pLoaderCallbacks = nullptr );

    virtual HRESULT CreateFromFile( _In_opt_ ResourceUploadBatch* pUploadBatch,
                                    _In_z_ LPCWSTR szFileName,
                                    _In_opt_ SDKMESH_CALLBACKS12* pLoaderCallbacks12 = nullptr );

    virtual HRESULT CreateFromMemory( _In_opt_ ResourceUploadBatch* pUploadBatch,
                                      _In_reads_(DataBytes) BYTE* pData,
                                      _In_ size_t DataBytes,
                                      _In_ bool bCopyStatic,
                                      _In_opt_ SDKMESH_CALLBACKS12* pLoaderCallbacks12 = nullptr );

    //frame manipulation
    void TransformBindPoseFrame( _In_ UINT iFrame, _In_ DirectX::CXMMATRIX parentWorld );
    void TransformFrame( _In_ UINT iFrame, _In_ DirectX::CXMMATRIX parentWorld, _In_ double fTime );
    void TransformFrameAbsolute( _In_ UINT iFrame, _In_ double fTime );

    //Direct3D 12 rendering helpers
    void RenderMesh( _In_ UINT iMesh,
                     _In_ bool bAdjacent,
                     _In_ ID3D12GraphicsCommandList* pd3dCommandList,
                     _In_ ID3D12DescriptorHeap *pResourceDescriptorHeap,
                     _In_ UINT iDiffuseSlot,
                     _In_ UINT iNormalSlot,
                     _In_ UINT iSpecularSlot );
    void RenderFrame( _In_ UINT iFrame,
                      _In_ bool bAdjacent,
                      _In_ ID3D12GraphicsCommandList* pd3dCommandList,
                      _In_ ID3D12DescriptorHeap *pResourceDescriptorHeap,
                      _In_ UINT iDiffuseSlot,
                      _In_ UINT iNormalSlot,
                      _In_ UINT iSpecularSlot );

public:
    CDXUTSDKMesh() noexcept;
    virtual ~CDXUTSDKMesh();

    virtual HRESULT Create( _In_ ResourceUploadBatch* pUploadBatch, _In_z_ LPCWSTR szFileName, _In_opt_ SDKMESH_CALLBACKS12* pLoaderCallbacks = nullptr );
    virtual HRESULT Create( _In_ ResourceUploadBatch* pUploadBatch, BYTE* pData, size_t DataBytes, _In_ bool bCopyStatic=false,
                            _In_opt_ SDKMESH_CALLBACKS12* pLoaderCallbacks = nullptr );
    // When you not provide SDKMESH_CALLBACK12, you must call this to reclare the resource view descriptor heap, or you
    // can not bind to the correct descriptor heap(s).
    HRESULT GetResourceDescriptorHeap(_In_ ID3D12Device* pDev12, _Out_ ID3D12DescriptorHeap **ppHeap) const;
    virtual HRESULT LoadAnimation( _In_z_ const WCHAR* szFileName );
    virtual void Destroy();

    //Frame manipulation
    void TransformBindPose( _In_ DirectX::CXMMATRIX world ) { TransformBindPoseFrame( 0, world ); };
    void TransformMesh( _In_ DirectX::CXMMATRIX world, _In_ double fTime );

    //Direct3D 12 Rendering
    virtual void Render( _In_ ID3D12GraphicsCommandList* pd3dCommandList,
                         _In_ ID3D12DescriptorHeap *pResourceDescriptorHeap,
                         _In_ UINT iDiffuseSlot = INVALID_SAMPLER_SLOT,
                         _In_ UINT iNormalSlot = INVALID_SAMPLER_SLOT,
                         _In_ UINT iSpecularSlot = INVALID_SAMPLER_SLOT );
    virtual void RenderAdjacent( _In_ ID3D12GraphicsCommandList* pd3dCommandList,
                                 _In_ ID3D12DescriptorHeap *pResourceDescriptorHeap,
                                 _In_ UINT iDiffuseSlot = INVALID_SAMPLER_SLOT,
                                 _In_ UINT iNormalSlot = INVALID_SAMPLER_SLOT,
                                 _In_ UINT iSpecularSlot = INVALID_SAMPLER_SLOT );

    //Helpers (D3D12 specific)
    static D3D12_PRIMITIVE_TOPOLOGY GetPrimitiveType12( _In_ SDKMESH_PRIMITIVE_TYPE PrimType );
    DXGI_FORMAT GetIBFormat12( _In_ UINT iMesh ) const;
    ID3D12Resource* GetVB12( _In_ UINT iMesh, _In_ UINT iVB ) const;
    ID3D12Resource* GetIB12( _In_ UINT iMesh ) const;
    SDKMESH_INDEX_TYPE GetIndexType( _In_ UINT iMesh ) const; 

    ID3D12Resource* GetAdjIB12( _In_ UINT iMesh ) const;

    //Helpers (general)
    const char* GetMeshPathA() const;
    const WCHAR* GetMeshPathW() const;
    UINT GetNumMeshes() const;
    UINT GetNumMaterials() const;
    UINT GetNumVBs() const;
    UINT GetNumIBs() const;

    ID3D12Resource* GetVB12At( _In_ UINT iVB ) const;
    ID3D12Resource* GetIB12At( _In_ UINT iIB ) const;

    BYTE* GetRawVerticesAt( _In_ UINT iVB ) const;
    BYTE* GetRawIndicesAt( _In_ UINT iIB ) const;

    SDKMESH_MATERIAL* GetMaterial( _In_ UINT iMaterial ) const;
    SDKMESH_MESH*     GetMesh( _In_ UINT iMesh ) const;
    UINT              GetNumSubsets( _In_ UINT iMesh ) const;
    SDKMESH_SUBSET*   GetSubset( _In_ UINT iMesh, _In_ UINT iSubset ) const;
    UINT              GetVertexStride( _In_ UINT iMesh, _In_ UINT iVB ) const;
    UINT              GetNumFrames() const;
    SDKMESH_FRAME*    GetFrame( _In_ UINT iFrame ) const; 
    SDKMESH_FRAME*    FindFrame( _In_z_ const char* pszName ) const;
    UINT64            GetNumVertices( _In_ UINT iMesh, _In_ UINT iVB ) const;
    UINT64            GetNumIndices( _In_ UINT iMesh ) const;
    DirectX::XMVECTOR GetMeshBBoxCenter( _In_ UINT iMesh ) const;
    DirectX::XMVECTOR GetMeshBBoxExtents( _In_ UINT iMesh ) const;
    UINT              GetOutstandingResources() const;
    UINT              GetOutstandingBufferResources() const;
    bool              CheckLoadDone();
    bool              IsLoaded() const;
    bool              IsLoading() const;
    void              SetLoading( _In_ bool bLoading );
    BOOL              HadLoadingError() const;

    //Animation
    UINT              GetNumInfluences( _In_ UINT iMesh ) const;
    DirectX::XMMATRIX GetMeshInfluenceMatrix( _In_ UINT iMesh, _In_ UINT iInfluence ) const;
    UINT              GetAnimationKeyFromTime( _In_ double fTime ) const;
    DirectX::XMMATRIX GetWorldMatrix( _In_ UINT iFrameIndex ) const;
    DirectX::XMMATRIX GetInfluenceMatrix( _In_ UINT iFrameIndex ) const;
    bool              GetAnimationProperties( _Out_ UINT* pNumKeys, _Out_ float* pFrameTime ) const;
};

#endif

