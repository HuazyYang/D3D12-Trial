#include "D3D12RendererContext.hpp"
#include "UIController.hpp"
#include "Win32Application.hpp"
#include "Camera.h"
#include <DirectXColors.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include "GeometryGenerator.h"
#include <wrl.h>
#include "MeshBuffer.h"
#include "RenderItem.h"
#include "UploadBuffer.h"
#include "GpuWaves.h"
#include "Material.h"
#include "LightUtils.h"
#include <map>
#include <array>
#include <random>
#include "Texture.h"
#include <ppl.h>
#include "RootSignatureGenerator.h"

using namespace DirectX;
using namespace Microsoft::WRL;

void CreateLandAndWavesApp(D3D12RendererContext **ppRenderer, IUIController **ppUIController);


int main() {

    int ret;
    D3D12RendererContext *pRenderer;
    IUIController *pUIController;

    CreateLandAndWavesApp(&pRenderer, &pUIController);

    ret = RunSample(pRenderer, pUIController, 800, 600, L"Land and Waves");
    SAFE_DELETE(pRenderer);
    return ret;
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

namespace LandAndWavesAppInternal {
    struct PassConstants {
        XMFLOAT4X4 ViewProj;
        XMFLOAT3 EyePosW;
        FLOAT Padding;
        XMFLOAT4 AmbientStrength;
        Light Lights[16];
    };

    struct ObjectConstants {
        XMFLOAT4X4 World;
        XMFLOAT4X4 WorldInvTranspose;
        XMFLOAT4X4 TexTransform;
        FLOAT WavesSpatialStep;
    };

    struct MaterialConstants {
        XMFLOAT4 DiffuseAlbedo;
        XMFLOAT3 FresnelR0;
        FLOAT Roughness;
        XMFLOAT4X4 MatTransform;
    };

    struct PipelineStateBuffer {
      ComPtr<ID3D12PipelineState> PSO;
      ComPtr<ID3D12RootSignature> RootSignature;
    };

    class FrameResources {
    public:
        ~FrameResources() {
            SAFE_RELEASE(CmdListAlloc);
        }

        HRESULT CreateCommmandAllocator(ID3D12Device *pd3dDevice) {
            HRESULT hr;

            V_RETURN(pd3dDevice->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&CmdListAlloc)
            ));
            return hr;
        }

        HRESULT CreateBuffers(ID3D12Device *pd3dDevice, int iPassCount, int iObjCount) {
            HRESULT hr;

            V_RETURN(PassCBs.CreateBuffer(pd3dDevice, iPassCount, sizeof(PassConstants), TRUE));
            V_RETURN(ObjectCBs.CreateBuffer(pd3dDevice, iObjCount, sizeof(ObjectConstants), TRUE));
            return hr;
        }

        static HRESULT CreateStaticBuffers(ID3D12Device *pd3dDevice, int iMatCount) {
            HRESULT hr;
            V_RETURN(MaterialCB.CreateBuffer(pd3dDevice, iMatCount, sizeof(MaterialConstants), TRUE));
            return hr;
        }

        ID3D12CommandAllocator *CmdListAlloc = nullptr;
        UploadBuffer PassCBs;
        UploadBuffer ObjectCBs;

        static UploadBuffer MaterialCB;

        int PassHeapIndex = -1;
        UINT64 FenceCount = 0;
    };

    UploadBuffer FrameResources::MaterialCB;
};

using namespace LandAndWavesAppInternal;

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers() {
    // Applications usually only need a handful of samplers.  So just define them all up front
    // and keep them available as part of the root signature.  

    const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
        0, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        1, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        2, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        3, // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
        4, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
        0.0f,                             // mipLODBias
        8);                               // maxAnisotropy

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
        5, // shaderRegister
        D3D12_FILTER_ANISOTROPIC, // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
        0.0f,                              // mipLODBias
        8);                                // maxAnisotropy

    const CD3DX12_STATIC_SAMPLER_DESC depthPointBias(
        6,
        D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        D3D12_TEXTURE_ADDRESS_MODE_BORDER,
        0.0f,
        16,
        D3D12_COMPARISON_FUNC_LESS_EQUAL,
        D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK
    );

    return {
        pointWrap, pointClamp,
        linearWrap, linearClamp,
        anisotropicWrap, anisotropicClamp, depthPointBias };
}

class LandAndWavesApp : public D3D12RendererContext, public IUIController {
public:
    LandAndWavesApp();
    ~LandAndWavesApp();
private:
    HRESULT OnInitPipelines() override;
    void OnFrameMoved(float fTime, float fElapsedTime) override;
    void OnRenderFrame(float fTime, float fElapsedTime) override;
    void OnKeyEvent(int downUp, unsigned short key, int repeatCnt) override;
    void OnMouseButtonEvent(UI_MOUSE_BUTTON_EVENT ev, UI_MOUSE_VIRTUAL_KEY keys, int x, int y) override;
    void OnMouseMove(UI_MOUSE_VIRTUAL_KEY keys, int x, int y) override;
    void OnMouseWheel(UI_MOUSE_VIRTUAL_KEY keys, int delta, int x, int y) override;
    void OnResizeFrame(int cx, int cy) override;

    HRESULT LoadTextures();
    HRESULT BuildLandGeometry();
    HRESULT BuildWavesGeometry();
    HRESULT BuildTreeBillBoardGeometry();
    HRESULT BuildWavesCSOs();
    HRESULT BuildPSOs();
    HRESULT BuildTreeBillBoardPSO();
    HRESULT BuildFrameResourcess();
    HRESULT BuildDescriptorHeap();
    VOID PostInitialize();

    void UpdatePassCBs(FrameResources *pFrameResource, float fTime, float fElapsedTime);
    void UpdateObjectCBs(FrameResources *pFrameResource, float fTime, float fElapsedTime);
    void UpdateMaterialCBs();

    void DrawRenderItem(
        FrameResources *pFrameResources,
        ID3D12GraphicsCommandList *pd3dCommandList,
        RenderItem *ri);

    float GetHillsHeight(float x, float z) const;
    XMFLOAT3 GetHillsNormals(float x, float z) const;

    enum {
        Solid = 0,
        Waves,
        TreeSprite,
    };

    ID3D12DescriptorHeap *m_pCbvSrvUavHeap = nullptr;

    std::vector<RenderItem> m_aRitems[3];

    std::map<std::string, Material *> m_aMaterials;
    Lights m_aLights;
    std::map<std::string, Texture *> m_aTextures;

    GpuWaves *m_pWaves = nullptr;

    std::map<std::string, PipelineStateBuffer> m_aPSOs;

    static const int s_iNumberOfFrames = 3;
    FrameResources m_aFrameResources[s_iNumberOfFrames];
    int m_iCurrentFrameIndex = 0;

    BOOL m_bWriteframe = FALSE;
    Camera m_Camera;

    FLOAT m_fSunTheta = 1.25f*XM_PI;
    FLOAT m_fSunPhi = XM_PIDIV4;

    POINT m_ptLastMousePos;
};

void CreateLandAndWavesApp(D3D12RendererContext **ppRenderer, IUIController **ppUIController) {
  LandAndWavesApp *pContext = new LandAndWavesApp;
  *ppRenderer = pContext;
  *ppUIController = pContext;
}

LandAndWavesApp::LandAndWavesApp() {
    m_Camera.SetOrbit(50.0f, 1.5f * XM_PI, XM_PIDIV2 - 0.1f);

    m_aLights.AmbientStrength = { 0.25f, 0.25f, 0.35f, 1.0f };
    m_aLights.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
    m_aLights.Lights[0].Strength = { 0.9f, 0.9f, 0.9f };
    m_aLights.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
    m_aLights.Lights[1].Strength = { 0.5f, 0.5f, 0.5f };
    m_aLights.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
    m_aLights.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

    Material *matGrass, *matWater, *matWirefence, *matTree;

    matGrass = new Material;
    matGrass->Name = "grass";
    matGrass->CBIndex = 0;
    matGrass->NumFrameDirty = s_iNumberOfFrames;
    matGrass->DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    matGrass->FresnelR0 = { 0.01f, 0.01f, 0.01f };
    matGrass->Roughness = 0.125f;
    m_aMaterials.insert({ matGrass->Name, matGrass });

    matWater = new Material;
    matWater->Name = "water";
    matWater->CBIndex = 1;
    matWater->NumFrameDirty = s_iNumberOfFrames;
    matWater->DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 0.5f };
    matWater->FresnelR0 = { 0.2f, 0.2f, 0.2f };
    matWater->Roughness = 0.0f;
    m_aMaterials.insert({ matWater->Name, matWater });

    matWirefence = new Material;
    matWirefence->Name = "wirefence";
    matWirefence->CBIndex = 2;
    matWirefence->NumFrameDirty = s_iNumberOfFrames;
    matWirefence->DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    matWirefence->FresnelR0 = { 0.1f, 0.1f, 0.1f };
    matWirefence->Roughness = 0.25f;

    m_aMaterials.insert({ matWirefence->Name, matWirefence });

    matTree = new Material;
    matTree->Name = "tree";
    matTree->CBIndex = 3;
    matTree->NumFrameDirty = s_iNumberOfFrames;
    matTree->DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    matTree->FresnelR0 = { 0.01f, 0.01f, 0.01f };
    matTree->Roughness = 0.125f;

    m_aMaterials.insert({ matTree->Name, matTree });

    //m_aDeviceConfig.MsaaEnabled = TRUE;
    memcpy(m_aRTVDefaultClearValue.Color, Colors::LightSteelBlue, sizeof(m_aRTVDefaultClearValue.Color));
}

LandAndWavesApp::~LandAndWavesApp() {

    SAFE_RELEASE(m_pCbvSrvUavHeap);

    for (auto &mat : m_aMaterials) {
        SAFE_RELEASE(mat.second);
    }

    for (auto &tex : m_aTextures) {
        SAFE_RELEASE(tex.second);
    }

    delete m_pWaves;
}

HRESULT LandAndWavesApp::OnInitPipelines() {
    HRESULT hr;

    // Reset the command list to prep for initialization commands.
    V_RETURN(m_pd3dCommandList->Reset(m_pd3dDirectCmdAlloc, nullptr));

    V_RETURN(LoadTextures());
    V_RETURN(BuildLandGeometry());
    V_RETURN(BuildTreeBillBoardGeometry());
    V_RETURN(BuildWavesGeometry());
    V_RETURN(BuildWavesCSOs());
    V_RETURN(BuildPSOs());
    V_RETURN(BuildTreeBillBoardPSO());
    V_RETURN(BuildFrameResourcess());
    V_RETURN(BuildDescriptorHeap());

    UpdateMaterialCBs();

    // Execute the initialization commands.
    V_RETURN(m_pd3dCommandList->Close());
    ID3D12CommandList *cmdList[] = { m_pd3dCommandList };
    m_pd3dCommandQueue->ExecuteCommandLists(1, cmdList);

    // Wait until initialization is complete.
    FlushCommandQueue();

    PostInitialize();

    return hr;
}

VOID LandAndWavesApp::PostInitialize() {
    for (auto &riLayer : m_aRitems) {
        for (auto &ri : riLayer) {
            ri.pMeshBuffer->DisposeUploaders();
        }
    }
}

HRESULT LandAndWavesApp::LoadTextures() {
    HRESULT hr;
    Texture *pTexture;

    pTexture = new Texture("grass");
    V_RETURN(pTexture->CreateTextureFromDDSFile(m_pd3dDevice, m_pd3dCommandList, LR"(Media/Textures/grass.dds)"));
    m_aTextures.insert({ "grass", pTexture });

    pTexture = new Texture("water");
    V_RETURN(pTexture->CreateTextureFromDDSFile(m_pd3dDevice, m_pd3dCommandList, LR"(Media/Textures/water1.dds)"));
    m_aTextures.insert({ "water", pTexture });

    pTexture = new Texture("wirefence");
    V_RETURN(pTexture->CreateTextureFromDDSFile(m_pd3dDevice, m_pd3dCommandList, LR"(Media/Textures/WireFence.dds)"));
    m_aTextures.insert({ "wirefence", pTexture });

    pTexture = new Texture("treearray");
    V_RETURN(pTexture->CreateTextureFromDDSFile(m_pd3dDevice, m_pd3dCommandList, LR"(Media/Textures/treeArray2.dds)"));
    m_aTextures.insert({ pTexture->Name, pTexture });

    return hr;
}

HRESULT LandAndWavesApp::BuildDescriptorHeap() {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc;
    UINT numDescriptors;
    HRESULT hr;
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle;
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle;
    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    D3D12_RESOURCE_DESC texDesc;
    UINT cbCbvByteSize;
    int iHeapIndex;
    int frameIndex;
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress0, cbAddress;
    int nRitemCount = 0;

    for (auto &riLayer : m_aRitems)
        nRitemCount += riLayer.size();

    numDescriptors = m_pWaves->DescriptorHeapCount() + (UINT)m_aMaterials.size() + 
        (UINT)m_aTextures.size() + (1 + nRitemCount) * s_iNumberOfFrames;

    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = numDescriptors;
    heapDesc.NodeMask = 0;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    V_RETURN(m_pd3dDevice->CreateDescriptorHeap(
        &heapDesc,
        IID_PPV_ARGS(&m_pCbvSrvUavHeap)
    ));

    cpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_pCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart());
    gpuHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(m_pCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart());
    iHeapIndex = 0;

    m_pWaves->BuildDescriptors(cpuHandle, gpuHandle, m_uCbvSrvUavDescriptorSize);

    cpuHandle.Offset(6, m_uCbvSrvUavDescriptorSize);
    gpuHandle.Offset(6, m_uCbvSrvUavDescriptorSize);
    iHeapIndex += 6;

    /// Material datas.
    cbCbvByteSize = d3dUtils::CalcConstantBufferByteSize(sizeof(MaterialConstants));
    cbAddress0 = FrameResources::MaterialCB.GetConstBufferAddress();
    for (auto &mat : m_aMaterials) {
        cbvDesc.BufferLocation = cbAddress0 + cbCbvByteSize * mat.second->CBIndex;
        cbvDesc.SizeInBytes = cbCbvByteSize;

        mat.second->CbvHeapIndex = iHeapIndex;

        m_pd3dDevice->CreateConstantBufferView(&cbvDesc, cpuHandle);

        cpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);
        iHeapIndex += 1;
    }


    for (auto &tex : m_aTextures) {
        texDesc = tex.second->Resource->GetDesc();
        srvDesc.Format = texDesc.Format;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        tex.second->SrvHeapIndex = iHeapIndex;

        m_pd3dDevice->CreateShaderResourceView(tex.second->Resource, &srvDesc, cpuHandle);
        cpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);
        iHeapIndex += 1;
    }

    /// Pass descritptor heaps.
    cbCbvByteSize = d3dUtils::CalcConstantBufferByteSize(sizeof(PassConstants));
    for (frameIndex = 0; frameIndex != s_iNumberOfFrames; ++frameIndex) {
        cbAddress0 = m_aFrameResources[frameIndex].PassCBs.GetConstBufferAddress();

        cbvDesc.BufferLocation = cbAddress0;
        cbvDesc.SizeInBytes = cbCbvByteSize;

        m_aFrameResources[frameIndex].PassHeapIndex = iHeapIndex;
        m_pd3dDevice->CreateConstantBufferView(&cbvDesc, cpuHandle);

        cpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);
        iHeapIndex += 1;
    }

    // Objcet descriptor heaps.
    cbCbvByteSize = d3dUtils::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    for (frameIndex = 0; frameIndex != s_iNumberOfFrames; ++frameIndex) {
        cbAddress0 = m_aFrameResources[frameIndex].ObjectCBs.GetConstBufferAddress();
        for (auto &riLayer : m_aRitems) {
            for (auto &ri : riLayer) {
                cbAddress = cbAddress0 + ri.uCBIndex * cbCbvByteSize;

                cbvDesc.BufferLocation = cbAddress;
                cbvDesc.SizeInBytes = cbCbvByteSize;

                ri.uCbvHeapIndex = iHeapIndex;

                m_pd3dDevice->CreateConstantBufferView(&cbvDesc, cpuHandle);
                cpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);
                iHeapIndex += 1;
            }
        }
    }

    return hr;
}

HRESULT LandAndWavesApp::BuildLandGeometry() {
    HRESULT hr;

    GeometryGenerator geoGen;
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.0f, 100, 100);
    UINT cbVertexBuffer, cbIndexBuffer;
    MeshBuffer *pMeshBuffer;

    concurrency::parallel_for_each(grid.Vertices.begin(), grid.Vertices.end(), [this](auto &val) {
        val.Position.y = GetHillsHeight(val.Position.x, val.Position.z);
        val.Normal = GetHillsNormals(val.Position.x, val.Position.z);
    });

    cbVertexBuffer = (UINT)grid.Vertices.size() * sizeof(GeometryGenerator::Vertex);
    cbIndexBuffer = (UINT)grid.GetIndices16().size() * sizeof(GeometryGenerator::uint16);

    V_RETURN(CreateMeshBuffer(&pMeshBuffer));
    pMeshBuffer->CreateVertexBuffer(
      m_pd3dDevice,
      m_pd3dCommandList,
      grid.Vertices.data(),
      grid.Vertices.size(),
      sizeof(GeometryGenerator::Vertex),
      nullptr
    );
    pMeshBuffer->CreateIndexBuffer(
      m_pd3dDevice,
      m_pd3dCommandList,
      grid.GetIndices16().data(),
      grid.GetIndices16().size(),
      sizeof(UINT16)
    );

    RenderItem ri;

    ri.SetMeshBuffer(pMeshBuffer);
    ri.uIndexCount = (UINT)grid.GetIndices16().size();
    ri.iNumFramesDirty = s_iNumberOfFrames;
    ri.uCBIndex = 0;
    ri.SetMaterial(m_aMaterials["grass"]);
    ri.pMaterial->SetDiffuseMap(m_aTextures["grass"]);
    XMStoreFloat4x4(&ri.matTexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));

    m_aRitems[Solid].push_back(ri);
    SAFE_RELEASE(pMeshBuffer);

    GeometryGenerator::MeshData box = geoGen.CreateBox(8.0f, 8.0f, 8.0f, 3);

    cbVertexBuffer = (UINT)box.Vertices.size() * sizeof(GeometryGenerator::Vertex);
    cbIndexBuffer = (UINT)box.GetIndices16().size() * sizeof(GeometryGenerator::uint16);

    V_RETURN(CreateMeshBuffer(&pMeshBuffer));
    pMeshBuffer->CreateVertexBuffer(
      m_pd3dDevice,
      m_pd3dCommandList,
      box.Vertices.data(),
      box.Vertices.size(),
      sizeof(GeometryGenerator::Vertex),
      nullptr
    );
    pMeshBuffer->CreateIndexBuffer(
      m_pd3dDevice,
      m_pd3dCommandList,
      box.GetIndices16().data(),
      box.GetIndices16().size(),
      sizeof(UINT16)
    );

    XMStoreFloat4x4(&ri.matWorld, XMMatrixTranslation(3.0f, 2.0f, -9.0f));
    ri.SetMeshBuffer(pMeshBuffer);
    ri.uIndexCount = (UINT)box.GetIndices16().size();
    ri.iNumFramesDirty = s_iNumberOfFrames;
    ri.uCBIndex = 1;
    ri.SetMaterial(m_aMaterials["wirefence"]);
    ri.pMaterial->SetDiffuseMap(m_aTextures["wirefence"]);
    XMStoreFloat4x4(&ri.matTexTransform, XMMatrixIdentity());

    m_aRitems[Solid].push_back(ri);
    SAFE_RELEASE(pMeshBuffer);

    return hr;
}

HRESULT LandAndWavesApp::BuildTreeBillBoardGeometry() {
    struct TreeSpriteVertex {
        XMFLOAT3 Pos;
        XMFLOAT2 Size;
    };

    std::default_random_engine dre;
    std::uniform_real_distribution<float> fd(-70.0f, 70.0f);
    TreeSpriteVertex vertices[300];
    HRESULT hr;

    for (auto &v : vertices) {

        v.Pos = { fd(dre), 0.0f, fd(dre) };
        v.Size = { 15.0f, 15.0f };
        v.Pos.y = GetHillsHeight(v.Pos.x, v.Pos.z) + 8.0f;
    }

    MeshBuffer *pMeshBuffer;

    V_RETURN(CreateMeshBuffer(&pMeshBuffer));
    pMeshBuffer->CreateVertexBuffer(
      m_pd3dDevice,
      m_pd3dCommandList,
      vertices,
      _countof(vertices),
      sizeof(TreeSpriteVertex),
      nullptr
    );

    RenderItem ri;

    ri.PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
    ri.SetMeshBuffer(pMeshBuffer);
    ri.uIndexCount = _countof(vertices);
    ri.iNumFramesDirty = s_iNumberOfFrames;
    ri.uCBIndex = 3;
    ri.SetMaterial(m_aMaterials["tree"]);
    ri.pMaterial->SetDiffuseMap(m_aTextures["treearray"]);

    m_aRitems[TreeSprite].push_back(ri);
    SAFE_RELEASE(pMeshBuffer);

    return S_OK;
}

HRESULT LandAndWavesApp::BuildWavesGeometry() {
    HRESULT hr;
    GeometryGenerator gen;
    GeometryGenerator::MeshData grid;
    MeshBuffer *pWavesBuffer;

    if (!m_pWaves) {
        m_pWaves = new GpuWaves(m_pd3dDevice, m_pd3dCommandList,
            256, 256, 0.25f, 0.03f, 2.0f, 0.2f);
    }

    grid = gen.CreateGrid(160, 160, m_pWaves->RowCount(), m_pWaves->ColumnCount());

    V_RETURN(CreateMeshBuffer(&pWavesBuffer));
    pWavesBuffer->CreateVertexBuffer(
      m_pd3dDevice,
      m_pd3dCommandList,
      grid.Vertices.data(),
      grid.Vertices.size(),
      sizeof(GeometryGenerator::Vertex),
      nullptr
    );
    pWavesBuffer->CreateIndexBuffer(
      m_pd3dDevice,
      m_pd3dCommandList,
      grid.Indices32.data(),
      grid.Indices32.size(),
      sizeof(UINT32)
    );

    RenderItem ri;
    ri.uIndexCount = (UINT)grid.Indices32.size();
    ri.iNumFramesDirty = s_iNumberOfFrames;
    ri.SetMeshBuffer(pWavesBuffer);
    ri.uCBIndex = 2;
    ri.SetMaterial(m_aMaterials["water"]);
    ri.pMaterial->SetDiffuseMap(m_aTextures["water"]);
    XMStoreFloat4x4(&ri.matTexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));

    m_aRitems[Waves].push_back(ri);

    SAFE_RELEASE(pWavesBuffer);

    return hr;
}

HRESULT LandAndWavesApp::BuildWavesCSOs() {
  HRESULT hr;
  ComPtr<ID3DBlob> pErrorBlob;
  ComPtr<ID3DBlob> CSBuffer;
  D3D12_COMPUTE_PIPELINE_STATE_DESC csoDesc;
  PipelineStateBuffer psoBuffer;

  UINT compileFlags = 0;
#if defined(_DEBUG)
  compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

  V_RETURN(d3dUtils::CompileShaderFromFile(TEXT("Shaders/GpuWaves.hlsl"), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
    "UpdateWavesCS", "cs_5_0", compileFlags, 0, CSBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
  if (pErrorBlob) {
    DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  /// Root Signature.
  RootSignatureGenerator signatureGen;
  signatureGen.AddRootConstants(6, 0);
  signatureGen.AddDescriptorTable(
    {
      RootSignatureGenerator::ComposeShaderResourceViewRange(1, 0)
    }
  );
  signatureGen.AddDescriptorTable(
    {
      RootSignatureGenerator::ComposeShaderResourceViewRange(1, 1)
    }
  );
  signatureGen.AddDescriptorTable(
    {
      RootSignatureGenerator::ComposeUnorderedAccessViewRange(1, 0)
    }
  );
  signatureGen.Generate(m_pd3dDevice, D3D12_ROOT_SIGNATURE_FLAG_NONE,
    &psoBuffer.RootSignature);

    ZeroMemory(&csoDesc, sizeof(csoDesc));
    csoDesc.pRootSignature = psoBuffer.RootSignature.Get();
    csoDesc.CS = {
        CSBuffer->GetBufferPointer(),
        CSBuffer->GetBufferSize()
    };
    csoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    V_RETURN(m_pd3dDevice->CreateComputePipelineState(&csoDesc, IID_PPV_ARGS(&psoBuffer.PSO)));

    m_aPSOs["waves_update"] = std::move(psoBuffer);

    pErrorBlob = nullptr;
    V_RETURN(d3dUtils::CompileShaderFromFile(TEXT("Shaders/GpuWaves.hlsl"), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "DisturbWavesCS", "cs_5_0", compileFlags, 0, CSBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
    if (pErrorBlob) {
        DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
        V_RETURN(E_FAIL);
    }

    signatureGen.Reset();
    signatureGen.AddRootConstants(6, 0);
    signatureGen.AddDescriptorTable(
      {
        RootSignatureGenerator::ComposeUnorderedAccessViewRange(1, 0)
      }
    );
    V_RETURN(signatureGen.Generate(m_pd3dDevice, D3D12_ROOT_SIGNATURE_FLAG_NONE,
      psoBuffer.RootSignature.GetAddressOf()));

    csoDesc.pRootSignature = psoBuffer.RootSignature.Get();
    csoDesc.CS = {
        CSBuffer->GetBufferPointer(),
        CSBuffer->GetBufferSize()
    };
    V_RETURN(m_pd3dDevice->CreateComputePipelineState(&csoDesc, IID_PPV_ARGS(&psoBuffer.PSO)));

    m_aPSOs["waves_disturb"] = std::move(psoBuffer);

    return hr;
}

HRESULT LandAndWavesApp::BuildPSOs() {

    HRESULT hr;
    ComPtr<ID3DBlob> pErrorBlob;
    ComPtr<ID3DBlob> VSBuffer, PSBuffer;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
    PipelineStateBuffer psoBuffer;

    D3D12_BLEND_DESC waterBlendDesc;
    D3D12_BLEND_DESC fenceBlendDesc = {};

    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    D3D_SHADER_MACRO defines[] = {
        { "NUM_DIR_LIGHTS", "1" },
        { "NUM_POINT_LIGHTS", "0" },
        { "NUM_SPOT_LIGHTS", "0" },
        { NULL, 0 },
    };
    D3D_SHADER_MACRO defDisplacementMap[] = {
        { "NUM_DIR_LIGHTS", "1" },
        { "NUM_POINT_LIGHTS", "0" },
        { "NUM_SPOT_LIGHTS", "0" },
        { "_WAVES_DISPLACEMENT_MAP", "1" },
        { NULL, 0 },
    };

    std::vector< D3D12_INPUT_ELEMENT_DESC> aInputLayout;

    pErrorBlob = nullptr;
    V_RETURN(d3dUtils::CompileShaderFromFile(TEXT("Shaders/LandAndWaves.hlsl"), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VS", "vs_5_0", compileFlags, 0, VSBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
    if (pErrorBlob) {
        DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
        V_RETURN(E_FAIL);
    }

    pErrorBlob = nullptr;
    V_RETURN(d3dUtils::CompileShaderFromFile(TEXT("Shaders/LandAndWaves.hlsl"), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PS", "ps_5_0", compileFlags, 0, PSBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
    if (pErrorBlob) {
        DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
        V_RETURN(E_FAIL);
    }

    /// Create the input layout.
    aInputLayout = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    RootSignatureGenerator signatureGen;

    signatureGen.AddDescriptorTable(
      { RootSignatureGenerator::ComposeConstBufferViewRange(1, 0) }
    );
    signatureGen.AddDescriptorTable(
      { RootSignatureGenerator::ComposeConstBufferViewRange(1, 1)}
    );
    signatureGen.AddDescriptorTable(
      { RootSignatureGenerator::ComposeConstBufferViewRange(1, 2) }
    );
    signatureGen.AddDescriptorTable(
      { RootSignatureGenerator::ComposeShaderResourceViewRange(1, 0) }
    );
    signatureGen.AddDescriptorTable(
      { RootSignatureGenerator::ComposeShaderResourceViewRange(1, 1)},
      D3D12_SHADER_VISIBILITY_PIXEL
    );

    auto staticSamplers = GetStaticSamplers();
    signatureGen.AddStaticSamples(
      (UINT)staticSamplers.size(),
      staticSamplers.data()
    );
    V_RETURN(signatureGen.Generate(m_pd3dDevice, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
      &psoBuffer.RootSignature));

    ZeroMemory(&psoDesc, sizeof(psoDesc));
    psoDesc.InputLayout = { aInputLayout.data(), (UINT)aInputLayout.size() };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.pRootSignature = psoBuffer.RootSignature.Get();
    psoDesc.VS = {
       VSBuffer->GetBufferPointer(),
       VSBuffer->GetBufferSize()
    };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.PS = {
        PSBuffer->GetBufferPointer(),
        PSBuffer->GetBufferSize()
    };
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = m_BackBufferFormat;
    psoDesc.SampleDesc = GetMsaaSampleDesc();
    psoDesc.DSVFormat = m_DepthStencilBufferFormat;
    psoBuffer.PSO = nullptr;
    V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc,
        IID_PPV_ARGS(&psoBuffer.PSO)));

    m_aPSOs["land"] = psoBuffer;

    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

    fenceBlendDesc.AlphaToCoverageEnable = FALSE;
    fenceBlendDesc.IndependentBlendEnable = FALSE;
    fenceBlendDesc.RenderTarget[0].BlendEnable = TRUE;
    fenceBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    fenceBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    fenceBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    fenceBlendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    fenceBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    fenceBlendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    fenceBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.BlendState = fenceBlendDesc;
    psoBuffer.PSO = nullptr;
    V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc,
        IID_PPV_ARGS(&psoBuffer.PSO)));

    m_aPSOs["fence"] = psoBuffer;

    pErrorBlob = nullptr;
    V_RETURN(d3dUtils::CompileShaderFromFile(TEXT("Shaders/LandAndWaves.hlsl"), defDisplacementMap, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VS", "vs_5_0", compileFlags, 0, VSBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
    if (pErrorBlob) {
        DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
        V_RETURN(E_FAIL);
    }

    ZeroMemory(&waterBlendDesc, sizeof(waterBlendDesc));
    waterBlendDesc.AlphaToCoverageEnable = FALSE;
    waterBlendDesc.IndependentBlendEnable = FALSE;
    waterBlendDesc.RenderTarget[0].BlendEnable = TRUE;
    waterBlendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    waterBlendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    waterBlendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    waterBlendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    waterBlendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    waterBlendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    waterBlendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    psoDesc.BlendState = waterBlendDesc;

    psoDesc.pRootSignature = psoBuffer.RootSignature.Get();
    psoDesc.VS = {
        VSBuffer->GetBufferPointer(),
        VSBuffer->GetBufferSize()
    };
    psoBuffer.PSO = nullptr;
    V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc,
        IID_PPV_ARGS(&psoBuffer.PSO)));

    m_aPSOs["water"] = psoBuffer;

    return hr;
}

HRESULT LandAndWavesApp::BuildTreeBillBoardPSO() {
    HRESULT hr;
    ComPtr<ID3DBlob> pErrorBlob;
    ComPtr<ID3DBlob> VSBuffer, PSBuffer, GSBuffer;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;
    PipelineStateBuffer psoBuffer;
    D3D12_RASTERIZER_DESC rsDesc;
    D3D12_BLEND_DESC blendDesc;

    UINT compileFlags = 0;
#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif
    D3D_SHADER_MACRO defines[] = {
        { "NUM_DIR_LIGHTS", "1" },
        { "NUM_POINT_LIGHTS", "0" },
        { "NUM_SPOT_LIGHTS", "0" },
        { NULL, 0 },
    };

    std::vector< D3D12_INPUT_ELEMENT_DESC> aInputLayout;

    pErrorBlob = nullptr;
    V_RETURN(d3dUtils::CompileShaderFromFile(TEXT("Shaders/TreeBillBoard.hlsl"), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VS", "vs_5_0", compileFlags, 0, VSBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
    if (pErrorBlob) {
        DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
        V_RETURN(E_FAIL);
    }

    pErrorBlob = nullptr;
    V_RETURN(d3dUtils::CompileShaderFromFile(TEXT("Shaders/TreeBillBoard.hlsl"), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "GS", "gs_5_0", compileFlags, 0, GSBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
    if (pErrorBlob) {
        DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
        V_RETURN(E_FAIL);
    }

    pErrorBlob = nullptr;
    V_RETURN(d3dUtils::CompileShaderFromFile(TEXT("Shaders/TreeBillBoard.hlsl"), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PS", "ps_5_0", compileFlags, 0, PSBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
    if (pErrorBlob) {
        DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
        V_RETURN(E_FAIL);
    }

    /// Create the input layout.
    aInputLayout = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    psoBuffer.RootSignature = m_aPSOs["land"].RootSignature;

    rsDesc = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    //rsDesc.FrontCounterClockwise = TRUE;

    ZeroMemory(&blendDesc, sizeof(blendDesc));
    blendDesc.AlphaToCoverageEnable = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    ZeroMemory(&psoDesc, sizeof(psoDesc));
    psoDesc.InputLayout = { aInputLayout.data(), (UINT)aInputLayout.size() };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    psoDesc.pRootSignature = psoBuffer.RootSignature.Get();
    psoDesc.VS = {
       VSBuffer->GetBufferPointer(),
       VSBuffer->GetBufferSize()
    };
    psoDesc.RasterizerState = rsDesc;
    psoDesc.GS = {
        GSBuffer->GetBufferPointer(),
        GSBuffer->GetBufferSize()
    };
    psoDesc.PS = {
        PSBuffer->GetBufferPointer(),
        PSBuffer->GetBufferSize()
    };
    psoDesc.BlendState = blendDesc;
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = m_BackBufferFormat;
    psoDesc.SampleDesc = GetMsaaSampleDesc();
    psoDesc.DSVFormat = m_DepthStencilBufferFormat;
    psoBuffer.PSO = nullptr;
    V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc,
        IID_PPV_ARGS(&psoBuffer.PSO)));

    m_aPSOs["treesprite"] = psoBuffer;

    return hr;
}

HRESULT LandAndWavesApp::BuildFrameResourcess() {
    HRESULT hr;
    int nRitemCount = 0;

    for (auto &riLayer : m_aRitems) {
        nRitemCount += riLayer.size();
    }

    for (auto &frs : m_aFrameResources) {
        V_RETURN(frs.CreateCommmandAllocator(m_pd3dDevice));
        V_RETURN(frs.CreateBuffers(m_pd3dDevice, 1, nRitemCount));
    }

    V_RETURN(FrameResources::CreateStaticBuffers(m_pd3dDevice, (INT)m_aMaterials.size()));

    return hr;
}

void LandAndWavesApp::OnFrameMoved(float fTime, float fElapsedTime) {
    FrameResources *pFrameResources;

    m_iCurrentFrameIndex = (m_iCurrentFrameIndex + 1) % s_iNumberOfFrames;
    pFrameResources = &m_aFrameResources[m_iCurrentFrameIndex];

    /// Sychronize it.
    if (pFrameResources->FenceCount != 0 && m_pd3dFence->GetCompletedValue() <
        pFrameResources->FenceCount) {

        if (!m_hFenceEvent)
            m_hFenceEvent = CreateEventEx(NULL, NULL, 0,EVENT_ALL_ACCESS);

        m_pd3dFence->SetEventOnCompletion(pFrameResources->FenceCount, m_hFenceEvent);
        WaitForSingleObject(m_hFenceEvent, INFINITE);
    }

    /// Update the buffers up to this time point.

    pFrameResources = &m_aFrameResources[m_iCurrentFrameIndex];

    // Pass constants.
    UpdatePassCBs(pFrameResources, fTime, fElapsedTime);

    /// Object constants.
    UpdateObjectCBs(pFrameResources, fTime, fElapsedTime);
}

void LandAndWavesApp::UpdatePassCBs(FrameResources *pFrameResources, float fTime, float fElapsedTime) {
    /// Pass constants.
    PassConstants passConstants;
    XMMATRIX M;
    XMFLOAT3 vDirection;

    M = XMMatrixTranspose(m_Camera.GetViewProj());

    XMStoreFloat4x4(&passConstants.ViewProj, M);

    passConstants.EyePosW = m_Camera.GetEyePosW();

    vDirection.x = -cos(m_fSunTheta) * sin(m_fSunPhi);
    vDirection.z = -sin(m_fSunTheta) * sin(m_fSunPhi);
    vDirection.y = -cos(m_fSunPhi);

    passConstants.AmbientStrength = m_aLights.AmbientStrength;
    passConstants.Lights[0] = m_aLights.Lights[0];
    passConstants.Lights[1] = m_aLights.Lights[1];
    passConstants.Lights[2] = m_aLights.Lights[2];

    pFrameResources->PassCBs.CopyData(&passConstants, sizeof(PassConstants), 0);
}

void LandAndWavesApp::UpdateObjectCBs(FrameResources *pFrameResource, float fTime, float fElapsedTime) {
    ObjectConstants objConstants;
    FLOAT du, dv;
    XMVECTOR Det;
    XMMATRIX M;

    du = m_aRitems[Waves][0].matTexTransform(3, 0);
    dv = m_aRitems[Waves][0].matTexTransform(3, 1);
    du += 0.1f * fTime;
    dv += 0.02f*fTime;
    if (du >= 1.0f)
        du -= 1.0f;
    if (dv >= 1.0f)
        dv -= 1.0f;

    m_aRitems[Waves][0].matTexTransform(3, 0) = du;
    m_aRitems[Waves][0].matTexTransform(3, 1) = dv;
    m_aRitems[Waves][0].iNumFramesDirty += 1;

    for (auto &riLayer : m_aRitems) {
        for (auto &ri : riLayer) {
            if (ri.iNumFramesDirty > 0) {
                M = XMLoadFloat4x4(&ri.matWorld);

                XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(M));

                M.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
                Det = XMMatrixDeterminant(M);
                M = XMMatrixInverse(&Det, M);

                XMStoreFloat4x4(&objConstants.WorldInvTranspose, M);

                objConstants.WavesSpatialStep = m_pWaves->SpatialStep();

                M = XMLoadFloat4x4(&ri.matTexTransform);
                XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(M));

                pFrameResource->ObjectCBs.CopyData(&objConstants, sizeof(objConstants), ri.uCBIndex);
                ri.iNumFramesDirty--;
            }
        }
    }
}

void LandAndWavesApp::UpdateMaterialCBs() {
    MaterialConstants matConstants;

    for (auto &mat : m_aMaterials) {
        matConstants.DiffuseAlbedo = mat.second->DiffuseAlbedo;
        matConstants.FresnelR0 = mat.second->FresnelR0;
        matConstants.Roughness = mat.second->Roughness;
        XMStoreFloat4x4(&matConstants.MatTransform,
            XMMatrixTranspose(XMLoadFloat4x4(&mat.second->MatTransform)));
        mat.second->NumFrameDirty--;

        FrameResources::MaterialCB.CopyData(&matConstants, sizeof(matConstants),
            mat.second->CBIndex);
    }
}

void LandAndWavesApp::OnRenderFrame(float fTime, float fElapsedTime) {

    HRESULT hr;
    static FLOAT t_base;
    CD3DX12_GPU_DESCRIPTOR_HANDLE heapHandle0, heapHandle;
    PipelineStateBuffer *pPSObuffer;

    auto pFrameResources = &m_aFrameResources[m_iCurrentFrameIndex];

    V(pFrameResources->CmdListAlloc->Reset());

    m_pd3dCommandList->Reset(pFrameResources->CmdListAlloc, NULL);

    m_pd3dCommandList->SetDescriptorHeaps(1, &m_pCbvSrvUavHeap);

    /// Update waves.
    if (fElapsedTime - t_base >= 0.25f) {
        int i;
        int j;
        FLOAT r;

        t_base += 0.25f;

        i = 4 + rand() % (m_pWaves->RowCount() - 8);
        j = 4 + rand() % (m_pWaves->ColumnCount() - 8);

        r = 1.0f + 1.0f * rand() / RAND_MAX;

        pPSObuffer = &m_aPSOs["waves_disturb"];

        m_pWaves->Disturb(m_pd3dCommandList, pPSObuffer->RootSignature.Get(), pPSObuffer->PSO.Get(), i, j, r);
    }

    pPSObuffer = &m_aPSOs["waves_update"];
    m_pWaves->Update(fTime, fElapsedTime, m_pd3dCommandList, pPSObuffer->RootSignature.Get(), pPSObuffer->PSO.Get());

    m_pd3dCommandList->RSSetViewports(1, &m_ScreenViewport);
    m_pd3dCommandList->RSSetScissorRects(1, &m_ScissorRect);

    // Indicate a state transition on the resource usage.
    PrepareNextFrame();

    pPSObuffer = &m_aPSOs["land"];
    /// One and only root signature.
    m_pd3dCommandList->SetGraphicsRootSignature(pPSObuffer->RootSignature.Get());

    /// Clear the render target view and depth stencil view.
    m_pd3dCommandList->ClearRenderTargetView(CurrentBackBufferView(), m_aRTVDefaultClearValue.Color, 0, nullptr);
    m_pd3dCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    m_pd3dCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), TRUE, &DepthStencilView());

    heapHandle0 = m_pCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
    heapHandle = heapHandle0;
    heapHandle.Offset(pFrameResources->PassHeapIndex, m_uCbvSrvUavDescriptorSize);
    m_pd3dCommandList->SetGraphicsRootDescriptorTable(0, heapHandle);
    m_pd3dCommandList->SetGraphicsRootDescriptorTable(3, m_pWaves->DisplacementMap());

    pPSObuffer = &m_aPSOs["land"];
    m_pd3dCommandList->SetPipelineState(pPSObuffer->PSO.Get());
    DrawRenderItem(pFrameResources, m_pd3dCommandList, &m_aRitems[Solid][0]);

    pPSObuffer = &m_aPSOs["fence"];
    m_pd3dCommandList->SetPipelineState(pPSObuffer->PSO.Get());

    DrawRenderItem(pFrameResources, m_pd3dCommandList, &m_aRitems[Solid][1]);

    pPSObuffer = &m_aPSOs["water"];
    m_pd3dCommandList->SetPipelineState(pPSObuffer->PSO.Get());
    DrawRenderItem(pFrameResources, m_pd3dCommandList, &m_aRitems[Waves][0]);

    /// Tree sprite.
    pPSObuffer = &m_aPSOs["treesprite"];
    m_pd3dCommandList->SetPipelineState(pPSObuffer->PSO.Get());
    DrawRenderItem(pFrameResources, m_pd3dCommandList, &m_aRitems[TreeSprite][0]);

    EndRenderFrame();

    // Done recording commands.
    m_pd3dCommandList->Close();

    // Add the command list to the queue for execution.
    ID3D12CommandList *cmdList[] = { m_pd3dCommandList };
    m_pd3dCommandQueue->ExecuteCommandLists(1, cmdList);

    this->Present();

    pFrameResources->FenceCount = ++m_FenceCount;

    m_pd3dCommandQueue->Signal(m_pd3dFence, pFrameResources->FenceCount);
}

void LandAndWavesApp::DrawRenderItem(
    FrameResources *pFrameResources,
    ID3D12GraphicsCommandList *pd3dCommandList,
    RenderItem *ri) {

    CD3DX12_GPU_DESCRIPTOR_HANDLE heapHandle0, heapHandle;

    heapHandle0 = m_pCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();

    pd3dCommandList->IASetPrimitiveTopology(ri->PrimitiveType);
    pd3dCommandList->IASetVertexBuffers(0, 1, &ri->pMeshBuffer->VertexBufferView());

    if (ri->PrimitiveType == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST) {
        pd3dCommandList->IASetIndexBuffer(&ri->pMeshBuffer->IndexBufferView());
    }

    heapHandle = heapHandle0;
    heapHandle.Offset(ri->uCbvHeapIndex, m_uCbvSrvUavDescriptorSize);
    pd3dCommandList->SetGraphicsRootDescriptorTable(1, heapHandle);

    heapHandle = heapHandle0;
    heapHandle.Offset(ri->pMaterial->CbvHeapIndex, m_uCbvSrvUavDescriptorSize);
    pd3dCommandList->SetGraphicsRootDescriptorTable(2, heapHandle);

    heapHandle = heapHandle0;
    heapHandle.Offset(ri->pMaterial->DiffuseMap->SrvHeapIndex, m_uCbvSrvUavDescriptorSize);
    pd3dCommandList->SetGraphicsRootDescriptorTable(4, heapHandle);

    if (ri->PrimitiveType == D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST) {
        pd3dCommandList->DrawIndexedInstanced(ri->uIndexCount, 1, ri->uStartIndexLocation,
            ri->iBaseVertexLocation, 0);
    } else {
        pd3dCommandList->DrawInstanced(ri->uIndexCount, 1, ri->iBaseVertexLocation, ri->uStartIndexLocation);
    }
}

void LandAndWavesApp::OnKeyEvent(int downUp, unsigned short key, int repeatCnt) {

    float dt = 0.0001f;

    if (key == VK_LEFT)
        m_fSunTheta -= 1.0f * dt;
    if (key == VK_RIGHT)
        m_fSunTheta += 1.0f * dt;
    if (key == VK_UP)
        m_fSunPhi -= 1.0f * dt;
    if (key == VK_DOWN)
        m_fSunPhi += 1.0f *dt;

    m_fSunPhi = std::max(0.1f, m_fSunPhi);
    m_fSunPhi = std::min(m_fSunPhi, XM_PIDIV2);
}

void LandAndWavesApp::OnMouseButtonEvent(UI_MOUSE_BUTTON_EVENT ev, UI_MOUSE_VIRTUAL_KEY key, int x, int y) {
    switch (ev) {
    case UI_WM_LBUTTONDOWN:
    case UI_WM_RBUTTONDOWN:
        m_ptLastMousePos.x = x;
        m_ptLastMousePos.y = y;
        BeginCaptureWindowInput();
        break;
    case UI_WM_LBUTTONUP:
    case UI_WM_RBUTTONUP:
        EndCaptureWindowInput();
        break;
    }
}

void LandAndWavesApp::OnMouseMove(UI_MOUSE_VIRTUAL_KEY keys, int x, int y) {
  if (keys & UI_MK_LBUTTON) {
      // Make each pixel correspond to a quarter of a degree.
      float dx = XMConvertToRadians(0.25f*static_cast<float>(x - m_ptLastMousePos.x));
      float dy = XMConvertToRadians(0.25f*static_cast<float>(y - m_ptLastMousePos.y));
      m_Camera.Rotate(dx, dy);
  }
  m_ptLastMousePos.x = x;
  m_ptLastMousePos.y = y;
}

void LandAndWavesApp::OnMouseWheel(UI_MOUSE_VIRTUAL_KEY keys, int delta, int x, int y) {
  m_Camera.Scale(0.02f*delta, 5.0f, 150.0f);
}

void LandAndWavesApp::OnResizeFrame(int cx, int cy) {
    m_Camera.SetProjMatrix(0.25f * XM_PI, GetAspectRatio(), 1.0f, 1000.0f);
}

float LandAndWavesApp::GetHillsHeight(float x, float z) const {
    return 0.3f*(z*sinf(0.1f*x) + x * cosf(0.1f*z));
}

XMFLOAT3 LandAndWavesApp::GetHillsNormals(float x, float z) const {
    // n = (-df/dx, 1, -df/dz)
    XMFLOAT3 n(
        -0.03f*z*cosf(0.1f*x) - 0.3f*cosf(0.1f*z),
        1.0f,
        -0.3f*sinf(0.1f*x) + 0.03f*x*sinf(0.1f*z));

    XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
    XMStoreFloat3(&n, unitNormal);

    return n;
}
