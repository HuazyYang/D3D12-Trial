#include <windows.h>
#include <D3D12RendererContext.hpp>
#include <Win32Application.hpp>
#include <DirectXMath.h>
#include <DirectXColors.h>
#include <ResourceUploadBatch.hpp>
#include <RootSignatureGenerator.h>
#include <array>
#include "MultithreadedDXUTMesh.h"
#include <Camera.h>
#include <UploadBuffer.h>
#include <imgui.h>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx12.h>
#include <DirectXCollision.h>
#include <ShlObj.h>
#include <process.h>

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

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrevInst, PWSTR lpszCmdline, int nShowCmd) {

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

enum SCENE_MT_RENDER_CASE {
  SCENE_MT_RENDER_CASE_DEFAULT,
  SCENE_MT_RENDER_CASE_SHADOW,
  SCENE_MT_RENDER_CASE_MIRROR_AREA,
};

struct SceneParamsStatic {
  SCENE_MT_RENDER_CASE RenderCase;
  PipelineStateTuple *pPipelineStateTuple;
  ID3D12Resource *pShadowTexture; // For rendering shadow map
  D3D12_CPU_DESCRIPTOR_HANDLE hRenderTargetView;
  D3D12_CPU_DESCRIPTOR_HANDLE hDepthStencilView;
  D3D12_VIEWPORT Viewport;
  D3D12_RECT ScissorRect;

  UploadBufferStack *pConstBufferStack; // For allocating constant buffers.

  UINT8 uStencilRef;

  XMFLOAT4 vTintColor;
  XMFLOAT4 vMirrorPlane;
};

struct SceneParamsDynamic {
  XMFLOAT4X4 matViewProj;
};

struct FrameResources {
  ComPtr<ID3D12CommandAllocator> CommandAllocator;

  ComPtr<ID3D12GraphicsCommandList> ShadowCommandLists[s_iNumShadows]; // Shared across frames in flight
  ComPtr<ID3D12CommandAllocator> ShadowCommandAllocators[s_iNumShadows];
  ComPtr<ID3D12GraphicsCommandList> MirrorCommandLists[s_iNumMirrors]; // Shared across frames in flight
  ComPtr<ID3D12CommandAllocator> MirrorCommandAllocators[s_iNumMirrors];

  ComPtr<ID3D12GraphicsCommandList> ChunkCommandLists[MAXIMUM_WAIT_OBJECTS]; // Shared across frames in flight
  ComPtr<ID3D12CommandAllocator> ChunkCommandAllocators[MAXIMUM_WAIT_OBJECTS];

  UploadBufferStack ConstBufferStack;
  UINT64 FencePoint;
};

class MultithreadedRenderingSample;

struct SCENE_RENDERING_THREAD_PARAMS {
  int SlotIndex;
  HANDLE CompletionEvent;
  FrameResources                      *pFrameResources;
  MultithreadedRenderingSample        *pInstance;
  int BatchIndex;
  SCENE_MT_RENDER_CASE RenderCase;
};

struct CHUNK_RENDERING_THREAD_PARAMS {
  MultithreadedRenderingSample *pInstance;
  int ChunkIndex;
  FrameResources *pFrameResources;
  HANDLE PassBeginEvent;
  HANDLE PassEndEvent;
};

// Chunk thread local variables
struct CHUNK_RENDERING_THREAD_LOCAL_VARS {
  int ChunkIndex;
  volatile ULONG NextDrawcallIndex;
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
    CHAR szArialFilePath[MAX_PATH];
    if(SHGetSpecialFolderPathA(nullptr, szArialFilePath, CSIDL_FONTS, FALSE)) {
      strcat_s(szArialFilePath, "\\Arial.ttf");
      io.Fonts->AddFontFromFileTTF(szArialFilePath, 16);
    }
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

    ImGui::Begin("Rendering opts", nullptr, ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_AlwaysAutoResize);
    ImGui::RadioButton("ST Def", (int*)&m_RenderSchedulingOption, RENDER_SCHEDULING_OPTION_ST);
    ImGui::RadioButton("MT Def/Scene", (int*)&m_RenderSchedulingOption, RENDER_SCHEDULING_OPTION_MT_SCENE);
    ImGui::RadioButton("MT Def/Chunk", (int*)&m_RenderSchedulingOption, RENDER_SCHEDULING_OPTION_MT_CHUNK);
    ImGui::Separator();
    ImGui::CheckboxFlags("Enable tight mirror stencil clipping space", &m_bOptmizeMirrorClipSpace, TRUE);
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

    BeginInteraction();
    LRESULT ret = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
    EndInteraction();
    return ret;
  }

  BOOL IsMultithreadedPerScene() const {
    return m_RenderSchedulingOption == RENDER_SCHEDULING_OPTION_MT_SCENE;
  }

  BOOL IsSinglethreadedDeferred() const {
    return m_RenderSchedulingOption == RENDER_SCHEDULING_OPTION_ST;
  }

  BOOL IsMultithreadedPerChunk() const {
    return m_RenderSchedulingOption == RENDER_SCHEDULING_OPTION_MT_CHUNK;
  }

  BOOL IsEnableOptmizeMirrorClipSpace() const {
    return m_bOptmizeMirrorClipSpace;
  }

private:
  void BeginInteraction() {
    ImGui::SetCurrentContext(m_pImGuiCtx);
  }
  void EndInteraction() {
  }

  ImGuiContext *m_pImGuiCtx = nullptr;
  ComPtr<ID3D12DescriptorHeap> m_pImGuiSrvHeap;

protected:
  // UI data
  RENDER_SCHEDULING_OPTIONS m_RenderSchedulingOption = RENDER_SCHEDULING_OPTION_ST;
  BOOL m_bOptmizeMirrorClipSpace = FALSE;
};

class MultithreadedRenderingSample : public D3D12RendererContext, public ImGuiInteractor {

public:
  MultithreadedRenderingSample();

private:
  HRESULT OnInitPipelines() override;
  void OnDestroy() override;
  UINT GetExtraDSVDescriptorCount() const override;
  void OnFrameMoved(float fTime, float fElapsed) override;
  void OnRenderFrame(float fTime, float fElapsed) override;
  void OnResizeFrame(int cx, int cy) override;
  void RenderScene(ID3D12GraphicsCommandList *pCommandList, const SceneParamsStatic *pStaticParams,
                   const SceneParamsDynamic *pDynamicParams);

  void RenderShadow(int iShadow, FrameResources *pFrameResources);
  void RenderMirror(int iMirror, FrameResources *pFrameResources);
  void RenderSceneDirect(FrameResources *pFrameResources);
  void OnPerChunkRenderDeferred(int chunkIndex, FrameResources* const* ppFrameResources);

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

  static VOID CALLBACK _PerSceneRenderDeferredProc(
    _Inout_     PTP_CALLBACK_INSTANCE Instance,
    _Inout_opt_ PVOID                 Context,
    _Inout_     PTP_WORK              Work
  );

  static unsigned int WINAPI _PerChunkRenderDeferredProc(LPVOID pv);

  static void RenderMesh(CMultithreadedDXUTMesh *pMesh,
                         UINT iMesh,
                         bool bAdjacent,
                         ID3D12GraphicsCommandList *pd3dCommandList,
                         D3D12_GPU_DESCRIPTOR_HANDLE hDescriptorStart,
                         UINT iDiffuseSlot,
                         UINT iNormalSlot,
                         UINT iSpecularSlot,
                         void *pUserContext);

  int GetCurrentChunkThreadIndex() const;
  void ResetCurrentChunkThreadDrawcallIndex();
  // Increment current chunk thread drawcall index
  // and return the previous draw call index
  ULONG IncrementCurrentChunkThreadDrawcallIndex();

  CMultithreadedDXUTMesh m_Model;
  ComPtr<ID3D12DescriptorHeap> m_pModelDescriptorHeap;
  std::future<HRESULT> m_InitPipelineWaitable;

  // Mirror models
  ComPtr<ID3D12Resource> m_pMirrorVertexBuffer;
  std::array<D3D12_VERTEX_BUFFER_VIEW, s_iNumMirrors> m_aMirrorVBVs;
  std::array<XMFLOAT4X4, s_iNumMirrors>             m_aMirrorWorldMatrices;
  std::array<XMFLOAT4, s_iNumMirrors>               m_aMirrorPlanes;
  std::array<BoundingBox, s_iNumMirrors>            m_aMirrorLocalAABBs;

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
  PTP_POOL m_pThreadpool = nullptr;
  PTP_CLEANUP_GROUP m_pCleanupGroup = nullptr;
  TP_CALLBACK_ENVIRON m_CallbackEnv;

  PTP_WORK m_aShadowWorkQueuePool[s_iNumShadows];
  SCENE_RENDERING_THREAD_PARAMS m_aShadowWorkQueueParams[s_iNumShadows];
  PTP_WORK m_aMirrorWorkQueuePool[s_iNumMirrors];
  SCENE_RENDERING_THREAD_PARAMS m_aMirrorWorkQueuePoolParams[s_iNumMirrors];
  DWORD m_dwChunkThreadsLocalSlot; // Chunk threads local index variable slot.
  UINT   m_uNumberOfChunkThreads; // Work queue item count in parallel for a single render frame.
  HANDLE m_aChunkThreads[MAXIMUM_WAIT_OBJECTS];
  CHUNK_RENDERING_THREAD_PARAMS m_aChunkThreadArgs[MAXIMUM_WAIT_OBJECTS];
  CHUNK_RENDERING_THREAD_LOCAL_VARS m_aChunkThreadLocalVars[MAXIMUM_WAIT_OBJECTS+1];
  volatile LONG m_lShowdownChunkThreads = 0;
};

HRESULT CreateMultithreadRenderingRendererAndInteractor(D3D12RendererContext **ppRenderer,
                                                        WindowInteractor **ppInteractor) {
  HRESULT hr = S_OK;
  auto pInstance = new MultithreadedRenderingSample;
  *ppRenderer = pInstance;
  *ppInteractor = pInstance;
  return hr;
}

MultithreadedRenderingSample::MultithreadedRenderingSample() {
  m_aDeviceConfig.SwapChainBackBufferFormatSRGB = TRUE;
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
  // thread pool
  V_RETURN(InitializeRendererThreadpool());
  V_RETURN(CreateFrameResources());

  InitCamera();
  InitLights();

  V_RETURN(ImGuiInteractor::OnInitialize(m_pd3dDevice, s_uTotalFrameCount, m_BackBufferFormat));

  V_RETURN(m_InitPipelineWaitable.get());
  return hr;
}

void MultithreadedRenderingSample::OnDestroy() {
  ImGuiInteractor::OnDestroy();

  for(auto &param : m_aShadowWorkQueueParams) {
    if(param.CompletionEvent != NULL) {
      CloseHandle(param.CompletionEvent);
      param.CompletionEvent = nullptr;
    }
  }
  for(auto &param : m_aMirrorWorkQueuePoolParams) {
    if(param.CompletionEvent != NULL) {
      CloseHandle(param.CompletionEvent);
      param.CompletionEvent = nullptr;
    }
  }

  if(m_pCleanupGroup) {
    CloseThreadpoolCleanupGroupMembers(m_pCleanupGroup, TRUE, nullptr);
    CloseThreadpoolCleanupGroup(m_pCleanupGroup);
  }
  if(m_pThreadpool)
    CloseThreadpool(m_pThreadpool);

  // Notify the chunk threads that we want exit now
  m_lShowdownChunkThreads = 1;
  FrameResources *pFrameResources = &m_aFrameResources[0];
  OnPerChunkRenderDeferred(-1, &pFrameResources);

  int numChunkThreads = m_uNumberOfChunkThreads - 1;
  for(int i = 0; i < numChunkThreads; ++i) {
    if(m_aChunkThreads[i] != nullptr)
      CloseHandle(m_aChunkThreads[i]);
    if(m_aChunkThreadArgs[i].PassBeginEvent != nullptr)
      CloseHandle(m_aChunkThreadArgs[i].PassBeginEvent);
    if(m_aChunkThreadArgs[i].PassEndEvent != nullptr)
      CloseHandle(m_aChunkThreadArgs[i].PassEndEvent);
  }
}

HRESULT MultithreadedRenderingSample::CreateFrameResources() {
  HRESULT hr = S_OK;
  char nameBuf[256];
  int numChunkThreads = m_uNumberOfChunkThreads - 1;
  int findex;
  const FrameResources *pFrameResources0 = &m_aFrameResources[0];

  for(findex = 0; findex < _countof(m_aFrameResources); ++findex) {

    auto &frameResources = m_aFrameResources[findex];

    V_RETURN(frameResources.ConstBufferStack.Initialize(m_pd3dDevice, (1 << 14), 1));
    V_RETURN(m_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                  IID_PPV_ARGS(&frameResources.CommandAllocator)));
    for(int i = 0; i < s_iNumShadows; ++i) {
      V_RETURN(m_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                    IID_PPV_ARGS(&frameResources.ShadowCommandAllocators[i])));
      sprintf_s(nameBuf, "ShadowCommandAlloactors[%d]", i);
      DX_SetDebugName(frameResources.ShadowCommandAllocators[i].Get(), nameBuf);
      if(findex == 0) {
        V_RETURN(m_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                frameResources.ShadowCommandAllocators[0].Get(), nullptr,
                                                IID_PPV_ARGS(&frameResources.ShadowCommandLists[i])));
        sprintf_s(nameBuf, "ShadowCommandLists[%d]", i);
        DX_SetDebugName(frameResources.ShadowCommandLists[i].Get(), nameBuf);
        frameResources.ShadowCommandLists[i]->Close();
      } else {
        frameResources.ShadowCommandLists[i] = pFrameResources0->ShadowCommandLists[i];
      }
    }

    for(int i = 0; i < s_iNumMirrors; ++i) {
      V_RETURN(m_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                    IID_PPV_ARGS(&frameResources.MirrorCommandAllocators[i])));
      sprintf_s(nameBuf, "MirrorCommandAllocators[%d]", i);
      DX_SetDebugName(frameResources.MirrorCommandAllocators[i].Get(), nameBuf);
      if(findex == 0) {
        V_RETURN(m_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                 frameResources.MirrorCommandAllocators[0].Get(), nullptr,
                                                 IID_PPV_ARGS(&frameResources.MirrorCommandLists[i])));
        sprintf_s(nameBuf, "MirrorCommandLists[%d]", i);
        DX_SetDebugName(frameResources.MirrorCommandLists[i].Get(), nameBuf);
        frameResources.MirrorCommandLists[i]->Close();
      } else {
        frameResources.MirrorCommandLists[i] = pFrameResources0->MirrorCommandLists[i];
      }
    }

    for(int i = 0; i < numChunkThreads; ++i) {
      V_RETURN(m_pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                    IID_PPV_ARGS(&frameResources.ChunkCommandAllocators[i])));
      sprintf_s(nameBuf, "ChunkCommandAllocators[%d]", i);
      DX_SetDebugName(frameResources.ChunkCommandAllocators[i].Get(), nameBuf);
      if(findex == 0) {
        V_RETURN(m_pd3dDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                frameResources.ChunkCommandAllocators[i].Get(), nullptr,
                                                IID_PPV_ARGS(&frameResources.ChunkCommandLists[i])));
        sprintf_s(nameBuf, "ChunkCommandLists[%d]", i);
        DX_SetDebugName(frameResources.ChunkCommandLists[i].Get(), nameBuf);
        frameResources.ChunkCommandLists[i]->Close();
      } else {
        frameResources.ChunkCommandLists[i] = pFrameResources0->ChunkCommandLists[i];
      }
    }
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
  MT_SDKMESH_CALLBACKS12 createAndRenderCallbacks = {};

  V_RETURN(uploadBatch.Begin(D3D12_COMMAND_LIST_TYPE_DIRECT));

  createAndRenderCallbacks.RenderMeshCallback.pRenderMesh = &MultithreadedRenderingSample::RenderMesh;
  createAndRenderCallbacks.RenderMeshCallback.pRenderUserContext = this;

  V_RETURN(m_Model.Create(&uploadBatch, LR"(directx-sdk-samples\Media\SquidRoom\SquidRoom.sdkmesh)",
    &createAndRenderCallbacks));

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

    BoundingBox::CreateFromPoints(m_aMirrorLocalAABBs[i], 4, &mirrorVertices[4*i].Position, sizeof(MirrorVertex));

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

  RENDER_SCHEDULING_OPTIONS ropts = m_RenderSchedulingOption;

  ImGuiInteractor::OnFrameMoved(m_pd3dDevice);

  if(ropts != m_RenderSchedulingOption) {
    // Fine tune work threads priority
    if (IsMultithreadedPerScene()) {
      SetThreadpoolCallbackPriority(&m_CallbackEnv, TP_CALLBACK_PRIORITY_NORMAL);
    } else {
      SetThreadpoolCallbackPriority(&m_CallbackEnv, TP_CALLBACK_PRIORITY_LOW);
    }

    if(IsMultithreadedPerChunk()) {
      for (UINT i = 0; i < m_uNumberOfChunkThreads; ++i) {
        SetThreadPriority(&m_aChunkThreads[i - 1], THREAD_PRIORITY_NORMAL);
      }
    } else {
      for (UINT i = 0; i < m_uNumberOfChunkThreads; ++i) {
        SetThreadPriority(&m_aChunkThreads[i - 1], THREAD_PRIORITY_LOWEST);
      }
    }
  }
}

XMMATRIX MultithreadedRenderingSample::CalcLightViewProj( int iLight, BOOL bAdapterFOV )
{
    XMVECTOR vLightDir = g_vLightDir[iLight];
    XMVECTOR vLightPos = g_vLightPos[iLight];

    XMVECTOR vLookAt = vLightPos + s_fSceneRadius * vLightDir;

    XMMATRIX mLightView = XMMatrixLookAtLH( vLightPos, vLookAt, g_XMIdentityR1 );

    XMMATRIX mLightProj =
        XMMatrixPerspectiveFovLH(g_fLightFOV[iLight], bAdapterFOV ? GetAspectRatio() : g_fLightAspect[iLight],
                                 g_fLightNearPlane[iLight], g_fLightFarPlane[iLight]);

    return mLightView * mLightProj;
}

int MultithreadedRenderingSample::GetCurrentChunkThreadIndex() const {
  auto pVars = reinterpret_cast<CHUNK_RENDERING_THREAD_LOCAL_VARS *>(TlsGetValue(m_dwChunkThreadsLocalSlot));
  return pVars->ChunkIndex;
}

void MultithreadedRenderingSample::ResetCurrentChunkThreadDrawcallIndex() {
  auto pVars = reinterpret_cast<CHUNK_RENDERING_THREAD_LOCAL_VARS *>(TlsGetValue(m_dwChunkThreadsLocalSlot));
  pVars->NextDrawcallIndex = 0;
}

ULONG MultithreadedRenderingSample::IncrementCurrentChunkThreadDrawcallIndex() {
  auto pVars = reinterpret_cast<CHUNK_RENDERING_THREAD_LOCAL_VARS *>(TlsGetValue(m_dwChunkThreadsLocalSlot));
  return InterlockedIncrement(&pVars->NextDrawcallIndex) - 1;
}

void MultithreadedRenderingSample::RenderMesh(
  CMultithreadedDXUTMesh* pMesh, 
  UINT iMesh,
  bool bAdjacent,
  ID3D12GraphicsCommandList* pd3dCommandList,
  D3D12_GPU_DESCRIPTOR_HANDLE hDescriptorStart,
  UINT iDiffuseSlot,
  UINT iNormalSlot,
  UINT iSpecularSlot,
  void *pUserContext
) {

  ULONG chunkIndex;

  auto pSample = reinterpret_cast<MultithreadedRenderingSample *>(pUserContext);

// Skip the task which is not in current thread slot. 
 if(pSample->IsMultithreadedPerChunk()) {
   chunkIndex = pSample->GetCurrentChunkThreadIndex() + 1;
   if ((pSample->IncrementCurrentChunkThreadDrawcallIndex() % (ULONG)pSample->m_uNumberOfChunkThreads) == chunkIndex) {
     pMesh->RenderMesh(
       iMesh,  
       bAdjacent,
       pd3dCommandList,
       hDescriptorStart,
       iDiffuseSlot,
       iNormalSlot,
       iSpecularSlot);
   }
  } else {
    pMesh->RenderMesh(
      iMesh,
      bAdjacent,
      pd3dCommandList,
      hDescriptorStart,
      iDiffuseSlot,
      iNormalSlot,
      iSpecularSlot);
  }
}

void MultithreadedRenderingSample::RenderScene(ID3D12GraphicsCommandList *pCommandList,
                                               const SceneParamsStatic *pSceneParamsStatic,
                                               const SceneParamsDynamic *pSceneParamsDynamic) {

  D3D12_CONSTANT_BUFFER_VIEW_DESC CBV;

  pCommandList->SetPipelineState(pSceneParamsStatic->pPipelineStateTuple->PSO.Get());
  pCommandList->SetGraphicsRootSignature(pSceneParamsStatic->pPipelineStateTuple->RootSignature.Get());
  pCommandList->SetDescriptorHeaps(1, m_pModelDescriptorHeap.GetAddressOf());

  pCommandList->RSSetViewports(1, &pSceneParamsStatic->Viewport);
  pCommandList->RSSetScissorRects(1, &pSceneParamsStatic->ScissorRect);
  pCommandList->OMSetStencilRef(pSceneParamsStatic->uStencilRef);

  if (pSceneParamsStatic->RenderCase != SCENE_MT_RENDER_CASE_SHADOW) {
    pCommandList->SetGraphicsRootDescriptorTable(5, m_pModelDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
  }

  CB_PER_SCENE sceneData;
  XMMATRIX M = XMLoadFloat4x4(&pSceneParamsDynamic->matViewProj);
  XMStoreFloat4x4(&sceneData.m_mViewProj, XMMatrixTranspose(M));
  sceneData.m_vTintColor = pSceneParamsStatic->vTintColor;
  XMStoreFloat4(&sceneData.m_vAmbientColor, s_vAmbientColor);
  sceneData.m_vMirrorPlane = pSceneParamsStatic->vMirrorPlane;
  pSceneParamsStatic->pConstBufferStack->Push(&sceneData, sizeof(sceneData), &CBV);
  pCommandList->SetGraphicsRootConstantBufferView(2, CBV.BufferLocation);

  if(pSceneParamsStatic->RenderCase != SCENE_MT_RENDER_CASE_SHADOW) {
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
    pSceneParamsStatic->pConstBufferStack->Push(&lightData, sizeof(lightData), &CBV);
    pCommandList->SetGraphicsRootConstantBufferView(1, CBV.BufferLocation);
  }

  CB_PER_OBJECT objData;
  XMStoreFloat4x4(&objData.m_mWorld, XMMatrixIdentity());
  XMStoreFloat4(&objData.m_vObjectColor, Colors::White);
  pSceneParamsStatic->pConstBufferStack->Push(&objData, sizeof(objData), &CBV);
  pCommandList->SetGraphicsRootConstantBufferView(0, CBV.BufferLocation);

  if(IsMultithreadedPerChunk())
    ResetCurrentChunkThreadDrawcallIndex();
  m_Model.Render(pCommandList,
    CD3DX12_GPU_DESCRIPTOR_HANDLE(m_pModelDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
      s_iNumShadows, m_uCbvSrvUavDescriptorSize),
      3, 4);
}

void MultithreadedRenderingSample::RenderMirror(int iMirror, FrameResources *pFrameResources) {

  ID3D12GraphicsCommandList *pCommandList;
  int chunkIndex = -1;

  if (IsMultithreadedPerScene()) {
    pCommandList = pFrameResources->MirrorCommandLists[iMirror].Get();

    pFrameResources->MirrorCommandAllocators[iMirror]->Reset();
    pCommandList->Reset(pFrameResources->MirrorCommandAllocators[iMirror].Get(), nullptr);
  } else if(IsMultithreadedPerChunk()) {
    chunkIndex = GetCurrentChunkThreadIndex();

    if(chunkIndex < 0) {
      m_pd3dCommandList->Reset(pFrameResources->CommandAllocator.Get(), nullptr);
      pCommandList = m_pd3dCommandList;
    } else {
      pFrameResources->ChunkCommandLists[chunkIndex]->Reset(pFrameResources->ChunkCommandAllocators[chunkIndex].Get(), nullptr);
      pFrameResources->ChunkCommandLists[chunkIndex]->Close();
      pCommandList = nullptr;
    }
  } else {
    pCommandList = m_pd3dCommandList;
  }

  D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = CurrentBackBufferView();
  D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = DepthStencilView();

  if (iMirror == 0) {
    if(chunkIndex < 0) {

      PrepareNextFrame(pCommandList);

      pCommandList->ClearRenderTargetView(rtvHandle, Colors::MidnightBlue, 0, nullptr);
      pCommandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0,
                                          nullptr);

      // Shadow SRVs
      CD3DX12_RESOURCE_BARRIER shadowBarriers[s_iNumShadows];
      for(int i = 0; i < s_iNumShadows; ++i) {
        shadowBarriers[i] = CD3DX12_RESOURCE_BARRIER::Transition(m_aShadowTextures[i].Get(),
          D3D12_RESOURCE_STATE_DEPTH_WRITE,
          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
      }
      pCommandList->ResourceBarrier(s_iNumShadows, shadowBarriers);
    }
  }

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
  if(XMVectorGetX(XMPlaneDotCoord(vMirrorPlane, vEyePt)) < 0.0f) {
    if(IsMultithreadedPerScene() || (IsMultithreadedPerChunk() && pCommandList)) {
      pCommandList->Close();
    }
    return;
  }

  XMMATRIX matMirrorWorld = XMLoadFloat4x4(&m_aMirrorWorldMatrices[iMirror]);
  XMMATRIX matReflect = XMMatrixReflect(vMirrorPlane);

  UINT sindex = iMirror % 8;
  UINT8 stencilRef = 1 << sindex;
  D3D12_RECT stencilAreaRect;

  auto pUploadBufferStack = &pFrameResources->ConstBufferStack;
  D3D12_CONSTANT_BUFFER_VIEW_DESC objCBV, sceneCBV;
  CB_PER_OBJECT objData;
  CB_PER_SCENE sceneData;
  PipelineStateTuple *pPipelineStateTuple;

  if (IsEnableOptmizeMirrorClipSpace()) {
    XMMATRIX matWVPS;
    float w1 = static_cast<float>(m_ScissorRect.right - m_ScissorRect.left) * 0.5f;
    float h1 = static_cast<float>(m_ScissorRect.bottom - m_ScissorRect.top) * 0.5f;
    XMFLOAT3 vCorners[8];
    XMVECTOR vScCorner, vViewDepth;
    XMVECTOR vScMin, vScMax;

    matWVPS = matMirrorWorld * matViewProj *
              XMMatrixSet(
                w1, .0f, .0f, .0f,
                .0f, -h1, .0f, .0f,
                .0f, .0f, .0f, .0f,
                w1, h1, .0f, 1.0f);

    m_aMirrorLocalAABBs[iMirror].GetCorners(vCorners);
    vScMin = g_XMFltMax;
    vScMax = g_XMFltMin;

    for (int i = 0; i < 4; ++i) {
      vScCorner = XMLoadFloat3(&vCorners[i]);
      vScCorner = XMVectorSetW(vScCorner, 1.0f);
      vScCorner = XMVector4Transform(vScCorner, matWVPS);
      vViewDepth = XMVectorSplatW(vScCorner);
      vScCorner = XMVectorDivide(vScCorner, vViewDepth);
      vScMin = XMVectorMin(vScMin, vScCorner);
      vScMax = XMVectorMax(vScMax, vScCorner);
    }

    vScMin = XMVectorFloor(XMVectorMax(vScMin, g_XMZero));
    vScMax = XMVectorCeiling(XMVectorMin(vScMax, XMVectorSet(2.0f * w1, 2.0f * h1, 0.0, 0.0)));

    stencilAreaRect.left   = static_cast<LONG>(XMVectorGetX(vScMin));
    stencilAreaRect.right  = static_cast<LONG>(XMVectorGetX(vScMax));
    stencilAreaRect.top    = static_cast<LONG>(XMVectorGetY(vScMin));
    stencilAreaRect.bottom = static_cast<LONG>(XMVectorGetY(vScMax));
  } else {
    stencilAreaRect = m_ScissorRect;
  }

  // Write mirrored area stencil value
  if (chunkIndex < 0) {

    pCommandList->OMSetRenderTargets(1, &rtvHandle, TRUE, &dsvHandle);
    pCommandList->RSSetViewports(1, &m_ScreenViewport);
    pCommandList->RSSetScissorRects(1, &stencilAreaRect);

    pPipelineStateTuple = &m_aPipelineLib[NAMED_PIPELINE_INDEX_MIRRORED_S0 + sindex];

    pCommandList->SetPipelineState(pPipelineStateTuple->PSO.Get());
    pCommandList->SetGraphicsRootSignature(pPipelineStateTuple->RootSignature.Get());
    pCommandList->OMSetStencilRef(stencilRef);

    XMStoreFloat4x4(&objData.m_mWorld, XMMatrixTranspose(matMirrorWorld));
    XMStoreFloat4x4(&sceneData.m_mViewProj, XMMatrixTranspose(matViewProj));
    pUploadBufferStack->Push(&objData, sizeof(objData), &objCBV);
    pUploadBufferStack->Push(&sceneData, sizeof(sceneData), &sceneCBV);
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
    pUploadBufferStack->Push(&sceneData, sizeof(sceneData), &sceneCBV);
    pCommandList->SetGraphicsRootConstantBufferView(0, objCBV.BufferLocation);
    pCommandList->SetGraphicsRootConstantBufferView(2, sceneCBV.BufferLocation);

    pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    pCommandList->IASetVertexBuffers(0, 1, &m_aMirrorVBVs[iMirror]);
    pCommandList->DrawInstanced(4, 1, 0, 0);
  }

  SceneParamsStatic staticParams = {};
  SceneParamsDynamic dynamicParams = {};

  staticParams.hDepthStencilView = DepthStencilView();
  staticParams.hRenderTargetView = CurrentBackBufferView();
  staticParams.pConstBufferStack = pUploadBufferStack;
  staticParams.pPipelineStateTuple = &m_aPipelineLib[NAMED_PIPELINE_INDEX_MIRRORED_RENDERING_S0 + iMirror];
  staticParams.Viewport = m_ScreenViewport;
  staticParams.ScissorRect = stencilAreaRect;
  staticParams.uStencilRef = stencilRef;
  staticParams.vMirrorPlane = m_aMirrorPlanes[iMirror];
  XMStoreFloat4(&staticParams.vTintColor, s_vMirrorTint);

  XMStoreFloat4x4(&dynamicParams.matViewProj, matReflect * matViewProj);

  if(IsMultithreadedPerChunk()) {
    ID3D12CommandAllocator *pCommandAllocator;

    if(chunkIndex >= 0) {
      pCommandList = pFrameResources->ChunkCommandLists[chunkIndex].Get();
      pCommandAllocator = pFrameResources->ChunkCommandAllocators[chunkIndex].Get();

      pCommandList->Reset(pCommandAllocator, nullptr);
    }

    pCommandList->OMSetRenderTargets(1, &rtvHandle, TRUE, &dsvHandle);

    RenderScene(pCommandList, &staticParams, &dynamicParams);

    if(chunkIndex != m_uNumberOfChunkThreads - 2)
      pCommandList->Close();
  } else {
    RenderScene(pCommandList, &staticParams, &dynamicParams);
  }

  // Clear stencil value and overwrite depth value of the mirror
  if(!IsMultithreadedPerChunk() || chunkIndex == m_uNumberOfChunkThreads - 2) {

    pCommandList->OMSetRenderTargets(1, &rtvHandle, TRUE, &dsvHandle);
    pCommandList->RSSetViewports(1, &m_ScreenViewport);
    pCommandList->RSSetScissorRects(1, &stencilAreaRect); // Optimize this to a samll scissor rect.

    pPipelineStateTuple = &m_aPipelineLib[NAMED_PIPELINE_INDEX_RENDERING_MIRROR_S0 + sindex];
    pCommandList->SetPipelineState(pPipelineStateTuple->PSO.Get());
    pCommandList->SetGraphicsRootSignature(pPipelineStateTuple->RootSignature.Get());
    pCommandList->OMSetStencilRef(stencilRef);

    if(IsMultithreadedPerChunk()) {
      XMStoreFloat4x4(&objData.m_mWorld, XMMatrixTranspose(matMirrorWorld));
      pUploadBufferStack->Push(&objData, sizeof(objData), &objCBV);
    }

    XMStoreFloat4x4(&sceneData.m_mViewProj, XMMatrixTranspose(matViewProj));
    pUploadBufferStack->Push(&sceneData, sizeof(sceneData), &sceneCBV);
    pCommandList->SetGraphicsRootConstantBufferView(0, objCBV.BufferLocation);
    pCommandList->SetGraphicsRootConstantBufferView(2, sceneCBV.BufferLocation);
    pCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    pCommandList->IASetVertexBuffers(0, 1, &m_aMirrorVBVs[iMirror]);
    pCommandList->DrawInstanced(4, 1, 0, 0);

    if (IsMultithreadedPerScene() || IsMultithreadedPerChunk()) {
      // EndRenderFrame(pCommandList);
      pCommandList->Close();
    }
  }
}

void MultithreadedRenderingSample::RenderShadow(int iShadow, FrameResources *pFrameResources) {

  SceneParamsStatic shadowStaticParams = {};
  SceneParamsDynamic shadowDynamicParams = {};
  auto pUploadBufferStack = &pFrameResources->ConstBufferStack;

  shadowStaticParams.RenderCase = SCENE_MT_RENDER_CASE_SHADOW;
  shadowStaticParams.pPipelineStateTuple = &m_aPipelineLib[NAMED_PIPELINE_INDEX_SHADOW];
  shadowStaticParams.hDepthStencilView = CD3DX12_CPU_DESCRIPTOR_HANDLE(
    m_pDSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), iShadow+1, m_uDsvDescriptorSize
  );
  shadowStaticParams.pShadowTexture = m_aShadowTextures[iShadow].Get();
  shadowStaticParams.pConstBufferStack = pUploadBufferStack;
  shadowStaticParams.Viewport = m_aShadowViewport;
  shadowStaticParams.ScissorRect = {
    (LONG)m_aShadowViewport.TopLeftX,
    (LONG)m_aShadowViewport.TopLeftY,
    (LONG)m_aShadowViewport.TopLeftX + (LONG)m_aShadowViewport.Width,
    (LONG)m_aShadowViewport.TopLeftY + (LONG)m_aShadowViewport.Height,
  };

  XMStoreFloat4x4(&shadowDynamicParams.matViewProj, CalcLightViewProj(iShadow, FALSE));

  ID3D12GraphicsCommandList *pCommandList;
  int chunkIndex = -1;

  if(IsMultithreadedPerScene()) {
    pCommandList = pFrameResources->ShadowCommandLists[iShadow].Get();

    pFrameResources->ShadowCommandAllocators[iShadow]->Reset();
    pCommandList->Reset(pFrameResources->ShadowCommandAllocators[iShadow].Get(), nullptr);
  } else if(IsMultithreadedPerChunk()) {

    chunkIndex = GetCurrentChunkThreadIndex();
    if(chunkIndex < 0)
      // Main thread
      pCommandList = m_pd3dCommandList;
    else
      pCommandList = pFrameResources->ChunkCommandLists[chunkIndex].Get();
  } else {
    pCommandList = m_pd3dCommandList;
  }

  if(chunkIndex < 0) {
    pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(shadowStaticParams.pShadowTexture,
                                                                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                                                          D3D12_RESOURCE_STATE_DEPTH_WRITE));
    pCommandList->ClearDepthStencilView(shadowStaticParams.hDepthStencilView, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
  }

  pCommandList->OMSetRenderTargets(0, nullptr, FALSE, &shadowStaticParams.hDepthStencilView);

  RenderScene(pCommandList, &shadowStaticParams, &shadowDynamicParams);

  if(IsMultithreadedPerScene())
    pCommandList->Close();
}

void MultithreadedRenderingSample::RenderSceneDirect(FrameResources *pFrameResources) {

  SceneParamsStatic staticParamsDirect = {};
  SceneParamsDynamic dynamicParamsDirect = {};

  staticParamsDirect.pPipelineStateTuple = &m_aPipelineLib[NAMED_PIPELINE_INDEX_NORMAL];
  staticParamsDirect.hRenderTargetView = CurrentBackBufferView();
  staticParamsDirect.hDepthStencilView = DepthStencilView();
  staticParamsDirect.pConstBufferStack = &pFrameResources->ConstBufferStack;
  staticParamsDirect.Viewport =  m_ScreenViewport;
  staticParamsDirect.ScissorRect = m_ScissorRect;
  XMStoreFloat4(&staticParamsDirect.vMirrorPlane, g_XMZero);
  XMStoreFloat4(&staticParamsDirect.vTintColor, Colors::White);

#ifdef RENDER_SCENE_LIGHT_POV
  if(m_bRenderSceneLightPOV)
    XMStoreFloat4x4(&dynamicParamsDirect.matViewProj, CalcLightViewProj( 0, TRUE ));
  else
#endif
    XMStoreFloat4x4(&dynamicParamsDirect.matViewProj, m_Camera.GetViewMatrix() * m_Camera.GetProjMatrix());

  ID3D12GraphicsCommandList *pCommandList;

  if(IsMultithreadedPerChunk()) {
    int chunkIndex = GetCurrentChunkThreadIndex();

    if(chunkIndex < 0)
      pCommandList = m_pd3dCommandList;
    else
      pCommandList = pFrameResources->ChunkCommandLists[chunkIndex].Get();
  } else {
    pCommandList = m_pd3dCommandList;
  }

  pCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), TRUE, &DepthStencilView());

  RenderScene(pCommandList, &staticParamsDirect, &dynamicParamsDirect);
}

void MultithreadedRenderingSample::OnPerChunkRenderDeferred(int chunkIndex, FrameResources* const* ppFrameResources) {

  HRESULT hr;
  FrameResources* volatile pFrameResources;
  ID3D12GraphicsCommandList *pCommandList;
  ID3D12CommandAllocator *pCommandAllocator;
  UINT i;
  ID3D12CommandList *cmdLists[MAXIMUM_WAIT_OBJECTS + 1];
  HANDLE hPassBeginEvent;
  HANDLE hPassEndEvent;
  HANDLE aPassBeginEvents[MAXIMUM_WAIT_OBJECTS];
  HANDLE aPassEndEvents[MAXIMUM_WAIT_OBJECTS];

  if (chunkIndex < 0) {

    pFrameResources = *ppFrameResources;

    cmdLists[0] = m_pd3dCommandList;
    for (i = 1; i < m_uNumberOfChunkThreads; ++i)
      cmdLists[i] = pFrameResources->ChunkCommandLists[i - 1].Get();

    pCommandList = m_pd3dCommandList;
    pCommandAllocator = pFrameResources->CommandAllocator.Get();

    for(i = 1; i < m_uNumberOfChunkThreads; ++i) {
      m_aChunkThreadArgs[i-1].pFrameResources = pFrameResources;
      aPassBeginEvents[i-1] = m_aChunkThreadArgs[i-1].PassBeginEvent;
      aPassEndEvents[i-1] = m_aChunkThreadArgs[i-1].PassEndEvent;
      SetEvent(aPassBeginEvents[i-1]);
    }

    if(m_lShowdownChunkThreads)
      return;

  } else {
    hPassBeginEvent = chunkIndex < 0 ? nullptr : m_aChunkThreadArgs[chunkIndex].PassBeginEvent;
    hPassEndEvent = chunkIndex < 0 ? nullptr : m_aChunkThreadArgs[chunkIndex].PassEndEvent;

    WaitForSingleObject(hPassBeginEvent, INFINITE);
    if (m_lShowdownChunkThreads)
      return;

    // This line can not be optimized.
    pFrameResources = *ppFrameResources;

    pCommandList = pFrameResources->ChunkCommandLists[chunkIndex].Get();
    pCommandAllocator = pFrameResources->ChunkCommandAllocators[chunkIndex].Get();
  }

  // Reset command allocators.
  V(pCommandAllocator->Reset());

  for (int iShadow = 0; iShadow < s_iNumShadows; ++iShadow) {

    if(chunkIndex < 0) {
      for(i = 1; i < m_uNumberOfChunkThreads; ++i) {
        SetEvent(aPassBeginEvents[i-1]);
      }
    } else {
      WaitForSingleObject(hPassBeginEvent, INFINITE);
    }

    V(pCommandList->Reset(pCommandAllocator, nullptr));

    RenderShadow(iShadow, pFrameResources);

    pCommandList->Close();

    // Wait for shadow pass complete
    if(chunkIndex < 0) {
      WaitForMultipleObjects(m_uNumberOfChunkThreads - 1, aPassEndEvents, TRUE, INFINITE);
      m_pd3dCommandQueue->ExecuteCommandLists(m_uNumberOfChunkThreads, cmdLists);
    } else {
      SetEvent(hPassEndEvent);
    }
  }

  for (int iMirror = 0; iMirror < s_iNumMirrors; ++iMirror) {
    if (chunkIndex < 0) {
      for (i = 1; i < m_uNumberOfChunkThreads; ++i) {
        SetEvent(aPassBeginEvents[i - 1]);
      }
    } else {
      WaitForSingleObject(hPassBeginEvent, INFINITE);
    }

    RenderMirror(iMirror, pFrameResources);

    // Wait for the mirror pass complete
    if (chunkIndex < 0) {
      WaitForMultipleObjects(m_uNumberOfChunkThreads - 1, aPassEndEvents, TRUE, INFINITE);
      m_pd3dCommandQueue->ExecuteCommandLists(m_uNumberOfChunkThreads, cmdLists);
    } else {
      SetEvent(hPassEndEvent);
    }
  }

  if (chunkIndex < 0) {
    for (i = 1; i < m_uNumberOfChunkThreads; ++i) {
      SetEvent(aPassBeginEvents[i - 1]);
    }
  } else {
    WaitForSingleObject(hPassBeginEvent, INFINITE);
  }

  pCommandList->Reset(pCommandAllocator, nullptr);

  RenderSceneDirect(pFrameResources);

  if(chunkIndex == m_uNumberOfChunkThreads - 2) {
    ImGuiInteractor::OnRender(m_pd3dDevice, pCommandList);
    EndRenderFrame(pCommandList);
  }

  pCommandList->Close();

  // Wait for mirror pass complete
  if(chunkIndex < 0) {
    WaitForMultipleObjects(m_uNumberOfChunkThreads - 1, aPassEndEvents, TRUE, INFINITE);
    m_pd3dCommandQueue->ExecuteCommandLists(m_uNumberOfChunkThreads, cmdLists);
  } else {
    SetEvent(hPassEndEvent);
  }
}

void MultithreadedRenderingSample::OnRenderFrame(float fTime, float fElapsed) {

  HRESULT hr;
  auto pFrameResources = &m_aFrameResources[m_iFrameIndex];

  if (IsMultithreadedPerScene()) {

    for (int i = 0; i < s_iNumShadows; ++i) {
      m_aShadowWorkQueueParams[i].pFrameResources = pFrameResources;
      SubmitThreadpoolWork(m_aShadowWorkQueuePool[i]);
    }

    for (int i = 0; i < s_iNumMirrors; ++i) {
      m_aMirrorWorkQueuePoolParams[i].pFrameResources = pFrameResources;
      SubmitThreadpoolWork(m_aMirrorWorkQueuePool[i]);
    }

    V(pFrameResources->CommandAllocator->Reset());
    V(m_pd3dCommandList->Reset(pFrameResources->CommandAllocator.Get(), nullptr));

    RenderSceneDirect(pFrameResources);

    ImGuiInteractor::OnRender(m_pd3dDevice, m_pd3dCommandList);

    EndRenderFrame();

    m_pd3dCommandList->Close();

    static_assert(s_iNumShadows + s_iNumMirrors <= MAXIMUM_WAIT_OBJECTS, "Scene number per frame must be less than 64");

    HANDLE finEvents[s_iNumShadows + s_iNumMirrors];
    ID3D12CommandList *cmdLists[s_iNumMirrors + s_iNumMirrors + 1];
    int i, j = 0;

    for(i = 0; i < s_iNumShadows; ++i, ++j) {
      finEvents[j] = m_aShadowWorkQueueParams[i].CompletionEvent;
      cmdLists[j] = pFrameResources->ShadowCommandLists[i].Get();
    }
    for(i = 0; i < s_iNumMirrors; ++i, ++j) {
      finEvents[j] = m_aMirrorWorkQueuePoolParams[i].CompletionEvent;
      cmdLists[j] = pFrameResources->MirrorCommandLists[i].Get();
    }
    cmdLists[j] = m_pd3dCommandList;

    WaitForMultipleObjects(j, finEvents, TRUE, INFINITE);
    m_pd3dCommandQueue->ExecuteCommandLists(j+1, cmdLists);

  } else if(IsMultithreadedPerChunk()) {

    OnPerChunkRenderDeferred(-1, &pFrameResources);

  } else if (IsSinglethreadedDeferred()) {

    V(pFrameResources->CommandAllocator->Reset());
    V(m_pd3dCommandList->Reset(pFrameResources->CommandAllocator.Get(), nullptr));

    for (int i = 0; i < s_iNumShadows; ++i)
      RenderShadow(i, pFrameResources);

    for (int i = 0; i < s_iNumMirrors; ++i)
      RenderMirror(i, pFrameResources);

    RenderSceneDirect(pFrameResources);

    ImGuiInteractor::OnRender(m_pd3dDevice, m_pd3dCommandList);

    EndRenderFrame();

    m_pd3dCommandList->Close();
    m_pd3dCommandQueue->ExecuteCommandLists(1, CommandListCast(&m_pd3dCommandList));
  }

  m_pSyncFence->Signal(m_pd3dCommandQueue, &pFrameResources->FencePoint);

  Present();
}

VOID CALLBACK MultithreadedRenderingSample::_PerSceneRenderDeferredProc(
  _Inout_     PTP_CALLBACK_INSTANCE Instance,
  _Inout_opt_ PVOID                 Context,
  _Inout_     PTP_WORK              Work
) {
  SCENE_RENDERING_THREAD_PARAMS *pParams = reinterpret_cast<SCENE_RENDERING_THREAD_PARAMS *>(Context);
  switch(pParams->RenderCase) {
    case SCENE_MT_RENDER_CASE_SHADOW:
      pParams->pInstance->RenderShadow(pParams->BatchIndex, pParams->pFrameResources);
      break;
    case SCENE_MT_RENDER_CASE_MIRROR_AREA:
      pParams->pInstance->RenderMirror(pParams->BatchIndex, pParams->pFrameResources);
      break;
    default:;
  }

  SetEvent(pParams->CompletionEvent);
}

unsigned int WINAPI MultithreadedRenderingSample::_PerChunkRenderDeferredProc(LPVOID pv) {

  auto pParams = reinterpret_cast<CHUNK_RENDERING_THREAD_PARAMS *>(pv);
  auto pInstance = pParams->pInstance;
  int chunkIndex = pParams->ChunkIndex;

  TlsSetValue(pInstance->m_dwChunkThreadsLocalSlot, (LPVOID)&pInstance->m_aChunkThreadLocalVars[chunkIndex+1]);

  for(;;) {
    pInstance->OnPerChunkRenderDeferred(pParams->ChunkIndex, &pParams->pFrameResources);

    if(pInstance->m_lShowdownChunkThreads)
      break;
  }

  return 0;
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
  int minProcCount = 1, maxProcCount = MAXIMUM_WAIT_OBJECTS;

  maxProcCount = GetLogicalProcessorCount() - 1;
  maxProcCount = std::min(maxProcCount, MAXIMUM_WAIT_OBJECTS);
  maxProcCount = std::max(maxProcCount, minProcCount);

  m_uNumberOfChunkThreads = maxProcCount + 1; // Include main thread

  ZeroMemory(m_aShadowWorkQueuePool, sizeof(m_aShadowWorkQueuePool));
  ZeroMemory(m_aShadowWorkQueueParams, sizeof(m_aShadowWorkQueueParams));
  ZeroMemory(m_aMirrorWorkQueuePool, sizeof(m_aMirrorWorkQueuePool));
  ZeroMemory(m_aMirrorWorkQueuePoolParams, sizeof(m_aMirrorWorkQueuePoolParams));

  ZeroMemory(m_aChunkThreads, sizeof(m_aChunkThreads));
  ZeroMemory(m_aChunkThreadArgs, sizeof(m_aChunkThreadArgs));
  ZeroMemory(m_aChunkThreadLocalVars, sizeof(m_aChunkThreadLocalVars));

  m_pThreadpool = CreateThreadpool(nullptr);
  m_pCleanupGroup = CreateThreadpoolCleanupGroup();
  if(!m_pThreadpool || !m_pCleanupGroup) {
    if(m_pThreadpool) {
      CloseThreadpool(m_pThreadpool);
      m_pThreadpool = nullptr;
    } if(m_pCleanupGroup) {
      CloseThreadpoolCleanupGroup(m_pCleanupGroup);
      m_pCleanupGroup = nullptr;
    }

    return HRESULT_FROM_WIN32(GetLastError());
  }

  InitializeThreadpoolEnvironment(&m_CallbackEnv);

  SetThreadpoolCallbackPool(&m_CallbackEnv, m_pThreadpool);
  SetThreadpoolCallbackCleanupGroup(&m_CallbackEnv, m_pCleanupGroup, nullptr);

  SetThreadpoolThreadMinimum(m_pThreadpool, minProcCount);
  SetThreadpoolThreadMaximum(m_pThreadpool, maxProcCount);

  for(int i = 0; i < s_iNumShadows; ++i) {

    m_aShadowWorkQueuePool[i] = CreateThreadpoolWork(_PerSceneRenderDeferredProc,
      &m_aShadowWorkQueueParams[i], &m_CallbackEnv);
    if(m_aShadowWorkQueuePool[i] == nullptr) {
      return HRESULT_FROM_WIN32(GetLastError());
    }

    m_aShadowWorkQueueParams[i].SlotIndex = i;
    m_aShadowWorkQueueParams[i].pInstance = this;
    m_aShadowWorkQueueParams[i].BatchIndex = i;
    m_aShadowWorkQueueParams[i].RenderCase = SCENE_MT_RENDER_CASE_SHADOW;
    if ((m_aShadowWorkQueueParams[i].CompletionEvent = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS)) ==
        nullptr) {
      return HRESULT_FROM_WIN32(GetLastError());
    }
  }

  for(int i = 0; i < s_iNumMirrors; ++i) {

    m_aMirrorWorkQueuePool[i] = CreateThreadpoolWork(_PerSceneRenderDeferredProc,
      &m_aMirrorWorkQueuePoolParams[i], &m_CallbackEnv);
    if(m_aMirrorWorkQueuePool[i] == nullptr) {
      return HRESULT_FROM_WIN32(GetLastError());
    }

    m_aMirrorWorkQueuePoolParams[i].SlotIndex = i;
    m_aMirrorWorkQueuePoolParams[i].pInstance = this;
    m_aMirrorWorkQueuePoolParams[i].BatchIndex = i;
    m_aMirrorWorkQueuePoolParams[i].RenderCase = SCENE_MT_RENDER_CASE_MIRROR_AREA;
    if ((m_aMirrorWorkQueuePoolParams[i].CompletionEvent = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS)) ==
        nullptr) {
      return HRESULT_FROM_WIN32(GetLastError());
    }
  }

  m_dwChunkThreadsLocalSlot = TlsAlloc();
  if(m_dwChunkThreadsLocalSlot == TLS_OUT_OF_INDEXES) {
    return HRESULT_FROM_WIN32(GetLastError());
  }

  // Mark main thread
  m_aChunkThreadLocalVars[0].ChunkIndex = -1;
  m_aChunkThreadLocalVars[0].NextDrawcallIndex = 0;
  TlsSetValue(m_dwChunkThreadsLocalSlot, (LPVOID)&m_aChunkThreadLocalVars[0]);

  for(int i = 0; i < maxProcCount; ++i) {

    if((m_aChunkThreadArgs[i].PassBeginEvent = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS)) == nullptr) {
      return HRESULT_FROM_WIN32(GetLastError());
    }

    if((m_aChunkThreadArgs[i].PassEndEvent = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS)) == nullptr) {
      return HRESULT_FROM_WIN32(GetLastError());
    }

    m_aChunkThreadArgs[i].pInstance = this;
    m_aChunkThreadArgs[i].ChunkIndex = i;
    m_aChunkThreadLocalVars[i+1].ChunkIndex = i;
    m_aChunkThreadLocalVars[i+1].NextDrawcallIndex = 0;

    m_aChunkThreads[i] = (HANDLE)_beginthreadex(nullptr, 0, MultithreadedRenderingSample::_PerChunkRenderDeferredProc,
      (void *)&m_aChunkThreadArgs[i], 0, nullptr);
    if(m_aChunkThreads[i] == nullptr) {
      return HRESULT_FROM_WIN32(GetLastError());
    }
  }

  return hr;
}
