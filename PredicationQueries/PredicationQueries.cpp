#include <D3D12RendererContext.hpp>
#include <Win32Application.hpp>
#include <DirectXMath.h>
#include <wrl/client.h>
#include <SDKmesh.h>
#include <d3dx12.h>
#include <RootSignatureGenerator.h>
#include <ResourceUploadBatch.hpp>
#include <UploadBuffer.h>
#include <Camera.h>
#include <imgui/imgui.h>
#include <imgui/backends/imgui_impl_win32.h>
#include <imgui/backends/imgui_impl_dx12.h>
#include <ShlObj.h>
#include <Shlwapi.h>

using namespace DirectX;
using namespace Microsoft::WRL;

void CreatePredicationQueriesAppInstance(D3D12RendererContext **ppRenderer, WindowInteractor **ppInteractor);

int main() {

  int ret;
  D3D12RendererContext *pRenderer;
  WindowInteractor *pInteractor;

  CreatePredicationQueriesAppInstance(&pRenderer, &pInteractor);
  ret = RunSample(pRenderer, pInteractor, 800, 600, L"PredicationQueries");
  SAFE_DELETE(pRenderer);

  return ret;
}

class FrameResources {
public:
  HRESULT Create(ID3D12Device *pDevice, UINT perFrameResourcesSizeInBytes, UINT count, UINT frameCount) {

    HRESULT hr;
    if (frameCount > 4)
      V_RETURN2(L"Can not create frame resources of count bigger than 4", E_INVALIDARG);

    V_RETURN(m_PerframeConstBuffer.CreateBuffer(pDevice, count, perFrameResourcesSizeInBytes, true));

    for (UINT i = 0; i < frameCount; ++i) {
      V_RETURN(pDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_aCommandAllocator[i])));
      m_uFencePoint[i] = 0;
    }

    return hr;
  }

  UploadBuffer *GetBuffer() { return &m_PerframeConstBuffer; }

  UINT64 &GetFencePoint(int frameIndex) {
    _ASSERT(frameIndex >= 0 && frameIndex < 4);
    if (frameIndex >= 0 && frameIndex < 4)
      return m_uFencePoint[frameIndex];
    else
      return m_uFencePoint[0];
  }

  ID3D12CommandAllocator *GetCommandAllocator(int frameIndex) const {

    _ASSERT(frameIndex >= 0 && frameIndex < 4);
    if (frameIndex >= 0 && frameIndex < 4)
      return m_aCommandAllocator[frameIndex].Get();
    else
      return m_aCommandAllocator[0].Get();
  }

private:
  UploadBuffer m_PerframeConstBuffer;
  ComPtr<ID3D12CommandAllocator> m_aCommandAllocator[4];
  UINT64 m_uFencePoint[4];
};

class PredicationQueriesRenderer : public D3D12RendererContext, public WindowInteractor {

public:
  PredicationQueriesRenderer();

private:
  virtual HRESULT OnInitPipelines() override;
  virtual void OnFrameMoved(float fTime, float fElapsedTime) override;
  virtual void OnRenderFrame(float fTime, float fElapsedTime) override;
  virtual void OnResizeFrame(int cx, int cy) override;
  virtual void OnDestroy() override;
  virtual LRESULT OnMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) override;

  HRESULT CreatePSOs();
  HRESULT LoadModels();
  HRESULT CreateOccludeQueryResources();

  // GUI staff
  HRESULT ImGui_Initialize();
  void ImGui_Destroy();
  void ImGui_ResizeFrame(int cx, int cy);
  void ImGui_FrameMoved();
  void ImGui_RenderFrame();
  LRESULT ImGui_HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

  ComPtr<ID3D12PipelineState> m_pRenderTexturedPSO;
  ComPtr<ID3D12PipelineState> m_pRenderOnTopPSO;
  ComPtr<ID3D12PipelineState> m_pRenderOccluderPSO;

  ComPtr<ID3D12RootSignature> m_pRootSignature;

  CDXUTSDKMesh m_CityMesh;
  int m_CityMeshDescriptorStartPos;
  CDXUTSDKMesh m_OccluderMesh;
  int m_OccluderMeshDescriptorStartPos;
  CDXUTSDKMesh m_HeavyMesh;
  int m_HeavyMeshDescriptorStartPos;
  CDXUTSDKMesh m_ColumnMesh;
  int m_ColumnMeshDescriptorStartPos;

  ComPtr<ID3D12QueryHeap> m_pQueryHeap;
  ComPtr<ID3D12Resource> m_pQueryResults;

  enum { NUM_MICROSCOPE_INSTANCES = 6 };

  ComPtr<ID3D12DescriptorHeap> m_pModelDescriptorHeap;

  std::future<HRESULT> m_ModelLoadWaitable;

  int m_iCurrentFrameIndex;

  struct PerFrameConstBuffer {
    XMFLOAT4X4 WorldViewProj;
  };

  FrameResources m_FrameResources;

  CModelViewerCamera m_Camera;

  struct UserControlVars {
    enum OccludePredicationOpt {
      OCCLUDE_PREDICATION_NONE,
      OCCLUDE_PREDICATION_OPT_BINARY,
      OCCLUDE_PREDICATION_OPT_OCCLUDE
    } m_PredicationOpt;
    bool m_bRenderOccluders;
  } m_UserControlVars;

  ComPtr<ID3D12DescriptorHeap> m_pImGuiSrvHeap;
};

void CreatePredicationQueriesAppInstance(D3D12RendererContext **ppRenderer, WindowInteractor **ppInteractor) {

  auto pInstance = new PredicationQueriesRenderer;
  *ppRenderer = pInstance;
  *ppInteractor = pInstance;
}

PredicationQueriesRenderer::PredicationQueriesRenderer() {
  this->m_aDeviceConfig.SwapChainBackBufferFormatSRGB = TRUE;
  m_UserControlVars.m_PredicationOpt = UserControlVars::OCCLUDE_PREDICATION_OPT_BINARY;
  m_UserControlVars.m_bRenderOccluders = false;
}

HRESULT PredicationQueriesRenderer::CreatePSOs() {

  HRESULT hr;
  ComPtr<ID3DBlob> VSMainBuffer, PSMainBuffer, PSOccluderBuffer;
  ComPtr<ID3DBlob> ErrorBuffer;
#if defined(_DEBUG)
  UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
  UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
#endif

  ErrorBuffer = nullptr;
  V_RETURN(d3dUtils::CompileShaderFromFile(L"Shaders/DrawPredicated.hlsl", nullptr, nullptr, "VSSceneMain", "vs_5_0", 0,
                                           0, &VSMainBuffer, &ErrorBuffer));
  if (ErrorBuffer) {
    DX_TRACEA("Failed to create vertex shader: %s", ErrorBuffer->GetBufferPointer());
    return E_FAIL;
  }

  ErrorBuffer = nullptr;
  V_RETURN(d3dUtils::CompileShaderFromFile(L"Shaders/DrawPredicated.hlsl", nullptr, nullptr, "PSSceneMain", "ps_5_0", 0,
                                           0, &PSMainBuffer, &ErrorBuffer));
  if (ErrorBuffer) {
    DX_TRACEA("Failed to create pixel shader: %s", ErrorBuffer->GetBufferPointer());
    return E_FAIL;
  }

  ErrorBuffer = nullptr;
  V_RETURN(d3dUtils::CompileShaderFromFile(L"Shaders/DrawPredicated.hlsl", nullptr, nullptr, "PSOccluder", "ps_5_0",
                                           compileFlags, 0, &PSOccluderBuffer, &ErrorBuffer));
  if (ErrorBuffer) {
    DX_TRACEA("Failed to create occluders' pixel shader: %s", ErrorBuffer->GetBufferPointer());
    return E_FAIL;
  }

  D3D12_INPUT_ELEMENT_DESC vertexDescs[] = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

  RootSignatureGenerator rootSignatureGen;
  rootSignatureGen.AddConstBufferView(0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
  rootSignatureGen.AddDescriptorTable(
      {
          CD3DX12_DESCRIPTOR_RANGE1{D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0},
      },
      D3D12_SHADER_VISIBILITY_PIXEL);
  rootSignatureGen.AddStaticSamples({CD3DX12_STATIC_SAMPLER_DESC{
      0, D3D12_FILTER_MIN_MAG_MIP_LINEAR, D3D12_TEXTURE_ADDRESS_MODE_WRAP, D3D12_TEXTURE_ADDRESS_MODE_WRAP,
      D3D12_TEXTURE_ADDRESS_MODE_WRAP, 0.0f, 16, D3D12_COMPARISON_FUNC_LESS_EQUAL,
      D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE, 0.0f, FLT_MAX, D3D12_SHADER_VISIBILITY_PIXEL}});
  rootSignatureGen.Generate(m_pd3dDevice, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
                            &m_pRootSignature);

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.InputLayout = {vertexDescs, _countof(vertexDescs)};
  psoDesc.pRootSignature = m_pRootSignature.Get();
  psoDesc.VS = CD3DX12_SHADER_BYTECODE(VSMainBuffer.Get());
  psoDesc.PS = CD3DX12_SHADER_BYTECODE(PSMainBuffer.Get());
  psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = m_BackBufferFormat;
  psoDesc.DSVFormat = m_DepthStencilBufferFormat;
  psoDesc.SampleDesc = GetMsaaSampleDesc();
  psoDesc.SampleMask = UINT_MAX;
  V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pRenderTexturedPSO)));

  psoDesc.PS = CD3DX12_SHADER_BYTECODE(PSOccluderBuffer.Get());
  psoDesc.DepthStencilState.DepthEnable = TRUE;
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
  psoDesc.BlendState.IndependentBlendEnable = TRUE;
  psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
  psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
  psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
  psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
  psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
  psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
  psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
  psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
  V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pRenderOnTopPSO)));

  psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
  psoDesc.DepthStencilState.DepthEnable = TRUE;
  psoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
  psoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
  psoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
  psoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ZERO;
  psoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ONE;
  psoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
  psoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
  psoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ONE;
  psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = 0x0F;

  V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pRenderOccluderPSO)));

  return hr;
}

HRESULT PredicationQueriesRenderer::LoadModels() {
  HRESULT hr;

  ResourceUploadBatch uploadBatch(m_pd3dDevice, &m_MemAllocator);
  ComPtr<ID3D12DescriptorHeap> pSrcDescriptorHeaps[4];

  V_RETURN(uploadBatch.Begin());

  V_RETURN(m_CityMesh.Create(&uploadBatch, L"Media/MicroscopeCity/occcity.sdkmesh"));
  m_CityMesh.GetResourceDescriptorHeap(m_pd3dDevice, FALSE, pSrcDescriptorHeaps[0].GetAddressOf());
  V_RETURN(m_HeavyMesh.Create(&uploadBatch, L"Media/MicroscopeCity/scanner.sdkmesh"));
  m_HeavyMesh.GetResourceDescriptorHeap(m_pd3dDevice, FALSE, pSrcDescriptorHeaps[1].GetAddressOf());
  V_RETURN(m_ColumnMesh.Create(&uploadBatch, L"Media/MicroscopeCity/column.sdkmesh"));
  m_ColumnMesh.GetResourceDescriptorHeap(m_pd3dDevice, FALSE, pSrcDescriptorHeaps[2].GetAddressOf());
  V_RETURN(m_OccluderMesh.Create(&uploadBatch, L"Media/MicroscopeCity/occluder.sdkmesh"));
  m_OccluderMesh.GetResourceDescriptorHeap(m_pd3dDevice, FALSE, pSrcDescriptorHeaps[3].GetAddressOf());

  V_RETURN(uploadBatch.End(m_pd3dCommandQueue, &m_ModelLoadWaitable));

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc;
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc2 = {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 0,
                                          D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};

  D3D12_CPU_DESCRIPTOR_HANDLE srcHandles[4];
  UINT srcRanges[4];
  D3D12_CPU_DESCRIPTOR_HANDLE destHandles[1];
  UINT destRanges[1];
  int i = 0, iPos = 0;
  int *aDestStartPostions[4] = {&m_CityMeshDescriptorStartPos, &m_HeavyMeshDescriptorStartPos,
                                &m_ColumnMeshDescriptorStartPos, &m_OccluderMeshDescriptorStartPos};

  for (auto &pSrcDescriptorHeap : pSrcDescriptorHeaps) {
    heapDesc = pSrcDescriptorHeap->GetDesc();

    if (heapDesc.NumDescriptors) {
      srcHandles[i] = pSrcDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
      srcRanges[i] = heapDesc.NumDescriptors;

      *aDestStartPostions[iPos] = heapDesc2.NumDescriptors;
      heapDesc2.NumDescriptors += srcRanges[i];
      ++i;
    } else
      aDestStartPostions[iPos] = 0;
    ++iPos;
  }

  V_RETURN(m_pd3dDevice->CreateDescriptorHeap(&heapDesc2, IID_PPV_ARGS(&m_pModelDescriptorHeap)));

  destHandles[0] = m_pModelDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
  destRanges[0] = heapDesc2.NumDescriptors;

  m_pd3dDevice->CopyDescriptors(1, destHandles, destRanges, i, srcHandles, srcRanges,
                                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  return hr;
}

HRESULT PredicationQueriesRenderer::CreateOccludeQueryResources() {
  HRESULT hr;
  D3D12_QUERY_HEAP_DESC heapDesc = {};

  heapDesc.Count = (NUM_MICROSCOPE_INSTANCES * s_iSwapChainBufferCount);
  heapDesc.NodeMask = 0;
  heapDesc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;

  V_RETURN(m_pd3dDevice->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&m_pQueryHeap)));

  D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(heapDesc.Count * 8);

  V_RETURN(m_pd3dDevice->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                                 D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_PREDICATION,
                                                 nullptr, IID_PPV_ARGS(&m_pQueryResults)));

  return hr;
}

HRESULT PredicationQueriesRenderer::OnInitPipelines() {
  HRESULT hr;

  V_RETURN(m_pd3dCommandList->Reset(m_pd3dDirectCmdAlloc, nullptr));

  V_RETURN(CreatePSOs());
  V_RETURN(LoadModels());
  V_RETURN(CreateOccludeQueryResources());

  V_RETURN(ImGui_Initialize());

  V_RETURN(m_FrameResources.Create(m_pd3dDevice, sizeof(PerFrameConstBuffer),
                                   s_iSwapChainBufferCount * (1 + 3 * NUM_MICROSCOPE_INSTANCES),
                                   s_iSwapChainBufferCount));

  V_RETURN(m_pd3dCommandList->Close());
  V(m_ModelLoadWaitable.get());
  m_pd3dCommandQueue->ExecuteCommandLists(1, CommandListCast(&m_pd3dCommandList));

  FlushCommandQueue();

  m_Camera.SetViewParams({0.0f, 0.8f, -2.3f}, g_XMZero);
  m_Camera.SetButtonMasks(MOUSE_LEFT_BUTTON, MOUSE_WHEEL, MOUSE_MIDDLE_BUTTON);

  m_iCurrentFrameIndex = 0;

  return hr;
}

void PredicationQueriesRenderer::OnFrameMoved(float fTime, float fElapsedTime) {

  HRESULT hr;

  m_Camera.FrameMove(fElapsedTime, this);

  m_iCurrentFrameIndex = (m_iCurrentFrameIndex + 1) % s_iSwapChainBufferCount;

  V(m_pSyncFence->WaitForSyncPoint(m_FrameResources.GetFencePoint(m_iCurrentFrameIndex)));

  ImGui_FrameMoved();
}
void PredicationQueriesRenderer::OnRenderFrame(float fTime, float fElapsedTime) {

  HRESULT hr;
  auto cbStartIndex = m_iCurrentFrameIndex * (1 + 3 * NUM_MICROSCOPE_INSTANCES);
  auto pCommandAllocator = m_FrameResources.GetCommandAllocator(m_iCurrentFrameIndex);
  auto perframeCbv = m_FrameResources.GetBuffer()->GetConstBufferAddress(m_iCurrentFrameIndex);
  auto pCommandList = m_pd3dCommandList;
  D3D12_GPU_VIRTUAL_ADDRESS cbAddress;

  V(pCommandAllocator->Reset());
  V(pCommandList->Reset(pCommandAllocator, m_pRenderTexturedPSO.Get()));

  PrepareNextFrame();

  float clearColor[] = {0.9569f, 0.9569f, 1.0f, 0.0f};

  pCommandList->ClearRenderTargetView(CurrentBackBufferView(), clearColor, 1, &m_ScissorRect);
  pCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH, 1.f, 0, 1, &m_ScissorRect);

  pCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), FALSE, &DepthStencilView());
  pCommandList->RSSetViewports(1, &m_ScreenViewport);
  pCommandList->RSSetScissorRects(1, &m_ScissorRect);

  pCommandList->SetGraphicsRootSignature(m_pRootSignature.Get());
  pCommandList->SetDescriptorHeaps(1, m_pModelDescriptorHeap.GetAddressOf());

  auto W = m_Camera.GetWorldMatrix();
  auto V = m_Camera.GetViewMatrix();
  auto P = m_Camera.GetProjMatrix();
  PerFrameConstBuffer constBuffer;

  XMStoreFloat4x4(&constBuffer.WorldViewProj, XMMatrixTranspose(W * V * P));
  m_FrameResources.GetBuffer()->CopyData(&constBuffer, sizeof(constBuffer), cbStartIndex);
  cbAddress = m_FrameResources.GetBuffer()->GetConstBufferAddress(cbStartIndex++);
  pCommandList->SetGraphicsRootConstantBufferView(0, cbAddress);

  m_CityMesh.Render(pCommandList,
                    CD3DX12_GPU_DESCRIPTOR_HANDLE(m_pModelDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
                                                  m_CityMeshDescriptorStartPos, m_uCbvSrvUavDescriptorSize),
                    1);
  m_ColumnMesh.Render(pCommandList,
                      CD3DX12_GPU_DESCRIPTOR_HANDLE(m_pModelDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
                                                    m_ColumnMeshDescriptorStartPos, m_uCbvSrvUavDescriptorSize),
                      1);

  UINT occluderByteSize =
      m_UserControlVars.m_PredicationOpt == UserControlVars::OCCLUDE_PREDICATION_OPT_BINARY ? 8 : 8;
  D3D12_QUERY_TYPE queryType = m_UserControlVars.m_PredicationOpt == UserControlVars::OCCLUDE_PREDICATION_OPT_BINARY ?
    D3D12_QUERY_TYPE_BINARY_OCCLUSION : D3D12_QUERY_TYPE_OCCLUSION;

  for (int i = 0; i < NUM_MICROSCOPE_INSTANCES; ++i) {

    auto W1 = XMMatrixRotationY(i * XM_2PI / NUM_MICROSCOPE_INSTANCES);
    XMStoreFloat4x4(&constBuffer.WorldViewProj, XMMatrixTranspose(W1 * W * V * P));

    m_FrameResources.GetBuffer()->CopyData(&constBuffer, sizeof(constBuffer), cbStartIndex);
    cbAddress = m_FrameResources.GetBuffer()->GetConstBufferAddress(cbStartIndex++);
    pCommandList->SetGraphicsRootConstantBufferView(0, cbAddress);

    if(m_UserControlVars.m_PredicationOpt != UserControlVars::OCCLUDE_PREDICATION_NONE)
      pCommandList->SetPredication(m_pQueryResults.Get(),
                                   (m_iCurrentFrameIndex * NUM_MICROSCOPE_INSTANCES + i) * occluderByteSize,
                                   D3D12_PREDICATION_OP_EQUAL_ZERO);

    m_HeavyMesh.Render(pCommandList,
                       CD3DX12_GPU_DESCRIPTOR_HANDLE(m_pModelDescriptorHeap->GetGPUDescriptorHandleForHeapStart(),
                                                     m_HeavyMeshDescriptorStartPos, m_uCbvSrvUavDescriptorSize),
                       1);
    if (m_UserControlVars.m_PredicationOpt != UserControlVars::OCCLUDE_PREDICATION_NONE)
      pCommandList->SetPredication(nullptr, 0, D3D12_PREDICATION_OP_EQUAL_ZERO);
  }

  pCommandList->SetPipelineState(m_pRenderOccluderPSO.Get());

  for (int i = 0; i < NUM_MICROSCOPE_INSTANCES; ++i) {
    auto W1 = XMMatrixRotationY(i * XM_2PI / NUM_MICROSCOPE_INSTANCES);
    XMStoreFloat4x4(&constBuffer.WorldViewProj, XMMatrixTranspose(W1 * W * V * P));

    m_FrameResources.GetBuffer()->CopyData(&constBuffer, sizeof(constBuffer), cbStartIndex);
    cbAddress = m_FrameResources.GetBuffer()->GetConstBufferAddress(cbStartIndex++);
    pCommandList->SetGraphicsRootConstantBufferView(0, cbAddress);

    if(m_UserControlVars.m_PredicationOpt != UserControlVars::OCCLUDE_PREDICATION_NONE)
      pCommandList->BeginQuery(m_pQueryHeap.Get(), queryType,
                               m_iCurrentFrameIndex * NUM_MICROSCOPE_INSTANCES + i);

    m_OccluderMesh.Render(pCommandList, CD3DX12_GPU_DESCRIPTOR_HANDLE());

    if (m_UserControlVars.m_PredicationOpt != UserControlVars::OCCLUDE_PREDICATION_NONE)
      pCommandList->EndQuery(m_pQueryHeap.Get(), queryType, m_iCurrentFrameIndex * NUM_MICROSCOPE_INSTANCES + i);
  }

  if (m_UserControlVars.m_PredicationOpt != UserControlVars::OCCLUDE_PREDICATION_NONE) {
    pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_pQueryResults.Get(),
                                                                           D3D12_RESOURCE_STATE_PREDICATION,
                                                                           D3D12_RESOURCE_STATE_COPY_DEST));
    pCommandList->ResolveQueryData(m_pQueryHeap.Get(), queryType, m_iCurrentFrameIndex * NUM_MICROSCOPE_INSTANCES,
                                   NUM_MICROSCOPE_INSTANCES, m_pQueryResults.Get(),
                                   (m_iCurrentFrameIndex * NUM_MICROSCOPE_INSTANCES) * occluderByteSize);
    pCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_pQueryResults.Get(),
                                                                           D3D12_RESOURCE_STATE_COPY_DEST,
                                                                           D3D12_RESOURCE_STATE_PREDICATION));
  }

  if(m_UserControlVars.m_bRenderOccluders) {
    pCommandList->SetPipelineState(m_pRenderOnTopPSO.Get());

    for (int i = 0; i < NUM_MICROSCOPE_INSTANCES; ++i) {
      auto W1 = XMMatrixRotationY(i * XM_2PI / NUM_MICROSCOPE_INSTANCES);
      XMStoreFloat4x4(&constBuffer.WorldViewProj, XMMatrixTranspose(W1 * W * V * P));

      m_FrameResources.GetBuffer()->CopyData(&constBuffer, sizeof(constBuffer), cbStartIndex);
      cbAddress = m_FrameResources.GetBuffer()->GetConstBufferAddress(cbStartIndex++);
      pCommandList->SetGraphicsRootConstantBufferView(0, cbAddress);

      m_OccluderMesh.Render(pCommandList, CD3DX12_GPU_DESCRIPTOR_HANDLE());
    }
  }

  ImGui_RenderFrame();

  EndRenderFrame(pCommandList);

  V(pCommandList->Close());
  m_pd3dCommandQueue->ExecuteCommandLists(1, CommandListCast(&pCommandList));

  m_pSyncFence->Signal(m_pd3dCommandQueue, &m_FrameResources.GetFencePoint(m_iCurrentFrameIndex));

  Present();
}
void PredicationQueriesRenderer::OnResizeFrame(int cx, int cy) {

  m_Camera.SetProjParams(XM_PIDIV4, GetAspectRatio(), 0.05f, 5000.0f);
  m_Camera.SetWindow(cx, cy);

  ImGui_ResizeFrame(cx, cy);
}

void PredicationQueriesRenderer::OnDestroy() {
  ImGui_Destroy();
}

LRESULT PredicationQueriesRenderer::OnMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {

  m_Camera.HandleMessages(hwnd, msg, wp, lp);
  return ImGui_HandleMessage(hwnd, msg, wp, lp);
}

HRESULT  PredicationQueriesRenderer::ImGui_Initialize() {
  HRESULT hr;
  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();

  auto io = ImGui::GetIO();
  (void)io;
  // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
  // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

  // Setup Dear ImGui style
  // ImGui::StyleColorsDark();
  ImGui::StyleColorsClassic();

  D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
  heapDesc.NumDescriptors = 1;
  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  V_RETURN(m_pd3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_pImGuiSrvHeap)));

  // Setup Platform/Renderer backends
  ImGui_ImplWin32_Init(this->GetHwnd());
  ImGui_ImplDX12_Init(m_pd3dDevice, s_iSwapChainBufferCount, m_BackBufferFormat, m_pImGuiSrvHeap.Get(),
                      m_pImGuiSrvHeap->GetCPUDescriptorHandleForHeapStart(),
                      m_pImGuiSrvHeap->GetGPUDescriptorHandleForHeapStart());

  // Load Fonts
  CHAR szArialFilePath[MAX_PATH];
  if (SHGetSpecialFolderPathA(nullptr, szArialFilePath, CSIDL_FONTS, FALSE)) {
    strcat_s(szArialFilePath, "\\Arial.ttf");
    io.Fonts->AddFontFromFileTTF(szArialFilePath, 16);
  }
  return hr;
}

void PredicationQueriesRenderer::ImGui_Destroy() {
  ImGui_ImplDX12_Shutdown();
  ImGui_ImplWin32_Shutdown();
}

void PredicationQueriesRenderer::ImGui_ResizeFrame(int cx, int cy) {
  ImGui_ImplDX12_InvalidateDeviceObjects();
  ImGui_ImplDX12_CreateDeviceObjects();
}

void PredicationQueriesRenderer::ImGui_FrameMoved() {
  ImGui_ImplDX12_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  ImGui::Begin("Render opts", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
  ImGui::BeginGroup();
  ImGui::RadioButton("No use of predication", (int *)&m_UserControlVars.m_PredicationOpt,
                     UserControlVars::OCCLUDE_PREDICATION_NONE);
  ImGui::RadioButton("Use Occlude Predication", (int *)&m_UserControlVars.m_PredicationOpt,
                     UserControlVars::OCCLUDE_PREDICATION_OPT_OCCLUDE);
  ImGui::RadioButton("Use Binary Occlude Predication", (int *)&m_UserControlVars.m_PredicationOpt,
                     UserControlVars::OCCLUDE_PREDICATION_OPT_BINARY);
  ImGui::EndGroup();
  ImGui::Separator();
  ImGui::Checkbox("Render Occluders", &m_UserControlVars.m_bRenderOccluders);
  ImGui::End();
}

void PredicationQueriesRenderer::ImGui_RenderFrame() {
  ImGui::Render();
  m_pd3dCommandList->SetDescriptorHeaps(1, m_pImGuiSrvHeap.GetAddressOf());
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), m_pd3dCommandList);
}

LRESULT PredicationQueriesRenderer::ImGui_HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  // Forward declare message handler from imgui_impl_win32.cpp
  extern LRESULT CALLBACK ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

  LRESULT ret = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
  return ret;
}
