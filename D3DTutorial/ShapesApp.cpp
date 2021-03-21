#include "D3D12App.h"
#include "Camera.h"
#include <DirectXColors.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include "GeometryGenerator.h"
#include <wrl.h>
#include "MeshBuffer.h"
#include "RenderItem.h"
#include "UploadBuffer.h"
#include "LightUtils.h"
#include "Material.h"
#include <map>
#include <array>
#include <fstream>
#include "Texture.h"
#include "CubeMapRenderTarget.h"
#include "ShadowMap.h"
#include <ppl.h>
#include "RootSignatureGenerator.h"

using namespace DirectX;
using namespace Microsoft::WRL;

extern D3D12App *CreateShapesApp(HINSTANCE hInstance);

int main() {

    HRESULT hr;
    D3D12App *pTheApp;


    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    pTheApp = CreateShapesApp(NULL);
    V(pTheApp->Initialize());
    if (FAILED(hr)) {
        SAFE_DELETE(pTheApp);
        return hr;
    }

    hr = pTheApp->Run();
    SAFE_DELETE(pTheApp);

    return hr;
}

extern std::array<const CD3DX12_STATIC_SAMPLER_DESC, 7> GetStaticSamplers();

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

namespace ShapesAppInternal {

    struct PassConstants {
        XMFLOAT4X4 ViewProj;
        XMFLOAT3 EyePosW;
        FLOAT HeightScale;
        FLOAT MinTessDistance;
        FLOAT MaxTessDistance;
        FLOAT MinTessFactor;
        FLOAT MaxTessFactor;
        XMFLOAT4X4 LightVPT;
        XMFLOAT4 AmbientStrength;
        Light Lights[16];
    };

    struct ObjectConstants {
        XMFLOAT4X4 World;
        XMFLOAT4X4 WorldInvTranspose;
        XMFLOAT4X4 TexTransform;

        UINT MaterialIndex;
    };

    struct ShadowPassConstants {
        XMFLOAT4X4 ViewProj;
    };

    struct MaterialConstants {
        XMFLOAT4 DiffuseAlbedo;
        XMFLOAT3 FresnelR0;
        FLOAT Roughness;
        XMFLOAT4X4 MatTransform;

        UINT DiffuseMapIndex;
        UINT NormalMapIndex;
        UINT Padding0[2];
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
            V_RETURN(MaterialCB.CreateBuffer(pd3dDevice, iMatCount, sizeof(MaterialConstants), FALSE));
            return hr;
        }

        static HRESULT CreateShadowPassBuffers(ID3D12Device *pd3dDevice) {
            HRESULT hr;

            V_RETURN(ShadowPassCBs.CreateBuffer(pd3dDevice, 1, sizeof(ShadowPassConstants), TRUE));
            return hr;
        }

        ID3D12CommandAllocator *CmdListAlloc = nullptr;
        UploadBuffer PassCBs;
        UploadBuffer ObjectCBs;
        static UploadBuffer MaterialCB;

        static UploadBuffer ShadowPassCBs;

        UINT64 FenceCount = 0;
    };

    UploadBuffer FrameResources::MaterialCB;
    UploadBuffer FrameResources::ShadowPassCBs;
};

using namespace ShapesAppInternal;

class ShapesApp : public D3D12App {
public:
    ShapesApp(HINSTANCE hIntance);
    ~ShapesApp();

    HRESULT Initialize() override;

private:
    UINT GetExtraRTVDescriptorCount() const override;
    UINT GetExtraDSVDescriptorCount() const override;

    HRESULT LoadTextures();
    HRESULT BuildGeometry();
    HRESULT BuildSkyDomeGeometry();
    HRESULT BuildSkyPSO();
    HRESULT BuildPSOs();
    HRESULT BuildShadowDepthPSO();
    HRESULT BuildFrameResourcess();
    HRESULT BuildDescriptorHeaps();

    HRESULT BuildCubeMapCamera(XMFLOAT3 vCenter);
    HRESULT BuildCubeMapRenderTargets();
    HRESULT BuildShadowDepthMapRenderTargets();

    VOID ComputeSceneWorldAABB();

    void PostInitialize();

    void UpdateMaterialCBs();
    void UpdateObjectCBs(FrameResources *pFrameResources, float fTime, float fElapsedTime);

    void UpdateShadowCBs(FrameResources *pFrameResources, float fTime, float fElapsedTime);
    void UpdatePassCBs(FrameResources *pFrameResources, float fTime, float fElapsedTime);

    void UpdateLightViewProj();

    virtual void Update(float fTime, float fElapsedTime);
    virtual void RenderFrame(float fTime, float fElapsedTime);
    virtual LRESULT OnKeyEvent(WPARAM wParam, LPARAM lParam);
    virtual LRESULT OnMouseEvent(UINT uMsg, WPARAM wParam, int x, int y);
    virtual LRESULT OnResize();

    void RenderSceneToCubeMap(FrameResources *pFrameResources);
    void RenderSceneToShadowDepthMap(FrameResources *pFrameResources);

    void DrawRenderItem(ID3D12GraphicsCommandList *pd3dCommandList,
        FrameResources *pFrameResources, std::vector<RenderItem> &ri, BOOL bRenderShadowDepth);
    void DrawSkyDomeRenderItem(ID3D12GraphicsCommandList *p3dCommandList,
        FrameResources *pFrameResources, RenderItem *ri);

    ID3D12DescriptorHeap *m_pCbvSrvUavHeap;

    std::map<std::string, Material*> m_aMaterials;
    Lights m_aLights;
    std::map<std::string, Texture *> m_aTextures;

    enum {
        Ground = 0,
        Opaque,
        OpaqueDynamicReflectors,
        Sky,
    };

    std::vector<RenderItem> m_aRitems[4];
    RenderItem *m_pSkullRitem;

    DirectX::BoundingBox m_aWorldAABBB;

    DirectX::XMFLOAT4X4 m_matLightViewProj;
    DirectX::XMFLOAT4X4 m_matLightViewProjTex;

    CubeMapRenderTarget *m_pCubeMapRenderTarget = nullptr;
    Camera m_CubeMapCamera[6];

    ShadowMap *m_pShadowMap = nullptr;

    std::map<std::string, PipelineStateBuffer> m_aPSOs;

    static const int s_iNumberOfFrames = 3;
    FrameResources m_aFrameResources[s_iNumberOfFrames];
    int m_iCurrentFrameIndex = 0;

    DirectX::XMFLOAT3 mBaseLightDirections[3] = {
        DirectX::XMFLOAT3(0.57735f, -0.57735f, 0.57735f),
        DirectX::XMFLOAT3(-0.57735f, -0.57735f, 0.57735f),
        DirectX::XMFLOAT3(0.0f, -0.707f, -0.707f)
    };
    DirectX::XMFLOAT3 mRotatedLightDirections[3];
    float mLightRotationAngle = .0f;

    Camera m_Camera;
    POINT m_ptLastMousePos;
};

D3D12App *CreateShapesApp(HINSTANCE hInstance) {
    return new ShapesApp(hInstance);
}

ShapesApp::ShapesApp(HINSTANCE hInstance)
    : D3D12App(hInstance) {

    m_Camera.SetPosition(0.0f, 2.0f, -15.0f);
    m_Camera.UpdateViewMatrix();

    /// Materials.
    auto bricks0 = new Material;
    bricks0->Name = "bricks0";
    bricks0->CBIndex = 0;
    bricks0->DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    bricks0->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    bricks0->Roughness = 0.3f;
    m_aMaterials.insert(std::make_pair(bricks0->Name, bricks0));

    auto bricks1 = new Material;
    bricks1->Name = "bricks1";
    bricks1->CBIndex = 1;
    bricks1->DiffuseAlbedo = { 1.0f, 1.0f, 1.0f, 1.0f };
    bricks1->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    bricks1->Roughness = 0.4f;
    m_aMaterials.insert(std::make_pair(bricks1->Name, bricks1));

    auto tile0 = new Material;
    tile0->Name = "tile0";
    tile0->CBIndex = 2;
    tile0->DiffuseAlbedo = { 0.9f, 0.9f, 0.9f, 1.0f };
    tile0->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
    tile0->Roughness = 0.1f;
    m_aMaterials.insert(std::make_pair(tile0->Name, tile0));

    auto stone0 = new Material;
    stone0->Name = "mirror0";
    stone0->CBIndex = 3;
    stone0->DiffuseAlbedo = { 0.0f, 0.0f, 0.1f, 1.0f };
    stone0->FresnelR0 = XMFLOAT3(0.98f, 0.97f, 0.95f);
    stone0->Roughness = 0.1f;
    m_aMaterials.insert(std::make_pair(stone0->Name, stone0));

    auto skullMat = new Material;
    skullMat->Name = "skull0";
    skullMat->CBIndex = 4;
    skullMat->DiffuseAlbedo = { 0.8f, 0.8f, 0.8f, 1.0f };
    skullMat->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
    skullMat->Roughness = 0.2f;
    m_aMaterials.insert(std::make_pair(skullMat->Name, skullMat));

    m_aLights.AmbientStrength = XMFLOAT4(0.25f, 0.25f, 0.35f, 1.0f);
    m_aLights.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
    m_aLights.Lights[0].Strength = { 0.9f, 0.8f, 0.7f };
    m_aLights.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
    m_aLights.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };
    m_aLights.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
    m_aLights.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

    m_aDeviceConfig.MsaaEnabled = TRUE;
    memcpy(m_aRTVDefaultClearValue.Color, Colors::LightSteelBlue, sizeof(m_aRTVDefaultClearValue.Color));
}

ShapesApp::~ShapesApp() {
    SAFE_RELEASE(m_pCbvSrvUavHeap);

    for (auto &mat : m_aMaterials) {
        SAFE_RELEASE(mat.second);
    }

    for (auto &ri : m_aTextures) {
        SAFE_RELEASE(ri.second);
    }

    SAFE_DELETE(m_pCubeMapRenderTarget);
    SAFE_DELETE(m_pShadowMap);
}

HRESULT ShapesApp::Initialize() {
    HRESULT hr;

    V_RETURN(__super::Initialize());

    // Reset the command list to prep for initialization commands.
    V_RETURN(m_pd3dCommandList->Reset(m_pd3dDirectCmdAlloc, nullptr));

    m_pShadowMap = new ShadowMap(
        m_pd3dDevice,
        m_pd3dCommandList,
        2048,
        2048
    );

    V_RETURN(LoadTextures());
    V_RETURN(BuildGeometry());
    V_RETURN(BuildSkyDomeGeometry());
    V_RETURN(BuildDescriptorHeaps());
    V_RETURN(BuildCubeMapCamera({ 0.0f, 2.0f, 0.0f }));
    V_RETURN(BuildCubeMapRenderTargets());
    V_RETURN(BuildShadowDepthMapRenderTargets());
    V_RETURN(BuildPSOs());
    V_RETURN(BuildSkyPSO());
    V_RETURN(BuildShadowDepthPSO());
    V_RETURN(BuildFrameResourcess());

    ComputeSceneWorldAABB();

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

void ShapesApp::PostInitialize() {
    /// Get riddle of upload buffers.
    for (auto &RiLayer : m_aRitems) {
        for(auto &ri : RiLayer)
            ri.pMeshBuffer->DisposeUploaders();
    }

    for (auto &ri : m_aTextures) {
        ri.second->DisposeUploaders();
    }
}

void ShapesApp::UpdateLightViewProj() {

    XMFLOAT3 vCorners[8];
    XMVECTOR vLightTarget, vLightPos, vLightDirection, vLightUp;
    float l, r, b, t, n, f;
    XMMATRIX matLightView, matLightProj, matTex, matVP, matVPT;
    XMVECTOR vMin, vMax, vTemp;
    auto depthMapDimension = m_pShadowMap->GetShadowMapDimension();
    XMVECTOR vDepthMapDimension;

    vLightTarget = XMLoadFloat3(&m_aWorldAABBB.Center);
    vLightDirection = XMLoadFloat3(&mRotatedLightDirections[0]);
    vLightPos = vLightTarget - vLightDirection;

    vLightUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    matLightView = XMMatrixLookAtLH(vLightPos, vLightTarget, vLightUp);

    m_aWorldAABBB.GetCorners(vCorners);

    vMin = XMVectorSet(D3D12_FLOAT32_MAX, D3D12_FLOAT32_MAX, D3D12_FLOAT32_MAX, 1.0f);
    vMax = XMVectorSet(-D3D12_FLOAT32_MAX, -D3D12_FLOAT32_MAX, -D3D12_FLOAT32_MAX, 1.0f);
    for (auto &v : vCorners) {
        vTemp = XMLoadFloat3(&v);

        vTemp = XMVector3TransformCoord(vTemp, matLightView);

        vMin = XMVectorMin(vMin, vTemp);
        vMax = XMVectorMax(vMax, vTemp);
    }

    vDepthMapDimension = XMVectorSet(depthMapDimension.first, depthMapDimension.second, 1.0f, 1.0f);

    /// Prevent jigger.
    vMin = (XMVectorMultiply(XMVectorDivide(vMin, vDepthMapDimension), vDepthMapDimension));
    vMax = (XMVectorMultiply(XMVectorDivide(vMax, vDepthMapDimension), vDepthMapDimension));

    l = XMVectorGetX(vMin);
    r = XMVectorGetX(vMax);
    b = XMVectorGetY(vMin);
    t = XMVectorGetY(vMax);
    n = XMVectorGetZ(vMin);
    f = XMVectorGetZ(vMax);

    matLightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

    matVP = matLightView * matLightProj;

    matTex = XMMatrixSet(
        0.5f, 0.0f, 0.0f, 0.0f,
        0.0f, -0.5f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.5f, 0.5f, 0.0f, 1.0f
    );

    matVPT = matVP * matTex;

    XMStoreFloat4x4(&m_matLightViewProj, matVP);
    XMStoreFloat4x4(&m_matLightViewProjTex, matVPT);
}

HRESULT ShapesApp::LoadTextures() {
    HRESULT hr;
    Texture *pTexture;
    const char *texNames[] = {
        "skyCubeMap",

        "brickDiffuseMap",
        "brickNormalMap",
        "brickDiffuseMap2",
        "brickNormalMap2",
        "tileDiffuseMap",
        "tileNormalMap",
        "defaultDiffuseMap",
        "defaultNormalMap",
    };
    const wchar_t *texFilePaths[] = {
        LR"(Media/Textures/grasscube1024.dds)",

        LR"(Media/Textures/bricks.dds)",
        LR"(Media/Textures/bricks_nmap.dds)",
        LR"(Media/Textures/bricks2.dds)",
        LR"(Media/Textures/bricks2_nmap.dds)",
        LR"(Media/Textures/tile.dds)",
        LR"(Media/Textures/tile_nmap.dds)",
        LR"(Media/Textures/white1x1.dds)",
        LR"(Media/Textures/default_nmap.dds)",
    };
    int i;

    for (i = 0; i < _countof(texNames); ++i) {
        pTexture = new Texture(texNames[i]);
        pTexture->SrvHeapIndex = i;
        V_RETURN(pTexture->CreateTextureFromDDSFile(m_pd3dDevice, m_pd3dCommandList, texFilePaths[i]));
        m_aTextures.insert({ pTexture->Name, pTexture });
    }

    m_aMaterials["bricks0"]->SetDiffuseMap(m_aTextures["brickDiffuseMap"]);
    m_aMaterials["bricks0"]->SetNormalMap(m_aTextures["brickNormalMap"]);
    m_aMaterials["bricks1"]->SetDiffuseMap(m_aTextures["brickDiffuseMap2"]);
    m_aMaterials["bricks1"]->SetNormalMap(m_aTextures["brickNormalMap2"]);
    m_aMaterials["tile0"]->SetDiffuseMap(m_aTextures["tileDiffuseMap"]);
    m_aMaterials["tile0"]->SetNormalMap(m_aTextures["tileNormalMap"]);
    m_aMaterials["mirror0"]->SetDiffuseMap(m_aTextures["defaultDiffuseMap"]);
    m_aMaterials["mirror0"]->SetNormalMap(m_aTextures["defaultNormalMap"]);
    m_aMaterials["skull0"]->SetDiffuseMap(m_aTextures["defaultDiffuseMap"]);
    m_aMaterials["skull0"]->SetNormalMap(m_aTextures["defaultNormalMap"]);

    return hr;
}

HRESULT ShapesApp::BuildGeometry() {
    HRESULT hr;
    UINT uNextCBIndex = 0;
    UINT iMaterialIndex = 0;


    using Vertex = GeometryGenerator::Vertex;
    using Vertices = std::vector<Vertex>;

    auto __compute_mesh_aabb = [](Vertices::const_iterator first, Vertices::const_iterator last)->BoundingBox {

        XMVECTOR vMin, vMax;
        XMVECTOR vCenter, vExtents;
        BoundingBox bb;

        vMin = XMVectorSet(D3D12_FLOAT32_MAX, D3D12_FLOAT32_MAX, D3D12_FLOAT32_MAX, 1.0);
        vMax = XMVectorSet(-D3D12_FLOAT32_MAX, -D3D12_FLOAT32_MAX, -D3D12_FLOAT32_MAX, 1.0);
        concurrency::parallel_for_each(first, last, [&](const auto &v) {
            auto v2 = XMLoadFloat3(&v.Position);
            vMin = XMVectorMin(vMin, v2);
            vMax = XMVectorMax(vMax, v2);
        });

        vCenter = XMVectorLerp(vMin, vMax, 0.5f);
        vExtents = XMVectorSubtract(vMax, vCenter);

        XMStoreFloat3(&bb.Center, vCenter);
        XMStoreFloat3(&bb.Extents, vExtents);

        return bb;
    };

    do {
        GeometryGenerator gen;
        GeometryGenerator::MeshData grid, box, sphere, cylinder;
        std::vector<GeometryGenerator::MeshData> vertices;
        std::vector<uint32_t> indices;
        UINT uVertexByteStide;
        UINT uIndexByteStride;
        RenderItem ri, sphRitem, cylRitem;
        int i;
        MeshBuffer *pBoxBuffer, *pGridBuffer, *pSphereBuffer, *pCylinderBuffer;
        BoundingBox AABB;

        grid = gen.CreateGrid(20.0f, 30.0, 60, 40);
        box = gen.CreateBox(1.5f, 0.5f, 1.5f, 0);
        sphere = gen.CreateSphere(0.5f, 20, 20);
        cylinder = gen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

        uVertexByteStide = sizeof(GeometryGenerator::Vertex);
        uIndexByteStride = sizeof(uint16_t);

        V_RETURN(CreateMeshBuffer(&pBoxBuffer));

        AABB = __compute_mesh_aabb(box.Vertices.begin(), box.Vertices.end());

        pBoxBuffer->CreateVertexBuffer(
          m_pd3dDevice,
          m_pd3dCommandList,
          box.Vertices.data(),
          box.Vertices.size(),
          uVertexByteStide,
          &AABB
        );
        pBoxBuffer->CreateIndexBuffer(
          m_pd3dDevice,
          m_pd3dCommandList,
          box.GetIndices16().data(),
          box.GetIndices16().size(),
          uIndexByteStride
        );

        V_RETURN(CreateMeshBuffer(&pGridBuffer));
        AABB = __compute_mesh_aabb(grid.Vertices.begin(), grid.Vertices.end());
        pGridBuffer->CreateVertexBuffer(
          m_pd3dDevice,
          m_pd3dCommandList,
          grid.Vertices.data(),
          grid.Vertices.size(),
          uVertexByteStide,
          &AABB
        );
        pGridBuffer->CreateIndexBuffer(
          m_pd3dDevice,
          m_pd3dCommandList,
          grid.GetIndices16().data(),
          grid.GetIndices16().size(),
          uIndexByteStride
        );

        V_RETURN(CreateMeshBuffer(&pSphereBuffer));
        AABB = __compute_mesh_aabb(sphere.Vertices.begin(), sphere.Vertices.end());
        pSphereBuffer->CreateVertexBuffer(
          m_pd3dDevice,
          m_pd3dCommandList,
          sphere.Vertices.data(),
          sphere.Vertices.size(),
          uVertexByteStide,
          &AABB
        );
        pSphereBuffer->CreateIndexBuffer(
          m_pd3dDevice,
          m_pd3dCommandList,
          sphere.GetIndices16().data(),
          sphere.GetIndices16().size(),
          uIndexByteStride
        );

        V_RETURN(CreateMeshBuffer(&pCylinderBuffer));
        AABB = __compute_mesh_aabb(cylinder.Vertices.begin(), cylinder.Vertices.end());
        pCylinderBuffer->CreateVertexBuffer(
          m_pd3dDevice,
          m_pd3dCommandList,
          cylinder.Vertices.data(),
          cylinder.Vertices.size(),
          uVertexByteStide,
          &AABB
        );
        pCylinderBuffer->CreateIndexBuffer(
          m_pd3dDevice,
          m_pd3dCommandList,
          cylinder.GetIndices16().data(),
          cylinder.GetIndices16().size(),
          uIndexByteStride
        );

        ri.SetMeshBuffer(pGridBuffer);
        ri.SetMaterial(m_aMaterials["tile0"]);
        XMStoreFloat4x4(&ri.matTexTransform, XMMatrixScaling(8.0f, 8.0f, 0.0f));
        ri.uIndexCount = (UINT)grid.GetIndices16().size();
        ri.uStartIndexLocation = 0;
        ri.iNumFramesDirty = s_iNumberOfFrames;
        ri.iBaseVertexLocation = 0;
        ri.uCBIndex = uNextCBIndex++;
        m_aRitems[Ground].push_back(ri);

        ri.SetMeshBuffer(pBoxBuffer);
        ri.SetMaterial(m_aMaterials["bricks1"]);
        XMStoreFloat4x4(&ri.matTexTransform, XMMatrixScaling(2.0f, 1.0f, 0.0f));
        XMStoreFloat4x4(&ri.matWorld, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
        ri.uIndexCount = (UINT)box.GetIndices16().size();
        ri.uStartIndexLocation = 0;
        ri.iBaseVertexLocation = 0;
        ri.uCBIndex = uNextCBIndex++;
        m_aRitems[Opaque].push_back(ri);

        ri.SetMeshBuffer(pSphereBuffer);
        ri.SetMaterial(m_aMaterials["mirror0"]);
        XMStoreFloat4x4(&ri.matWorld, XMMatrixScaling(2.0f, 2.0f, 2.0f)*XMMatrixTranslation(0.0f, 2.0f, 0.0f));
        XMStoreFloat4x4(&ri.matTexTransform, XMMatrixIdentity());
        ri.uIndexCount = (UINT)sphere.GetIndices16().size();
        ri.uStartIndexLocation = 0;
        ri.iBaseVertexLocation = 0;
        ri.uCBIndex = uNextCBIndex++;
        m_aRitems[OpaqueDynamicReflectors].push_back(ri);

        sphRitem.SetMeshBuffer(pSphereBuffer);
        sphRitem.SetMaterial(m_aMaterials["mirror0"]);
        sphRitem.uIndexCount = (UINT)sphere.GetIndices16().size();
        sphRitem.iNumFramesDirty = s_iNumberOfFrames;
        sphRitem.uStartIndexLocation = 0;;

        cylRitem.SetMeshBuffer(pCylinderBuffer);
        cylRitem.SetMaterial(m_aMaterials["bricks0"]);
        cylRitem.uIndexCount = (UINT)cylinder.GetIndices16().size();
        cylRitem.iNumFramesDirty = s_iNumberOfFrames;
        cylRitem.uStartIndexLocation = 0;

        for (i = 0; i < 5; ++i) {
            XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
            XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

            XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
            XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

            XMStoreFloat4x4(&sphRitem.matWorld, leftSphereWorld);
            sphRitem.uCBIndex = uNextCBIndex++;
            m_aRitems[Opaque].push_back(sphRitem);

            XMStoreFloat4x4(&sphRitem.matWorld, rightSphereWorld);
            sphRitem.uCBIndex = uNextCBIndex++;
            m_aRitems[Opaque].push_back(sphRitem);

            XMStoreFloat4x4(&cylRitem.matWorld, leftCylWorld);
            cylRitem.uCBIndex = uNextCBIndex++;
            m_aRitems[Opaque].push_back(cylRitem);

            XMStoreFloat4x4(&cylRitem.matWorld, rightCylWorld);
            cylRitem.uCBIndex = uNextCBIndex++;
            m_aRitems[Opaque].push_back(cylRitem);
        }

        SAFE_RELEASE(pBoxBuffer);
        SAFE_RELEASE(pGridBuffer);
        SAFE_RELEASE(pSphereBuffer);
        SAFE_RELEASE(pCylinderBuffer);
    } while (0);

    if (1) {
        /// Load the skull model.
        MeshBuffer *pSkullBuffer;
        struct Vertex {
            XMFLOAT3 v;
            XMFLOAT3 normal;
            XMFLOAT2 tex;
        };
        std::vector<Vertex> skullVertices;
        std::vector<uint32_t> skullIndices;

        WCHAR szModelPath[MAX_PATH];
        std::ifstream fin;

        float vbuff[6];
        uint32_t ibuff[3];
        uint32_t vcount, tcount;
        auto vit = skullVertices.begin();
        auto vitEnd = skullVertices.end();
        auto iit = skullIndices.begin();
        auto iitEnd = skullIndices.end();
        RenderItem skullRitem;

        XMVECTOR vMin, vMax, vTemp;
        XMVECTOR vCenter, vExtents;
        BoundingBox AABB;

        V_RETURN(FindDemoMediaFileAbsPath(L"Media/Models/Skull.dat", _countof(szModelPath), szModelPath));

        fin.open(szModelPath, std::ios::binary);
        if (!fin) {
            V_RETURN(E_INVALIDARG);
        }

        fin.read((char *)&vcount, sizeof(uint32_t));
        fin.read((char *)&tcount, sizeof(uint32_t));
        skullVertices.resize(vcount);
        vit = skullVertices.begin();
        vitEnd = skullVertices.end();

        vMin = XMVectorSet(D3D12_FLOAT32_MAX, D3D12_FLOAT32_MAX, D3D12_FLOAT32_MAX, 1.0);
        vMax = XMVectorSet(-D3D12_FLOAT32_MAX, -D3D12_FLOAT32_MAX, -D3D12_FLOAT32_MAX, 1.0);

        for (; vit != vitEnd; ++vit) {
            fin.read((char *)&vbuff, 6 * sizeof(float));
            vit->v = *(XMFLOAT3 *)vbuff;
            vit->normal = *(XMFLOAT3 *)(vbuff + 3);
            vit->tex = XMFLOAT2{ 0.0f, 0.0f };

            vTemp = XMLoadFloat3(&vit->v);

            vMin = XMVectorMin(vMin, vTemp);
            vMax = XMVectorMax(vMax, vTemp);
        }

        vCenter = XMVectorLerp(vMin, vMax, 0.5f);
        vExtents = XMVectorSubtract(vMax, vCenter);
        XMStoreFloat3(&AABB.Center, vCenter);
        XMStoreFloat3(&AABB.Extents, vExtents);

        skullIndices.resize(tcount * 3);
        iit = skullIndices.begin();
        iitEnd = skullIndices.end();
        for (; iit != iitEnd; iit += 3) {
            fin.read((char *)ibuff, sizeof(ibuff));
            memcpy(&*iit, ibuff, sizeof(ibuff));
        }
        fin.close();

        V_RETURN(CreateMeshBuffer(&pSkullBuffer));
        pSkullBuffer->CreateVertexBuffer(
          m_pd3dDevice,
          m_pd3dCommandList,
          skullVertices.data(),
          skullVertices.size(),
          sizeof(Vertex),
          &AABB
        );
        pSkullBuffer->CreateIndexBuffer(
          m_pd3dDevice,
          m_pd3dCommandList,
          skullIndices.data(),
          skullIndices.size(),
          sizeof(UINT32)
        );

        skullRitem.SetMeshBuffer(pSkullBuffer);
        skullRitem.SetMaterial(m_aMaterials["skull0"]);
        skullRitem.uCBIndex = uNextCBIndex++;
        skullRitem.iNumFramesDirty = s_iNumberOfFrames;
        skullRitem.uIndexCount = skullIndices.size();
        skullRitem.uStartIndexLocation = 0;

        m_aRitems[Opaque].push_back(skullRitem);

        m_pSkullRitem = &m_aRitems[Opaque].back();

        SAFE_RELEASE(pSkullBuffer);
    }

    return hr;
}

HRESULT ShapesApp::BuildSkyDomeGeometry() {
    GeometryGenerator gen;
    GeometryGenerator::MeshData dome;
    MeshBuffer *pMeshBuffer;
    HRESULT hr;
    RenderItem ri;
    UINT iNextCBIndex = 0;

    dome = gen.CreateSphere(5000.0f, 40, 10);

    V_RETURN(CreateMeshBuffer(&pMeshBuffer));
    pMeshBuffer->CreateVertexBuffer(
      m_pd3dDevice,
      m_pd3dCommandList,
      dome.Vertices.data(),
      dome.Vertices.size(),
      sizeof(GeometryGenerator::Vertex),
      nullptr
    );
    pMeshBuffer->CreateIndexBuffer(
      m_pd3dDevice,
      m_pd3dCommandList,
      dome.Indices32.data(),
      dome.Indices32.size(),
      sizeof(UINT32)
    );

    ri.SetMeshBuffer(pMeshBuffer);
    ri.iNumFramesDirty = s_iNumberOfFrames;
    ri.uIndexCount = dome.Indices32.size();
    ri.iNumFramesDirty = s_iNumberOfFrames;

    for (auto &RiLayer : m_aRitems) {
        for (auto &ri : RiLayer) {
            iNextCBIndex = std::max(ri.uCBIndex + 1, iNextCBIndex);
        }
    }

    ri.uCBIndex = iNextCBIndex;

    m_aRitems[Sky].push_back(std::move(ri));

    SAFE_RELEASE(pMeshBuffer);

    return hr;
}

VOID ShapesApp::ComputeSceneWorldAABB() {

    XMVECTOR vCenter, vExtents;
    XMMATRIX matWorld;
    XMVECTOR vMin, vMax, vMin2, vMax2;
    BoundingBox AABB;
    int i;

    vMin2 = XMVectorSet(D3D12_FLOAT32_MAX, D3D12_FLOAT32_MAX, D3D12_FLOAT32_MAX, 1.0);
    vMax2 = XMVectorSet(-D3D12_FLOAT32_MAX, -D3D12_FLOAT32_MAX, -D3D12_FLOAT32_MAX, 1.0);

    for (i = 0; i < _countof(m_aRitems); ++i) {
        auto &riLayer = m_aRitems[i];

        if (i == Sky)
            continue;

        for (auto &ri : riLayer) {

          AABB = ri.pMeshBuffer->GetBoundingBox();
            vCenter = XMLoadFloat3(&AABB.Center);
            vExtents = XMLoadFloat3(&AABB.Extents);

            matWorld = XMLoadFloat4x4(&ri.matWorld);

            vCenter = XMVector3TransformCoord(vCenter, matWorld);
            vExtents = XMVector3TransformNormal(vExtents, matWorld);

            vMin = XMVectorSubtract(vCenter, vExtents);
            vMax = XMVectorAdd(vCenter, vExtents);

            vMin2 = XMVectorMin(vMin2, vMin);
            vMax2 = XMVectorMax(vMax2, vMax);
        }
    }

    vCenter = XMVectorLerp(vMin2, vMax2, 0.5f);
    vExtents = XMVectorSubtract(vMax2, vCenter);

    XMStoreFloat3(&m_aWorldAABBB.Center, vCenter);
    XMStoreFloat3(&m_aWorldAABBB.Extents, vExtents);
}

HRESULT ShapesApp::BuildCubeMapCamera(XMFLOAT3 vCenter) {
    FLOAT x = vCenter.x,
          y = vCenter.y,
          z = vCenter.z;
    FLOAT r = 1.0f;
    int i;

    XMFLOAT3 vTargets[] = {
        { x + r, y, z},   /// +X
        { x - r, y, z},   /// -X
        { x, y + r, z},    /// +Y
        { x, y - r, z},    /// -Y
        { x, y, z + r},    /// +Z
        { x, y, z - r},    /// -Z
    };

    XMFLOAT3 vUps[] = {
        {0.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, -1.0f},
        {0.0f, 0.0f, +1.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
    };

    for (i = 0; i < 6; ++i) {
        m_CubeMapCamera[i].LookAt(vCenter, vTargets[i], vUps[i]);
        m_CubeMapCamera[i].SetLens(0.5f*XM_PI, 1.0f, 0.1f, 1000.0f);
        m_CubeMapCamera[i].UpdateViewMatrix();
    }

    return S_OK;
}

UINT ShapesApp::GetExtraRTVDescriptorCount() const {
    return 6;
}

UINT ShapesApp::GetExtraDSVDescriptorCount() const {
    /// one for dynamic cube map;
    /// one for shadow depth map;
    return 1 + 1;
}

HRESULT ShapesApp::BuildCubeMapRenderTargets() {
    HRESULT hr;
    CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuRtv, hCpuDsv;
    CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv;
    CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv;

    m_pCubeMapRenderTarget = new CubeMapRenderTarget(
        m_pd3dDevice, m_pd3dCommandList, 512, 512, DXGI_FORMAT_R8G8B8A8_UNORM,
        (const FLOAT *)Colors::LightSteelBlue, 1.0f);

    hCpuRtv = m_pRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    hCpuRtv.Offset(s_iSwapChainBufferCount, m_uRtvDescriptorSize);
    hCpuDsv = m_pDSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    hCpuDsv.Offset(1, m_uDsvDescriptorSize);

    hCpuSrv = m_pCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart();
    hCpuSrv.Offset(m_aTextures.size(), m_uCbvSrvUavDescriptorSize);

    hGpuSrv = m_pCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
    hGpuSrv.Offset(m_aTextures.size(), m_uCbvSrvUavDescriptorSize);

    V_RETURN(m_pCubeMapRenderTarget->BuildDescriptorHandles(
        hCpuRtv,
        m_uRtvDescriptorSize,
        hCpuDsv,
        hCpuSrv,
        hGpuSrv
    ));

    return hr;
}

HRESULT ShapesApp::BuildShadowDepthMapRenderTargets() {

    CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDsv;
    CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv;
    CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv;

    hCpuDsv = m_pDSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    hCpuDsv.Offset(2, m_uDsvDescriptorSize);

    hCpuSrv = m_pCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart();
    hCpuSrv.Offset(m_aTextures.size() + 1, m_uCbvSrvUavDescriptorSize);

    hGpuSrv = m_pCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
    hGpuSrv.Offset(m_aTextures.size() + 1, m_uCbvSrvUavDescriptorSize);

    m_pShadowMap->CreateDescriptors(
        hCpuDsv,
        hCpuSrv,
        hGpuSrv
    );

    return S_OK;
}

HRESULT ShapesApp::BuildSkyPSO() {
    HRESULT hr;
    ComPtr<ID3DBlob> pErrorBlob;
    ComPtr<ID3DBlob> VSBuffer, PSBuffer;
    UINT compileFlags = 0;
    std::vector<D3D12_INPUT_ELEMENT_DESC> aInputLayout;
    PipelineStateBuffer psoBuffer;

#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    V_RETURN(d3dUtils::CompileShaderFromFile(TEXT("Shaders/Sky.hlsl"), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VS", "vs_5_1", compileFlags, 0, VSBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
    if (pErrorBlob) {
        DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
        V_RETURN(E_FAIL);
    }

    pErrorBlob = nullptr;
    V_RETURN(d3dUtils::CompileShaderFromFile(TEXT("Shaders/Sky.hlsl"), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PS", "ps_5_1", compileFlags, 0, PSBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
    if (pErrorBlob) {
        DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
        V_RETURN(E_FAIL);
    }

    psoBuffer.RootSignature = m_aPSOs["solid"].RootSignature;

    /// Create the input layout.
    aInputLayout = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { aInputLayout.data(), (UINT)aInputLayout.size() };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.pRootSignature = psoBuffer.RootSignature.Get();
    psoDesc.VS = {
        VSBuffer->GetBufferPointer(),
        VSBuffer->GetBufferSize()
    };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_FRONT;
    psoDesc.PS = {
        PSBuffer->GetBufferPointer(),
        PSBuffer->GetBufferSize()
    };
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = m_BackBufferFormat;
    psoDesc.SampleDesc = GetMsaaSampleDesc();
    psoDesc.DSVFormat = m_DepthStencilBufferFormat;
    V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc,
        IID_PPV_ARGS(&psoBuffer.PSO)));

    m_aPSOs["sky"] = psoBuffer;

    psoDesc.RTVFormats[0] = m_pCubeMapRenderTarget->RenderTargetFormat();
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.DSVFormat = m_pCubeMapRenderTarget->DepthStencilFormat();
    psoBuffer.PSO = nullptr;
    V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc,
        IID_PPV_ARGS(&psoBuffer.PSO)));

    m_aPSOs["cubemap_sky"] = psoBuffer;

    return hr;
}

HRESULT ShapesApp::BuildPSOs() {
  HRESULT hr;
  ComPtr<ID3DBlob> pErrorBlob;
  ComPtr<ID3DBlob> VSBuffer,
    HSBuffer,
    DSBuffer,
    PSBuffer, PSNoEnvMapBuffer;
  UINT compileFlags = 0;
  std::vector<D3D12_INPUT_ELEMENT_DESC> aInputLayout;
  PipelineStateBuffer psoBuffer;
  D3D_SHADER_MACRO defines[] = {
  { "NUM_DIR_LIGHTS", "3" },
  { "NUM_POINT_LIGHTS", "0" },
  { "NUM_SPOT_LIGHTS", "0" },
  { NULL, 0 },
  };
  D3D_SHADER_MACRO noEnvMapDefines[] = {
      { "NUM_DIR_LIGHTS", "3" },
      { "NUM_POINT_LIGHTS", "0" },
      { "NUM_SPOT_LIGHTS", "0" },
      { "_USE_DIFFUSE_ENV_MAP", "0" },
      { NULL, 0 },
  };

#if defined(_DEBUG)
  compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

  V_RETURN(d3dUtils::CompileShaderFromFile(TEXT("Shaders/Shapes.hlsl"), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
    "VS", "vs_5_1", compileFlags, 0, VSBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
  if (pErrorBlob) {
    DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  V_RETURN(d3dUtils::CompileShaderFromFile(TEXT("Shaders/Shapes.hlsl"), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
    "HS", "hs_5_1", compileFlags, 0, HSBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
  if (pErrorBlob) {
    DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  V_RETURN(d3dUtils::CompileShaderFromFile(TEXT("Shaders/Shapes.hlsl"), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
    "DS", "ds_5_1", compileFlags, 0, DSBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
  if (pErrorBlob) {
    DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  pErrorBlob = nullptr;
  V_RETURN(d3dUtils::CompileShaderFromFile(TEXT("Shaders/Shapes.hlsl"), defines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
    "PS", "ps_5_1", compileFlags, 0, PSBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
  if (pErrorBlob) {
    DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  pErrorBlob = nullptr;
  V_RETURN(d3dUtils::CompileShaderFromFile(TEXT("Shaders/Shapes.hlsl"), noEnvMapDefines, D3D_COMPILE_STANDARD_FILE_INCLUDE,
    "PS", "ps_5_1", compileFlags, 0, PSNoEnvMapBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
  if (pErrorBlob) {
    DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  /// Root signatures.
  RootSignatureGenerator signatureGen;

  signatureGen.AddConstBufferView(0);
  signatureGen.AddConstBufferView(1);
  signatureGen.AddShaderResourceView(0);
  signatureGen.AddDescriptorTable(
    {
      RootSignatureGenerator::ComposeShaderResourceViewRange(1, 1, 0)
    },
    D3D12_SHADER_VISIBILITY_PIXEL
  );
  signatureGen.AddDescriptorTable(
    {
      RootSignatureGenerator::ComposeShaderResourceViewRange(1, 2, 0)
    },
    D3D12_SHADER_VISIBILITY_PIXEL
  );
  signatureGen.AddDescriptorTable(
    {
      RootSignatureGenerator::ComposeShaderResourceViewRange(8, 0, 1)
    }
  );
  auto staticSamplers = GetStaticSamplers();
  signatureGen.AddStaticSamples(
    (UINT)staticSamplers.size(),
    staticSamplers.data()
  );

  V_RETURN(signatureGen.Generate(m_pd3dDevice, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
    psoBuffer.RootSignature.GetAddressOf()));

    /// Create the input layout.
    aInputLayout = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TANGENTU", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = { aInputLayout.data(), (UINT)aInputLayout.size() };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH/*D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE*/;
    psoDesc.pRootSignature = psoBuffer.RootSignature.Get();
    psoDesc.VS = {
        VSBuffer->GetBufferPointer(),
        VSBuffer->GetBufferSize()
    };
    psoDesc.HS = {
        HSBuffer->GetBufferPointer(),
        HSBuffer->GetBufferSize()
    };
    psoDesc.DS = {
        DSBuffer->GetBufferPointer(),
        DSBuffer->GetBufferSize()
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
    V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc,
        IID_PPV_ARGS(&psoBuffer.PSO)));

    m_aPSOs["solid"] = psoBuffer;

    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.RTVFormats[0] = m_pCubeMapRenderTarget->RenderTargetFormat();
    psoDesc.DSVFormat = m_pCubeMapRenderTarget->DepthStencilFormat();
    psoBuffer.PSO = nullptr;
    V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc,
        IID_PPV_ARGS(&psoBuffer.PSO)));

    m_aPSOs["cubemap"] = psoBuffer;

    psoDesc.PS = {
        PSNoEnvMapBuffer->GetBufferPointer(),
        PSNoEnvMapBuffer->GetBufferSize()
    };
    psoDesc.SampleDesc = GetMsaaSampleDesc();
    psoDesc.RTVFormats[0] = m_BackBufferFormat;
    psoDesc.DSVFormat = m_DepthStencilBufferFormat;
    psoBuffer.PSO = nullptr;
    V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc,
        IID_PPV_ARGS(&psoBuffer.PSO)));

    m_aPSOs["solid_no_env_map"] = psoBuffer;

    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.RTVFormats[0] = m_pCubeMapRenderTarget->RenderTargetFormat();
    psoDesc.DSVFormat = m_pCubeMapRenderTarget->DepthStencilFormat();
    psoBuffer.PSO = nullptr;
    V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc,
        IID_PPV_ARGS(&psoBuffer.PSO)));

    m_aPSOs["cubemap_no_env_map"] = psoBuffer;

    return hr;
}

HRESULT ShapesApp::BuildShadowDepthPSO() {

    HRESULT hr;
    ComPtr<ID3DBlob> pErrorBlob;
    ComPtr<ID3DBlob> VSBuffer, DebugVSBuffer, DebugPSBuffer;
    ComPtr<ID3DBlob> PSNoEnvMapBuffer;
    UINT compileFlags = 0;
    D3D12_INPUT_ELEMENT_DESC aInputLayout;
    PipelineStateBuffer psoBuffer;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc;

#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    V_RETURN(d3dUtils::CompileShaderFromFile(TEXT("Shaders/ShadowDepth.hlsl"), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VS", "vs_5_1", compileFlags, 0, VSBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
    if (pErrorBlob) {
        DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
        V_RETURN(E_FAIL);
    }

    V_RETURN(d3dUtils::CompileShaderFromFile(TEXT("Shaders/ShadowDepth.hlsl"), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "ShadowDepthDebugVS", "vs_5_1", compileFlags, 0, DebugVSBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
    if (pErrorBlob) {
        DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
        V_RETURN(E_FAIL);
    }

    V_RETURN(d3dUtils::CompileShaderFromFile(TEXT("Shaders/ShadowDepth.hlsl"), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "ShadowDepthDebugPS", "ps_5_1", compileFlags, 0, DebugPSBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
    if (pErrorBlob) {
        DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
        V_RETURN(E_FAIL);
    }

    psoBuffer.RootSignature = m_aPSOs["solid"].RootSignature;

    aInputLayout =
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0};

    ZeroMemory(&psoDesc, sizeof(psoDesc));
    psoDesc.InputLayout = { &aInputLayout, 1 };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.pRootSignature = psoBuffer.RootSignature.Get();
    psoDesc.VS = {
        VSBuffer->GetBufferPointer(),
        VSBuffer->GetBufferSize()
    };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.RasterizerState.DepthBias = 100000;
    psoDesc.RasterizerState.DepthBiasClamp = 0.0f;
    psoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.NumRenderTargets = 0;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;
    psoDesc.DSVFormat = m_pShadowMap->GetDepthFormat();
    V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc,
        IID_PPV_ARGS(&psoBuffer.PSO)));

    m_aPSOs["shadowdepth"] = psoBuffer;

    psoDesc.InputLayout = { nullptr, 0 };
    psoDesc.VS = {
        DebugVSBuffer->GetBufferPointer(),
        DebugVSBuffer->GetBufferSize()
    };
    psoDesc.PS = {
        DebugPSBuffer->GetBufferPointer(),
        DebugPSBuffer->GetBufferSize()
    };
    psoDesc.SampleDesc = GetMsaaSampleDesc();
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = m_BackBufferFormat;
    psoDesc.DSVFormat = m_DepthStencilBufferFormat;
    psoBuffer.PSO = nullptr;
    V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc,
        IID_PPV_ARGS(&psoBuffer.PSO)));

    m_aPSOs["debugshadow"] = psoBuffer;

    return hr;
}

HRESULT ShapesApp::BuildFrameResourcess() {
    HRESULT hr;

    for (auto &frs : m_aFrameResources) {
        V_RETURN(frs.CreateCommmandAllocator(m_pd3dDevice));
        V_RETURN(frs.CreateBuffers(m_pd3dDevice, 6 + 1,
            m_aRitems[Ground].size()+ m_aRitems[Opaque].size() + m_aRitems[OpaqueDynamicReflectors].size()
            + m_aRitems[Sky].size()));
    }

    FrameResources::CreateStaticBuffers(m_pd3dDevice, m_aMaterials.size());
    FrameResources::CreateShadowPassBuffers(m_pd3dDevice);

    return hr;
}

HRESULT ShapesApp::BuildDescriptorHeaps() {
    HRESULT hr;
    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    UINT numDescriptors;
    CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle, cpuHandle2;
    D3D12_RESOURCE_DESC texDesc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    UINT iHeapIndex = 0;
    UINT iMaterialIndex = 0;

    numDescriptors = m_aTextures.size() + 2;

    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.NodeMask = 0;
    cbvHeapDesc.NumDescriptors = numDescriptors;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    V_RETURN(m_pd3dDevice->CreateDescriptorHeap(
        &cbvHeapDesc,
        IID_PPV_ARGS(&m_pCbvSrvUavHeap)
    ));

    cpuHandle = m_pCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart();

    for (auto &tex : m_aTextures) {
        texDesc = tex.second->Resource->GetDesc();
        srvDesc.Format = texDesc.Format;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (texDesc.DepthOrArraySize == 6) {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.TextureCube.MipLevels = texDesc.MipLevels;
            srvDesc.TextureCube.MostDetailedMip = 0;
            srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;
        } else {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels = texDesc.MipLevels;
            srvDesc.Texture2D.PlaneSlice = 0;
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
        }

        cpuHandle2 = cpuHandle;
        cpuHandle2.Offset(tex.second->SrvHeapIndex, m_uCbvSrvUavDescriptorSize);

        m_pd3dDevice->CreateShaderResourceView(tex.second->Resource, &srvDesc, cpuHandle2);
    }

    return hr;
}

void ShapesApp::UpdateMaterialCBs() {
    MaterialConstants matConstants;

    for (auto &mat : m_aMaterials) {
        matConstants.DiffuseAlbedo = mat.second->DiffuseAlbedo;
        matConstants.FresnelR0 = mat.second->FresnelR0;
        matConstants.Roughness = mat.second->Roughness;
        XMStoreFloat4x4(&matConstants.MatTransform,
            XMMatrixTranspose(XMLoadFloat4x4(&mat.second->MatTransform)));
        matConstants.DiffuseMapIndex = mat.second->DiffuseMap->SrvHeapIndex - 1;
        matConstants.NormalMapIndex = mat.second->NormalMap->SrvHeapIndex - 1;

        mat.second->NumFrameDirty--;

        FrameResources::MaterialCB.CopyData(&matConstants, sizeof(matConstants),
            mat.second->CBIndex);
    }
}

void ShapesApp::UpdateObjectCBs(FrameResources *pFrameResources, float fTime, float fElapsedTime) {
    /// Object constants.
    XMMATRIX M;
    XMVECTOR Det;
    ObjectConstants objConstants;
    int i = 0;

    for(auto &RiLayer : m_aRitems) {
        for (auto &ri : RiLayer) {
            if (ri.iNumFramesDirty > 0) {

                M = XMLoadFloat4x4(&ri.matWorld);

                XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(M));

                M.r[3] = XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f);
                Det = XMMatrixDeterminant(M);
                M = XMMatrixInverse(&Det, M);

                XMStoreFloat4x4(&objConstants.WorldInvTranspose, M);

                M = XMLoadFloat4x4(&ri.matTexTransform);
                M = XMMatrixTranspose(M);
                XMStoreFloat4x4(&objConstants.TexTransform, M);

                if(ri.pMaterial)
                    objConstants.MaterialIndex = ri.pMaterial->CBIndex;

                pFrameResources->ObjectCBs.CopyData(&objConstants, sizeof(ObjectConstants), ri.uCBIndex);
                ri.iNumFramesDirty--;
            }
        }
    }
}

void ShapesApp::UpdateShadowCBs(FrameResources *pFrameResource, float fTime, float fElapsedTime) {
    ShadowPassConstants passConstants;
    XMMATRIX matVP;

    matVP = XMLoadFloat4x4(&m_matLightViewProj);
    XMStoreFloat4x4(&passConstants.ViewProj, XMMatrixTranspose(matVP));

    FrameResources::ShadowPassCBs.CopyData(&passConstants, sizeof(ShadowPassConstants), 0);
}

void ShapesApp::UpdatePassCBs(FrameResources *pFrameResources, float fTime, float fElapsedTime) {
    /// Pass constants.
    PassConstants passConstants;
    XMMATRIX ViewProj, M;
    Camera *pCamera;
    int i;

    for (i = 0; i < 7; ++i) {
        if (i < 6)
            pCamera = &m_CubeMapCamera[i];
        else
            pCamera = &m_Camera;

        ViewProj = XMMatrixMultiply(pCamera->GetView(), pCamera->GetProj());
        M = XMMatrixTranspose(ViewProj);
        XMStoreFloat4x4(&passConstants.ViewProj, M);
        passConstants.EyePosW = pCamera->GetPosition3f();

        passConstants.HeightScale = 0.05f;
        passConstants.MinTessDistance = 5.0f;
        passConstants.MaxTessDistance = 15.0f;
        passConstants.MinTessFactor = 1.0f;
        passConstants.MaxTessFactor = 3.0f;

        M = XMLoadFloat4x4(&m_matLightViewProjTex);
        XMStoreFloat4x4(&passConstants.LightVPT, XMMatrixTranspose(M));

        passConstants.AmbientStrength = m_aLights.AmbientStrength;
        passConstants.Lights[0].Direction = mRotatedLightDirections[0];
        passConstants.Lights[0].Strength = m_aLights.Lights[0].Strength;
        passConstants.Lights[1].Direction = mRotatedLightDirections[1];
        passConstants.Lights[1].Strength = m_aLights.Lights[1].Strength;
        passConstants.Lights[2].Direction = mRotatedLightDirections[2];
        passConstants.Lights[2].Strength = m_aLights.Lights[2].Strength;

        pFrameResources->PassCBs.CopyData(&passConstants, sizeof(PassConstants), i);
    }
}

void ShapesApp::Update(float fTime, float fElapsedTime) {
    FrameResources *pFrameResources;

    m_iCurrentFrameIndex = (m_iCurrentFrameIndex + 1) % s_iNumberOfFrames;
    pFrameResources = &m_aFrameResources[m_iCurrentFrameIndex];

    /// Sychronize it.
    if (pFrameResources->FenceCount != 0 && m_pd3dFence->GetCompletedValue() <
        pFrameResources->FenceCount) {

        if (!m_hFenceEvent)
            m_hFenceEvent = CreateEventEx(NULL, NULL, 0, EVENT_ALL_ACCESS);

        m_pd3dFence->SetEventOnCompletion(pFrameResources->FenceCount, m_hFenceEvent);
        WaitForSingleObject(m_hFenceEvent, INFINITE);
    }

    XMMATRIX skullScale = XMMatrixScaling(0.2f, 0.2f, 0.2f);
    XMMATRIX skullOffset = XMMatrixTranslation(3.0f, 2.0f, 0.0f);
    XMMATRIX skullLocalRotate = XMMatrixRotationY(2.0f*fElapsedTime);
    XMMATRIX skullGlobalRotate = XMMatrixRotationY(0.5f*fElapsedTime);
    XMStoreFloat4x4(&m_pSkullRitem->matWorld, skullScale*skullLocalRotate*skullOffset*skullGlobalRotate);
    m_pSkullRitem->iNumFramesDirty = s_iNumberOfFrames;

    //
    // Animate the lights (and hence shadows).
    //

    mLightRotationAngle += 0.1f*fTime;

    XMMATRIX R = XMMatrixRotationY(mLightRotationAngle);
    for (int i = 0; i < 3; ++i) {
        XMVECTOR lightDir = XMLoadFloat3(&mBaseLightDirections[i]);
        lightDir = XMVector3TransformNormal(lightDir, R);
        XMStoreFloat3(&mRotatedLightDirections[i], lightDir);
    }

    /// Update the buffers up to this time point.
    UpdateLightViewProj();

    UpdateShadowCBs(pFrameResources, fTime, fElapsedTime);
    UpdatePassCBs(pFrameResources, fTime, fElapsedTime);
    UpdateObjectCBs(pFrameResources, fTime, fElapsedTime);
}

void ShapesApp::RenderFrame(float fTime, float fElapsedTime) {

    HRESULT hr;
    auto pFrameResources = &m_aFrameResources[m_iCurrentFrameIndex];
    CD3DX12_GPU_DESCRIPTOR_HANDLE heapHandle, srvHandle;
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress, cbPassAddress;
    UINT cbPassCBByteSize;
    D3D12_GPU_DESCRIPTOR_HANDLE nullHandle = m_pCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();

    V(pFrameResources->CmdListAlloc->Reset());

    V(m_pd3dCommandList->Reset(pFrameResources->CmdListAlloc, nullptr));

    m_pd3dCommandList->SetDescriptorHeaps(1, &m_pCbvSrvUavHeap);

    /// One and only Root Signature.
    m_pd3dCommandList->SetGraphicsRootSignature(m_aPSOs["solid"].RootSignature.Get());
    m_pd3dCommandList->SetGraphicsRootDescriptorTable(3, nullHandle);
    m_pd3dCommandList->SetGraphicsRootDescriptorTable(4, nullHandle);
    m_pd3dCommandList->SetGraphicsRootDescriptorTable(5, nullHandle);

    RenderSceneToShadowDepthMap(pFrameResources);

    RenderSceneToCubeMap(pFrameResources);

    m_pd3dCommandList->RSSetViewports(1, &m_ScreenViewport);
    m_pd3dCommandList->RSSetScissorRects(1, &m_ScissorRect);

    // Indicate a state transition on the resource usage.
    PrepareNextFrame();

    /// Clear the render target view and depth stencil view.
    m_pd3dCommandList->ClearRenderTargetView(CurrentBackBufferView(), m_aRTVDefaultClearValue.Color, 0, nullptr);
    m_pd3dCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    m_pd3dCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), TRUE, &DepthStencilView());

    m_pd3dCommandList->SetGraphicsRootDescriptorTable(4, m_pShadowMap->Srv());

    /// ShadowMap depth map rendered on a sub area.
    m_pd3dCommandList->SetPipelineState(m_aPSOs["debugshadow"].PSO.Get());
    m_pd3dCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    m_pd3dCommandList->DrawInstanced(4, 1, 0, 0);

    cbAddress = pFrameResources->PassCBs.GetConstBufferAddress();
    cbPassCBByteSize = d3dUtils::CalcConstantBufferByteSize(sizeof(PassConstants));
    cbPassAddress = cbAddress + 6 * cbPassCBByteSize;
    m_pd3dCommandList->SetGraphicsRootConstantBufferView(0, cbPassAddress);

    cbAddress = pFrameResources->MaterialCB.GetConstBufferAddress();
    m_pd3dCommandList->SetGraphicsRootShaderResourceView(2, cbAddress);

    heapHandle = m_pCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
    srvHandle = heapHandle;
    m_pd3dCommandList->SetGraphicsRootDescriptorTable(3, srvHandle);
    m_pd3dCommandList->SetGraphicsRootDescriptorTable(4, m_pShadowMap->Srv());

    srvHandle = heapHandle;
    srvHandle.Offset(1, m_uCbvSrvUavDescriptorSize);
    m_pd3dCommandList->SetGraphicsRootDescriptorTable(5, srvHandle);

    m_pd3dCommandList->SetPipelineState(m_aPSOs["solid_no_env_map"].PSO.Get());
    DrawRenderItem(m_pd3dCommandList, pFrameResources, m_aRitems[Ground], FALSE);

    m_pd3dCommandList->SetPipelineState(m_aPSOs["solid"].PSO.Get());
    DrawRenderItem(m_pd3dCommandList, pFrameResources, m_aRitems[Opaque], FALSE);

    m_pd3dCommandList->SetGraphicsRootDescriptorTable(3, m_pCubeMapRenderTarget->Srv());
    DrawRenderItem(m_pd3dCommandList, pFrameResources, m_aRitems[OpaqueDynamicReflectors], FALSE);

    /// Draw the sky dome.
    auto pSkyDome = &m_aRitems[Sky][0];

    /// Root signature do not change, so just change everything different.
    m_pd3dCommandList->SetPipelineState(m_aPSOs["sky"].PSO.Get());

    srvHandle = heapHandle;
    m_pd3dCommandList->SetGraphicsRootDescriptorTable(3, srvHandle);

    DrawSkyDomeRenderItem(m_pd3dCommandList, pFrameResources, &m_aRitems[Sky][0]);

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

void ShapesApp::RenderSceneToCubeMap(FrameResources *pFrameResources) {

    PipelineStateBuffer *psoBuffer1, *psoBuffer2, *psoBuffer3;
    int i;
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress, cbMatAddress;
    CD3DX12_GPU_DESCRIPTOR_HANDLE heapHandle, srvHandle;
    UINT cbCBByteSize;
    FLOAT clrColor[4] = {};

    m_pd3dCommandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_pCubeMapRenderTarget->Resource(),
            D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET));

    psoBuffer1 = &m_aPSOs["cubemap_no_env_map"];
    psoBuffer2 = &m_aPSOs["cubemap"];
    psoBuffer3 = &m_aPSOs["cubemap_sky"];

    heapHandle = m_pCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();
    cbCBByteSize = d3dUtils::CalcConstantBufferByteSize(sizeof(PassConstants));
    cbAddress = pFrameResources->PassCBs.GetConstBufferAddress();
    cbMatAddress = pFrameResources->MaterialCB.GetConstBufferAddress();

    /// Set the cbvs and srvs that merely change.
    m_pd3dCommandList->SetGraphicsRootShaderResourceView(2, cbMatAddress);

    srvHandle = heapHandle;
    m_pd3dCommandList->SetGraphicsRootDescriptorTable(3, srvHandle);
    m_pd3dCommandList->SetGraphicsRootDescriptorTable(4, m_pShadowMap->Srv());
    srvHandle.Offset(1, m_uCbvSrvUavDescriptorSize);
    m_pd3dCommandList->SetGraphicsRootDescriptorTable(5, srvHandle);

    /// Render the items.
    for (i = 0; i < 6; ++i) {
        m_pd3dCommandList->ClearRenderTargetView(m_pCubeMapRenderTarget->Rtv()[i], Colors::LightSteelBlue, 0, nullptr);
        m_pd3dCommandList->ClearDepthStencilView(m_pCubeMapRenderTarget->Dsv(),
            D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        m_pd3dCommandList->RSSetViewports(1, &m_pCubeMapRenderTarget->Viewport());
        m_pd3dCommandList->RSSetScissorRects(1, &m_pCubeMapRenderTarget->ScissorRect());

        m_pd3dCommandList->OMSetRenderTargets(1, &m_pCubeMapRenderTarget->Rtv()[i],
            true, &m_pCubeMapRenderTarget->Dsv());

        m_pd3dCommandList->SetGraphicsRootConstantBufferView(0, cbAddress);
        cbAddress += cbCBByteSize;

        m_pd3dCommandList->SetPipelineState(psoBuffer1->PSO.Get());

        DrawRenderItem(m_pd3dCommandList, pFrameResources, m_aRitems[Ground], FALSE);

        m_pd3dCommandList->SetPipelineState(psoBuffer2->PSO.Get());

        DrawRenderItem(m_pd3dCommandList, pFrameResources, m_aRitems[Opaque], FALSE);
        DrawRenderItem(m_pd3dCommandList, pFrameResources, m_aRitems[OpaqueDynamicReflectors], FALSE);

        m_pd3dCommandList->SetPipelineState(psoBuffer3->PSO.Get());
        DrawSkyDomeRenderItem(m_pd3dCommandList, pFrameResources, &m_aRitems[Sky][0]);
    }

    m_pd3dCommandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_pCubeMapRenderTarget->Resource(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_GENERIC_READ));
}

void ShapesApp::RenderSceneToShadowDepthMap(FrameResources *pFrameResources) {
    PipelineStateBuffer *psoBuffer;
    int i;

    psoBuffer = &m_aPSOs["shadowdepth"];

    m_pd3dCommandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_pShadowMap->Resource(), D3D12_RESOURCE_STATE_GENERIC_READ,
            D3D12_RESOURCE_STATE_DEPTH_WRITE));

    m_pd3dCommandList->ClearDepthStencilView(m_pShadowMap->Dsv(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 1,
        &m_pShadowMap->ScissorRect());

    m_pd3dCommandList->RSSetViewports(1, &m_pShadowMap->Viewport());
    m_pd3dCommandList->RSSetScissorRects(1, &m_pShadowMap->ScissorRect());

    m_pd3dCommandList->OMSetRenderTargets(0, nullptr, FALSE, &m_pShadowMap->Dsv());

    m_pd3dCommandList->SetPipelineState(psoBuffer->PSO.Get());

    m_pd3dCommandList->SetGraphicsRootConstantBufferView(0,
        FrameResources::ShadowPassCBs.GetConstBufferAddress());

    for (i = 0; i < _countof(m_aRitems); ++i) {
        if (i == Sky)
            continue;

        DrawRenderItem(m_pd3dCommandList, pFrameResources, m_aRitems[i], TRUE);
    }

    m_pd3dCommandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_pShadowMap->Resource(), D3D12_RESOURCE_STATE_DEPTH_WRITE,
            D3D12_RESOURCE_STATE_GENERIC_READ));
}

void ShapesApp::DrawRenderItem(ID3D12GraphicsCommandList *pd3dCommandList,
    FrameResources *pFrameResources, std::vector<RenderItem> &aRitemLayer, BOOL bRenderShadowDepth) {

    D3D12_GPU_VIRTUAL_ADDRESS cbAddress0, cbAddress;
    UINT cbCBByteSize = d3dUtils::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    cbAddress0 = pFrameResources->ObjectCBs.GetConstBufferAddress();

    for (auto &ri : aRitemLayer) {
        pd3dCommandList->IASetVertexBuffers(
            0, 1, &ri.pMeshBuffer->VertexBufferView());
        pd3dCommandList->IASetIndexBuffer(
            &ri.pMeshBuffer->IndexBufferView());
        if (bRenderShadowDepth)
            pd3dCommandList->IASetPrimitiveTopology(ri.PrimitiveType);
        else
            pd3dCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_3_CONTROL_POINT_PATCHLIST);

        cbAddress = cbAddress0 + cbCBByteSize * ri.uCBIndex;

        pd3dCommandList->SetGraphicsRootConstantBufferView(1, cbAddress);

        pd3dCommandList->DrawIndexedInstanced(ri.uIndexCount, 1, ri.uStartIndexLocation,
            ri.iBaseVertexLocation, 0);
    }
}

void ShapesApp::DrawSkyDomeRenderItem(ID3D12GraphicsCommandList *p3dCommandList,
    FrameResources *pFrameResources, RenderItem *ri) {
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress;
    UINT cbObjCBSize = d3dUtils::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    cbAddress = pFrameResources->ObjectCBs.GetConstBufferAddress();
    cbAddress += cbObjCBSize * ri->uCBIndex;

    p3dCommandList->SetGraphicsRootConstantBufferView(1, cbAddress);

    m_pd3dCommandList->IASetPrimitiveTopology(ri->PrimitiveType);
    p3dCommandList->IASetVertexBuffers(0, 1, &ri->pMeshBuffer->VertexBufferView());
    p3dCommandList->IASetIndexBuffer(&ri->pMeshBuffer->IndexBufferView());

    p3dCommandList->DrawIndexedInstanced(ri->uIndexCount, 1, ri->uStartIndexLocation,
        ri->iBaseVertexLocation, 0);
}

LRESULT ShapesApp::OnKeyEvent(WPARAM wParam, LPARAM lParam) {
    float dt = m_GameTimer.DeltaTime();
    switch (wParam) {
    case 'w':
    case 'W':
        m_Camera.Walk(10.0f * dt); break;
    case 'S':
    case's':
        m_Camera.Walk(-10.0f * dt); break;
    case 'A':
    case 'a':
        m_Camera.Strafe(-10.0f * dt); break;
    case 'D':
    case 'd':
        m_Camera.Strafe(10.0f * dt); break;
    }

    m_Camera.UpdateViewMatrix();

    return S_OK;
}

LRESULT ShapesApp::OnMouseEvent(UINT uMsg, WPARAM wParam, int x, int y) {
    switch (uMsg) {
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
        m_ptLastMousePos.x = x;
        m_ptLastMousePos.y = y;
        SetCapture(this->m_hMainWnd);
        break;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
        ReleaseCapture();
    case WM_MOUSEMOVE:
        if (wParam & MK_LBUTTON) {
            // Make each pixel correspond to a quarter of a degree.
            float dx = XMConvertToRadians(0.25f*static_cast<float>(x - m_ptLastMousePos.x));
            float dy = XMConvertToRadians(0.25f*static_cast<float>(y - m_ptLastMousePos.y));

            m_Camera.Pitch(dy);
            m_Camera.RotateY(dx);
            m_Camera.UpdateViewMatrix();

        } else if (wParam & MK_RBUTTON) {
            // Make each pixel correspond to 0.005 unit in the scene.
            float dx = 0.05f*static_cast<float>(x - m_ptLastMousePos.x);
            float dy = 0.05f*static_cast<float>(y - m_ptLastMousePos.y);
        }

        m_ptLastMousePos.x = x;
        m_ptLastMousePos.y = y;
        break;
    }

    return S_OK;
}

LRESULT ShapesApp::OnResize() {
    LRESULT lr;

    lr = __super::OnResize();
    m_Camera.SetLens(0.25f*XM_PI, GetAspectRatio(), 1.0f, 1000.0f);

    return lr;
}

