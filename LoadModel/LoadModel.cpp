#include "Model.hpp"
#include <Camera.h>
#include <D3D12RendererContext.hpp>
#include <DirectXColors.h>
#include <RootSignatureGenerator.h>
#include <UIController.hpp>
#include <UploadBuffer.h>
#include <Win32Application.hpp>

using Microsoft::WRL::ComPtr;
using namespace DirectX;

HRESULT CreateRendererAndUIInstance(D3D12RendererContext **ppRenderer, IUIController **ppUIController);

int main() {

  HRESULT hr;
  int ret;
  D3D12RendererContext *pRenderer;
  IUIController *pUIController;

  V_RETURN(CreateRendererAndUIInstance(&pRenderer, &pUIController));
  ret = RunSample(pRenderer, pUIController, 800, 600, L"Load Model for D3D12");

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

class LoadModelSample : public D3D12RendererContext, public IUIController {
public:
  LoadModelSample();

  HRESULT CheckDeviceFeatureSupport(ID3D12Device5 *pDevice) override;
  HRESULT OnInitPipelines() override;

  void OnResizeFrame(int cx, int cy) override;
  void OnFrameMoved(float fTime, float fElapsedTime) override;
  void OnRenderFrame(float fTime, float fElapedTime) override;

private:
  void OnKeyEvent(int downUp, unsigned short key, int repeatCnt) override;
  void OnMouseButtonEvent(UI_MOUSE_BUTTON_EVENT ev, UI_MOUSE_VIRTUAL_KEY keys, int x, int y) override;
  void OnMouseMove(UI_MOUSE_VIRTUAL_KEY keys, int x, int y) override;

  HRESULT LoadModel();
  HRESULT CreatePSOs();
  HRESULT CreateDescriptorHeap();

  Model m_Model;
  std::vector<ComPtr<ID3D12PipelineState>> m_aPSOs;
  ComPtr<ID3D12RootSignature> m_pRootSignature;
  ComPtr<ID3D12DescriptorHeap> m_pModelDescriptorHeap;
  FrameResources m_aFrameResources[3];
  int m_iCurrentFrameIndex = 0;
  Camera m_Camera;

  XMINT2 m_ptLastMousePos;
};

HRESULT CreateRendererAndUIInstance(D3D12RendererContext **ppRenderer, IUIController **ppUIController) {
  LoadModelSample *pInstance = new LoadModelSample();
  *ppRenderer = pInstance;
  *ppUIController = pInstance;
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
  V_RETURN(CreateDescriptorHeap());

  for (auto &frameRes : m_aFrameResources) {
    frameRes.CreateCommandAllocator(m_pd3dDevice);
  }
  FrameResources::CreateBuffers(m_pd3dDevice, 3);

  m_Camera.SetViewParams(XMFLOAT3(100.0f, 5.0f, 5.0f), XMFLOAT3(.0f, .0f, .0f));

  return hr;
}

HRESULT LoadModelSample::CreatePSOs() {

  HRESULT hr;
  ComPtr<ID3DBlob> pVSBuffer, pPSBuffer;
  ComPtr<ID3DBlob> pErrorBuffer;

  V_RETURN(d3dUtils::CompileShaderFromFile(L"Shaders/basic.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", 0, 0,
                                           &pVSBuffer, &pErrorBuffer));
  if (pErrorBuffer) {
    DX_TRACE(L"Compile VS error: %S\n", pErrorBuffer->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  V_RETURN(d3dUtils::CompileShaderFromFile(L"Shaders/basic.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", 0, 0,
                                           &pPSBuffer, &pErrorBuffer));
  if (pErrorBuffer) {
    DX_TRACE(L"Compile VS error: %S\n", pErrorBuffer->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  RootSignatureGenerator rsGen;
  rsGen.AddConstBufferView(0);
  rsGen.AddDescriptorTable({RootSignatureGenerator::ComposeDesciptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0)},
                           D3D12_SHADER_VISIBILITY_PIXEL);
  rsGen.AddStaticSamples({RootSignatureGenerator::ComposeStaticSampler(0, D3D12_FILTER_MIN_MAG_MIP_LINEAR)});

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

  m_aPSOs.reserve(m_Model.inputLayouts.size());

  D3D12_INPUT_ELEMENT_DESC inputLayout[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
  };

  ComPtr<ID3D12PipelineState> pso;

  psoDesc.InputLayout = {inputLayout, std::size(inputLayout)};

  V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(pso.ReleaseAndGetAddressOf())));
  m_aPSOs.push_back(std::move(pso));

  return hr;
}

HRESULT LoadModelSample::CreateDescriptorHeap() {

  HRESULT hr;
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  heapDesc.NumDescriptors = m_Model.texturesCache.size();

  V_RETURN(m_pd3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_pModelDescriptorHeap)));

  CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle;
  cpuHandle = m_pModelDescriptorHeap->GetCPUDescriptorHandleForHeapStart();

  for (auto &texCache : m_Model.texturesCache) {

    cpuHandle = m_pModelDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    cpuHandle.Offset(texCache.descriptorIndex, m_uCbvSrvUavDescriptorSize);

    if (texCache.defaultBuffer) {
      m_pd3dDevice->CreateShaderResourceView(texCache.defaultBuffer.Get(), nullptr, cpuHandle);
    }
  }

  return hr;
}

HRESULT LoadModelSample::LoadModel() {
  HRESULT hr;

  ResourceUploadBatch uploadBatch(m_pd3dDevice, &m_MemAllocator);

  uploadBatch.Begin(D3D12_COMMAND_LIST_TYPE_DIRECT);

  V_RETURN(m_Model.CreateFromSDKMESH(
      &uploadBatch, LR"(D:\repos\directx-sdk-samples\Media\powerplant\\powerplant.sdkmesh)", ModelLoader_Default));

  std::future<HRESULT> batchWaitable;

  V_RETURN(uploadBatch.End(m_pd3dCommandQueue, &batchWaitable));

  V_RETURN(batchWaitable.get());

  return hr;
}

void LoadModelSample::OnResizeFrame(int cx, int cy) {
  m_Camera.SetProjMatrix(XM_PIDIV4, GetAspectRatio(), 1.0f, 10000.f);
}

void LoadModelSample::OnFrameMoved(float fTime, float fElapsedTime) {

  m_iCurrentFrameIndex = (m_iCurrentFrameIndex + 1) % 3;
  auto pFrameResource = &m_aFrameResources[m_iCurrentFrameIndex];

  m_pSyncFence->WaitForSyncPoint(pFrameResource->FencePoint);

  XMMATRIX VP = m_Camera.GetViewProj();
  PassConstants pc;
  XMStoreFloat4x4(&pc.ViewProj, XMMatrixTranspose(VP));
  pc.EyePosW = m_Camera.GetEyePosW();
  FrameResources::PassCBs.CopyData(&pc, sizeof(pc), m_iCurrentFrameIndex);
}

void LoadModelSample::OnRenderFrame(float fTime, float fElapsedTime) {

  HRESULT hr;
  auto pFrameResource = &m_aFrameResources[m_iCurrentFrameIndex];

  V(pFrameResource->CommandAllocator->Reset());
  V(m_pd3dCommandList->Reset(pFrameResource->CommandAllocator.Get(), nullptr));

  m_pd3dCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
                                                                              D3D12_RESOURCE_STATE_PRESENT,
                                                                              D3D12_RESOURCE_STATE_RENDER_TARGET));

  m_pd3dCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::Black, 1, &m_ScissorRect);
  m_pd3dCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f,
                                           0, 0, nullptr);

  m_pd3dCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), TRUE, &DepthStencilView());
  m_pd3dCommandList->SetGraphicsRootSignature(m_pRootSignature.Get());

  m_pd3dCommandList->RSSetViewports(1, &m_ScreenViewport);
  m_pd3dCommandList->RSSetScissorRects(1, &m_ScissorRect);

  m_pd3dCommandList->SetPipelineState(m_aPSOs[0].Get());
  m_pd3dCommandList->SetDescriptorHeaps(1, m_pModelDescriptorHeap.GetAddressOf());

  for (auto &mesh : m_Model.meshes) {
    if (mesh->opaqueMeshParts.empty())
      continue;

    D3D12_VERTEX_BUFFER_VIEW vbv;
    D3D12_INDEX_BUFFER_VIEW ibv;

    for (auto &part : mesh->opaqueMeshParts) {

      vbv.BufferLocation = part->staticVertexBuffer->GetGPUVirtualAddress();
      vbv.SizeInBytes = part->vertexBufferSize;
      vbv.StrideInBytes = part->vertexStride;

      ibv.BufferLocation = part->staticIndexBuffer->GetGPUVirtualAddress();
      ibv.Format = part->indexFormat;
      ibv.SizeInBytes = part->indexBufferSize;

      m_pd3dCommandList->IASetPrimitiveTopology(part->primitiveType);
      m_pd3dCommandList->IASetVertexBuffers(0, 1, &vbv);
      m_pd3dCommandList->IASetIndexBuffer(&ibv);

      m_pd3dCommandList->SetGraphicsRootConstantBufferView(
          0, FrameResources::PassCBs.GetConstBufferAddress(m_iCurrentFrameIndex));

      auto &mat = m_Model.materials[part->materialIndex];
      CD3DX12_GPU_DESCRIPTOR_HANDLE diffHandle;
      diffHandle = m_pModelDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
      diffHandle.Offset(mat.diffuseTextureIndex, m_uCbvSrvUavDescriptorSize);

      m_pd3dCommandList->SetGraphicsRootDescriptorTable(1, diffHandle);

      m_pd3dCommandList->DrawIndexedInstanced(part->indexCount, 1, 0, 0, 0);
    }
  }

  m_pd3dCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
                                                                              D3D12_RESOURCE_STATE_RENDER_TARGET,
                                                                              D3D12_RESOURCE_STATE_PRESENT));

  V(m_pd3dCommandList->Close());
  m_pd3dCommandQueue->ExecuteCommandLists(1, CommandListCast(&m_pd3dCommandList));

  m_pSyncFence->Signal(m_pd3dCommandQueue, &pFrameResource->FencePoint);

  Present();
}

void LoadModelSample::OnKeyEvent(int downUp, unsigned short key, int repeatCnt) {
  float dt = 0.001f;
  switch (key) {
  case 'w':
  case 'W':
    m_Camera.Walk(10.0f * dt);
    break;
  case 'S':
  case 's':
    m_Camera.Walk(-10.0f * dt);
    break;
  case 'A':
  case 'a':
    m_Camera.Strafe(-10.0f * dt);
    break;
  case 'D':
  case 'd':
    m_Camera.Strafe(10.0f * dt);
    break;
  }

  m_Camera.UpdateViewMatrix();
}

void LoadModelSample::OnMouseButtonEvent(UI_MOUSE_BUTTON_EVENT ev, UI_MOUSE_VIRTUAL_KEY keys, int x, int y) {
  switch (ev) {
  case UI_WM_LBUTTONDOWN:
  case UI_WM_RBUTTONDOWN:
    m_ptLastMousePos.x = x;
    m_ptLastMousePos.y = y;
    BeginCaptureWindowInput();
    break;
  case WM_LBUTTONUP:
  case WM_RBUTTONUP:
    EndCaptureWindowInput();
    break;
  }
}

void LoadModelSample::OnMouseMove(UI_MOUSE_VIRTUAL_KEY keys, int x, int y) {
  // Make each pixel correspond to a quarter of a degree.
  if (keys & UI_MK_LBUTTON) {
    float dx = XMConvertToRadians(0.25f * static_cast<float>(x - m_ptLastMousePos.x));
    float dy = XMConvertToRadians(0.25f * static_cast<float>(y - m_ptLastMousePos.y));

    m_Camera.Pitch(dy);
    m_Camera.RotateY(dx);
    m_Camera.UpdateViewMatrix();
  }
  m_ptLastMousePos.x = x;
  m_ptLastMousePos.y = y;
}
