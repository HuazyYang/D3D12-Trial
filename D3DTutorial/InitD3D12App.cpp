#include <DirectXColors.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <wrl.h>
#include <array>
#include <D3D12RendererContext.hpp>
#include <Win32Application.hpp>
#include <UIController.hpp>
#include "UploadBuffer.h"
#include "MeshBuffer.h"
#include "RenderItem.h"
#include "Camera.h"
#include "Texture.h"
#include "GeometryGenerator.h"
#include <random>
#include "RootSignatureGenerator.h"

static HRESULT CreateCubeRendererContextAndUIController(D3D12RendererContext **ppRenderer,
                                                        IUIController **ppUIController);

int main() {

  int ret;
  D3D12RendererContext *pRenderer;
  IUIController *pUIController;

  CreateCubeRendererContextAndUIController(&pRenderer, &pUIController);
  ret = RunSample(pRenderer, pUIController, 800, 600, L"Cube");
  SAFE_DELETE(pRenderer);

  return ret;
}

using namespace DirectX;
using namespace DirectX::PackedVector;
using namespace Microsoft::WRL;

namespace CubeAppInternal {
struct ObjectConstants {
  XMMATRIX matWorldViewProj;
  XMMATRIX matTexTransform;
};
}; // namespace CubeAppInternal

using namespace CubeAppInternal;

class InitD3D12App : public D3D12RendererContext, public IUIController {
public:
  InitD3D12App();
  ~InitD3D12App();

private:
  HRESULT OnInitPipelines() override;
  void OnFrameMoved(float fTime, float fElapsedTime) override;
  void OnRenderFrame(float fTime, float fEplased) override;
  void OnResizeFrame(int cx, int cy) override;
  void OnMouseButtonEvent(UI_MOUSE_BUTTON_EVENT ev, UI_MOUSE_VIRTUAL_KEY keys, int x,
                          int y) override;
  void OnMouseMove(UI_MOUSE_VIRTUAL_KEY keys, int x, int y);
  void OnMouseWheel(UI_MOUSE_VIRTUAL_KEY keys, int delta, int x, int y) override;

  HRESULT BuildDescriptorHeaps();
  HRESULT BuildSamplerHeaps();
  HRESULT BuildBoxGeometry();
  HRESULT BuildPSO();
  HRESULT BuildTextures();
  VOID PostInitialize();

  ID3D12DescriptorHeap *m_CbvHeap = nullptr;
  ID3D12DescriptorHeap *m_pSamplerHeap = nullptr;

  Texture m_aTextures[2];

  UploadBuffer m_CbUploadBuffer;
  ID3D12RootSignature *m_pRootSignature = nullptr;

  RenderItem m_aRitem;

  ID3D12PipelineState *m_psoDefaultPSO = nullptr;

  Camera m_Camera;
  POINT m_ptLastMousePos;
};

HRESULT CreateCubeRendererContextAndUIController(D3D12RendererContext **ppRenderer,
                                                 IUIController **ppUIController) {
  InitD3D12App *pInstance = new InitD3D12App;
  *ppRenderer = pInstance;
  *ppUIController = pInstance;
  return S_OK;
}

InitD3D12App::InitD3D12App() {

  m_aDeviceConfig.MsaaEnabled = TRUE;
  m_aDeviceConfig.MsaaSampleCount = 8;
  m_aDeviceConfig.MsaaQaulityLevel = 4;

  memcpy(m_aRTVDefaultClearValue.Color, &DirectX::Colors::LightBlue,
         sizeof(m_aRTVDefaultClearValue.Color));

  m_Camera.SetOrbit(5.0f, 1.5f * XM_PI, 0.25f * XM_PI);
}

InitD3D12App::~InitD3D12App() {
  SAFE_RELEASE(m_CbvHeap);
  SAFE_RELEASE(m_pSamplerHeap);

  SAFE_RELEASE(m_pRootSignature);
  SAFE_RELEASE(m_psoDefaultPSO);
}

HRESULT InitD3D12App::OnInitPipelines() {

  HRESULT hr;

  /// Reset the command list to prepare for initalization commands.
  V_RETURN(m_pd3dCommandList->Reset(m_pd3dDirectCmdAlloc, nullptr));

  V_RETURN(BuildTextures());
  V_RETURN(BuildBoxGeometry());
  V_RETURN(BuildPSO());
  V_RETURN(BuildDescriptorHeaps());
  V_RETURN(BuildSamplerHeaps());

  // Execute the initialization commands.
  V_RETURN(m_pd3dCommandList->Close());
  ID3D12CommandList *cmdsLists[] = {m_pd3dCommandList};
  m_pd3dCommandQueue->ExecuteCommandLists(1, cmdsLists);

  FlushCommandQueue();

  PostInitialize();

  return hr;
}

VOID InitD3D12App::PostInitialize() {

  m_aRitem.pMeshBuffer->DisposeUploaders();
  for (auto &tex : m_aTextures) {
    tex.DisposeUploaders();
  }
}

HRESULT InitD3D12App::BuildTextures() {
  HRESULT hr;
  ComPtr<ID3D12Resource> pResource, pUploadHeap;

  V_RETURN(m_aTextures[0].CreateTextureFromDDSFile(m_pd3dDevice, m_pd3dCommandList,
                                                   LR"(Media/Textures/DX11/flare.dds)"));

  V_RETURN(m_aTextures[1].CreateTextureFromDDSFile(m_pd3dDevice, m_pd3dCommandList,
                                                   LR"(Media/Textures/DX11/flarealpha.dds)"));

  return hr;
}

HRESULT InitD3D12App::BuildDescriptorHeaps() {
  D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
  HRESULT hr;
  UINT uObjCBByteSize;
  CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle;
  D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
  D3D12_RESOURCE_DESC texDesc;

  cbvHeapDesc.NumDescriptors = 3;
  cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  cbvHeapDesc.NodeMask = 0;

  V_RETURN(m_pd3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&m_CbvHeap)));

  uObjCBByteSize = d3dUtils::CalcConstantBufferByteSize(sizeof(ObjectConstants));

  V_RETURN(m_CbUploadBuffer.CreateBuffer(m_pd3dDevice, 1, sizeof(ObjectConstants), TRUE));

  cpuHandle = CD3DX12_CPU_DESCRIPTOR_HANDLE(m_CbvHeap->GetCPUDescriptorHandleForHeapStart());

  cbvDesc.BufferLocation = m_CbUploadBuffer.GetConstBufferAddress();
  cbvDesc.SizeInBytes = uObjCBByteSize;

  m_pd3dDevice->CreateConstantBufferView(&cbvDesc, cpuHandle);

  cpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);

  texDesc = m_aTextures[0].Resource->GetDesc();

  ZeroMemory(&srvDesc, sizeof(srvDesc));
  srvDesc.Format = texDesc.Format;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.Texture2DArray.MostDetailedMip = 0;
  srvDesc.Texture2DArray.MipLevels = texDesc.MipLevels;
  srvDesc.Texture2DArray.FirstArraySlice = 0;
  srvDesc.Texture2DArray.ArraySize = texDesc.DepthOrArraySize;
  srvDesc.Texture2DArray.PlaneSlice = 0;
  srvDesc.Texture2DArray.ResourceMinLODClamp = 0.0f;

  m_pd3dDevice->CreateShaderResourceView(m_aTextures[0].Resource, &srvDesc, cpuHandle);

  cpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);

  m_pd3dDevice->CreateShaderResourceView(m_aTextures[1].Resource, &srvDesc, cpuHandle);

  return hr;
}

HRESULT InitD3D12App::BuildSamplerHeaps() {
  D3D12_DESCRIPTOR_HEAP_DESC heapDesc;
  HRESULT hr;
  D3D12_SAMPLER_DESC samDesc;
  CD3DX12_CPU_DESCRIPTOR_HANDLE samHeap;

  heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
  heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  heapDesc.NumDescriptors = 2;
  heapDesc.NodeMask = 0;

  V_RETURN(m_pd3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&m_pSamplerHeap)));

  ZeroMemory(&samDesc, sizeof(samDesc));
  samDesc.Filter = D3D12_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR;
  samDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  samDesc.MipLODBias = 0;
  samDesc.MaxAnisotropy = 1;
  samDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
  samDesc.MinLOD = 0.0f;
  samDesc.MaxLOD = D3D12_FLOAT32_MAX;

  samHeap = m_pSamplerHeap->GetCPUDescriptorHandleForHeapStart();

  m_pd3dDevice->CreateSampler(&samDesc, samHeap);

  samHeap.Offset(1, m_uCbvSrvUavDescriptorSize);

  samDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
  samDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;

  m_pd3dDevice->CreateSampler(&samDesc, samHeap);

  return hr;
}

HRESULT InitD3D12App::BuildBoxGeometry() {
  HRESULT hr;
  GeometryGenerator gen;
  GeometryGenerator::MeshData datas;
  MeshBuffer *pBoxMeshBuffer;

  datas = gen.CreateBox(1.0f, 1.0f, 1.0f, 0);

  UINT uVertexBufferSize = sizeof(GeometryGenerator::Vertex) * (UINT)datas.Vertices.size();
  UINT uIndexBufferSize = sizeof(uint16_t) * (UINT)datas.GetIndices16().size();

  V_RETURN(CreateMeshBuffer(&pBoxMeshBuffer));

  pBoxMeshBuffer->CreateVertexBuffer(m_pd3dDevice, m_pd3dCommandList, datas.Vertices.data(),
                                     datas.Vertices.size(), sizeof(GeometryGenerator::Vertex),
                                     nullptr);
  pBoxMeshBuffer->CreateIndexBuffer(m_pd3dDevice, m_pd3dCommandList, datas.GetIndices16().data(),
                                    datas.GetIndices16().size(), sizeof(UINT16));

  XMStoreFloat4x4(&m_aRitem.matWorld, XMMatrixScaling(2.0f, 2.0f, 2.0f));
  m_aRitem.uIndexCount = (UINT)datas.GetIndices16().size();
  m_aRitem.uStartIndexLocation = 0;
  m_aRitem.iBaseVertexLocation = 0;
  m_aRitem.pMeshBuffer = pBoxMeshBuffer;
  m_aRitem.pMeshBuffer->AddRef();

  SAFE_RELEASE(pBoxMeshBuffer);

  return hr;
}

HRESULT InitD3D12App::BuildPSO() {
  HRESULT hr;
  ComPtr<ID3DBlob> pErrorBlob;
  UINT compileFlags = 0;
  ComPtr<ID3DBlob> VSBuffer, PSBuffer;
  std::vector<D3D12_INPUT_ELEMENT_DESC> aInputLayout;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};

#if defined(_DEBUG)
  compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

  d3dUtils::DxcCompilerWrapper compilerWrapper;
  ComPtr<IDxcBlob> pFileBlob;

  V_RETURN(d3dUtils::CompileShaderFromFile(
      TEXT("Shaders/box.hlsl"), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "BoxVS", "vs_5_0",
      compileFlags, 0, VSBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
  if (pErrorBlob) {
    DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  pErrorBlob = nullptr;
  V_RETURN(d3dUtils::CompileShaderFromFile(
      TEXT("Shaders/box.hlsl"), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, "BoxPS", "ps_5_0",
      compileFlags, 0, PSBuffer.GetAddressOf(), pErrorBlob.GetAddressOf()));
  if (pErrorBlob) {
    DXOutputDebugStringA((const char *)pErrorBlob->GetBufferPointer());
    V_RETURN(E_FAIL);
  }

  /// Create the input layout.
  aInputLayout = {
      {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
       0},
      {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
       0},
  };

  /// Root signature.
  RootSignatureGenerator signatureGen;
  // signatureGen.AddDescriptorTable(
  //  { RootSignatureGenerator::ComposeConstBufferViewRange(1, 0)}
  //);
  // signatureGen.AddDescriptorTable(
  //  {RootSignatureGenerator::ComposeShaderResourceViewRange(2, 0)},
  //  D3D12_SHADER_VISIBILITY_PIXEL
  //);
  // signatureGen.AddDescriptorTable(
  //  { RootSignatureGenerator::ComposeSampler(2, 0)}
  //);

  // V_RETURN(signatureGen.Generate(m_pd3dDevice,
  // D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
  //  &m_pRootSignature));

  CHAR rootSignatureCode[] =
      R"(
      #define MYROOTSIG \
      "RootFlags( ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT ), " \
      "DescriptorTable( CBV(b0) )," \
      "DescriptorTable( SRV(t0, numDescriptors = 2), visibility=SHADER_VISIBILITY_PIXEL )," \
      "DescriptorTable( Sampler(s0, numDescriptors = 2), visibility=SHADER_VISIBILITY_PIXEL ) "
      )";
  V_RETURN(signatureGen.Generate(m_pd3dDevice, rootSignatureCode, _countof(rootSignatureCode) - 1,
                                 "MYROOTSIG", &m_pRootSignature));

  ZeroMemory(&psoDesc, sizeof(psoDesc));
  psoDesc.InputLayout = {aInputLayout.data(), (UINT)aInputLayout.size()};
  psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.pRootSignature = m_pRootSignature;
  psoDesc.VS = {(BYTE *)VSBuffer->GetBufferPointer(), VSBuffer->GetBufferSize()};
  psoDesc.PS = {(BYTE *)PSBuffer->GetBufferPointer(), PSBuffer->GetBufferSize()};
  psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  psoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
  psoDesc.SampleMask = UINT_MAX;
  psoDesc.SampleDesc = GetMsaaSampleDesc();
  psoDesc.NumRenderTargets = 1;
  psoDesc.RTVFormats[0] = m_BackBufferFormat;
  psoDesc.DSVFormat = m_DepthStencilBufferFormat;

  V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_psoDefaultPSO)));

  return hr;
}

void InitD3D12App::OnResizeFrame(int cx, int cy) {
  m_Camera.SetProjMatrix(0.25f * XM_PI, GetAspectRatio(), 1.0f, 1000.0f);
}

void InitD3D12App::OnMouseButtonEvent(UI_MOUSE_BUTTON_EVENT ev, UI_MOUSE_VIRTUAL_KEY keys, int x,
                                      int y) {
  switch (ev) {
  case UI_WM_LBUTTONDOWN:
    m_ptLastMousePos.x = x;
    m_ptLastMousePos.y = y;
    BeginCaptureWindowInput();
    break;
  case UI_WM_LBUTTONUP:
    EndCaptureWindowInput();
    break;
  }
}

void InitD3D12App::OnMouseWheel(UI_MOUSE_VIRTUAL_KEY keys, int delta, int x, int y) {

  m_Camera.Scale(0.001f * delta, 3.0f, 15.0f);

  m_ptLastMousePos.x = x;
  m_ptLastMousePos.y = y;
}

void InitD3D12App::OnMouseMove(UI_MOUSE_VIRTUAL_KEY keys, int x, int y) {
  if (keys & UI_MK_LBUTTON) {
    // Make each pixel correspond to a quarter of a degree.
    float dx = XMConvertToRadians(0.25f * static_cast<float>(x - m_ptLastMousePos.x));
    float dy = XMConvertToRadians(0.25f * static_cast<float>(y - m_ptLastMousePos.y));
    m_Camera.Rotate(dx, dy);
  }
  m_ptLastMousePos.x = x;
  m_ptLastMousePos.y = y;
}

void InitD3D12App::OnFrameMoved(float fTime, float fElapsed) {

  XMMATRIX matRotate =
      XMMatrixRotationAxis(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), fElapsed * 2.0f * XM_PI);
  XMMATRIX matTrans1 = XMMatrixTranslation(-0.5f, -0.5f, 0.0f);
  XMMATRIX matTrans2 = XMMatrixTranslation(0.5f, 0.5f, 0.0f);

  ObjectConstants objBuffer;

  objBuffer.matWorldViewProj = XMLoadFloat4x4(&m_aRitem.matWorld) * m_Camera.GetViewProj();
  objBuffer.matWorldViewProj = XMMatrixTranspose(objBuffer.matWorldViewProj);

  objBuffer.matTexTransform = XMMatrixTranspose(matTrans1 * matRotate * matTrans2);

  m_CbUploadBuffer.CopyData(&objBuffer, sizeof(objBuffer), 0);
}

void InitD3D12App::OnRenderFrame(float fTime, float fElapsed) {

  HRESULT hr;
  ID3D12DescriptorHeap *const aDescriptorHeaps[] = {m_CbvHeap, m_pSamplerHeap};
  CD3DX12_GPU_DESCRIPTOR_HANDLE cbvSrvHandle;

  // Reuse the memory associated with command recording.
  // We can only reset when the associated command lists have finished execution on the GPU.
  V(m_pd3dDirectCmdAlloc->Reset());

  // A command list can be reset after it has been added to the command queue via
  // ExecuteCommandList. Reusing the command list reuses memory.
  V(m_pd3dCommandList->Reset(m_pd3dDirectCmdAlloc, m_psoDefaultPSO));

  // Indicate a state transition on the resource usage.
  PrepareNextFrame();

  // Set the viewport and scissor rect.  This needs to be reset whenever the command list is reset.
  m_pd3dCommandList->RSSetViewports(1, &m_ScreenViewport);
  m_pd3dCommandList->RSSetScissorRects(1, &m_ScissorRect);

  // Clear the back buffer and depth buffer.
  m_pd3dCommandList->ClearRenderTargetView(CurrentBackBufferView(), DirectX::Colors::LightBlue, 0,
                                           nullptr);
  m_pd3dCommandList->ClearDepthStencilView(
      DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

  // Specify the buffers we are going to render to.
  m_pd3dCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

  m_pd3dCommandList->SetDescriptorHeaps(2, aDescriptorHeaps);

  m_pd3dCommandList->SetGraphicsRootSignature(m_pRootSignature);

  m_pd3dCommandList->IASetVertexBuffers(0, 1, &m_aRitem.pMeshBuffer->VertexBufferView());
  m_pd3dCommandList->IASetIndexBuffer(&m_aRitem.pMeshBuffer->IndexBufferView());
  m_pd3dCommandList->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  cbvSrvHandle = m_CbvHeap->GetGPUDescriptorHandleForHeapStart();
  m_pd3dCommandList->SetGraphicsRootDescriptorTable(0, cbvSrvHandle);
  cbvSrvHandle.Offset(1, m_uCbvSrvUavDescriptorSize);
  m_pd3dCommandList->SetGraphicsRootDescriptorTable(1, cbvSrvHandle);
  m_pd3dCommandList->SetGraphicsRootDescriptorTable(
      2, m_pSamplerHeap->GetGPUDescriptorHandleForHeapStart());

  m_pd3dCommandList->DrawIndexedInstanced(m_aRitem.uIndexCount, 1, 0, 0, 0);

  // Indicate a state transition on the resource usage.
  EndRenderFrame();

  // Done recording commands.
  V(m_pd3dCommandList->Close());

  // Add the command list to the queue for execution.
  ID3D12CommandList *cmdsLists[] = {m_pd3dCommandList};
  m_pd3dCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

  // swap the back and front buffers
  Present();

  // Wait until frame commands are complete.  This waiting is inefficient and is
  // done for simplicity.  Later we will show how to organize our rendering code
  // so we do not have to wait per frame.
  FlushCommandQueue();
}