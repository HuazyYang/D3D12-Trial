#include <windows.h>
#include <D3D12RendererContext.hpp>
#include <Win32Application.hpp>
#include <DirectXMath.h>
#include <DirectXColors.h>
#include <SDKmesh.h>
#include <ResourceUploadBatch.hpp>
#include <array>
#include <RootSignatureGenerator.h>
#include <Camera.h>
#include <UploadBuffer.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>

#undef min
#undef max

// #define UNCOMPRESSED_VERTEX_DATA // The sdkmesh file contained uncompressed vertex data
#define RENDER_SCENE_LIGHT_POV      // F4 toggles between the usual camera and the 0th light's point-of-view

using Microsoft::WRL::ComPtr;
using namespace DirectX;

static const int s_iNumLights  = 4;
static const int s_iNumShadows = 1;
static const int s_iNumMirrors = 4;

//
// Default view parameters
//
static const XMVECTORF32    s_vDefaultEye           = { 30.0f, 150.0f, -150.0f, 0.f };
static const XMVECTORF32    s_vDefaultLookAt        = { 0.0f, 60.0f, 0.0f, 0.f };
static const FLOAT          s_fNearPlane            = 2.0f;
static const FLOAT          s_fFarPlane             = 4000.0f;
static const FLOAT          s_fFOV                  = XM_PI / 4.0f;
static const XMVECTORF32    s_vSceneCenter          = { 0.0f, 350.0f, 0.0f, 0.f };
static const FLOAT          s_fSceneRadius          = 600.0f;
static const FLOAT          s_fDefaultCameraRadius  = 300.0f;
static const FLOAT          s_fMinCameraRadius      = 150.0f;
static const FLOAT          s_fMaxCameraRadius      = 450.0f;

//
// Lights parameters
//
static const XMVECTORF32              s_vAmbientColor = { 0.04f * 0.760f, 0.04f * 0.793f, 0.04f * 0.822f, 1.000f };
static const XMVECTORF32              s_vMirrorTint = { 0.3f, 0.5f, 1.0f, 1.0f };

struct MirrorVertex {
  XMFLOAT3 Position;
  XMFLOAT3 Normal;
  XMFLOAT2 Texcoord;
  XMFLOAT3 Tangent;
};

HRESULT CreateMultithreadRenderingRendererAndInteractor(D3D12RendererContext **ppRenderer,
                                                        WindowInteractor **ppInteractor);

int main() {

  HRESULT hr;
  int ret;
  D3D12RendererContext *pRenderer;
  WindowInteractor *pInteractor;

  V_RETURN(CreateMultithreadRenderingRendererAndInteractor(&pRenderer, &pInteractor));
  ret = RunSample(pRenderer, pInteractor, 800, 600, L"MultithreadRendering");
  SAFE_DELETE(pRenderer);

  return ret;
}

struct PipelineStateTuple {
  ComPtr<ID3D12PipelineState> PSO;
  ComPtr<ID3D12RootSignature> RootSignature;
};

struct CB_PER_OBJECT {
  XMFLOAT4X4 m_mWorld;
  XMFLOAT4 m_vObjectColor;
};

struct CB_PER_LIGHT {
  struct LightDataStruct {
    XMFLOAT4X4 m_mLightViewProj;
    XMFLOAT4 m_vLightPos;
    XMFLOAT4 m_vLightDir;
    XMFLOAT4 m_vLightColor;
    XMFLOAT4 m_vFalloffs; // x = dist end, y = dist range, z = cos angle end, w = cos range
  } m_LightData[s_iNumLights];
};

struct CB_PER_SCENE {
  XMFLOAT4X4 m_mViewProj;
  XMFLOAT4 m_vMirrorPlane;
  XMFLOAT4 m_vAmbientColor;
  XMFLOAT4 m_vTintColor;
};

struct SceneParamsStatic {
  BOOL bRenderShadow;
  PipelineStateTuple *pPipelineStateTuple;
  ID3D12Resource *pShadowTexture; // For rendering shadow map
  D3D12_CPU_DESCRIPTOR_HANDLE hRenderTargetView;
  D3D12_CPU_DESCRIPTOR_HANDLE hDepthStencilView;
  UploadBufferStack *pConstBufferStack; // For allocating constant buffers.

  UINT8 uStencilRef;

  XMFLOAT4 vTintColor;
  XMFLOAT4 vMirrorPlane;
  const D3D12_VIEWPORT *pViewport;
};

struct SceneParamsDynamic {
  XMFLOAT4X4 matViewProj;
};

struct FrameResources {
  ComPtr<ID3D12CommandAllocator> CommandAllocator;

  ComPtr<ID3D12GraphicsCommandList> ShadowCommandList;
  ComPtr<ID3D12CommandAllocator> ShadowCommandAllocator;
  ComPtr<ID3D12GraphicsCommandList> MirrorCommandList;
  ComPtr<ID3D12CommandAllocator> MirrorCommandAllocator;

  UploadBufferStack ConstBufferStack;
  UINT64 FencePoint;
};

class ImGuiInteractor: public WindowInteractor {

public:
  ~ImGuiInteractor() {
    OnDestroy();
  }

protected:
  enum RENDER_SCHEDULING_OPTIONS: int {
    RENDER_SCHEDULING_OPTION_ST,
    RENDER_SCHEDULING_OPTION_MT_SCENE,
    RENDER_SCHEDULING_OPTION_MT_CHUNK
  };

  HRESULT OnInitialize(ID3D12Device *pd3dDevice, UINT uFramesInFlight, DXGI_FORMAT RTVFormat) {
    HRESULT hr;
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    m_pImGuiCtx = ImGui::CreateContext();

    auto io = ImGui::GetIO(); (void)io;
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsClassic();

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors = 1;
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    V_RETURN(pd3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_pImGuiSrvHeap)));

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(this->GetHwnd());
    ImGui_ImplDX12_Init(pd3dDevice, uFramesInFlight, RTVFormat,
      m_pImGuiSrvHeap.Get(),
      m_pImGuiSrvHeap->GetCPUDescriptorHandleForHeapStart(),
      m_pImGuiSrvHeap->GetGPUDescriptorHandleForHeapStart());

    // Load Fonts
    io.Fonts->AddFontDefault();

    return hr;
  }

  void OnDestroy() {
    if(m_pImGuiCtx) {
      BeginInteraction();
      ImGui_ImplDX12_Shutdown();
      ImGui_ImplWin32_Shutdown();
      ImGui::DestroyContext();
      EndInteraction();
      m_pImGuiCtx = nullptr;
    }
  }

  void OnResizeFrame(int cx, int cy) {
    BeginInteraction();
    ImGui_ImplDX12_InvalidateDeviceObjects();
    ImGui_ImplDX12_CreateDeviceObjects();
    EndInteraction();
  }

  void OnFrameMoved(ID3D12Device *pDevice) {

    BeginInteraction();
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::Begin("", nullptr, ImGuiWindowFlags_NoBackground);
    ImGui::RadioButton("ST Def", (int*)&m_RenderSchedulingOption, RENDER_SCHEDULING_OPTION_ST);
    ImGui::RadioButton("MT Def/Scene", (int*)&m_RenderSchedulingOption, RENDER_SCHEDULING_OPTION_MT_SCENE);
    ImGui::RadioButton("MT Def/Chunk", (int*)&m_RenderSchedulingOption, RENDER_SCHEDULING_OPTION_MT_CHUNK);
    ImGui::End();

    EndInteraction();
  }

  void OnRender(ID3D12Device *pDevice, ID3D12GraphicsCommandList *pCommandList) {

    BeginInteraction();

    ImGui::Render();
    pCommandList->SetDescriptorHeaps(1, m_pImGuiSrvHeap.GetAddressOf());
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), pCommandList);

    EndInteraction();
  }

  LRESULT OnMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool *pbNoFurtherProcessing) override {
    // Forward declare message handler from imgui_impl_win32.cpp
    extern LRESULT CALLBACK ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    LRESULT ret = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
    return ret;
  }

private:
  void BeginInteraction() {
    ImGui::SetCurrentContext(m_pImGuiCtx);
  }
  void EndInteraction() {
  }

  ImGuiContext *m_pImGuiCtx = nullptr;
  ComPtr<ID3D12DescriptorHeap> m_pImGuiSrvHeap;
  RENDER_SCHEDULING_OPTIONS m_RenderSchedulingOption = RENDER_SCHEDULING_OPTION_ST;
};

class MultithreadedRenderingSample : public D3D12RendererContext, public ImGuiInteractor {
public:
private:
  HRESULT OnInitPipelines() override;
  void OnDestroy() override;
  UINT GetExtraDSVDescriptorCount() const override;
  void OnFrameMoved(float fTime, float fElapsed) override;
  void OnRenderFrame(float fTime, float fElapsed) override;
  void OnResizeFrame(int cx, int cy) override;

  void RenderShadowMap(int iShadow, ID3D12GraphicsCommandList *pCommandList);
  void RenderScene(ID3D12GraphicsCommandList *pCommandList, const SceneParamsStatic *pStaticParams,
                   const SceneParamsDynamic *pDynamicParams);
  void RenderSceneSetup(ID3D12GraphicsCommandList *pCommandList, const SceneParamsStatic *pStaticParams,
                        const SceneParamsDynamic *pDynamicParams);
  void RenderMirror(int iMirror, FrameResources *pFrameResources, ID3D12GraphicsCommandList *pCommandList);

  // UI
  LRESULT OnMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool *pbNoFurtherProcessing) override;

  HRESULT CreatePSOs();
  HRESULT LoadModel();
  HRESULT CreateMirrorModels(ResourceUploadBatch *pUploadBatch);
  HRESULT CreateShadowDepthStencilViews();
  HRESULT CreateStaticDescriptorHeap();
  HRESULT CreateFrameResources();

  void InitCamera();
  void InitLights();

  XMMATRIX CalcLightViewProj( int iLight, BOOL bAdapterFOV);

  HRESULT InitializeRendererThreadpool();

  CDXUTSDKMesh m_Model;
  ComPtr<ID3D12DescriptorHeap> m_pModelDescriptorHeap;
  std::future<HRESULT> m_InitPipelineWaitable;

  // Mirror models
  ComPtr<ID3D12Resource> m_pMirrorVertexBuffer;
  std::array<D3D12_VERTEX_BUFFER_VIEW, s_iNumMirrors> m_aMirrorVBVs;
  std::array<XMFLOAT4X4, s_iNumMirrors>             m_aMirrorWorldMatrices;
  std::array<XMFLOAT4, s_iNumMirrors>               m_aMirrorPlanes;

  D3D12_VIEWPORT m_aShadowViewport;
  XMFLOAT2 m_vShadowResoltion = { 2048.f, 2048.f };
  std::array<ComPtr<ID3D12Resource>, s_iNumShadows> m_aShadowTextures;
  ComPtr<ID3D12DescriptorHeap> m_pStaticDescriptorHeap;

  enum NAMED_PIPELINE_INDEX {
    NAMED_PIPELINE_INDEX_SHADOW,      // For writing shadow depth map
    NAMED_PIPELINE_INDEX_MIRRORED_S0, // For rendering stencil area with stencil bit 0, no depth write
    NAMED_PIPELINE_INDEX_MIRRORED_S1, // For rendering single mirrored objects, stencil bit 1
    NAMED_PIPELINE_INDEX_MIRRORED_S2,
    NAMED_PIPELINE_INDEX_MIRRORED_S3,
    NAMED_PIPELINE_INDEX_MIRRORED_S4,
    NAMED_PIPELINE_INDEX_MIRRORED_S5,
    NAMED_PIPELINE_INDEX_MIRRORED_S6,
    NAMED_PIPELINE_INDEX_MIRRORED_S7,
    NAMED_PIPELINE_INDEX_MIRRORED_CLEAR_DEPTH_S0, // For clearing depth buffer in stencil area
    NAMED_PIPELINE_INDEX_MIRRORED_CLEAR_DEPTH_S1,
    NAMED_PIPELINE_INDEX_MIRRORED_CLEAR_DEPTH_S2,
    NAMED_PIPELINE_INDEX_MIRRORED_CLEAR_DEPTH_S3,
    NAMED_PIPELINE_INDEX_MIRRORED_CLEAR_DEPTH_S4,
    NAMED_PIPELINE_INDEX_MIRRORED_CLEAR_DEPTH_S5,
    NAMED_PIPELINE_INDEX_MIRRORED_CLEAR_DEPTH_S6,
    NAMED_PIPELINE_INDEX_MIRRORED_CLEAR_DEPTH_S7,
    NAMED_PIPELINE_INDEX_MIRRORED_RENDERING_S0, // For render mirrored area, depth test and write, stencil test but no
                                                // write
    NAMED_PIPELINE_INDEX_MIRRORED_RENDERING_S1,
    NAMED_PIPELINE_INDEX_MIRRORED_RENDERING_S2,
    NAMED_PIPELINE_INDEX_MIRRORED_RENDERING_S3,
    NAMED_PIPELINE_INDEX_MIRRORED_RENDERING_S4,
    NAMED_PIPELINE_INDEX_MIRRORED_RENDERING_S5,
    NAMED_PIPELINE_INDEX_MIRRORED_RENDERING_S6,
    NAMED_PIPELINE_INDEX_MIRRORED_RENDERING_S7,
    NAMED_PIPELINE_INDEX_RENDERING_MIRROR_S0, // For render mirror, overwrite depth, clear the stencil bit.
    NAMED_PIPELINE_INDEX_RENDERING_MIRROR_S1,
    NAMED_PIPELINE_INDEX_RENDERING_MIRROR_S2,
    NAMED_PIPELINE_INDEX_RENDERING_MIRROR_S3,
    NAMED_PIPELINE_INDEX_RENDERING_MIRROR_S4,
    NAMED_PIPELINE_INDEX_RENDERING_MIRROR_S5,
    NAMED_PIPELINE_INDEX_RENDERING_MIRROR_S6,
    NAMED_PIPELINE_INDEX_RENDERING_MIRROR_S7,
    NAMED_PIPELINE_INDEX_NORMAL, // render the scene but no mirrors
    NAMED_PIPELINE_INDEX_MAX
  };
  // Store all the pipeline state we will use.
  std::array<PipelineStateTuple, NAMED_PIPELINE_INDEX_MAX> m_aPipelineLib;
  INT m_iFrameIndex = 0;
  static const UINT s_uTotalFrameCount = 3;
  FrameResources m_aFrameResources[s_uTotalFrameCount];

  // Camera parameters
  CModelViewerCamera m_Camera;
  BOOL m_bRenderSceneLightPOV = FALSE;

  // Light parameters
  XMVECTORF32                    g_vLightColor[s_iNumLights];
  XMVECTORF32                    g_vLightPos[s_iNumLights];
  XMVECTORF32                    g_vLightDir[s_iNumLights];
  FLOAT                          g_fLightFOV[s_iNumLights];
  FLOAT                          g_fLightAspect[s_iNumLights];
  FLOAT                          g_fLightNearPlane[s_iNumLights];
  FLOAT                          g_fLightFarPlane[s_iNumLights];
  FLOAT                          g_fLightFalloffDistEnd[s_iNumLights];
  FLOAT                          g_fLightFalloffDistRange[s_iNumLights];
  FLOAT                          g_fLightFalloffCosAngleEnd[s_iNumLights];
  FLOAT                          g_fLightFalloffCosAngleRange[s_iNumLights];

  // Thread pool scheduling
  PTP_POOL m_pThreadPool = nullptr;
  PTP_CLEANUP_GROUP m_pCleanupGroup = nullptr;
  TP_CALLBACK_ENVIRON m_CallbackEnv;
  PTP_WORK m_pWorkQueuePool[64]; // Same as maximum kernel objects can be passed to WaitForMultipleObjects
  int m_iWorkQueueMaxParallelCapacity = 64; // Determine the maximum parallel work queue count according to current
                                            // active CPU logical processor count.
  int m_aWorkQueueCurrWorkIndices[64]; // Used as the arguments passed to work queue.
  HANDLE m_aWorkQueueParallelEvents[64]; // Notify the main thread that work item call back function has completed.
};

HRESULT CreateMultithreadRenderingRendererAndInteractor(D3D12RendererContext **ppRenderer,
                                                        WindowInteractor **ppInteractor) {
  HRESULT hr = S_OK;
  auto pInstance = new MultithreadedRenderingSample;
  *ppRenderer = pInstance;
  *ppInteractor = pInstance;
  return hr;
}

UINT MultithreadedRenderingSample::GetExtraDSVDescriptorCount() const {
  return s_iNumShadows;
}

HRESULT MultithreadedRenderingSample::OnInitPipelines() {
  HRESULT hr;

  V_RETURN(LoadModel());
  V_RETURN(CreatePSOs());
  V_RETURN(CreateShadowDepthStencilViews());
  V_RETURN(CreateStaticDescriptorHeap());
  V_RETURN(CreateFrameResources());

  InitCamera();
  InitLights();

  V_RETURN(ImGuiInteractor::OnInitialize(m_pd3dDevice, s_uTotalFrameCount, m_BackBufferFormat));

  V_RETURN(m_InitPipelineWaitable.get());
  return hr;
}

void MultithreadedRenderingSample::OnDestroy() {
  ImGuiInteractor::OnDestroy();
}

HRESULT MultithreadedRenderingSample::CreateFrameResources() {
  HRESULT hr = S_OK;
  for(auto &frameResources : m_aFrameResources) {
    V_RETURN(frameResources.ConstBufferStack.Initialize(m_pd3dDevice, (1 << 12), 1));
    V_RETURN(m_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  IID_PPV_ARGS(&frameResources.CommandAllocator)));
    frameResources.FencePoint = 0;
  }

  return hr;
}

HRESULT MultithreadedRenderingSample::CreateShadowDepthStencilViews() {
  HRESULT hr = S_OK;
  D3D12_CLEAR_VALUE clearValue = {};
  clearValue.Format = DXGI_FORMAT_D32_FLOAT;
  clearValue.DepthStencil.Depth = 1.0f;

  D3D12_RESOURCE_DESC texDesc = {};
  texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texDesc.Format = DXGI_FORMAT_R32_TYPELESS;
  texDesc.Width = (UINT)m_vShadowResoltion.x;
  texDesc.Height = (UINT)m_vShadowResoltion.y;
  texDesc.DepthOrArraySize = 1;
  texDesc.MipLevels = 0;
  texDesc.SampleDesc.Count = 1;
  texDesc.SampleDesc.Quality = 0;
  texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

  D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
  dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
  dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
  dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
  dsvDesc.Texture2D.MipSlice = 0;

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle;
  handle = m_pDSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
  handle.Offset(1, m_uDsvDescriptorSize);

  for(auto it = m_aShadowTextures.begin(); it != m_aShadowTextures.end(); ++it) {
    V_RETURN(m_pd3dDevice->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
      D3D12_HEAP_FLAG_NONE,
      &texDesc,
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
      &clearValue,
      IID_PPV_ARGS(&*it)
    ));

    m_pd3dDevice->CreateDepthStencilView(it->Get(), &dsvDesc, handle);
    handle.Offset(1, m_uDsvDescriptorSize);
  }

  m_aShadowViewport.TopLeftX = .0f;
  m_aShadowViewport.TopLeftY = .0f;
  m_aShadowViewport.Width = m_vShadowResoltion.x;
  m_aShadowViewport.Height = m_vShadowResoltion.y;
  m_aShadowViewport.MinDepth = .0f;
  m_aShadowViewport.MaxDepth = 1.0f;

  return hr;
}

HRESULT MultithreadedRenderingSample::CreateStaticDescriptorHeap() {
  HRESULT hr;
  ComPtr<ID3D12DescriptorHeap> pTempHeap;
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
  UINT numModelDescriptors;

  V_RETURN(m_Model.GetResourceDescriptorHeap(m_pd3dDevice, FALSE, &m_pModelDescriptorHeap));

  heapDesc = m_pModelDescriptorHeap->GetDesc();
  numModelDescriptors = heapDesc.NumDescriptors;
  heapDesc.NumDescriptors += s_iNumShadows;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  V_RETURN(m_pd3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&pTempHeap)));

  m_pd3dDevice->CopyDescriptorsSimple(numModelDescriptors,
                                      CD3DX12_CPU_DESCRIPTOR_HANDLE(pTempHeap->GetCPUDescriptorHandleForHeapStart(),
                                                                    m_uCbvSrvUavDescriptorSize, s_iNumShadows),
                                      m_pModelDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                                      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;
  srvDesc.Texture2D.PlaneSlice = 0;

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle0;
  handle0 = pTempHeap->GetCPUDescriptorHandleForHeapStart();

  for(auto it = m_aShadowTextures.begin(); it != m_aShadowTextures.end(); ++it) {
    m_pd3dDevice->CreateShaderResourceView(it->Get(), &srvDesc, handle0);
    handle0.Offset(1, m_uCbvSrvUavDescriptorSize);
  }

  std::swap(m_pModelDescriptorHeap, pTempHeap);

  return hr;
}

HRESULT MultithreadedRenderingSample::LoadModel() {
  HRESULT hr;
  ResourceUploadBatch uploadBatch(m_pd3dDevice, &m_MemAllocator);

  V_RETURN(uploadBatch.Begin(D3D12_COMMAND_LIST_TYPE_DIRECT));

  V_RETURN(m_Model.Create(&uploadBatch, LR"(directx-sdk-samples\Media\SquidRoom\SquidRoom.sdkmesh)"));

  V_RETURN(CreateMirrorModels(&uploadBatch));

  V_RETURN(uploadBatch.End(m_pd3dCommandQueue, &m_InitPipelineWaitable));

  return hr;
}

HRESULT MultithreadedRenderingSample::CreateMirrorModels(ResourceUploadBatch *pUploadBatch) {
  HRESULT hr;

  // These values are hard-coded based on the sdkmesh contents, plus some
  // hand-fiddling, pending a better solution in the pipeline.
  const XMVECTORF32 s_vMirrorCenter[] = {
                                        { -35.1688f, 89.279683f, -0.7488765f, 0.0f },
                                        { 41.2174f, 89.279683f, -0.7488745f, 0.0f },
                                        { 3.024275f, 89.279683f, -54.344299f, 0.0f },
                                        { 3.02427475f, 89.279683f, 52.8466f, 0.0f } };

  const XMVECTORF32 s_vMirrorNormal[] = {
                                        { -0.998638464f, -0.052165297f, 0.0f, 0.0f },
                                        { 0.998638407f, -0.052166381f, 3.15017E-08f, 0.0f },
                                        { 0.0f, -0.076278878f, -0.997086522f, 0.0f },
                                        { -5.22129E-08f, -0.076279957f, 0.99708644f, 0.0f } };

  const XMVECTORF32 s_vMirrorDims[] = {
                                      { 104.190899f, 92.19923178f, 0.0f, 0.0f  },
                                      { 104.190895f, 92.19922656f, 0.0f, 0.0f  },
                                      { 76.3862f,    92.3427325f, 0.0f, 0.0f   },
                                      { 76.386196f,  92.34274043f, 0.0f, 0.0f  } };

  MirrorVertex mirrorVertices[4 * s_iNumMirrors];
  XMMATRIX W;
  XMVECTOR V, R0, R1, R2;
  XMVECTOR Plane;
  XMVECTOR vOffsets[] = {
      { -.5f, .5f, .0f, .0f },
      { .5f, .5f, .0f, .0f },
      { -.5f, -.5f, .0f, .0f },
      { .5f, -.5f, .0f, .0f }
  };

  // Create default vertex buffer
  UINT vbSizeInBytes = sizeof(mirrorVertices) / s_iNumMirrors;
  m_pd3dDevice->CreateCommittedResource(
    &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
    D3D12_HEAP_FLAG_NONE,
    &CD3DX12_RESOURCE_DESC::Buffer(vbSizeInBytes * s_iNumMirrors),
    D3D12_RESOURCE_STATE_COPY_DEST,
    nullptr,
    IID_PPV_ARGS(&m_pMirrorVertexBuffer)
  );

  for(int i = 0; i < s_iNumMirrors; ++i) {
    for(int j = 0; j < 4; ++j) {

      auto &v = mirrorVertices[i * 4 + j];
      V = s_vMirrorDims[i] * vOffsets[j];
      XMStoreFloat3(&v.Position, V);
      XMStoreFloat3(&v.Normal, g_XMZero);
      XMStoreFloat2(&v.Texcoord, g_XMZero);
      XMStoreFloat3(&v.Tangent, g_XMZero);
    }

    m_aMirrorVBVs[i].BufferLocation = m_pMirrorVertexBuffer->GetGPUVirtualAddress() + (i * vbSizeInBytes);
    m_aMirrorVBVs[i].SizeInBytes = vbSizeInBytes;
    m_aMirrorVBVs[i].StrideInBytes = sizeof(MirrorVertex);

    // Set up the mirror local-to-world matrix
    R2 = XMVector3Normalize(-s_vMirrorNormal[i]);
    R1 = g_XMIdentityR1;
    R0 = XMVector3Normalize(XMVector3Cross(R1, R2));
    R1 = XMVector3Cross(R2, R0);
    W.r[0] = R0;
    W.r[1] = R1;
    W.r[2] = R2;
    W.r[3] = s_vMirrorCenter[i];
    W.r[3] = XMVectorSetW(W.r[3], 1.0f);

    Plane = XMPlaneFromPointNormal(s_vMirrorCenter[i], s_vMirrorNormal[i]);
    XMStoreFloat4x4(&m_aMirrorWorldMatrices[i], W);
    XMStoreFloat4(&m_aMirrorPlanes[i], Plane);
  }

  D3D12_SUBRESOURCE_DATA subdata = {};
  subdata.pData = mirrorVertices;
  subdata.RowPitch = sizeof(mirrorVertices);
  subdata.SlicePitch = subdata.RowPitch;

  V_RETURN(pUploadBatch->Enqueue(m_pMirrorVertexBuffer.Get(), 0, 1, &subdata));

  return hr;
}

HRESULT MultithreadedRenderingSample::CreatePSOs() {

  HRESULT hr;
  ComPtr<ID3DBlob> pVSBuffer, pPSBuffer, pErrorBuffer;

  D3D_SHADER_MACRO defines[] = {
#ifdef UNCOMPRESSED_VERTEX_DATA
    { "UNCOMPRESSED_VERTEX_DATA", "" }
#endif
    { nullptr, nullptr }
  };
  ComPtr<ID3D12RootSignature> pRootSignature;

  V(d3dUtils::CompileShaderFromFile(L"Shaders/MultithreadedRendering_VSPS.hlsl", defines, nullptr, "VSMain", "vs_5_0", 0, 0, &pVSBuffer, &pErrorBuffer));
  if(FAILED(hr)) {
    DX_TRACE(L"Compile VS error: %S\n", pErrorBuffer ? pErrorBuffer->GetBufferPointer(): "Unknown");
    return hr;
  } else if(pErrorBuffer) {
    DX_TRACE(L"Compile VS warning: %S\n", pErrorBuffer->GetBufferPointer());
    pErrorBuffer = nullptr;
  }

  V(d3dUtils::CompileShaderFromFile(L"Shaders/MultithreadedRendering_VSPS.hlsl", defines, nullptr, "VSMain", "vs_5_0", 0, 0, &pVSBuffer, &pErrorBuffer));
  if(FAILED(hr)) {
    DX_TRACE(L"Compile VS error: %S\n", pErrorBuffer ? pErrorBuffer->GetBufferPointer(): "Unknown");
    return hr;
  } else if(pErrorBuffer) {
    DX_TRACE(L"Compile VS warning: %S\n", pErrorBuffer->GetBufferPointer());
    pErrorBuffer = nullptr;
  }

  V(d3dUtils::CompileShaderFromFile(L"Shaders/MultithreadedRendering_VSPS.hlsl", defines, nullptr, "PSMain", "ps_5_0", 0, 0, &pPSBuffer, &pErrorBuffer));
  if(FAILED(hr)) {
    DX_TRACE(L"Compile PS error: %S\n", pErrorBuffer ? pErrorBuffer->GetBufferPointer(): "Unknown");
    return hr;
  } else if(pErrorBuffer) {
    DX_TRACE(L"Compile PS warning: %S\n", pErrorBuffer->GetBufferPointer());
    pErrorBuffer = nullptr;
  }

  D3D12_INPUT_ELEMENT_DESC UncompressedLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
  };

#ifdef UNCOMPRESSED_VERTEX_DATA
  D3D12_INPUT_ELEMENT_DESC (&CompressedLayout)[4] = UncompressedLayout;
#else
  D3D12_INPUT_ELEMENT_DESC CompressedLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R10G10B10A2_UNORM, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R16G16_FLOAT, 0,  16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TANGENT", 0, DXGI_FORMAT_R10G10B10A2_UNORM, 0, 20, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
  };
#endif

  RootSignatureGenerator rsGen;
  rsGen.AddConstBufferView(0);
  rsGen.AddConstBufferView(1, 0, D3D12_SHADER_VISIBILITY_PIXEL);
  rsGen.AddConstBufferView(2);
  rsGen.AddDescriptorTable({ CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0) }, D3D12_SHADER_VISIBILITY_PIXEL);
  rsGen.AddDescriptorTable({ CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1) }, D3D12_SHADER_VISIBILITY_PIXEL);
  rsGen.AddDescriptorTable({ CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2) }, D3D12_SHADER_VISIBILITY_PIXEL);
  rsGen.AddStaticSamples(
    {
      CD3DX12_STATIC_SAMPLER_DESC {
        0, D3D12_FILTER_MIN_MAG_MIP_POINT, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP
      },
      CD3DX12_STATIC_SAMPLER_DESC {
        1, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP
      }
    }
  );

  rsGen.Generate(m_pd3dDevice, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT, &pRootSignature);

  CD3DX12_BLEND_DESC defaultBS{ D3D12_DEFAULT };

  D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPSODesc = {};
  shadowPSODesc.pRootSignature = pRootSignature.Get();
  shadowPSODesc.VS = {
    pVSBuffer->GetBufferPointer(),
    pVSBuffer->GetBufferSize()
  };
  shadowPSODesc.BlendState = CD3DX12_BLEND_DESC{ D3D12_DEFAULT };
  shadowPSODesc.BlendState.RenderTarget[0].BlendEnable = TRUE; // No color buffer write
  shadowPSODesc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0;
  shadowPSODesc.SampleMask = UINT_MAX;
  shadowPSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC{D3D12_DEFAULT};
  shadowPSODesc.RasterizerState.DepthBias = 1 << 8;
  shadowPSODesc.RasterizerState.SlopeScaledDepthBias = 1.E-4f;
  shadowPSODesc.RasterizerState.DepthBiasClamp = 1.E-2f;
  shadowPSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC{ D3D12_DEFAULT };
  shadowPSODesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  shadowPSODesc.InputLayout = {
    CompressedLayout,
    (UINT)std::size(CompressedLayout)
  };
  shadowPSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  shadowPSODesc.NumRenderTargets = 0;
  shadowPSODesc.DSVFormat = DXGI_FORMAT_D32_FLOAT;
  shadowPSODesc.SampleDesc.Count = 1;
  shadowPSODesc.SampleDesc.Quality = 0;

  m_aPipelineLib[NAMED_PIPELINE_INDEX_SHADOW].RootSignature = pRootSignature;
  m_pd3dDevice->CreateGraphicsPipelineState(&shadowPSODesc,
                                            IID_PPV_ARGS(&m_aPipelineLib[NAMED_PIPELINE_INDEX_SHADOW].PSO));

  // Shadow map rendering pipeline
  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.pRootSignature = pRootSignature.Get();
  psoDesc.VS = {
    pVSBuffer->GetBufferPointer(),
    pVSBuffer->GetBufferSize()
  };
  psoDesc.PS = {
    pPSBuffer->GetBufferPointer(),
    pPSBuffer->GetBufferSize()
  };
  psoDesc.BlendState = defaultBS;
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC{ D3D12_DEFAULT };
  psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC{ D3D12_DEFAULT };
  psoDesc.InputLayout = {
    CompressedLayout,
    (UINT)std::size(CompressedLayout)
  };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = m_BackBufferFormat;
  psoDesc.DSVFormat = m_DepthStencilBufferFormat;
  psoDesc.SampleDesc = GetMsaaSampleDesc();

  m_aPipelineLib[NAMED_PIPELINE_INDEX_NORMAL].RootSignature = pRootSignature;
  V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc,
                                            IID_PPV_ARGS(&m_aPipelineLib[NAMED_PIPELINE_INDEX_NORMAL].PSO)));

  // Mirrored area stencil write pipeline
  psoDesc = {};
  psoDesc.pRootSignature = pRootSignature.Get();
  psoDesc.VS = {
    pVSBuffer->GetBufferPointer(),
    pVSBuffer->GetBufferSize()
  };
  psoDesc.BlendState = CD3DX12_BLEND_DESC{ D3D12_DEFAULT };
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC{ D3D12_DEFAULT };
  psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC{ D3D12_DEFAULT };
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // Perform depth test but no depth write.
  psoDesc.DepthStencilState.StencilEnable = TRUE;
  psoDesc.DepthStencilState.FrontFace = {
      D3D12_STENCIL_OP_REPLACE, // StencilFailOp;
      D3D12_STENCIL_OP_KEEP, // StencilDepthFailOp;
      D3D12_STENCIL_OP_REPLACE, // StencilPassOp;
      D3D12_COMPARISON_FUNC_ALWAYS // StencilFunc;
  };
  psoDesc.DepthStencilState.BackFace = {
      D3D12_STENCIL_OP_REPLACE, // StencilFailOp;
      D3D12_STENCIL_OP_KEEP, // StencilDepthFailOp;
      D3D12_STENCIL_OP_REPLACE, // StencilPassOp;
      D3D12_COMPARISON_FUNC_ALWAYS // StencilFunc;
  };
  psoDesc.InputLayout = {
    UncompressedLayout,
    (UINT)std::size(UncompressedLayout)
  };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 0;
  psoDesc.DSVFormat = m_DepthStencilBufferFormat;
  psoDesc.SampleDesc = GetMsaaSampleDesc();

  // Clear depth value in stencil area
  D3D12_GRAPHICS_PIPELINE_STATE_DESC clrDepthDesc = {};
  clrDepthDesc.pRootSignature = pRootSignature.Get();
  clrDepthDesc.VS = {
    pVSBuffer->GetBufferPointer(),
    pVSBuffer->GetBufferSize()
  };
  clrDepthDesc.BlendState = CD3DX12_BLEND_DESC{ D3D12_DEFAULT };
  clrDepthDesc.SampleMask = UINT_MAX;
  clrDepthDesc.RasterizerState = CD3DX12_RASTERIZER_DESC{ D3D12_DEFAULT };
  clrDepthDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC{ D3D12_DEFAULT };
  clrDepthDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL; // write depth directly
  clrDepthDesc.DepthStencilState.StencilEnable = TRUE;
  clrDepthDesc.DepthStencilState.FrontFace = {
      D3D12_STENCIL_OP_KEEP, // StencilFailOp;
      D3D12_STENCIL_OP_KEEP, // StencilDepthFailOp;
      D3D12_STENCIL_OP_KEEP, // StencilPassOp;
      D3D12_COMPARISON_FUNC_EQUAL, // StencilFunc;
  };
  clrDepthDesc.DepthStencilState.BackFace = {
      D3D12_STENCIL_OP_KEEP, // StencilFailOp;
      D3D12_STENCIL_OP_KEEP, // StencilDepthFailOp;
      D3D12_STENCIL_OP_KEEP, // StencilPassOp;
      D3D12_COMPARISON_FUNC_NEVER // StencilFunc;
  };
  clrDepthDesc.InputLayout = {
    UncompressedLayout,
    (UINT)std::size(UncompressedLayout)
  };
  clrDepthDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  clrDepthDesc.NumRenderTargets = 0;
  clrDepthDesc.DSVFormat = m_DepthStencilBufferFormat;
  clrDepthDesc.SampleDesc = GetMsaaSampleDesc();

  // Mirrored area rendering pipeline
  D3D12_GRAPHICS_PIPELINE_STATE_DESC objPSODesc = {};
  objPSODesc = {};
  objPSODesc.pRootSignature = pRootSignature.Get();
  objPSODesc.VS = {
    pVSBuffer->GetBufferPointer(),
    pVSBuffer->GetBufferSize()
  };
  objPSODesc.PS = {
    pPSBuffer->GetBufferPointer(),
    pPSBuffer->GetBufferSize()
  };
  objPSODesc.BlendState = CD3DX12_BLEND_DESC{ D3D12_DEFAULT };
  objPSODesc.SampleMask = UINT_MAX;
  objPSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC{ D3D12_DEFAULT };
  objPSODesc.RasterizerState.FrontCounterClockwise = TRUE;
  objPSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC{ D3D12_DEFAULT };
  objPSODesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
  objPSODesc.DepthStencilState.StencilEnable = TRUE;
  objPSODesc.DepthStencilState.FrontFace = {
      D3D12_STENCIL_OP_KEEP, // StencilFailOp;
      D3D12_STENCIL_OP_KEEP, // StencilDepthFailOp;
      D3D12_STENCIL_OP_KEEP, // StencilPassOp;
      D3D12_COMPARISON_FUNC_EQUAL // StencilFunc;
  };
  objPSODesc.DepthStencilState.BackFace = {
      D3D12_STENCIL_OP_KEEP, // StencilFailOp;
      D3D12_STENCIL_OP_KEEP, // StencilDepthFailOp;
      D3D12_STENCIL_OP_KEEP, // StencilPassOp;
      D3D12_COMPARISON_FUNC_NEVER // StencilFunc;
  };
  objPSODesc.InputLayout = {
    CompressedLayout,
    (UINT)std::size(CompressedLayout)
  };
  objPSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  objPSODesc.NumRenderTargets = 1;
  objPSODesc.RTVFormats[0] = m_BackBufferFormat;
  objPSODesc.DSVFormat = m_DepthStencilBufferFormat;
  objPSODesc.SampleDesc = GetMsaaSampleDesc();

  // Mirror rendering pipeline, enable blending.
  D3D12_GRAPHICS_PIPELINE_STATE_DESC mirrorPSODesc = {};
  mirrorPSODesc.pRootSignature = pRootSignature.Get();
  mirrorPSODesc.VS = {
    pVSBuffer->GetBufferPointer(),
    pVSBuffer->GetBufferSize()
  };
  mirrorPSODesc.BlendState = CD3DX12_BLEND_DESC{ D3D12_DEFAULT };
  mirrorPSODesc.SampleMask = UINT_MAX;
  mirrorPSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC{ D3D12_DEFAULT };
  mirrorPSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC{ D3D12_DEFAULT };
  mirrorPSODesc.DepthStencilState.StencilEnable = TRUE;
  mirrorPSODesc.DepthStencilState.FrontFace = {
      D3D12_STENCIL_OP_ZERO, // StencilFailOp;
      D3D12_STENCIL_OP_KEEP, // StencilDepthFailOp;
      D3D12_STENCIL_OP_ZERO, // StencilPassOp;
      D3D12_COMPARISON_FUNC_EQUAL // StencilFunc;
  };
  mirrorPSODesc.DepthStencilState.BackFace = {
      D3D12_STENCIL_OP_KEEP, // StencilFailOp;
      D3D12_STENCIL_OP_KEEP, // StencilDepthFailOp;
      D3D12_STENCIL_OP_KEEP, // StencilPassOp;
      D3D12_COMPARISON_FUNC_NEVER // StencilFunc;
  };
  mirrorPSODesc.InputLayout = {
    UncompressedLayout,
    (UINT)std::size(UncompressedLayout)
  };
  mirrorPSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  mirrorPSODesc.NumRenderTargets = 0;
  mirrorPSODesc.DSVFormat = m_DepthStencilBufferFormat;
  mirrorPSODesc.SampleDesc = GetMsaaSampleDesc();

  for (int i = 0; i < 8; ++i) {

    UINT stencilRef = 1 << i;

    psoDesc.DepthStencilState.StencilReadMask = 0;
    psoDesc.DepthStencilState.StencilWriteMask = stencilRef;
    clrDepthDesc.DepthStencilState.StencilReadMask = stencilRef;
    clrDepthDesc.DepthStencilState.StencilWriteMask = 0;
    objPSODesc.DepthStencilState.StencilReadMask = stencilRef;
    objPSODesc.DepthStencilState.StencilWriteMask = 0;
    mirrorPSODesc.DepthStencilState.StencilReadMask = stencilRef;
    mirrorPSODesc.DepthStencilState.StencilWriteMask = stencilRef;

    m_aPipelineLib[i + NAMED_PIPELINE_INDEX_MIRRORED_S0].RootSignature = pRootSignature;
    V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&m_aPipelineLib[i + NAMED_PIPELINE_INDEX_MIRRORED_S0].PSO)));

    m_aPipelineLib[i + NAMED_PIPELINE_INDEX_MIRRORED_CLEAR_DEPTH_S0].RootSignature = pRootSignature;
    V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(
        &clrDepthDesc, IID_PPV_ARGS(&m_aPipelineLib[i + NAMED_PIPELINE_INDEX_MIRRORED_CLEAR_DEPTH_S0].PSO)));

    m_aPipelineLib[i + NAMED_PIPELINE_INDEX_MIRRORED_RENDERING_S0].RootSignature = pRootSignature;
    V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(
        &objPSODesc, IID_PPV_ARGS(&m_aPipelineLib[i + NAMED_PIPELINE_INDEX_MIRRORED_RENDERING_S0].PSO)));

    m_aPipelineLib[i + NAMED_PIPELINE_INDEX_RENDERING_MIRROR_S0].RootSignature = pRootSignature;
    V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(
        &mirrorPSODesc, IID_PPV_ARGS(&m_aPipelineLib[i + NAMED_PIPELINE_INDEX_RENDERING_MIRROR_S0].PSO)));
  }

  return hr;
}

void MultithreadedRenderingSample::InitCamera() {
  m_Camera.SetViewParams(s_vDefaultEye, s_vDefaultLookAt);
  m_Camera.SetRadius(s_fDefaultCameraRadius, s_fMinCameraRadius, s_fMaxCameraRadius);
}

void MultithreadedRenderingSample::InitLights() {

      // Our hand-tuned approximation to the sky light
    static const XMVECTORF32 s_lightDir0 = { -0.67f, -0.71f, +0.21f, 0.f };

    //g_vLightColor[0] =                  XMFLOAT4( 1.5f * 0.160f, 1.5f * 0.341f, 1.5f * 1.000f, 1.000f );
    g_vLightColor[0] =                  { 3.0f * 0.160f, 3.0f * 0.341f, 3.0f * 1.000f, 1.000f };
    g_vLightDir[0].v =                  XMVector3Normalize( s_lightDir0 ); 
    g_vLightPos[0].v =                  s_vSceneCenter - s_fSceneRadius * g_vLightDir[0];
    g_fLightFOV[0] =                    XM_PI / 4.0f;

    // The three overhead lamps
    static const XMVECTORF32 s_lightPos1 = { 0.0f, 400.0f, -250.0f, 0.f };
    static const XMVECTORF32 s_lightPos2 = { 0.0f, 400.0f, 0.0f, 0.f };
    static const XMVECTORF32 s_lightPos3 = { 0.0f, 400.0f, 250.0f, 0.f };

    g_vLightColor[1] =                  { 0.4f * 0.895f, 0.4f * 0.634f, 0.4f * 0.626f, 1.0f };
    g_vLightPos[1] =                    s_lightPos1;
    g_vLightDir[1] =                    g_XMNegIdentityR1;
    g_fLightFOV[1] =                    65.0f * ( XM_PI / 180.0f );
    
    g_vLightColor[2] =                  { 0.5f * 0.388f, 0.5f * 0.641f, 0.5f * 0.401f, 1.0f };
    g_vLightPos[2] =                    s_lightPos2;
    g_vLightDir[2] =                    g_XMNegIdentityR1;
    g_fLightFOV[2] =                    65.0f * ( XM_PI / 180.0f );
    
    g_vLightColor[3] =                  { 0.4f * 1.000f, 0.4f * 0.837f, 0.4f * 0.848f, 1.0f };
    g_vLightPos[3] =                    s_lightPos3;
    g_vLightDir[3] =                    g_XMNegIdentityR1;
    g_fLightFOV[3] =                    65.0f * ( XM_PI / 180.0f );
    
    // For the time beings, let's make these params follow the same pattern for all lights
    for ( int iLight = 0; iLight < s_iNumLights; ++iLight )
    {
        g_fLightAspect[iLight] = 1.0f;
        g_fLightNearPlane[iLight] = 100.f;
        g_fLightFarPlane[iLight] = 2.0f * s_fSceneRadius;

        g_fLightFalloffDistEnd[iLight] = g_fLightFarPlane[iLight];
        g_fLightFalloffDistRange[iLight] = 100.0f;

        g_fLightFalloffCosAngleEnd[iLight] = cosf( g_fLightFOV[iLight] / 2.0f );
        g_fLightFalloffCosAngleRange[iLight] = 0.1f;
    }
}

void MultithreadedRenderingSample::OnResizeFrame(int cx, int cy) {
  m_Camera.SetProjParams(s_fFOV, GetAspectRatio(), s_fNearPlane, s_fFarPlane);
  m_Camera.SetWindow(cx, cy);
  m_Camera.SetButtonMasks(MOUSE_MIDDLE_BUTTON, MOUSE_WHEEL, MOUSE_LEFT_BUTTON);

  ImGuiInteractor::OnResizeFrame(cx, cy);
}

void MultithreadedRenderingSample::OnFrameMoved(float fTime, float fElapsed) {

    // Jigger the overhead lights --- these are hard-coded to indices 1,2,3
    // Ideally, we'd attach the lights to the relevant objects in the mesh
    // file and animate those objects.  But for now, just some hard-coded
    // swinging...
    XMVECTOR cycle1 = XMVectorSet( 0.f,
                                   0.f,
                                   0.20f * sinf( 2.0f * ( fTime + 0.0f * XM_PI ) ),
                                   0.f );
    XMVECTOR v = g_XMNegIdentityR1 + cycle1;
    g_vLightDir[1].v = XMVector3Normalize( v );

    XMVECTOR cycle2 = XMVectorSet( 0.10f * cosf( 1.6f * ( fTime + 0.3f * XM_PI ) ),
                                   0.f,
                                   0.10f * sinf( 1.6f * ( fTime + 0.0f * XM_PI ) ),
                                   0.f );
    v = g_XMNegIdentityR1 + cycle2;
    g_vLightDir[2].v = XMVector3Normalize( v );

    XMVECTOR cycle3 = XMVectorSet( 0.30f * cosf( 2.4f * ( fTime + 0.3f * XM_PI ) ),
                                   0.f,
                                   0.f,
                                   0.f );
    v = g_XMNegIdentityR1 + cycle3;
    g_vLightDir[3].v = XMVector3Normalize( v );

  m_Camera.FrameMove(fElapsed, this);

  m_iFrameIndex = (m_iFrameIndex + 1) % s_uTotalFrameCount;
  auto pFrameResources = &m_aFrameResources[m_iFrameIndex];
  m_pSyncFence->WaitForSyncPoint(pFrameResources->FencePoint);

  pFrameResources->ConstBufferStack.Clear();

  ImGuiInteractor::OnFrameMoved(m_pd3dDevice);
}

XMMATRIX MultithreadedRenderingSample::CalcLightViewProj( int iLight, BOOL bAdapterFOV )
{
    XMVECTOR vLightDir = g_vLightDir[iLight];
    XMVECTOR vLightPos = g_vLightPos[iLight];

    XMVECTOR vLookAt = vLightPos + s_fSceneRadius * vLightDir;

    XMMATRIX mLightView = XMMatrixLookAtLH( vLightPos, vLookAt, g_XMIdentityR1 );

    XMMATRIX mLightProj = XMMatrixPerspectiveFovLH( g_fLightFOV[iLight], bAdapterFOV ? GetAspectRatio() : g_fLightAspect[iLight], g_fLightNearPlane[iLight], g_fLightFarPlane[iLight] );

    return mLightView * mLightProj;
}

void MultithreadedRenderingSample::RenderScene(ID3D12GraphicsCommandList *pCommandList,
                                               const SceneParamsStatic *pSceneParamsStatic,
                                               const SceneParamsDynamic *pSceneParamsDynamic) {

  m_pd3dCommandList->SetPipelineState(pSceneParamsStatic->pPipelineStateTuple->PSO.Get());
  m_pd3dCommandList->SetGraphicsRootSignature(pSceneParamsStatic->pPipelineStateTuple->RootSignature.Get());
  m_pd3dCommandList->SetDescriptorHeaps(1, m_pModelDescriptorHeap.GetAddressOf());

  m_pd3dCommandList->RSSetViewports(1, pSceneParamsStatic->pViewport);

  D3D12_RECT scissorRect{(LONG)pSceneParamsStatic->pViewport->TopLeftX, (LONG)pSceneParamsStatic->pViewport->TopLeftY,
                         (LONG)(pSceneParamsStatic->pViewport->TopLeftX + pSceneParamsStatic->pViewport->Width),
                         (LONG)(pSceneParamsStatic->pViewport->TopLeftY + pSceneParamsStatic->pViewport->Height)};
                         m_pd3dCommandList->RSSetScissorRects(1, &scissorRect);

  if (!pSceneParamsStatic->bRenderShadow) {
    // Shadow SRVs
    m_pd3dCommandList->SetGraphicsRootDescriptorTable(5, m_pModelDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
  }

  CB_PER_SCENE sceneData;
  XMMATRIX M = XMLoadFloat4x4(&pSceneParamsDynamic->matViewProj);
  XMStoreFloat4x4(&sceneData.m_mViewProj, XMMatrixTranspose(M));
  XMStoreFloat4(&sceneData.m_vAmbientColor, s_vAmbientColor);
  sceneData.m_vTintColor = pSceneParamsStatic->vTintColor;
  sceneData.m_vMirrorPlane = pSceneParamsStatic->vMirrorPlane;
  pSceneParamsStatic->pConstBufferStack->Push(&sceneData, sizeof(sceneData));
  m_pd3dCommandList->SetGraphicsRootConstantBufferView(2, pSceneParamsStatic->pConstBufferStack->Top().BufferLocation);

  if(!pSceneParamsStatic->bRenderShadow) {
    CB_PER_LIGHT lightData;
    for (int iLight = 0; iLight < s_iNumLights; ++iLight) {
      XMVECTOR vLightPos = XMVectorSetW(g_vLightPos[iLight], 1.0f);
      XMVECTOR vLightDir = XMVectorSetW(g_vLightDir[iLight], 0.0f);

      XMMATRIX mLightViewProj = CalcLightViewProj(iLight, FALSE);

      XMStoreFloat4(&lightData.m_LightData[iLight].m_vLightColor, g_vLightColor[iLight]);
      XMStoreFloat4(&lightData.m_LightData[iLight].m_vLightPos, vLightPos);
      XMStoreFloat4(&lightData.m_LightData[iLight].m_vLightDir, vLightDir);
      XMStoreFloat4x4(&lightData.m_LightData[iLight].m_mLightViewProj, XMMatrixTranspose(mLightViewProj));
      lightData.m_LightData[iLight].m_vFalloffs =
          XMFLOAT4(g_fLightFalloffDistEnd[iLight], g_fLightFalloffDistRange[iLight], g_fLightFalloffCosAngleEnd[iLight],
                  g_fLightFalloffCosAngleRange[iLight]);
    }
    pSceneParamsStatic->pConstBufferStack->Push(&lightData, sizeof(lightData));
    m_pd3dCommandList->SetGraphicsRootConstantBufferView(1, pSceneParamsStatic->pConstBufferStack->Top().BufferLocation);
  }

  CB_PER_OBJECT objData;
  XMStoreFloat4x4(&objData.m_mWorld, XMMatrixIdentity());
  XMStoreFloat4(&objData.m_vObjectColor, Colors::White);
  pSceneParamsStatic->pConstBufferStack->Push(&objData, sizeof(objData));
  m_pd3dCommandList->SetGraphicsRootConstantBufferView(0, pSceneParamsStatic->pConstBufferStack->Top().BufferLocation);

  m_Model.Render(m_pd3dCommandList,
    CD3DX12_GPU_DESCRIPTOR_HANDLE(m_pModelDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
      s_iNumShadows, m_uCbvSrvUavDescriptorSize),
      3, 4);
}

void MultithreadedRenderingSample::RenderMirror(int iMirror, FrameResources *pFrameResources, ID3D12GraphicsCommandList *pCommandList) {

  XMVECTOR vEyePt;
  XMMATRIX matViewProj;

#ifdef RENDER_SCENE_LIGHT_POV
  if (m_bRenderSceneLightPOV) {
    vEyePt = g_vLightPos[0];
    matViewProj = CalcLightViewProj(0, TRUE);
  } else
#endif
  {
    vEyePt = m_Camera.GetEyePt();
    matViewProj = m_Camera.GetViewMatrix() * m_Camera.GetProjMatrix();
  }

  XMVECTOR vMirrorPlane = XMLoadFloat4(&m_aMirrorPlanes[iMirror]);

  // Test for back-facing mirror
  if(XMVectorGetX(XMPlaneDotCoord(vMirrorPlane, vEyePt)) < 0.0f)
    return;

  XMMATRIX matReflect = XMMatrixReflect(vMirrorPlane);

  UINT sindex = iMirror % 8;
  UINT8 stencilRef = 1 << sindex;

  // Write mirrored area stencil value
  auto pPipelineStateTuple = &m_aPipelineLib[NAMED_PIPELINE_INDEX_MIRRORED_S0 + sindex];
  pCommandList->SetPipelineState(pPipelineStateTuple->PSO.Get());
  pCommandList->SetGraphicsRootSignature(pPipelineStateTuple->RootSignature.Get());
  pCommandList->OMSetStencilRef(stencilRef);

  D3D12_CONSTANT_BUFFER_VIEW_DESC objCBV, sceneCBV;
  CB_PER_OBJECT objData;
  CB_PER_SCENE sceneData;

  XMStoreFloat4x4(&objData.m_mWorld, XMMatrixTranspose(XMLoadFloat4x4(&m_aMirrorWorldMatrices[iMirror])));
  XMStoreFloat4x4(&sceneData.m_mViewProj, XMMatrixTranspose(matViewProj));
  pFrameResources->ConstBufferStack.Push(&objData, sizeof(objData));
  objCBV = pFrameResources->ConstBufferStack.Top();
  pFrameResources->ConstBufferStack.Push(&sceneData, sizeof(sceneData));
  sceneCBV = pFrameResources->ConstBufferStack.Top();
  pCommandList->SetGraphicsRootConstantBufferView(0, objCBV.BufferLocation);
  pCommandList->SetGraphicsRootConstantBufferView(2, sceneCBV.BufferLocation);

  pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  pCommandList->IASetVertexBuffers(0, 1, &m_aMirrorVBVs[iMirror]);
  pCommandList->DrawInstanced(4, 1, 0, 0);

  // Clear depth value in stencil area
  pPipelineStateTuple = &m_aPipelineLib[NAMED_PIPELINE_INDEX_MIRRORED_CLEAR_DEPTH_S0 + sindex];
  pCommandList->SetPipelineState(pPipelineStateTuple->PSO.Get());
  pCommandList->SetGraphicsRootSignature(pPipelineStateTuple->RootSignature.Get());
  pCommandList->OMSetStencilRef(stencilRef);

  sceneData.m_mViewProj._31 = sceneData.m_mViewProj._41;
  sceneData.m_mViewProj._32 = sceneData.m_mViewProj._42;
  sceneData.m_mViewProj._33 = sceneData.m_mViewProj._43;
  sceneData.m_mViewProj._34 = sceneData.m_mViewProj._44;
  pFrameResources->ConstBufferStack.Push(&sceneData, sizeof(sceneData));
  sceneCBV = pFrameResources->ConstBufferStack.Top();
  pCommandList->SetGraphicsRootConstantBufferView(0, objCBV.BufferLocation);
  pCommandList->SetGraphicsRootConstantBufferView(2, sceneCBV.BufferLocation);

  pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  pCommandList->IASetVertexBuffers(0, 1, &m_aMirrorVBVs[iMirror]);
  pCommandList->DrawInstanced(4, 1, 0, 0);

  SceneParamsStatic staticParams = {};
  SceneParamsDynamic dynamicParams = {};

  staticParams.hDepthStencilView = DepthStencilView();
  staticParams.hRenderTargetView = CurrentBackBufferView();
  staticParams.pConstBufferStack = &pFrameResources->ConstBufferStack;
  staticParams.pPipelineStateTuple = &m_aPipelineLib[NAMED_PIPELINE_INDEX_MIRRORED_RENDERING_S0 + iMirror];
  staticParams.pViewport = &m_ScreenViewport;
  staticParams.uStencilRef = stencilRef;
  staticParams.vMirrorPlane = m_aMirrorPlanes[iMirror];
  XMStoreFloat4(&staticParams.vTintColor, s_vMirrorTint);

  XMStoreFloat4x4(&dynamicParams.matViewProj, matReflect * matViewProj);

  RenderScene(pCommandList, &staticParams, &dynamicParams);

  // Clear stencil value and overwrite depth value of the mirror
  pPipelineStateTuple = &m_aPipelineLib[NAMED_PIPELINE_INDEX_RENDERING_MIRROR_S0 + sindex];
  pCommandList->SetPipelineState(pPipelineStateTuple->PSO.Get());
  pCommandList->SetGraphicsRootSignature(pPipelineStateTuple->RootSignature.Get());
  pCommandList->OMSetStencilRef(stencilRef);

  XMStoreFloat4x4(&sceneData.m_mViewProj, XMMatrixTranspose(matViewProj));
  pFrameResources->ConstBufferStack.Push(&sceneData, sizeof(sceneData));
  sceneCBV = pFrameResources->ConstBufferStack.Top();
  pCommandList->SetGraphicsRootConstantBufferView(0, objCBV.BufferLocation);
  pCommandList->SetGraphicsRootConstantBufferView(2, sceneCBV.BufferLocation);

  pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  pCommandList->IASetVertexBuffers(0, 1, &m_aMirrorVBVs[iMirror]);
  pCommandList->DrawInstanced(4, 1, 0, 0);
}

static VOID CALLBACK _PerSceneRenderDeferredProc(
  _Inout_     PTP_CALLBACK_INSTANCE Instance,
  _Inout_opt_ PVOID                 Context,
  _Inout_     PTP_WORK              Work
) {

}

static VOID CALLBACK _PerChunkRenderDeferredProc(
  _Inout_     PTP_CALLBACK_INSTANCE Instance,
  _Inout_opt_ PVOID                 Context,
  _Inout_     PTP_WORK              Work
) {

}

void MultithreadedRenderingSample::OnRenderFrame(float fTime, float fElapsed) {

  HRESULT hr;
  auto pFrameResources = &m_aFrameResources[m_iFrameIndex];

  V(pFrameResources->CommandAllocator->Reset());
  V(m_pd3dCommandList->Reset(pFrameResources->CommandAllocator.Get(), nullptr));

  SceneParamsStatic staticParamsShadow = {};
  SceneParamsDynamic dynamicParamsShadow = {};

  for(int i = 0; i < s_iNumShadows; ++i) {
    staticParamsShadow.bRenderShadow = TRUE;
    staticParamsShadow.pPipelineStateTuple = &m_aPipelineLib[NAMED_PIPELINE_INDEX_SHADOW];
    staticParamsShadow.hDepthStencilView = CD3DX12_CPU_DESCRIPTOR_HANDLE(
      m_pDSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), i+1, m_uDsvDescriptorSize
    );
    staticParamsShadow.pShadowTexture = m_aShadowTextures[i].Get();
    staticParamsShadow.pConstBufferStack = &pFrameResources->ConstBufferStack;
    staticParamsShadow.pViewport = &m_aShadowViewport;

    XMStoreFloat4x4(&dynamicParamsShadow.matViewProj, CalcLightViewProj(i, FALSE));

    m_pd3dCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_aShadowTextures[i].Get(),
      D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_WRITE));
    m_pd3dCommandList->ClearDepthStencilView(staticParamsShadow.hDepthStencilView, D3D12_CLEAR_FLAG_DEPTH, 1.0, 0, 0, nullptr);
    m_pd3dCommandList->OMSetRenderTargets(0, nullptr, FALSE, &staticParamsShadow.hDepthStencilView);

    RenderScene(m_pd3dCommandList, &staticParamsShadow, &dynamicParamsShadow);

    m_pd3dCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_aShadowTextures[i].Get(),
      D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));
  }

  PrepareNextFrame();

  SceneParamsStatic staticParamsDirect = {};
  SceneParamsDynamic dynamicParamsDirect = {};

  staticParamsDirect.pPipelineStateTuple = &m_aPipelineLib[NAMED_PIPELINE_INDEX_NORMAL];
  staticParamsDirect.hRenderTargetView = CurrentBackBufferView();
  staticParamsDirect.hDepthStencilView = DepthStencilView();
  staticParamsDirect.pConstBufferStack = &pFrameResources->ConstBufferStack;
  staticParamsDirect.pViewport = &m_ScreenViewport;
  XMStoreFloat4(&staticParamsDirect.vMirrorPlane, g_XMZero);
  XMStoreFloat4(&staticParamsDirect.vTintColor, Colors::White);

#ifdef RENDER_SCENE_LIGHT_POV
  if(m_bRenderSceneLightPOV)
    XMStoreFloat4x4(&dynamicParamsDirect.matViewProj, CalcLightViewProj( 0, TRUE ));
  else
#endif
    XMStoreFloat4x4(&dynamicParamsDirect.matViewProj, m_Camera.GetViewMatrix() * m_Camera.GetProjMatrix());

  m_pd3dCommandList->ClearRenderTargetView(staticParamsDirect.hRenderTargetView, Colors::MidnightBlue, 0, nullptr);
  m_pd3dCommandList->ClearDepthStencilView(staticParamsDirect.hDepthStencilView, D3D12_CLEAR_FLAG_DEPTH|D3D12_CLEAR_FLAG_STENCIL,
    1.0f, 0, 0, nullptr);
  m_pd3dCommandList->OMSetRenderTargets(1, &staticParamsDirect.hRenderTargetView, TRUE, &staticParamsDirect.hDepthStencilView);
  m_pd3dCommandList->RSSetViewports(1, &m_ScreenViewport);
  m_pd3dCommandList->RSSetScissorRects(1, &m_ScissorRect);

  for(int i = 0; i < s_iNumMirrors; ++i) {
    RenderMirror(i, pFrameResources, m_pd3dCommandList);
  }

  RenderScene(m_pd3dCommandList, &staticParamsDirect, &dynamicParamsDirect);

  ImGuiInteractor::OnRender(m_pd3dDevice, m_pd3dCommandList);

  EndRenderFrame();

  m_pd3dCommandList->Close();
  m_pd3dCommandQueue->ExecuteCommandLists(1, CommandListCast(&m_pd3dCommandList));

  m_pSyncFence->Signal(m_pd3dCommandQueue, &pFrameResources->FencePoint);

  Present();
}

LRESULT MultithreadedRenderingSample::OnMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool *pbNoFurtherProcessing) {

  if(msg == WM_KEYDOWN && wp == VK_F4) {
    m_bRenderSceneLightPOV ^= 1;
  }

  m_Camera.HandleMessages(hwnd, msg, wp, lp);
  return ImGuiInteractor::OnMsgProc(hwnd, msg, wp, lp, pbNoFurtherProcessing);
}

//  Helper function to count bits in the processor mask
static DWORD CountSetBits(ULONG_PTR bitMask)
{
    DWORD LSHIFT = sizeof(ULONG_PTR)*8 - 1;
    DWORD bitSetCount = 0;
    ULONG_PTR bitTest = ULONG_PTR(1) << LSHIFT;
    DWORD i;

    for( i = 0; i <= LSHIFT; ++i)
    {
        bitSetCount += ((bitMask & bitTest)?1:0);
        bitTest/=2;
    }

    return bitSetCount;
}

static int GetLogicalProcessorCount()
{
    DWORD procCoreCount = 0;    // Return 0 on any failure.  That'll show them.
    auto Glpi = &GetLogicalProcessorInformation;

    bool done = false;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = nullptr;
    DWORD returnLength = 0;

    while (!done) 
    {
        BOOL rc = Glpi(buffer, &returnLength);

        if (FALSE == rc) 
        {
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) 
            {
                if (buffer) 
                    free(buffer);

                buffer = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION>( malloc( returnLength ) );

                if ( !buffer ) 
                {
                    // Allocation failure
                    return procCoreCount;
                }
            } 
            else 
            {
                // Unanticipated error
                return procCoreCount;
            }
        } 
        else done = true;
    }

    assert( buffer );
    _Analysis_assume_( buffer );

    DWORD byteOffset = 0;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr = buffer;
    while (byteOffset < returnLength) 
    {
        if (ptr->Relationship == RelationProcessorCore) 
        {
            if(ptr->ProcessorCore.Flags)
            {
                //  Hyperthreading or SMT is enabled.
                //  Logical processors are on the same core.
                procCoreCount += 1;
            }
            else
            {
                //  Logical processors are on different cores.
                procCoreCount += CountSetBits(ptr->ProcessorMask);
            }
        }
        byteOffset += sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        ptr++;
    }

    free (buffer);

    return procCoreCount;
}

HRESULT MultithreadedRenderingSample::InitializeRendererThreadpool() {

  HRESULT hr = S_OK;
  int minProcCount = 1, maxProcCount = 64;

  m_pThreadPool = CreateThreadpool(nullptr);
  m_pCleanupGroup = CreateThreadpoolCleanupGroup();
  if(!m_pThreadPool || !m_pCleanupGroup)
    return HRESULT_FROM_WIN32(GetLastError());

  InitializeThreadpoolEnvironment(&m_CallbackEnv);

  SetThreadpoolCallbackPool(&m_CallbackEnv, m_pThreadPool);
  SetThreadpoolCallbackCleanupGroup(&m_CallbackEnv, m_pCleanupGroup, nullptr);

  maxProcCount = GetLogicalProcessorCount() - 1;
  maxProcCount = std::max(maxProcCount, minProcCount);
  SetThreadpoolThreadMinimum(m_pThreadPool, minProcCount);
  SetThreadpoolThreadMaximum(m_pThreadPool, maxProcCount);

  m_iWorkQueueMaxParallelCapacity = std::min(64, maxProcCount);

  return hr;
}
