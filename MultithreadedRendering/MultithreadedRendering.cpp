#include <windows.h>
#include <D3D12RendererContext.hpp>
#include <Win32Application.hpp>
#include <DirectXMath.h>
#include <SDKmesh.h>
#include <ResourceUploadBatch.hpp>
#include <array>
#include <RootSignatureGenerator.h>
#include <Camera.h>

// #define UNCOMPRESSED_VERTEX_DATA // The sdkmesh file contained uncompressed vertex data
#define RENDER_SCENE_LIGHT_POV      // F4 toggles between the usual camera and the 0th light's point-of-view

using Microsoft::WRL::ComPtr;
using namespace DirectX;

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
static const int                      s_iNumLights = 4;
static const XMVECTORF32              s_vAmbientColor = { 0.04f * 0.760f, 0.04f * 0.793f, 0.04f * 0.822f, 1.000f };
static const XMVECTORF32              s_vMirrorTint = { 0.3f, 0.5f, 1.0f, 1.0f };

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

class UploadBufferStack {
public:
  UploadBufferStack() {
    m_uBlockSize = 0;
    m_uReservedBlockCount = 0;
    m_uCurrBlock = -1;
    m_pCurrBufferLocation = 0;
    m_uCurrOffsetInBlock = 0;
    m_uCurrBufferSize = 0;
  }

  HRESULT Initialize(ID3D12Device *pDevice, UINT BlockSize, UINT ReservedBlockCount) {

    if(pDevice == nullptr || BlockSize < 1)
      return E_INVALIDARG;

    m_pd3dDevice = pDevice;
    m_uBlockSize = d3dUtils::CalcConstantBufferByteSize(BlockSize);
    m_uReservedBlockCount = ReservedBlockCount;
    m_uCurrBlock = -1;
    m_pCurrBufferLocation = 0;
    m_uCurrOffsetInBlock = 0;
    m_uCurrBufferSize = 0;
    return S_OK;
  }

  HRESULT Push(_In_ const void *pData, _In_ UINT uBufferSize) {

    if(m_pd3dDevice == nullptr)
      return E_FAIL;

    HRESULT hr = S_OK;
    ID3D12Resource *pCurrBuffer = nullptr;
    UINT uNextOffset = m_uCurrOffsetInBlock + m_uCurrBufferSize;
    UINT uEndOffset;
    void *pMappedData = nullptr;

    if(uBufferSize > m_uBlockSize) {
      DX_TRACE(L"UploadBufferStack: Block size(%u byte) is smaller than request block size(%u byte)!", m_uBlockSize,
               uBufferSize);
      V_RETURN(E_INVALIDARG);
    }

    uBufferSize = d3dUtils::CalcConstantBufferByteSize(uBufferSize);
    uEndOffset = uNextOffset + m_uBlockSize;

    if (m_uCurrBlock == -1 || (m_uCurrBlock == (m_aBufferStorage.size() - 1) && uEndOffset > m_uBlockSize)) {
      // Allocate a new block
      V_RETURN(m_pd3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(m_uBlockSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&pCurrBuffer)
      ));
      m_uCurrBlock += 1;
      pCurrBuffer->Map(0, nullptr, &pMappedData);
      m_aBufferStorage.emplace_back(pCurrBuffer, m_uBlockSize, pMappedData);
      pCurrBuffer->Release();
      uNextOffset = 0;
    } else if (uEndOffset > m_uBlockSize) {
      m_uCurrBlock += 1;
      pCurrBuffer = m_aBufferStorage[m_uCurrBlock].UploadBuffer.Get();
      pMappedData = m_aBufferStorage[m_uCurrBlock].pMappedData;
      uNextOffset = 0;
    } else {
      pCurrBuffer = m_aBufferStorage[m_uCurrBlock].UploadBuffer.Get();
      pMappedData = m_aBufferStorage[m_uCurrBlock].pMappedData;
    }

    m_pCurrBufferLocation = pCurrBuffer->GetGPUVirtualAddress() + uNextOffset;

    memcpy((BYTE *)pMappedData + uNextOffset, pData, uBufferSize);

    m_uCurrOffsetInBlock = uNextOffset;
    m_uCurrBufferSize  = uBufferSize;

    return hr;
  }

  D3D12_CONSTANT_BUFFER_VIEW_DESC Top() {
    return { m_pCurrBufferLocation, m_uCurrBufferSize };
  }

  void Clear() {

    if(m_aBufferStorage.empty()) {
      m_uCurrBlock = -1;
    } else {
      m_uCurrBlock = 0;
    }

    m_pCurrBufferLocation = 0;
    m_uCurrOffsetInBlock = 0;
    m_uCurrBufferSize = 0;
  }

  void ClearCapacity() {
    if(m_aBufferStorage.size() > m_uReservedBlockCount) {
      m_aBufferStorage.erase(std::next(m_aBufferStorage.begin(), (ptrdiff_t)m_uReservedBlockCount),
                             m_aBufferStorage.end());
    }

    if(m_aBufferStorage.empty()) {
      m_uCurrBlock = -1;
    } else {
      m_uCurrBlock = 0;
    }
  }

private:
  struct BufferStorage {
    ComPtr<ID3D12Resource> UploadBuffer;
    UINT BufferSize;
    void *pMappedData;
  };

  ComPtr<ID3D12Device> m_pd3dDevice;
  std::vector<BufferStorage> m_aBufferStorage;
  UINT m_uBlockSize;
  UINT m_uReservedBlockCount;
  UINT m_uCurrBlock;
  D3D12_GPU_VIRTUAL_ADDRESS m_pCurrBufferLocation;
  UINT m_uCurrOffsetInBlock;
  UINT m_uCurrBufferSize;
};

struct SceneParamsStatic {
  BOOL bRenderShadow;
  ID3D12PipelineState *pPipelineState;
  ID3D12Resource *pShadowTexture; // For rendering shadow map
  D3D12_CPU_DESCRIPTOR_HANDLE hRenderTargetView;
  D3D12_CPU_DESCRIPTOR_HANDLE hDepthStencilView;
  UINT8 uStencilRef;

  XMFLOAT4 vTintColor;
  XMFLOAT4 vMirrorPlane;
  D3D12_VIEWPORT aViewport;
};

struct SceneParamsDynamic {
  XMFLOAT4X4 matViewProj;
};

class MultithreadedRenderingSample : public D3D12RendererContext, public WindowInteractor {
public:
private:
  HRESULT OnInitPipelines() override;
  UINT GetExtraDSVDescriptorCount() const override;
  void OnFrameMoved(float fTime, float fElapsed) override;
  void OnRenderFrame(float fTime, float fElapsed) override;
  void OnResizeFrame(int cx, int cy) override;
  LRESULT OnMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool *pbNoFurtherProcessing) override;

  HRESULT CreatePSOs();
  HRESULT LoadModel();
  HRESULT CreateShadowDepthStencilViews();
  HRESULT CreateStaticDescriptorHeap();
  HRESULT CreateFrameResources();

  void InitCamera();
  void InitLights();

  void SetupRenderingScene(ID3D12GraphicsCommandList *pd3dCommandList, const SceneParamsStatic *pStaticParams,
    const SceneParamsDynamic *pDynamicParams);

  CDXUTSDKMesh m_Model;
  ComPtr<ID3D12DescriptorHeap> m_pModelDescriptorHeap;

  std::future<HRESULT> m_InitPipelineWaitable;
  D3D12_VIEWPORT m_aShadowViewport;
  const UINT m_iShadowMapCount = 1;
  XMFLOAT2 m_vShadowResoltion = { 2048.f, 2048.f };
  std::vector<ComPtr<ID3D12Resource>> m_aShadowTextures{ m_iShadowMapCount };
  ComPtr<ID3D12DescriptorHeap> m_pStaticDescriptorHeap;

  enum NAMED_PIPELINE_INDEX {
    NAMED_PIPELINE_INDEX_SHADOW,                // For writing shadow depth map
    NAMED_PIPELINE_INDEX_MIRRORED_S0,           // For rendering single mirrored objects, stencil bit 0
    NAMED_PIPELINE_INDEX_MIRRORED_S1,           // For rendering single mirrored objects, stencil bit 1
    NAMED_PIPELINE_INDEX_MIRRORED_S2,           // For rendering single mirrored objects, ...
    NAMED_PIPELINE_INDEX_MIRRORED_S3,           // For rendering single mirrored objects
    NAMED_PIPELINE_INDEX_MIRRORED_S4,           // For rendering single mirrored objects
    NAMED_PIPELINE_INDEX_MIRRORED_S5,           // For rendering single mirrored objects
    NAMED_PIPELINE_INDEX_MIRRORED_S6,           // For rendering single mirrored objects
    NAMED_PIPELINE_INDEX_MIRRORED_S7,           // For rendering single mirrored objects
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
  UploadBufferStack m_aUploadBufferStack;
  INT m_iFrameIndex = 0;
  const UINT m_uTotalFrameCount = 3;

  struct FrameResources {

    struct ObjectConstBuffer {
      XMFLOAT4X4  World;
      XMFLOAT4    ObjectColor;
    };

    struct LightConstBuffer {
      XMFLOAT4X4 LightViewProj;
      XMFLOAT4 LightPos;
      XMFLOAT4 LightDir;
      XMFLOAT4 LightColor;
      XMFLOAT4 Falloffs;
    };

    struct FrameConstantBuffer {
      XMFLOAT4X4 ViewProj;
      XMFLOAT4 MirrorPlane;
      XMFLOAT4 AmbientColor;
      XMFLOAT4 TintColor;
    };

    D3D12_GPU_VIRTUAL_ADDRESS FrameConstGpuAddress;
    D3D12_GPU_VIRTUAL_ADDRESS LightConstGpuAddress;
    ComPtr<ID3D12CommandAllocator> CommandAllocator;
  } m_aFrameResources[3];

  // Camera parameters
  CModelViewerCamera m_Camera;

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
  return m_iShadowMapCount;
}

HRESULT MultithreadedRenderingSample::OnInitPipelines() {
  HRESULT hr;

  V_RETURN(LoadModel());
  V_RETURN(CreatePSOs());
  V_RETURN(CreateShadowDepthStencilViews());
  V_RETURN(CreateStaticDescriptorHeap());

  // Initalize upload buffer stack
  V_RETURN(m_aUploadBufferStack.Initialize(m_pd3dDevice, (1 << 12), 2));

  InitCamera();
  InitLights();

  V_RETURN(m_InitPipelineWaitable.get());
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
      D3D12_RESOURCE_STATE_GENERIC_READ,
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
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  heapDesc.NumDescriptors = m_iShadowMapCount;
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;

  V_RETURN(m_pd3dDevice->CreateDescriptorHeap(
    &heapDesc,
    IID_PPV_ARGS(&m_pStaticDescriptorHeap)
  ));

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;
  srvDesc.Texture2D.PlaneSlice = 0;

  CD3DX12_CPU_DESCRIPTOR_HANDLE handle0;
  handle0 = m_pStaticDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

  for(auto it = m_aShadowTextures.begin(); it != m_aShadowTextures.end(); ++it) {
    m_pd3dDevice->CreateShaderResourceView(it->Get(), &srvDesc, handle0);
    handle0.Offset(1, m_uDsvDescriptorSize);
  }

  return hr;
}

HRESULT MultithreadedRenderingSample::LoadModel() {
  HRESULT hr;
  ResourceUploadBatch uploadBatch(m_pd3dDevice, &m_MemAllocator);

  V_RETURN(uploadBatch.Begin(D3D12_COMMAND_LIST_TYPE_DIRECT));

  V_RETURN(m_Model.Create(&uploadBatch, LR"(directx-sdk-samples\Media\SquidRoom\SquidRoom.sdkmesh)"));

  V_RETURN(uploadBatch.End(m_pd3dCommandQueue, &m_InitPipelineWaitable));

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

#ifdef UNCOMPRESSED_VERTEX_DATA
  D3D12_INPUT_ELEMENT_DESC ElementLayout[] = {
    { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 32, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
  };
#else
  D3D12_INPUT_ELEMENT_DESC ElementLayout[] = {
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
  rsGen.AddDescriptorTable({ CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 2) }, D3D12_SHADER_VISIBILITY_PIXEL);
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
    ElementLayout,
    (UINT)std::size(ElementLayout)
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
    ElementLayout,
    (UINT)std::size(ElementLayout)
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
  psoDesc.PS = {
    pPSBuffer->GetBufferPointer(),
    pPSBuffer->GetBufferSize()
  };
  psoDesc.BlendState = CD3DX12_BLEND_DESC{ D3D12_DEFAULT };
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC{ D3D12_DEFAULT };
  psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC{ D3D12_DEFAULT };
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // Perform depth test but no depth write.
  psoDesc.DepthStencilState.StencilEnable = TRUE;
  psoDesc.DepthStencilState.FrontFace = {
      D3D12_STENCIL_OP_KEEP, // StencilFailOp;
      D3D12_STENCIL_OP_KEEP, // StencilDepthFailOp;
      D3D12_STENCIL_OP_REPLACE, // StencilPassOp;
      D3D12_COMPARISON_FUNC_ALWAYS // StencilFunc;
  };
  psoDesc.DepthStencilState.BackFace = {
      D3D12_STENCIL_OP_KEEP, // StencilFailOp;
      D3D12_STENCIL_OP_KEEP, // StencilDepthFailOp;
      D3D12_STENCIL_OP_KEEP, // StencilPassOp;
      D3D12_COMPARISON_FUNC_NEVER // StencilFunc;
  };
  psoDesc.InputLayout = {
    ElementLayout,
    (UINT)std::size(ElementLayout)
  };
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = m_BackBufferFormat;
  psoDesc.DSVFormat = m_DepthStencilBufferFormat;
  psoDesc.SampleDesc = GetMsaaSampleDesc();

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
    ElementLayout,
    (UINT)std::size(ElementLayout)
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
  mirrorPSODesc.PS = {
    pPSBuffer->GetBufferPointer(),
    pPSBuffer->GetBufferSize()
  };
  mirrorPSODesc.BlendState = CD3DX12_BLEND_DESC{ D3D12_DEFAULT };
  mirrorPSODesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
  mirrorPSODesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
  mirrorPSODesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
  mirrorPSODesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
  mirrorPSODesc.SampleMask = UINT_MAX;
  mirrorPSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC{ D3D12_DEFAULT };
  mirrorPSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC{ D3D12_DEFAULT };
  mirrorPSODesc.DepthStencilState.StencilEnable = TRUE;
  mirrorPSODesc.DepthStencilState.FrontFace = {
      D3D12_STENCIL_OP_REPLACE, // StencilFailOp;
      D3D12_STENCIL_OP_REPLACE, // StencilDepthFailOp;
      D3D12_STENCIL_OP_REPLACE, // StencilPassOp;
      D3D12_COMPARISON_FUNC_ALWAYS // StencilFunc;
  };
  mirrorPSODesc.DepthStencilState.BackFace = {
      D3D12_STENCIL_OP_KEEP, // StencilFailOp;
      D3D12_STENCIL_OP_KEEP, // StencilDepthFailOp;
      D3D12_STENCIL_OP_KEEP, // StencilPassOp;
      D3D12_COMPARISON_FUNC_NEVER // StencilFunc;
  };
  mirrorPSODesc.InputLayout = {
    ElementLayout,
    (UINT)std::size(ElementLayout)
  };
  mirrorPSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  mirrorPSODesc.NumRenderTargets = 1;
  mirrorPSODesc.RTVFormats[0] = m_BackBufferFormat;
  mirrorPSODesc.DSVFormat = m_DepthStencilBufferFormat;
  mirrorPSODesc.SampleDesc = GetMsaaSampleDesc();

  for(int i = 0; i < 8; ++i) {
    psoDesc.DepthStencilState.StencilReadMask = 1 << i;
    psoDesc.DepthStencilState.StencilWriteMask = 1 << i;
    m_aPipelineLib[i].RootSignature = pRootSignature;
    V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(
      &psoDesc, IID_PPV_ARGS(&m_aPipelineLib[i + NAMED_PIPELINE_INDEX_MIRRORED_S0].PSO)));

    objPSODesc.DepthStencilState.StencilReadMask = psoDesc.DepthStencilState.StencilReadMask;
    objPSODesc.DepthStencilState.StencilWriteMask = psoDesc.DepthStencilState.StencilWriteMask;

    V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(
      &objPSODesc, IID_PPV_ARGS(&m_aPipelineLib[i + NAMED_PIPELINE_INDEX_MIRRORED_RENDERING_S0].PSO)));

    mirrorPSODesc.DepthStencilState.StencilWriteMask = psoDesc.DepthStencilState.StencilWriteMask;

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
}

void MultithreadedRenderingSample::SetupRenderingScene(ID3D12GraphicsCommandList *pd3dCommandList, const SceneParamsStatic *pStaticParams,
    const SceneParamsDynamic *pDynamicParams) {

  pd3dCommandList->SetPipelineState(pStaticParams->pPipelineState);

  if(pStaticParams->bRenderShadow) {

    pd3dCommandList->OMSetRenderTargets(0, nullptr, TRUE, &pStaticParams->hDepthStencilView);

    pd3dCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                            pStaticParams->pShadowTexture, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
                                            D3D12_RESOURCE_STATE_DEPTH_WRITE));


    m_aUploadBufferStack.Push();
  } else {
    pd3dCommandList->OMSetRenderTargets(1, &pStaticParams->hRenderTargetView, TRUE, &pStaticParams->hDepthStencilView);


    pd3dCommandList->OMSetStencilRef(pStaticParams->uStencilRef);
  }

}

void MultithreadedRenderingSample::OnRenderFrame(float fTime, float fElapsed) {

}

LRESULT MultithreadedRenderingSample::OnMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool *pbNoFurtherProcessing) {

  m_Camera.HandleMessages(hwnd, msg, wp, lp);
  return 0;
}
