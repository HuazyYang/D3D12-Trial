#include <windows.h>
#include "Camera.h"
#include "D3D12RendererContext.hpp"
#include "MeshBuffer.h"
#include "Texture.h"
#include "UploadBuffer.h"
#include "Win32Application.hpp"
#include "d3dx12.h"
#include <DirectXColors.h>

using namespace DirectX;
using namespace Microsoft::WRL;

static void CreateAppInstance(D3D12RendererContext **ppRenderer, WindowInteractor **pInteractor);

int main() {
  int ret;
  D3D12RendererContext *pRenderer;
  WindowInteractor *pInteractor;

  CreateAppInstance(&pRenderer, &pInteractor);
  ret = RunSample(pRenderer, pInteractor, 800, 600, L"HDRToneMappingCS");
  SAFE_DELETE(pRenderer);
  return ret;
}

struct SkyboxRenderParams {
  XMFLOAT4X4 MatWorldViewProj;
};

struct BoomCBGuassWeights {
  XMFLOAT4 vGaussWeights[15];
};

struct BoomCSParams {
  union {
    struct {
      UINT uOutputWidth;
      float fInverse;
    };
    struct {
      XMUINT2 vOutputSize;
    };
  };
  XMUINT2 vInputSize;
};

struct FrameResources {
  FrameResources() {
    CmdAllocator = nullptr;
    FencePoint = 0;
  }
  ~FrameResources() { SAFE_RELEASE(CmdAllocator); }

  HRESULT CreateCommandAllocator(ID3D12Device *pd3dDevice) {

    HRESULT hr;

    V_RETURN(pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&CmdAllocator)));
    return hr;
  }

  static HRESULT CreateBuffers(ID3D12Device *pd3dDevice, int NumFrames) {
    HRESULT hr;

    V_RETURN(SkyboxCBs.CreateBuffer(pd3dDevice, NumFrames, sizeof(SkyboxRenderParams), TRUE));
    V_RETURN(BoomGaussWeightsCBs.CreateBuffer(pd3dDevice, 1, sizeof(BoomCBGuassWeights), TRUE));
    V_RETURN(BoomCSCBs.CreateBuffer(pd3dDevice, 2, sizeof(BoomCSParams), TRUE));

    return hr;
  }

  ID3D12CommandAllocator *CmdAllocator;
  static UploadBuffer SkyboxCBs;
  static UploadBuffer BoomGaussWeightsCBs;
  static UploadBuffer BoomCSCBs;

  UINT64 FencePoint;
};

UploadBuffer FrameResources::SkyboxCBs;
UploadBuffer FrameResources::BoomGaussWeightsCBs;
UploadBuffer FrameResources::BoomCSCBs;

class HDRToneMappingCSApp : public D3D12RendererContext, public WindowInteractor {
public:
  HDRToneMappingCSApp();
  ~HDRToneMappingCSApp();

private:
  HRESULT OnInitPipelines() override;
  void PostInitialize();
  void OnFrameMoved(float fTime, float fTimeElasped) override;
  void OnRenderFrame(float fTime, float fTimeElasped) override;

  void OnResizeFrame(int cx, int cy) override;
  LRESULT OnMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool *pbNoFurtherProcessing) override;

  UINT GetExtraRTVDescriptorCount() const override;

  HRESULT LoadTextures();
  HRESULT CreateGeometryBuffers();
  HRESULT CreateSkyboxPSO();
  HRESULT CreateLuminanceSumPSO();
  HRESULT CreateBoomPSO();
  HRESULT CreateFinalRenderPSO();
  HRESULT CreateCbvSrvUavHeap();
  HRESULT CreateDescriptors();

  void RenderSkybox(FrameResources *pFrameResource);
  void MeasureLuminanceCS();
  void BoomCS();
  void RenderFinalScreenQuad();

  const int ToneMappingTexSize = 81; /// 3^(5-1)

  ID3D12DescriptorHeap *m_pCbvUavSrvHeap = nullptr;

  D3D12_GPU_DESCRIPTOR_HANDLE m_hSkyboxCubeMapGpuHandle = {0};

  /// Render sky box into a texture.
  ID3D12Resource *m_pColorTexture = nullptr;
  D3D12_CPU_DESCRIPTOR_HANDLE m_hColorRTV = {0};
  D3D12_GPU_DESCRIPTOR_HANDLE m_hColorSrv = {0};

  ID3D12Resource *m_pLum1DTexture[2] = {0};
  D3D12_GPU_DESCRIPTOR_HANDLE m_hLum1DSrv[2] = {0};
  D3D12_GPU_DESCRIPTOR_HANDLE m_hLum1DUav[2] = {0};

  ID3D12Resource *m_pBoomTexture[2] = {0};
  D3D12_GPU_DESCRIPTOR_HANDLE m_hBoomSrv[2] = {0};
  D3D12_GPU_DESCRIPTOR_HANDLE m_hBoomUav[2] = {0};

  D3D12_GPU_DESCRIPTOR_HANDLE m_hNullSrv = {0};

  ID3D12RootSignature *m_pSkyboxRootSignature = nullptr;
  ID3D12PipelineState *m_pSkyboxPSO = nullptr;
  ID3D12PipelineState *m_pSkyboxPSOForBackbuffer = nullptr;
  Texture *m_pSkyBoxCubeMap = nullptr;

  ID3D12RootSignature *m_pLuminanceSumSignature = nullptr;
  ID3D12PipelineState *m_pLuminanceSumPSO = nullptr;
  ID3D12PipelineState *m_pLuminanceSumPSO2 = nullptr;

  ID3D12RootSignature *m_pBoomSignature = nullptr;
  ID3D12PipelineState *m_pBoomPSO = nullptr;
  ID3D12PipelineState *m_pBoomPSO2 = nullptr;

  ID3D12RootSignature *m_pFinalRenderSignature = nullptr;
  ID3D12PipelineState *m_pFinalRenderPSO = nullptr;

  MeshBuffer *m_pSkyboxBuffer = nullptr;

  CModelViewerCamera m_Camera;

  int m_iCurrentFrameIndex = 0;

  FrameResources m_aFrameResources[3];

  BOOL m_bPostProcessON;
  BOOL m_bBlur;
  BOOL m_bBoom;
  POINT m_ptLastMousePos;
};

void CreateAppInstance(D3D12RendererContext **ppRenderer, WindowInteractor **ppUIController) {
  HDRToneMappingCSApp *pContext = new HDRToneMappingCSApp;
  *ppRenderer = pContext;
  *ppUIController = pContext;
}

HDRToneMappingCSApp::HDRToneMappingCSApp() {
  // m_bVsyncEnabled = TRUE;
  m_bPostProcessON = TRUE;
  m_bBlur = FALSE;
  m_bBoom = FALSE;
}

HDRToneMappingCSApp::~HDRToneMappingCSApp() {

  SAFE_RELEASE(m_pCbvUavSrvHeap);

  SAFE_RELEASE(m_pColorTexture);
  SAFE_RELEASE(m_pLum1DTexture[0]);
  SAFE_RELEASE(m_pLum1DTexture[1]);
  SAFE_RELEASE(m_pBoomTexture[0]);
  SAFE_RELEASE(m_pBoomTexture[1]);

  SAFE_RELEASE(m_pSkyboxRootSignature);
  SAFE_RELEASE(m_pSkyboxPSO);
  SAFE_RELEASE(m_pSkyboxPSOForBackbuffer);
  SAFE_DELETE(m_pSkyBoxCubeMap);

  SAFE_RELEASE(m_pLuminanceSumSignature);
  SAFE_RELEASE(m_pLuminanceSumPSO);
  SAFE_RELEASE(m_pLuminanceSumPSO2);
  SAFE_RELEASE(m_pBoomSignature);
  SAFE_RELEASE(m_pBoomPSO);
  SAFE_RELEASE(m_pBoomPSO2);
  SAFE_RELEASE(m_pFinalRenderSignature);
  SAFE_RELEASE(m_pFinalRenderPSO);

  SAFE_RELEASE(m_pSkyboxBuffer);
}

UINT HDRToneMappingCSApp::GetExtraRTVDescriptorCount() const { return 1; }

HRESULT HDRToneMappingCSApp::OnInitPipelines() {

  HRESULT hr;

  /// Reset the command list to prepare for initalization commands.
  m_pd3dDirectCmdAlloc->Reset();
  m_pd3dCommandList->Reset(m_pd3dDirectCmdAlloc, nullptr);

  V_RETURN(CreateGeometryBuffers());
  V_RETURN(CreateSkyboxPSO());
  V_RETURN(CreateLuminanceSumPSO());
  V_RETURN(CreateBoomPSO());
  V_RETURN(CreateFinalRenderPSO());
  V_RETURN(LoadTextures());

  for (auto i = 0; i < 3; ++i) {
    m_aFrameResources[i].CreateCommandAllocator(m_pd3dDevice);
  }
  FrameResources::CreateBuffers(m_pd3dDevice, 3);

  // Execute the initialization commands.
  m_pd3dCommandList->Close();
  ID3D12CommandList *cmdList[] = {m_pd3dCommandList};
  m_pd3dCommandQueue->ExecuteCommandLists(1, cmdList);

  FlushCommandQueue();

  PostInitialize();

  m_Camera.SetViewParams({0.0f, -10.5f, -3.0f}, {0.0f, 0.0f, 0.0f});

  return hr;
}

void HDRToneMappingCSApp::PostInitialize() { m_pSkyBoxCubeMap->DisposeUploaders(); }

HRESULT HDRToneMappingCSApp::LoadTextures() {

  HRESULT hr;

  m_pSkyBoxCubeMap = new Texture("Sky Cube Map");

  V_RETURN(m_pSkyBoxCubeMap->CreateTextureFromDDSFile(m_pd3dDevice, m_pd3dCommandList,
                                                      L"Media/Light Probes/uffizi_cross32.dds"));

  return hr;
}

HRESULT HDRToneMappingCSApp::CreateSkyboxPSO() {

  HRESULT hr;
  DWORD compileFlags = 0;
  Microsoft::WRL::ComPtr<ID3DBlob> pVSBuffer, pPSBuffer;
  Microsoft::WRL::ComPtr<ID3DBlob> pErrorBuffer;

#if defined(_DEBUG)
  compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
  compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

  V_RETURN(d3dUtils::CompileShaderFromFile(L"Shaders/Skybox.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "SkyVSMain",
                                           "vs_5_0", compileFlags, 0, pVSBuffer.GetAddressOf(),
                                           pErrorBuffer.GetAddressOf()));
  if (pErrorBuffer) {
    DXOutputDebugStringA((const char *)pErrorBuffer->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  V_RETURN(d3dUtils::CompileShaderFromFile(L"Shaders/Skybox.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "SkyPSMain",
                                           "ps_5_0", compileFlags, 0, pPSBuffer.GetAddressOf(),
                                           pErrorBuffer.GetAddressOf()));
  if (pErrorBuffer) {
    DXOutputDebugStringA((const char *)pErrorBuffer->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  CD3DX12_ROOT_PARAMETER rootParameters[2];
  CD3DX12_DESCRIPTOR_RANGE dpRange[1];
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc = {};
  Microsoft::WRL::ComPtr<ID3DBlob> pSignatureBlob;

  dpRange[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
  rootParameters[0].InitAsConstantBufferView(0);
  rootParameters[1].InitAsDescriptorTable(1, dpRange);

  CD3DX12_STATIC_SAMPLER_DESC staticSamplers[] = {{0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                                                   D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
                                                   .0f, 1, D3D12_COMPARISON_FUNC_ALWAYS}};

  rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
  rootSigDesc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
  rootSigDesc.Desc_1_0.NumParameters = _countof(rootParameters);
  rootSigDesc.Desc_1_0.pParameters = rootParameters;
  rootSigDesc.Desc_1_0.NumStaticSamplers = 1;
  rootSigDesc.Desc_1_0.pStaticSamplers = staticSamplers;
  rootSigDesc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  V_RETURN(
      D3D12SerializeVersionedRootSignature(&rootSigDesc, pSignatureBlob.GetAddressOf(), pErrorBuffer.GetAddressOf()));
  if (pErrorBuffer) {
    DXOutputDebugStringA((const char *)pErrorBuffer->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  V_RETURN(m_pd3dDevice->CreateRootSignature(0, pSignatureBlob->GetBufferPointer(), pSignatureBlob->GetBufferSize(),
                                             IID_PPV_ARGS(&m_pSkyboxRootSignature)));

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

  D3D12_INPUT_ELEMENT_DESC aInputLayout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

  psoDesc.pRootSignature = m_pSkyboxRootSignature;
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.InputLayout = {aInputLayout, _countof(aInputLayout)};
  psoDesc.VS = {pVSBuffer->GetBufferPointer(), pVSBuffer->GetBufferSize()};
  psoDesc.PS = {pPSBuffer->GetBufferPointer(), pPSBuffer->GetBufferSize()};
  psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
  psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  psoDesc.DepthStencilState.DepthEnable = FALSE;
  psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = DXGI_FORMAT_R32G32B32A32_FLOAT;
  psoDesc.DSVFormat = m_DepthStencilBufferFormat;
  psoDesc.SampleDesc.Count = 1;
  psoDesc.SampleDesc.Quality = 0;
  psoDesc.SampleMask = UINT_MAX;
  V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pSkyboxPSO)));

  psoDesc.RTVFormats[0] = m_BackBufferFormat;
  psoDesc.SampleDesc = GetMsaaSampleDesc();
  V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pSkyboxPSOForBackbuffer)));

  return hr;
}

HRESULT HDRToneMappingCSApp::CreateLuminanceSumPSO() {
  HRESULT hr;
  ComPtr<ID3DBlob> pCSBuffer, pCSBuffer2, pErrorBuffer;
  DWORD compileFlags = 0;

#if defined(_DEBUG)
  compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
  compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

  V_RETURN(d3dUtils::CompileShaderFromFile(L"Shaders/ReduceTo1DCS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "CSMain",
                                           "cs_5_0", compileFlags, 0, pCSBuffer.GetAddressOf(),
                                           pErrorBuffer.GetAddressOf()));
  if (pErrorBuffer) {
    DXOutputDebugStringA((const char *)pErrorBuffer->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  V_RETURN(d3dUtils::CompileShaderFromFile(L"Shaders/ReduceToSingleCS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                           "CSMain", "cs_5_0", compileFlags, 0, pCSBuffer2.GetAddressOf(),
                                           pErrorBuffer.GetAddressOf()));
  if (pErrorBuffer) {
    DXOutputDebugStringA((const char *)pErrorBuffer->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  CD3DX12_ROOT_PARAMETER rootParams[2];
  CD3DX12_DESCRIPTOR_RANGE tables[2];
  ComPtr<ID3DBlob> pSignatureBuffer;

  tables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
  tables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

  rootParams[0].InitAsConstants(4, 0);
  rootParams[1].InitAsDescriptorTable(2, &tables[0]);

  D3D12_VERSIONED_ROOT_SIGNATURE_DESC sigDesc = {};
  sigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
  sigDesc.Desc_1_0.NumParameters = _countof(rootParams);
  sigDesc.Desc_1_0.pParameters = rootParams;

  V_RETURN(
      D3D12SerializeVersionedRootSignature(&sigDesc, pSignatureBuffer.GetAddressOf(), pErrorBuffer.GetAddressOf()));
  if (pErrorBuffer) {
    DXOutputDebugStringA((const char *)pErrorBuffer->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  V_RETURN(m_pd3dDevice->CreateRootSignature(0, pSignatureBuffer->GetBufferPointer(), pSignatureBuffer->GetBufferSize(),
                                             IID_PPV_ARGS(&m_pLuminanceSumSignature)));

  D3D12_COMPUTE_PIPELINE_STATE_DESC csoDesc = {};

  csoDesc.pRootSignature = m_pLuminanceSumSignature;
  csoDesc.CS = {pCSBuffer->GetBufferPointer(), pCSBuffer->GetBufferSize()};
  csoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
  csoDesc.NodeMask = 0;
  V_RETURN(m_pd3dDevice->CreateComputePipelineState(&csoDesc, IID_PPV_ARGS(&m_pLuminanceSumPSO)));

  csoDesc.CS = {pCSBuffer2->GetBufferPointer(), pCSBuffer2->GetBufferSize()};
  V_RETURN(m_pd3dDevice->CreateComputePipelineState(&csoDesc, IID_PPV_ARGS(&m_pLuminanceSumPSO2)));

  return hr;
}

HRESULT HDRToneMappingCSApp::CreateBoomPSO() {

  HRESULT hr;
  ComPtr<ID3DBlob> pCSBuffer, pCSBuffer2, pErrorBuffer;
  DWORD compileFlags = 0;

#if defined(_DEBUG)
  compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
  compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

  V_RETURN(d3dUtils::CompileShaderFromFile(L"Shaders/BrightPassAndHorzFilterCS.hlsl", nullptr,
                                           D3D_COMPILE_STANDARD_FILE_INCLUDE, "BrightPassCS", "cs_5_0", compileFlags, 0,
                                           pCSBuffer.GetAddressOf(), pErrorBuffer.GetAddressOf()));
  if (pErrorBuffer) {
    DXOutputDebugStringA((const char *)pErrorBuffer->GetBufferPointer());
    V_RETURN(E_FAIL);
  }
  V_RETURN(d3dUtils::CompileShaderFromFile(L"Shaders/FilterCS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                                           "CSVerticalFilter", "cs_5_0", compileFlags, 0, pCSBuffer2.GetAddressOf(),
                                           pErrorBuffer.GetAddressOf()));
  if (pErrorBuffer) {
    DXOutputDebugStringA((const char *)pErrorBuffer->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  CD3DX12_ROOT_PARAMETER rootParams[5];
  CD3DX12_DESCRIPTOR_RANGE tables[3];

  rootParams[0].InitAsConstantBufferView(0);
  rootParams[1].InitAsConstantBufferView(1);
  tables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
  tables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
  tables[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
  rootParams[2].InitAsDescriptorTable(1, &tables[0]);
  rootParams[3].InitAsDescriptorTable(1, &tables[1]);
  rootParams[4].InitAsDescriptorTable(1, &tables[2]);

  ComPtr<ID3DBlob> pSignatureBuffer;
  D3D12_VERSIONED_ROOT_SIGNATURE_DESC sigDesc = {};
  sigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
  sigDesc.Desc_1_0.NumParameters = _countof(rootParams);
  sigDesc.Desc_1_0.pParameters = rootParams;

  V_RETURN(
      D3D12SerializeVersionedRootSignature(&sigDesc, pSignatureBuffer.GetAddressOf(), pErrorBuffer.GetAddressOf()));
  if (pErrorBuffer) {
    DXOutputDebugStringA((const char *)pErrorBuffer->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  V_RETURN(m_pd3dDevice->CreateRootSignature(0, pSignatureBuffer->GetBufferPointer(), pSignatureBuffer->GetBufferSize(),
                                             IID_PPV_ARGS(&m_pBoomSignature)));

  D3D12_COMPUTE_PIPELINE_STATE_DESC csoDesc = {};

  csoDesc.pRootSignature = m_pBoomSignature;
  csoDesc.CS = {pCSBuffer->GetBufferPointer(), pCSBuffer->GetBufferSize()};
  V_RETURN(m_pd3dDevice->CreateComputePipelineState(&csoDesc, IID_PPV_ARGS(&m_pBoomPSO)));

  csoDesc.CS = {pCSBuffer2->GetBufferPointer(), pCSBuffer2->GetBufferSize()};
  V_RETURN(m_pd3dDevice->CreateComputePipelineState(&csoDesc, IID_PPV_ARGS(&m_pBoomPSO2)));

  return hr;
}

HRESULT HDRToneMappingCSApp::CreateFinalRenderPSO() {
  HRESULT hr;
  ComPtr<ID3DBlob> pVSBuffer, pPSBuffer, pErrorBuffer;
  DWORD compileFlags = 0;

#if defined(_DEBUG)
  compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
  compileFlags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

  V_RETURN(d3dUtils::CompileShaderFromFile(L"Shaders/FinalPass.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "QuadVS",
                                           "vs_5_0", compileFlags, 0, pVSBuffer.GetAddressOf(),
                                           pErrorBuffer.GetAddressOf()));
  if (pErrorBuffer) {
    DXOutputDebugStringA((const char *)pErrorBuffer->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  V_RETURN(d3dUtils::CompileShaderFromFile(L"Shaders/FinalPass.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "PSFinalPass",
                                           "ps_5_0", compileFlags, 0, pPSBuffer.GetAddressOf(),
                                           pErrorBuffer.GetAddressOf()));
  if (pErrorBuffer) {
    DXOutputDebugStringA((const char *)pErrorBuffer->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  CD3DX12_ROOT_PARAMETER rootParams[4];
  CD3DX12_DESCRIPTOR_RANGE tables[3];

  rootParams[0].InitAsConstants(1, 0);
  tables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
  tables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);
  tables[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2);
  rootParams[1].InitAsDescriptorTable(1, &tables[0]);
  rootParams[2].InitAsDescriptorTable(1, &tables[1]);
  rootParams[3].InitAsDescriptorTable(1, &tables[2]);

  CD3DX12_STATIC_SAMPLER_DESC staticSamplers[] = {

      {
          0,
          D3D12_FILTER_MIN_MAG_MIP_POINT,
      },
      {
          1,
          D3D12_FILTER_MIN_MAG_MIP_LINEAR,
          D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
          D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
          D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
      },
  };

  D3D12_VERSIONED_ROOT_SIGNATURE_DESC sigDesc = {};
  ComPtr<ID3DBlob> pSignatureBuffer;

  sigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_0;
  sigDesc.Desc_1_0.pParameters = rootParams;
  sigDesc.Desc_1_0.NumParameters = _countof(rootParams);
  sigDesc.Desc_1_0.pStaticSamplers = staticSamplers;
  sigDesc.Desc_1_0.NumStaticSamplers = _countof(staticSamplers);
  sigDesc.Desc_1_0.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
  V_RETURN(
      D3D12SerializeVersionedRootSignature(&sigDesc, pSignatureBuffer.GetAddressOf(), pErrorBuffer.GetAddressOf()));
  if (pErrorBuffer) {
    DXOutputDebugStringA((const char *)pErrorBuffer->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  V_RETURN(m_pd3dDevice->CreateRootSignature(0, pSignatureBuffer->GetBufferPointer(), pSignatureBuffer->GetBufferSize(),
                                             IID_PPV_ARGS(&m_pFinalRenderSignature)));

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

  psoDesc.pRootSignature = m_pFinalRenderSignature;
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.VS = {pVSBuffer->GetBufferPointer(), pVSBuffer->GetBufferSize()};
  psoDesc.PS = {pPSBuffer->GetBufferPointer(), pPSBuffer->GetBufferSize()};
  psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
  psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  psoDesc.DepthStencilState.DepthEnable = FALSE;
  psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = m_BackBufferFormat;
  psoDesc.DSVFormat = m_DepthStencilBufferFormat;
  psoDesc.SampleDesc = GetMsaaSampleDesc();
  psoDesc.SampleMask = UINT_MAX;

  V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pFinalRenderPSO)));

  return hr;
}

HRESULT HDRToneMappingCSApp::CreateGeometryBuffers() {

  HRESULT hr;
  FLOAT fHighW = -1.0f - 1.0f / (FLOAT)m_uFrameWidth;
  FLOAT fHighH = -1.0f - 1.0f / (FLOAT)m_uFrameHeight;
  FLOAT fLowW = 1.0f + 1.0f / (FLOAT)m_uFrameWidth;
  FLOAT fLowH = 1.0f + 1.0f / (FLOAT)m_uFrameHeight;

  XMFLOAT4 vertices[] = {{fLowW, fLowH, 1.0f, 1.0f},
                         {fLowW, fHighH, 1.0f, 1.0f},
                         {fHighW, fLowH, 1.0f, 1.0f},
                         {fHighW, fHighH, 1.0f, 1.0f}};

  if (!m_pSkyboxBuffer) {
    V_RETURN(CreateMeshBuffer(&m_pSkyboxBuffer));
    m_pSkyboxBuffer->CreateVertexBuffer(m_pd3dDevice, m_pd3dCommandList, vertices, _countof(vertices),
                                        sizeof(XMFLOAT4));
  } else {
    V_RETURN(m_pSkyboxBuffer->UploadVertexBuffer(m_pd3dCommandList, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                 D3D12_RESOURCE_STATE_GENERIC_READ, vertices, sizeof(vertices)));
  }

  return hr;
}

HRESULT HDRToneMappingCSApp::CreateCbvSrvUavHeap() {
  HRESULT hr;
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};

  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.NodeMask = 0;
  heapDesc.NumDescriptors = 11;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

  SAFE_RELEASE(m_pCbvUavSrvHeap);
  V_RETURN(m_pd3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_pCbvUavSrvHeap)));

  return hr;
}

HRESULT HDRToneMappingCSApp::CreateDescriptors() {
  HRESULT hr;
  int i;
  D3D12_RESOURCE_DESC texDesc = {};
  D3D12_CLEAR_VALUE clrValue = {};
  D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
  CD3DX12_CPU_DESCRIPTOR_HANDLE hRTVCpuHandle;
  CD3DX12_CPU_DESCRIPTOR_HANDLE hSrvUavCpuHandle;
  CD3DX12_GPU_DESCRIPTOR_HANDLE hSrvUavGpuHandle;

  SAFE_RELEASE(m_pColorTexture);
  SAFE_RELEASE(m_pLum1DTexture[0]);
  SAFE_RELEASE(m_pLum1DTexture[1]);
  SAFE_RELEASE(m_pBoomTexture[0]);
  SAFE_RELEASE(m_pBoomTexture[1]);

  /// Color RTVs.
  texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texDesc.Width = m_uFrameWidth;
  texDesc.Height = m_uFrameHeight;
  texDesc.DepthOrArraySize = 1;
  texDesc.MipLevels = 1;
  texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  texDesc.SampleDesc = GetMsaaSampleDesc();
  texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  clrValue.Format = texDesc.Format;
  memcpy(clrValue.Color, &Colors::DimGray, sizeof(clrValue.Color));
  V_RETURN(m_pd3dDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                                 D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                 &clrValue, IID_PPV_ARGS(&m_pColorTexture)));
  DX_SetDebugName(m_pColorTexture, "Color Texture");

  /// Color Texture RTV.
  rtvDesc.Format = DXGI_FORMAT_UNKNOWN;
  rtvDesc.ViewDimension = IsMsaaEnabled() ? D3D12_RTV_DIMENSION_TEXTURE2DMS : D3D12_RTV_DIMENSION_TEXTURE2D;
  if (!IsMsaaEnabled()) {
    rtvDesc.Texture2D.MipSlice = 0;
    rtvDesc.Texture2D.PlaneSlice = 0;
  }
  hRTVCpuHandle = m_pRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
  hRTVCpuHandle.Offset(s_iSwapChainBufferCount, m_uRtvDescriptorSize);
  m_pd3dDevice->CreateRenderTargetView(m_pColorTexture, &rtvDesc, hRTVCpuHandle);

  m_hColorRTV = hRTVCpuHandle;

  /// Color SRV, Luminance Sum 1D SRV, Luminance Sum 1D UAV2,
  ///    Luminance 1D SRV2, Luminance 1D UAV1 must be continuous in heap area.

  /// Color  Texture SRV.
  srvDesc.Format = DXGI_FORMAT_UNKNOWN;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.ViewDimension = IsMsaaEnabled() ? D3D12_SRV_DIMENSION_TEXTURE2DMS : D3D12_SRV_DIMENSION_TEXTURE2D;
  if (!IsMsaaEnabled()) {
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
  }
  hSrvUavCpuHandle = m_pCbvUavSrvHeap->GetCPUDescriptorHandleForHeapStart();
  m_pd3dDevice->CreateShaderResourceView(m_pColorTexture, &srvDesc, hSrvUavCpuHandle);
  hSrvUavGpuHandle = m_pCbvUavSrvHeap->GetGPUDescriptorHandleForHeapStart();
  m_hColorSrv = hSrvUavGpuHandle;

  hSrvUavCpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);
  hSrvUavGpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);

  UINT uLum1DHeapOffsets[4] = {0, 3, 2, 1};
  CD3DX12_CPU_DESCRIPTOR_HANDLE hLum1DCpuHandle;
  for (i = 0; i < 2; ++i) {
    V_RETURN(m_pd3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(UINT64(ceil(m_uFrameWidth / 8.0) * ceil(m_uFrameHeight / 8.0) * sizeof(float)),
                                       D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_pLum1DTexture[i])));

    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.StructureByteStride = sizeof(float);
    srvDesc.Buffer.FirstElement = 0;
    srvDesc.Buffer.NumElements = UINT(ceil(m_uFrameWidth / 8.0) * ceil(m_uFrameHeight / 8.0));
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    hLum1DCpuHandle = hSrvUavCpuHandle;
    hLum1DCpuHandle.InitOffsetted(hSrvUavCpuHandle, uLum1DHeapOffsets[i << 1], m_uCbvSrvUavDescriptorSize);
    m_pd3dDevice->CreateShaderResourceView(m_pLum1DTexture[i], &srvDesc, hLum1DCpuHandle);
    m_hLum1DSrv[i].ptr = hSrvUavGpuHandle.ptr + m_uCbvSrvUavDescriptorSize * uLum1DHeapOffsets[i << 1];

    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.StructureByteStride = srvDesc.Buffer.StructureByteStride;
    uavDesc.Buffer.FirstElement = 0;
    uavDesc.Buffer.NumElements = srvDesc.Buffer.NumElements;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    hLum1DCpuHandle = hSrvUavCpuHandle;
    hLum1DCpuHandle.InitOffsetted(hSrvUavCpuHandle, uLum1DHeapOffsets[(i << 1) + 1], m_uCbvSrvUavDescriptorSize);
    m_pd3dDevice->CreateUnorderedAccessView(m_pLum1DTexture[i], nullptr, &uavDesc, hLum1DCpuHandle);
    m_hLum1DUav[i].ptr = hSrvUavGpuHandle.ptr + m_uCbvSrvUavDescriptorSize * uLum1DHeapOffsets[(i << 1) + 1];
  }

  hSrvUavCpuHandle.Offset(4, m_uCbvSrvUavDescriptorSize);
  hSrvUavGpuHandle.Offset(4, m_uCbvSrvUavDescriptorSize);

  ZeroMemory(&texDesc, sizeof(texDesc));
  texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texDesc.Width = m_uFrameWidth / 8;
  texDesc.Height = m_uFrameHeight / 8;
  texDesc.DepthOrArraySize = 1;
  texDesc.MipLevels = 1;
  texDesc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  texDesc.SampleDesc = GetMsaaSampleDesc();
  texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

  for (i = 0; i < 2; ++i) {
    V_RETURN(m_pd3dDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                                   D3D12_HEAP_FLAG_NONE, &texDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                                   nullptr, IID_PPV_ARGS(&m_pBoomTexture[i])));

    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    m_pd3dDevice->CreateShaderResourceView(m_pBoomTexture[i], &srvDesc, hSrvUavCpuHandle);
    m_hBoomSrv[i] = hSrvUavGpuHandle;

    hSrvUavCpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);
    hSrvUavGpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);

    ZeroMemory(&uavDesc, sizeof(uavDesc));
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;
    uavDesc.Texture2D.PlaneSlice = 0;
    m_pd3dDevice->CreateUnorderedAccessView(m_pBoomTexture[i], nullptr, &uavDesc, hSrvUavCpuHandle);
    m_hBoomUav[i] = hSrvUavGpuHandle;

    hSrvUavCpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);
    hSrvUavGpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);
  }

  ZeroMemory(&srvDesc, sizeof(srvDesc));
  texDesc = m_pSkyBoxCubeMap->Resource->GetDesc();
  srvDesc.Format = DXGI_FORMAT_UNKNOWN;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
  srvDesc.TextureCube.MipLevels = texDesc.MipLevels;
  srvDesc.TextureCube.MostDetailedMip = 0;
  srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

  m_pd3dDevice->CreateShaderResourceView(m_pSkyBoxCubeMap->Resource, &srvDesc, hSrvUavCpuHandle);
  m_hSkyboxCubeMapGpuHandle = hSrvUavGpuHandle;

  hSrvUavCpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);
  hSrvUavGpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);

  ZeroMemory(&srvDesc, sizeof(srvDesc));
  srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
  srvDesc.Texture2D.MipLevels = 1;
  m_pd3dDevice->CreateShaderResourceView(nullptr, &srvDesc, hSrvUavCpuHandle);
  m_hNullSrv = hSrvUavGpuHandle;

  hSrvUavCpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);
  hSrvUavGpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);

  return hr;
}

void HDRToneMappingCSApp::OnFrameMoved(float fTime, float fTimeElapsed) {

  m_Camera.FrameMove(fTimeElapsed, this);

  FrameResources *pFrameResources;

  m_iCurrentFrameIndex = (m_iCurrentFrameIndex + 1) % 3;
  pFrameResources = &m_aFrameResources[m_iCurrentFrameIndex];

  /// Sychronize it.
  m_pSyncFence->WaitForSyncPoint(pFrameResources->FencePoint);

  SkyboxRenderParams skyboxRenderParams;
  XMMATRIX matWVP;

  matWVP = m_Camera.GetViewMatrix() * m_Camera.GetProjMatrix();
  matWVP = XMMatrixInverse(nullptr, matWVP);
  XMStoreFloat4x4(&skyboxRenderParams.MatWorldViewProj, matWVP);

  pFrameResources->SkyboxCBs.CopyData(&skyboxRenderParams, sizeof(skyboxRenderParams), m_iCurrentFrameIndex);
}

void HDRToneMappingCSApp::OnRenderFrame(float fTime, float fTimeElapsed) {

  HRESULT hr;
  FrameResources *pFrameResources = &m_aFrameResources[m_iCurrentFrameIndex];
  D3D12_CPU_DESCRIPTOR_HANDLE hSkyboxRTVHandle;

  V(pFrameResources->CmdAllocator->Reset());
  V(m_pd3dCommandList->Reset(pFrameResources->CmdAllocator, m_pSkyboxPSO));

  m_pd3dCommandList->SetDescriptorHeaps(1, &m_pCbvUavSrvHeap);

  /// Render sky box.
  if (m_bPostProcessON) {
    m_pd3dCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_pColorTexture,
                                                                                D3D12_RESOURCE_STATE_GENERIC_READ,
                                                                                D3D12_RESOURCE_STATE_RENDER_TARGET));
    hSkyboxRTVHandle = m_hColorRTV;
  } else {
    m_pd3dCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
                                                                                D3D12_RESOURCE_STATE_PRESENT,
                                                                                D3D12_RESOURCE_STATE_RENDER_TARGET));
    hSkyboxRTVHandle = CurrentBackBufferView();
  }

  m_pd3dCommandList->RSSetViewports(1, &m_ScreenViewport);
  m_pd3dCommandList->RSSetScissorRects(1, &m_ScissorRect);

  m_pd3dCommandList->ClearRenderTargetView(hSkyboxRTVHandle, (const FLOAT *)&Colors::DimGray, 1, &m_ScissorRect);
  m_pd3dCommandList->OMSetRenderTargets(1, &hSkyboxRTVHandle, FALSE, nullptr);

  RenderSkybox(pFrameResources);

  if (m_bPostProcessON) {

    MeasureLuminanceCS();

    if (m_bBoom)
      BoomCS();

    RenderFinalScreenQuad();
  }

  m_pd3dCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
                                                                              D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                                              D3D12_RESOURCE_STATE_PRESENT));

  // Done recording commands.
  m_pd3dCommandList->Close();

  // Add the command list to the queue for execution.
  ID3D12CommandList *cmdList[] = {m_pd3dCommandList};
  m_pd3dCommandQueue->ExecuteCommandLists(1, cmdList);

  V(m_pSyncFence->Signal(m_pd3dCommandQueue, &pFrameResources->FencePoint));

  Present();
}

void HDRToneMappingCSApp::RenderSkybox(FrameResources *pFrameResource) {

  D3D12_GPU_VIRTUAL_ADDRESS cbAddress = pFrameResource->SkyboxCBs.GetConstBufferAddress();
  UINT cbCBByteSize = d3dUtils::CalcConstantBufferByteSize(sizeof(SkyboxRenderParams));

  m_pd3dCommandList->SetGraphicsRootSignature(m_pSkyboxRootSignature);
  m_pd3dCommandList->SetPipelineState(m_bPostProcessON ? m_pSkyboxPSO : m_pSkyboxPSOForBackbuffer);

  cbAddress = cbAddress + cbCBByteSize * m_iCurrentFrameIndex;
  m_pd3dCommandList->SetGraphicsRootConstantBufferView(0, cbAddress);
  m_pd3dCommandList->SetGraphicsRootDescriptorTable(1, m_hSkyboxCubeMapGpuHandle);

  m_pd3dCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  m_pd3dCommandList->IASetVertexBuffers(0, 1, &m_pSkyboxBuffer->VertexBufferView());
  m_pd3dCommandList->IASetIndexBuffer(nullptr);
  m_pd3dCommandList->DrawInstanced(4, 1, 0, 0);
}

void HDRToneMappingCSApp::MeasureLuminanceCS() {

  int dimx = int(ceil(ToneMappingTexSize / 8.0));
  int dimy = dimx;
  UINT parami[4];

  D3D12_RESOURCE_BARRIER resBarriers[2] = {
      CD3DX12_RESOURCE_BARRIER::Transition(m_pColorTexture, D3D12_RESOURCE_STATE_RENDER_TARGET,
                                           D3D12_RESOURCE_STATE_GENERIC_READ),
      CD3DX12_RESOURCE_BARRIER::Transition(m_pLum1DTexture[0], D3D12_RESOURCE_STATE_GENERIC_READ,
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};

  m_pd3dCommandList->ResourceBarrier(2, resBarriers);

  m_pd3dCommandList->SetPipelineState(m_pLuminanceSumPSO);
  m_pd3dCommandList->SetComputeRootSignature(m_pLuminanceSumSignature);

  parami[0] = UINT(dimx);
  parami[1] = UINT(dimy);
  parami[2] = UINT(m_uFrameWidth);
  parami[3] = UINT(m_uFrameHeight);
  m_pd3dCommandList->SetComputeRoot32BitConstants(0, 4, parami, 0);
  m_pd3dCommandList->SetComputeRootDescriptorTable(1, m_hColorSrv);

  m_pd3dCommandList->Dispatch(dimx, dimy, 1);

  ///
  int dim = dimx * dimy;
  int nNumToReduce = dim;
  dim = int(ceil(dim / 128.0));

  if (nNumToReduce > 1) {

    m_pd3dCommandList->SetPipelineState(m_pLuminanceSumPSO2);

    for (;;) {
      resBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_pLum1DTexture[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                            D3D12_RESOURCE_STATE_GENERIC_READ);
      resBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_pLum1DTexture[1], D3D12_RESOURCE_STATE_GENERIC_READ,
                                                            D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
      m_pd3dCommandList->ResourceBarrier(2, resBarriers);

      parami[0] = UINT(nNumToReduce);
      parami[1] = UINT(dim);
      m_pd3dCommandList->SetComputeRoot32BitConstants(0, 2, parami, 0);
      m_pd3dCommandList->SetComputeRootDescriptorTable(1, m_hLum1DSrv[0]);

      m_pd3dCommandList->Dispatch(dim, 1, 1);

      nNumToReduce = dim;
      dim = int(ceil(dim / 128.0));
      if (nNumToReduce == 1)
        break;

      std::swap(m_pLum1DTexture[0], m_pLum1DTexture[1]);
      std::swap(m_hLum1DSrv[0], m_hLum1DSrv[1]);
      std::swap(m_hLum1DUav[0], m_hLum1DUav[1]);
    }
  } else {
    std::swap(m_pLum1DTexture[0], m_pLum1DTexture[1]);
    std::swap(m_hLum1DSrv[0], m_hLum1DSrv[1]);
    std::swap(m_hLum1DUav[0], m_hLum1DUav[1]);
  }

  resBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_pLum1DTexture[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ);
  m_pd3dCommandList->ResourceBarrier(1, resBarriers);
}

static float GaussianDistribution(float x, float y, float rho) {
  float g = 1.0f / sqrtf(2.0f * XM_PI * rho * rho);
  g *= expf(-(x * x + y * y) / (2 * rho * rho));

  return g;
}

static HRESULT GetSampleWeights_D3D11(XMFLOAT4 *avColorWeight, float fDeviation, float fMultiplier) {
  // Fill the center texel
  float weight = 1.0f * GaussianDistribution(0, 0, fDeviation);
  avColorWeight[7] = XMFLOAT4(weight, weight, weight, 1.0f);

  // Fill the right side
  for (int i = 1; i < 8; i++) {
    weight = fMultiplier * GaussianDistribution((float)i, 0, fDeviation);
    avColorWeight[7 - i] = XMFLOAT4(weight, weight, weight, 1.0f);
  }

  // Copy to the left side
  for (int i = 8; i < 15; i++) {
    avColorWeight[i] = avColorWeight[14 - i];
  }

  return S_OK;
}

void HDRToneMappingCSApp::BoomCS() {

  D3D12_GPU_VIRTUAL_ADDRESS cbGaussWeightsAddress, cbParamsAddress;
  UINT cbCBByteSize = d3dUtils::CalcConstantBufferByteSize(sizeof(BoomCSParams));
  D3D12_GPU_DESCRIPTOR_HANDLE srvHandle1, srvHandle2, uavHandle;
  int dimx, dimy;
  D3D12_RESOURCE_BARRIER resBarriers[2] = {
      CD3DX12_RESOURCE_BARRIER::Transition(m_pBoomTexture[0], D3D12_RESOURCE_STATE_GENERIC_READ,
                                           D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
  };

  m_pd3dCommandList->ResourceBarrier(1, resBarriers);

  m_pd3dCommandList->SetComputeRootSignature(m_pBoomSignature);
  m_pd3dCommandList->SetPipelineState(m_pBoomPSO);

  cbGaussWeightsAddress = FrameResources::BoomGaussWeightsCBs.GetConstBufferAddress();
  m_pd3dCommandList->SetComputeRootConstantBufferView(0, cbGaussWeightsAddress);
  cbParamsAddress = FrameResources::BoomCSCBs.GetConstBufferAddress();
  m_pd3dCommandList->SetComputeRootConstantBufferView(1, cbParamsAddress);
  srvHandle1 = m_hColorSrv;
  srvHandle2 = m_hLum1DSrv[1];
  uavHandle = m_hBoomUav[0];
  m_pd3dCommandList->SetComputeRootDescriptorTable(2, srvHandle1);
  m_pd3dCommandList->SetComputeRootDescriptorTable(3, srvHandle2);
  m_pd3dCommandList->SetComputeRootDescriptorTable(4, uavHandle);

  dimx = m_uFrameWidth / 8;
  dimy = m_uFrameHeight / 8;

  m_pd3dCommandList->Dispatch((int)ceil((float)dimx / (128 - 2 * 7)), dimy, 1);

  resBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_pBoomTexture[0], D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ);
  resBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(m_pBoomTexture[1], D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  m_pd3dCommandList->ResourceBarrier(2, resBarriers);

  m_pd3dCommandList->SetPipelineState(m_pBoomPSO2);

  m_pd3dCommandList->SetComputeRootConstantBufferView(0, cbGaussWeightsAddress);
  cbParamsAddress += cbCBByteSize;
  m_pd3dCommandList->SetComputeRootConstantBufferView(1, cbParamsAddress);
  srvHandle1 = m_hBoomSrv[0];
  uavHandle = m_hBoomUav[1];
  m_pd3dCommandList->SetComputeRootDescriptorTable(2, srvHandle1);
  m_pd3dCommandList->SetComputeRootDescriptorTable(3, srvHandle2);
  m_pd3dCommandList->SetComputeRootDescriptorTable(4, uavHandle);

  m_pd3dCommandList->Dispatch(dimx, (int)ceil((float)dimy / (128 - 2 * 7)), 1);

  resBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_pBoomTexture[1], D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ);
  m_pd3dCommandList->ResourceBarrier(1, resBarriers);
}

void HDRToneMappingCSApp::RenderFinalScreenQuad() {

  float paramf;
  D3D12_VERTEX_BUFFER_VIEW dummyVBV = {};
  D3D12_GPU_DESCRIPTOR_HANDLE hBoomSrv;

  if (m_bBoom)
    hBoomSrv = m_hBoomSrv[1];
  else
    hBoomSrv = m_hNullSrv;

  m_pd3dCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
                                                                              D3D12_RESOURCE_STATE_PRESENT,
                                                                              D3D12_RESOURCE_STATE_RENDER_TARGET));

  m_pd3dCommandList->SetGraphicsRootSignature(m_pFinalRenderSignature);
  m_pd3dCommandList->SetPipelineState(m_pFinalRenderPSO);

  paramf = 1.0f / (ToneMappingTexSize * ToneMappingTexSize);
  m_pd3dCommandList->SetGraphicsRoot32BitConstant(0, *(int *)&paramf, 0);
  m_pd3dCommandList->SetGraphicsRootDescriptorTable(1, m_hColorSrv);
  m_pd3dCommandList->SetGraphicsRootDescriptorTable(2, m_hLum1DSrv[1]);
  m_pd3dCommandList->SetGraphicsRootDescriptorTable(3, hBoomSrv);

  m_pd3dCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), FALSE, nullptr);

  m_pd3dCommandList->RSSetViewports(1, &m_ScreenViewport);
  m_pd3dCommandList->RSSetScissorRects(1, &m_ScissorRect);

  m_pd3dCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
  m_pd3dCommandList->IASetVertexBuffers(0, 1, &dummyVBV);
  m_pd3dCommandList->IASetIndexBuffer(nullptr);
  m_pd3dCommandList->DrawInstanced(4, 1, 0, 0);
}

void HDRToneMappingCSApp::OnResizeFrame(int cx, int cy) {

  m_Camera.SetProjParams(XM_PIDIV4, GetAspectRatio(), 0.1f, 5000.0f);
  m_Camera.SetWindow(cx, cy);

  m_pd3dDirectCmdAlloc->Reset();
  m_pd3dCommandList->Reset(m_pd3dDirectCmdAlloc, nullptr);

  /// Recreate resource the may changed.
  CreateGeometryBuffers();

  CreateCbvSrvUavHeap();

  CreateDescriptors();

  m_pd3dCommandList->Close();

  ID3D12CommandList *cmdList[] = {m_pd3dCommandList};
  m_pd3dCommandQueue->ExecuteCommandLists(1, cmdList);

  FlushCommandQueue();

  /// Update CBs.
  BoomCBGuassWeights weightsCB;
  GetSampleWeights_D3D11(weightsCB.vGaussWeights, 3.0f, 1.25f);
  BoomCSParams boomCSParams1, boomCSParams2;

  boomCSParams1.uOutputWidth = cx / 8;
  boomCSParams1.fInverse = 1.0f / (ToneMappingTexSize * ToneMappingTexSize);
  boomCSParams1.vInputSize = {(uint32_t)cx, (uint32_t)cy};

  boomCSParams2.vOutputSize = {(uint32_t)cx / 8, (uint32_t)cy / 8};
  boomCSParams2.vInputSize = boomCSParams2.vOutputSize;

  FrameResources::BoomGaussWeightsCBs.CopyData(&weightsCB, sizeof(weightsCB), 0);
  FrameResources::BoomCSCBs.CopyData(&boomCSParams1, sizeof(BoomCSParams), 0);
  FrameResources::BoomCSCBs.CopyData(&boomCSParams2, sizeof(BoomCSParams), 1);
}

LRESULT HDRToneMappingCSApp::OnMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool *pbNoFurtherProcessing) {
  
  if(msg == WM_KEYDOWN) {
    if (wp == L'B' || wp == L'b') {
      m_bBoom ^= 1;
    } else if (wp == L'P' || wp == L'p') {
      m_bPostProcessON ^= 1;
    } else if (wp == L'G' || wp == L'g') {
      m_bBlur ^= 1;
    }
  }

  m_Camera.HandleMessages(hwnd, msg, wp, lp);

  return 0L;
}
