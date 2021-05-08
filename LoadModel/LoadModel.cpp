#include <windows.h>
#include <Camera.h>
#include <D3D12RendererContext.hpp>
#include <DirectXColors.h>
#include <RootSignatureGenerator.h>
#include <UploadBuffer.h>
#include <Win32Application.hpp>
#include <DirectXMath.h>
#include <ResourceUploadBatch.hpp>
#include <SDKmesh.h>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

HRESULT CreateRendererAndInteractor(D3D12RendererContext **ppRenderer, WindowInteractor **pInteractor);

int main() {

  HRESULT hr;
  int ret;
  D3D12RendererContext *pRenderer;
  WindowInteractor *pInteractor;

  V_RETURN(CreateRendererAndInteractor(&pRenderer, &pInteractor));
  ret = RunSample(pRenderer, pInteractor, 800, 600, L"Load Model for D3D12");

  SAFE_DELETE(pRenderer);

  return ret;
}

struct PassConstants {
  XMFLOAT4X4 ViewProj;
  XMFLOAT3 EyePosW;
  float Padding0;
};

struct ObjConstants {
  XMFLOAT4X4 World;
  XMFLOAT4X4 WorldInvTranspose;
};

struct FrameResources {
public:
  HRESULT CreateCommandAllocator(ID3D12Device *pDevice) {
    HRESULT hr;

    V_RETURN(pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&CommandAllocator)));
    return hr;
  }

  static HRESULT CreateBuffers(ID3D12Device *pDevice, int iPassCount) {
    HRESULT hr;

    V_RETURN(PassCBs.CreateBuffer(pDevice, iPassCount, sizeof(PassConstants), TRUE));
    return hr;
  }

  ComPtr<ID3D12CommandAllocator> CommandAllocator;
  static UploadBuffer PassCBs;
  UINT64 FencePoint = 0;
};

UploadBuffer FrameResources::PassCBs;

class LoadModelSample : public D3D12RendererContext, public WindowInteractor {
public:
  LoadModelSample();

  HRESULT CheckDeviceFeatureSupport(ID3D12Device5 *pDevice) override;
  HRESULT OnInitPipelines() override;

  void OnResizeFrame(int cx, int cy) override;
  void OnFrameMoved(float fTime, float fElapsedTime) override;
  void OnRenderFrame(float fTime, float fElapedTime) override;

private:
  LRESULT OnMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool *pbNoFurtherProcessing) override;

  HRESULT LoadModel();
  HRESULT CreatePSOs();

  CDXUTSDKMesh m_Model;
  std::vector<ComPtr<ID3D12PipelineState>> m_aPSOs;
  ComPtr<ID3D12RootSignature> m_pRootSignature;
  ComPtr<ID3D12DescriptorHeap> m_pModelDescriptorHeap;
  FrameResources m_aFrameResources[3];
  int m_iCurrentFrameIndex = 0;
  CFirstPersonCamera m_Camera;

  XMINT2 m_ptLastMousePos;
};

HRESULT CreateRendererAndInteractor(D3D12RendererContext **ppRenderer, WindowInteractor **pInteractor) {
  LoadModelSample *pInstance = new LoadModelSample();
  *ppRenderer = pInstance;
  *pInteractor = pInstance;
  return S_OK;
}

LoadModelSample::LoadModelSample() { this->m_aDeviceConfig.VsyncEnabled = FALSE; }

HRESULT LoadModelSample::CheckDeviceFeatureSupport(ID3D12Device5 *pDevice) {
  HRESULT hr;

  V_RETURN(__super::CheckDeviceFeatureSupport(pDevice));

  D3D12_FEATURE_DATA_SHADER_CACHE featureData = {};

  if (FAILED(pDevice->CheckFeatureSupport(D3D12_FEATURE_SHADER_CACHE, &featureData, sizeof(featureData))) ||
      (featureData.SupportFlags & D3D12_SHADER_CACHE_SUPPORT_LIBRARY) == 0) {
    return E_NOTIMPL;
  }
  return S_OK;
}

HRESULT LoadModelSample::OnInitPipelines() {

  HRESULT hr;

  V_RETURN(LoadModel());
  V_RETURN(CreatePSOs());

  for (auto &frameRes : m_aFrameResources) {
    frameRes.CreateCommandAllocator(m_pd3dDevice);
  }
  FrameResources::CreateBuffers(m_pd3dDevice, 3);

  XMFLOAT3 vMin = XMFLOAT3(-1000.0f, -1000.0f, -1000.0f);
  XMFLOAT3 vMax = XMFLOAT3(1000.0f, 1000.0f, 1000.0f);

  m_Camera.SetViewParams(XMVectorSet(100.0f, 5.0f, 5.0f, 0.f), g_XMZero);
  m_Camera.SetRotateButtons(TRUE, FALSE, FALSE);
  m_Camera.SetScalers(0.01f, 10.0f);
  m_Camera.SetDrag(true);
  m_Camera.SetEnableYAxisMovement(true);
  m_Camera.SetClipToBoundary(TRUE, &vMin, &vMax);
  m_Camera.FrameMove(0, this);

  return hr;
}

HRESULT LoadModelSample::CreatePSOs() {

  HRESULT hr;
  ComPtr<ID3DBlob> pVSBuffer, pPSBuffer;
  ComPtr<ID3DBlob> pErrorBuffer;

  V(d3dUtils::CompileShaderFromFile(L"Shaders/basic.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", 0, 0,
                                           &pVSBuffer, &pErrorBuffer));
  if (FAILED(hr)) {
    DX_TRACE(L"Compile VS error: %S\n", pErrorBuffer ? pErrorBuffer->GetBufferPointer() : "Unknown");
    return hr;
  } else if(pErrorBuffer) {
    DX_TRACE(L"Compil VS warning: %S\n", pErrorBuffer->GetBufferPointer());
    pErrorBuffer = nullptr;
  }

  V(d3dUtils::CompileShaderFromFile(L"Shaders/basic.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", 0, 0,
                                           &pPSBuffer, &pErrorBuffer));
  if (FAILED(hr)) {
    DX_TRACE(L"Compile PS error: %S\n", pErrorBuffer ? pErrorBuffer->GetBufferPointer() : "Unknown");
    return hr;
  } else if(pErrorBuffer) {
    DX_TRACE(L"Compil PS warning: %S\n", pErrorBuffer->GetBufferPointer());
    pErrorBuffer = nullptr;
  }

  RootSignatureGenerator rsGen;
  rsGen.AddConstBufferView(0);
  rsGen.AddDescriptorTable({CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0)},
                           D3D12_SHADER_VISIBILITY_PIXEL);
  rsGen.AddStaticSamples({CD3DX12_STATIC_SAMPLER_DESC(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR)});

  rsGen.Generate(m_pd3dDevice, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT, &m_pRootSignature);

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.pRootSignature = m_pRootSignature.Get();
  psoDesc.VS = {pVSBuffer->GetBufferPointer(), pVSBuffer->GetBufferSize()};
  psoDesc.PS = {pPSBuffer->GetBufferPointer(), pPSBuffer->GetBufferSize()};
  psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = m_BackBufferFormat;
  psoDesc.DSVFormat = m_DepthStencilBufferFormat;
  psoDesc.SampleDesc = GetMsaaSampleDesc();
  psoDesc.SampleMask = UINT_MAX;

  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  ComPtr<ID3D12PipelineState> pso;

  psoDesc.InputLayout = {inputLayout, (UINT)std::size(inputLayout)};

  V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pso.ReleaseAndGetAddressOf())));
  m_aPSOs.push_back(std::move(pso));

  return hr;
}

HRESULT LoadModelSample::LoadModel() {
  HRESULT hr;

  ResourceUploadBatch uploadBatch(m_pd3dDevice, &m_MemAllocator);

  uploadBatch.Begin(D3D12_COMMAND_LIST_TYPE_DIRECT);

  m_Model.Create(&uploadBatch, LR"(directx-sdk-samples\Media\powerplant\\powerplant.sdkmesh)");

  m_Model.GetResourceDescriptorHeap(m_pd3dDevice, TRUE, &m_pModelDescriptorHeap);

  std::future<HRESULT> batchWaitable;

  V_RETURN(uploadBatch.End(m_pd3dCommandQueue, &batchWaitable));

  V_RETURN(batchWaitable.get());

  return hr;
}

void LoadModelSample::OnResizeFrame(int cx, int cy) {
  m_Camera.SetProjParams(XM_PIDIV4, GetAspectRatio(), 0.1f, 100000.0f);
}

void LoadModelSample::OnFrameMoved(float fTime, float fElapsedTime) {

  m_Camera.FrameMove(fElapsedTime, this);

  m_iCurrentFrameIndex = (m_iCurrentFrameIndex + 1) % 3;
  auto pFrameResource = &m_aFrameResources[m_iCurrentFrameIndex];

  m_pSyncFence->WaitForSyncPoint(pFrameResource->FencePoint);

  XMMATRIX VP = m_Camera.GetViewMatrix() * m_Camera.GetProjMatrix();
  PassConstants pc;
  XMStoreFloat4x4(&pc.ViewProj, XMMatrixTranspose(VP));
  XMStoreFloat3(&pc.EyePosW, m_Camera.GetEyePt());
  FrameResources::PassCBs.CopyData(&pc, sizeof(pc), m_iCurrentFrameIndex);
}

void LoadModelSample::OnRenderFrame(float fTime, float fElapsedTime) {

  HRESULT hr;
  auto pFrameResource = &m_aFrameResources[m_iCurrentFrameIndex];

  V(pFrameResource->CommandAllocator->Reset());
  V(m_pd3dCommandList->Reset(pFrameResource->CommandAllocator.Get(), nullptr));

  PrepareNextFrame();

  m_pd3dCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::Black, 1, &m_ScissorRect);
  m_pd3dCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f,
                                           0, 0, nullptr);

  m_pd3dCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), TRUE, &DepthStencilView());
  m_pd3dCommandList->SetGraphicsRootSignature(m_pRootSignature.Get());

  m_pd3dCommandList->RSSetViewports(1, &m_ScreenViewport);
  m_pd3dCommandList->RSSetScissorRects(1, &m_ScissorRect);

  m_pd3dCommandList->SetPipelineState(m_aPSOs[0].Get());
  m_pd3dCommandList->SetDescriptorHeaps(1, m_pModelDescriptorHeap.GetAddressOf());

  m_pd3dCommandList->SetGraphicsRootConstantBufferView(0, FrameResources::PassCBs.GetConstBufferAddress(m_iCurrentFrameIndex));
  m_Model.Render(m_pd3dCommandList, m_pModelDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), 1, INVALID_SAMPLER_SLOT, INVALID_SAMPLER_SLOT);

  EndRenderFrame();

  V(m_pd3dCommandList->Close());
  m_pd3dCommandQueue->ExecuteCommandLists(1, CommandListCast(&m_pd3dCommandList));

  m_pSyncFence->Signal(m_pd3dCommandQueue, &pFrameResource->FencePoint);

  Present();
}

LRESULT LoadModelSample::OnMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool *pbNoFurtherProcessing) {

  LRESULT ret = 0;

  m_Camera.HandleMessages(hwnd, msg, wp, lp);

  return ret;
}
