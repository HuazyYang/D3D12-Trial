#include <windows.h>
#include "Camera.h"
#include "D3D12RendererContext.hpp"
#include "DXRHelpers/AccelerationStructureGenerator.h"
#include "DXRHelpers/RaytracingPipelineGenerator.h"
#include "DXRHelpers/ShaderBindingTableGenerator.h"
#include "MeshBuffer.h"
#include "RootSignatureGenerator.h"
#include "UploadBuffer.h"
#include "Win32Application.hpp"
#include <DirectXColors.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <array>
#include <wrl.h>

#include "Camera.h"

#include "GeometryGenerator.h"

extern void CreateAppInstance(D3D12RendererContext **ppRenderer, WindowInteractor **ppUIController);

int main() {
  D3D12RendererContext *pRenderer;
  WindowInteractor *pUIController;
  int ret;

  CreateAppInstance(&pRenderer, &pUIController);
  ret = RunSample(pRenderer, pUIController, 800, 600, L"HelloDXR");
  SAFE_DELETE(pRenderer);
  return ret;
}

using namespace DirectX;
using namespace DirectX::PackedVector;
using namespace Microsoft::WRL;

namespace CubeAppInternal {

struct FrameConstants {
  XMFLOAT4X4 InvProjection;
  XMFLOAT3 EyePosW;
  FLOAT Padding1[1];
  XMFLOAT4X4 ViewInverse;
};

struct SpotLight {
  XMFLOAT3 LightPos;
  FLOAT Padding0[1];
  XMFLOAT4 LightStenghth;
};

struct InstanceNormalXform {
  XMFLOAT4X4 WorldInvTranspose;
};

static XMMATRIX NormalTranspseFromWorld(CXMMATRIX matWorld) {
  XMMATRIX inv = matWorld;
  inv.r[3] = XMVectorSet(.0f, .0f, .0f, 1.0f);
  inv = XMMatrixInverse(nullptr, inv);
  return inv;
}

struct ObjectConstants {};

class FrameResources {
public:
  FrameResources();
  ~FrameResources();

  HRESULT CreateCommandAllocator(ID3D12Device *pd3dDevice);

  static HRESULT CreatePerframeBuffers(ID3D12Device *pd3dDevice, int iPassCount);

  static HRESULT CreateInstanceNormalXformBuffer(ID3D12Device *pd3dDevice,
                                                 _Inout_ std::vector<UINT> &instancesPerHitGroup) {
    HRESULT hr;
    UINT totalCount = 0;
    for (auto &count : instancesPerHitGroup) {
      count = d3dUtils::CalcConstantBufferByteSize(count * sizeof(InstanceNormalXform)) / sizeof(InstanceNormalXform);
      totalCount += count;
    }

    V_RETURN(InstanceNormalXformBuffer.CreateBuffer(pd3dDevice, totalCount, sizeof(InstanceNormalXform), FALSE));
    return hr;
  }

  UINT64 FenceCount;
  ID3D12CommandAllocator *CmdAllocator;
  static UploadBuffer PerframeBuffer;
  static UploadBuffer SpotLightBuffer;

  static UploadBuffer InstanceNormalXformBuffer;
};

UploadBuffer FrameResources::PerframeBuffer;
UploadBuffer FrameResources::SpotLightBuffer;
UploadBuffer FrameResources::InstanceNormalXformBuffer;

FrameResources::FrameResources() {
  FenceCount = 0;
  CmdAllocator = nullptr;
}
FrameResources::~FrameResources() { SAFE_RELEASE(CmdAllocator); }

HRESULT FrameResources::CreateCommandAllocator(ID3D12Device *pd3dDevice) {
  HRESULT hr;

  V_RETURN(pd3dDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&CmdAllocator)));

  return hr;
}

HRESULT FrameResources::CreatePerframeBuffers(ID3D12Device *pd3dDevice, int iPassCount) {
  HRESULT hr;
  V_RETURN(PerframeBuffer.CreateBuffer(pd3dDevice, iPassCount, sizeof(FrameConstants), TRUE));

  V_RETURN(SpotLightBuffer.CreateBuffer(pd3dDevice, 1, sizeof(SpotLight), TRUE));
  return hr;
}
}; // namespace CubeAppInternal

using namespace CubeAppInternal;

struct Vertex {
  Vertex() {}
  Vertex(const XMFLOAT3 &pos, const XMFLOAT4 &color) : Pos(pos), Color(color) {}
  Vertex(const XMFLOAT4 &pos, const XMFLOAT4 &n, const XMFLOAT4 &color)
      : Pos(pos.x, pos.y, pos.z), Normal(n.x, n.y, n.z), Color(color) {}

  DirectX::XMFLOAT3 Pos;
  DirectX::XMFLOAT3 Normal;
  DirectX::XMFLOAT4 Color;
};

template <class Vertex>
void GenerateMengerSponge(int32_t level, float probability, std::vector<Vertex> &outputVertices,
                          std::vector<UINT> &outputIndices);

struct InstanceBuffer {
  ComPtr<ID3D12Resource> pInstanceBuffer;
  DirectX::XMFLOAT4X4 Transform;
};

struct AccelerationStructureBuffers {
  ComPtr<ID3D12Resource> pScratch;
  ComPtr<ID3D12Resource> pResult;
  ComPtr<ID3D12Resource> pInstanceDesc;
};

class HelloDXRApp : public D3D12RendererContext, public WindowInteractor {
public:
  HelloDXRApp();
  ~HelloDXRApp();

private:
  HRESULT OnInitPipelines() override;
  void OnFrameMoved(float fTime, float fElapsedTime) override;
  void OnRenderFrame(float fTime, float fEplased) override;
  void OnResizeFrame(int cx, int cy) override;
  LRESULT OnMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool *pbNoFurtherProcessing) override;

  /// Top AS instance description.
  struct InstanceBuffers {
    AccelerationStructureBuffers BottomASBuffers;
    ComPtr<MeshBuffer> MeshBuffer;
    std::vector<XMFLOAT4X4> Xforms;
    UINT HitGroupIndex;

    /// Instance buffer start index in GPU.
    UINT InstanceXformBufferStartOffset;
  };

  HRESULT CreateGeometryBuffer();
  HRESULT CreateAccelerationStructures();
  HRESULT CreateBottomLevelAS(const std::vector<MeshBuffer *> &aMeshBuffers, AccelerationStructureBuffers &buffers);
  HRESULT CreateTopLevelAS(_In_ const std::vector<InstanceBuffers> &instances,
                           _In_ AccelerationStructureBuffers &buffers, _In_ BOOL updateOnly);

  VOID AnimateInstances(float fTime, float fElapsed);

  HRESULT CreateRaytracingPipeline();

  /// Create ray tracing resources.
  HRESULT CreateCbvSrvUavDescriptorHeap();

  HRESULT CreateRaytracingOutputBuffer();

  /// Create Shader bindiing table to update resource to ray tracing pipeline.
  HRESULT CreateRaytracingShaderBindingTable(FrameResources *pFrameResources);

  VOID PostInitialize();

  std::vector<MeshBuffer *> m_aRitemMeshBuffers;

  std::vector<InstanceBuffers> m_aInstanceBuffers;

  std::vector<AccelerationStructureBuffers> m_aBottomLevelASs;
  AccelerationStructureBuffers m_aTopASBuffers;

  ComPtr<ID3D12StateObject> m_pRaytracingStateObject;
  ComPtr<ID3D12StateObjectProperties> m_pRaytracingStateProps;

  ComPtr<ID3D12RootSignature> m_pRaytracingRootSignature;
  ComPtr<ID3D12RootSignature> m_pMissRootSignature;
  ComPtr<ID3D12RootSignature> m_pHitRootSignature;

  ComPtr<ID3D12Resource> m_pRaytracingOutputBuffer;

  D3D12_GPU_DESCRIPTOR_HANDLE m_hRaytracingOutputUav;
  D3D12_GPU_DESCRIPTOR_HANDLE m_hRaytracingAccelSrv;

  ComPtr<ID3D12DescriptorHeap> m_pCbvSrvUavHeap;

  /// !!! Only to manage runtime context
  ShaderBindingTableGenerator m_aSBTGenerator;

  ComPtr<ID3D12Resource> m_pSBTBuffer;

  static const UINT s_uNumberOfFrames = 3;
  FrameResources m_aFrameResources[s_uNumberOfFrames];
  UINT m_iCurrentFrameIndex;

  POINT m_ptLastMousePos;

  float m_fLightRotationAngle;
  float m_fAnimationTimeElapsed;

  CModelViewerCamera m_Camera;
};

void CreateAppInstance(D3D12RendererContext **ppRenderer, WindowInteractor **ppUIController) {
  auto pContext = new HelloDXRApp;
  *ppRenderer = pContext;
  *ppUIController = pContext;
}

HelloDXRApp::HelloDXRApp() {

  m_aDeviceConfig.MsaaEnabled = FALSE;
  m_aDeviceConfig.MsaaSampleCount = 1;
  m_aDeviceConfig.MsaaQaulityLevel = 0;

  m_aDeviceConfig.RaytracingEnabled = TRUE;

  m_iCurrentFrameIndex = 0;

  m_fAnimationTimeElapsed = 0;

  memcpy(m_aRTVDefaultClearValue.Color, &DirectX::Colors::LightBlue, sizeof(m_aRTVDefaultClearValue.Color));
}

HelloDXRApp::~HelloDXRApp() {

  for (auto ri : m_aRitemMeshBuffers) {
    SAFE_RELEASE(ri);
  }
}

HRESULT HelloDXRApp::OnInitPipelines() {

  HRESULT hr;

  /// Reset the command list to prepare for initalization commands.
  V_RETURN(m_pd3dCommandList->Reset(m_pd3dDirectCmdAlloc, nullptr));

  V_RETURN(CreateGeometryBuffer());

  ID3D12CommandList *cmdsLists[] = {m_pd3dCommandList};
  V_RETURN(m_pd3dCommandList->Close());
  m_pd3dCommandQueue->ExecuteCommandLists(1, cmdsLists);
  FlushCommandQueue();

  m_pd3dCommandList->Reset(m_pd3dDirectCmdAlloc, nullptr);

  V_RETURN(CreateAccelerationStructures());

  V_RETURN(CreateRaytracingPipeline());

  // V_RETURN(CreateRaytracingOutputBuffer());
  // V_RETURN(CreateCbvSrvUavDescriptorHeap());
  // V_RETURN(CreateRaytracingShaderBindingTable());

  for (auto &frameRes : m_aFrameResources) {
    V_RETURN(frameRes.CreateCommandAllocator(m_pd3dDevice));
  }
  FrameResources::CreatePerframeBuffers(m_pd3dDevice, s_uNumberOfFrames);

  // Execute the initialization commands.
  V_RETURN(m_pd3dCommandList->Close());
  m_pd3dCommandQueue->ExecuteCommandLists(1, cmdsLists);
  FlushCommandQueue();

  PostInitialize();

  m_Camera.SetViewParams({0.0, 0.0f, -10.0f}, {0.0f, 0.0f, 0.0f});

  return hr;
}

VOID HelloDXRApp::PostInitialize() {

  // for (auto &ri : m_aRitemMeshBuffers) {
  //  ri->DisposeUploaders();
  //}

  /// Dispose the instance buffer.
  // m_aTopASBuffers.pInstanceDesc = nullptr;
}

HRESULT HelloDXRApp::CreateGeometryBuffer() {

  HRESULT hr;

  Vertex vertices[4] = {
      //{ {.0f , 1.0f, .0f, .0f}, {.0f, .0f, 1.0f, .0f}, {1.0f, 1.0f, .0f, 1.0f} },
      //{ {1.0f, -1.0f, .0f, .0f}, {.0f, .0f, 1.0f, .0f}, {.0f, 1.0f, 1.0f, 1.0f} },
      //{ {-1.0f, -1.0f, .0f, .0f}, {.0f, .0f, 1.0f, .0f}, {1.0f, .0f, 1.0f, 1.0f} }

      {{std::sqrtf(8.f / 9.f), 0.f, -1.f / 3.f, .0f}, {.0f, -1.0f, 0.0f, .0f}, {1.f, 0.f, 0.f, 1.f}},
      {{-std::sqrtf(2.f / 9.f), std::sqrtf(2.f / 3.f), -1.f / 3.f, .0f}, {.0f, 1.0f, 0.0f, .0f}, {0.f, 1.f, 0.f, 1.f}},
      {{-std::sqrtf(2.f / 9.f), -std::sqrtf(2.f / 3.f), -1.f / 3.f, .0f},
       {.0f, -1.0f, 1.0f, .0f},
       {0.f, 0.f, 1.f, 1.f}},
      {{0.f, 0.f, 1.f, .0f}, {.0f, .0f, 1.0f, .0f}, {1, 0, 1, 1}},
  };
  UINT indices[] = {0, 1, 2, 0, 3, 1, 0, 2, 3, 1, 3, 2};

  Vertex tetrahedronVertices[_countof(indices)];
  UINT I1, I2, I3;
  XMVECTOR V1, V2, V3, N;

  for (UINT i = 0; i < _countof(indices); i += 3) {
    I1 = indices[i];
    I2 = indices[i + 1];
    I3 = indices[i + 2];
    V1 = XMLoadFloat3(&vertices[I1].Pos);
    V2 = XMLoadFloat3(&vertices[I2].Pos);
    V3 = XMLoadFloat3(&vertices[I3].Pos);

    N = XMVector3Normalize(XMVector3Cross(XMVectorSubtract(V1, V2), XMVectorSubtract(V3, V2)));
    XMStoreFloat3(&tetrahedronVertices[i].Pos, V1);
    XMStoreFloat3(&tetrahedronVertices[i].Normal, N);
    tetrahedronVertices[i].Color = vertices[I1].Color;

    XMStoreFloat3(&tetrahedronVertices[i + 1].Pos, V2);
    XMStoreFloat3(&tetrahedronVertices[i + 1].Normal, N);
    tetrahedronVertices[i + 1].Color = vertices[I2].Color;

    XMStoreFloat3(&tetrahedronVertices[i + 2].Pos, V3);
    XMStoreFloat3(&tetrahedronVertices[i + 2].Normal, N);
    tetrahedronVertices[i + 2].Color = vertices[I3].Color;

    indices[i] = i;
    indices[i + 1] = i + 1;
    indices[i + 2] = i + 2;
  }

  MeshBuffer *pMeshBuffer;
  /// Triangle.
  V_RETURN(CreateMeshBuffer(&pMeshBuffer));
  V_RETURN(pMeshBuffer->CreateVertexBuffer(m_pd3dDevice, m_pd3dCommandList, tetrahedronVertices,
                                           _countof(tetrahedronVertices), sizeof(Vertex), nullptr));
  V_RETURN(pMeshBuffer->CreateIndexBuffer(m_pd3dDevice, m_pd3dCommandList, indices, _countof(indices), sizeof(UINT)));

  m_aRitemMeshBuffers.push_back(pMeshBuffer);

  /// Plane
  V_RETURN(CreateMeshBuffer(&pMeshBuffer));

  Vertex planeVertices[] = {{{-1.0f, .0f, -1.0f, .0f}, {.0f, 1.0f, .0f, .0f}, {3.0f, 3.0f, 3.0f, 1.0f}},
                            {{-1.0f, .0f, 1.0f, .0f}, {.0f, 1.0f, .0f, .0f}, {3.0f, 3.0f, 3.0f, 1.0f}},
                            {{1.0f, .0f, 1.0f, .0f}, {.0f, 1.0f, .0f, .0f}, {3.0f, 3.0f, 3.0f, 1.0f}},
                            {{1.0f, .0f, -1.0f, .0f}, {.0f, 1.0f, .0f, .0f}, {3.0f, 3.0f, 3.0f, 1.0f}}};
  UINT planeIndices[] = {0, 1, 2, 0, 2, 3};

  V_RETURN(pMeshBuffer->CreateVertexBuffer(m_pd3dDevice, m_pd3dCommandList, planeVertices, _countof(planeVertices),
                                           sizeof(Vertex), nullptr));
  V_RETURN(pMeshBuffer->CreateIndexBuffer(m_pd3dDevice, m_pd3dCommandList, planeIndices, _countof(planeIndices),
                                          sizeof(UINT)));

  m_aRitemMeshBuffers.push_back(pMeshBuffer);

  std::vector<Vertex> mengerVertices;
  std::vector<UINT> mengerIndices;

  GenerateMengerSponge(3, 0.75f, mengerVertices, mengerIndices);

  V_RETURN(CreateMeshBuffer(&pMeshBuffer));
  V_RETURN(pMeshBuffer->CreateVertexBuffer(m_pd3dDevice, m_pd3dCommandList, mengerVertices.data(),
                                           (UINT)mengerVertices.size(), sizeof(Vertex), nullptr));
  V_RETURN(pMeshBuffer->CreateIndexBuffer(m_pd3dDevice, m_pd3dCommandList, mengerIndices.data(),
                                          (UINT)mengerIndices.size(), sizeof(UINT)));

  m_aRitemMeshBuffers.push_back(pMeshBuffer);

  return hr;
}

HRESULT HelloDXRApp::CreateAccelerationStructures() {
  HRESULT hr;
  XMMATRIX planeMatrix;
  XMMATRIX mengerMatrix;
  std::vector<UINT> aInstancesPerHitGroups;

  planeMatrix = XMMatrixTranslation(.0f, -1.0f, .0f) * XMMatrixScaling(6.0f, 1.0f, 6.0f);

  mengerMatrix = XMMatrixScaling(2.0f, 2.f, 2.0f) * XMMatrixTranslation(.0f, -0.0f, 2.0f);

  m_aInstanceBuffers.resize(3);
  V_RETURN(CreateBottomLevelAS({m_aRitemMeshBuffers[0]}, m_aInstanceBuffers[0].BottomASBuffers));
  m_aInstanceBuffers[0].MeshBuffer = m_aRitemMeshBuffers[0];
  m_aInstanceBuffers[0].HitGroupIndex = {0};
  m_aInstanceBuffers[0].Xforms.resize(3);
  XMStoreFloat4x4(&m_aInstanceBuffers[0].Xforms[0], XMMatrixIdentity());
  XMStoreFloat4x4(&m_aInstanceBuffers[0].Xforms[1], XMMatrixTranslation(-4.0f, .0f, .0f));
  XMStoreFloat4x4(&m_aInstanceBuffers[0].Xforms[2], XMMatrixTranslation(4.0f, .0f, .0f));
  aInstancesPerHitGroups.push_back(3);

  V_RETURN(CreateBottomLevelAS({m_aRitemMeshBuffers[1]}, m_aInstanceBuffers[1].BottomASBuffers));
  m_aInstanceBuffers[1].MeshBuffer = m_aRitemMeshBuffers[1];
  m_aInstanceBuffers[1].HitGroupIndex = {2};
  m_aInstanceBuffers[1].Xforms.resize(1);
  XMStoreFloat4x4(&m_aInstanceBuffers[1].Xforms[0], planeMatrix);
  aInstancesPerHitGroups.push_back(1);

  V_RETURN(CreateBottomLevelAS({m_aRitemMeshBuffers[2]}, m_aInstanceBuffers[2].BottomASBuffers));
  m_aInstanceBuffers[2].MeshBuffer = m_aRitemMeshBuffers[2];
  m_aInstanceBuffers[2].HitGroupIndex = {4};
  m_aInstanceBuffers[2].Xforms.resize(1);
  XMStoreFloat4x4(&m_aInstanceBuffers[2].Xforms[0], mengerMatrix);
  aInstancesPerHitGroups.push_back(1);

  V_RETURN(CreateTopLevelAS(m_aInstanceBuffers, m_aTopASBuffers, FALSE));

  FrameResources::CreateInstanceNormalXformBuffer(m_pd3dDevice, aInstancesPerHitGroups);
  UINT i = 0, j, bufferIndexOffset = 0;
  UINT bufferStartOffset = 0;
  InstanceNormalXform normalXfrom;

  for (auto &count : aInstancesPerHitGroups) {
    m_aInstanceBuffers[i].InstanceXformBufferStartOffset = bufferStartOffset;
    j = bufferIndexOffset;

    /// Copy data.
    for (auto &xform : m_aInstanceBuffers[i].Xforms) {

      XMStoreFloat4x4(&normalXfrom.WorldInvTranspose, NormalTranspseFromWorld(XMLoadFloat4x4(&xform)));

      FrameResources::InstanceNormalXformBuffer.CopyData(&normalXfrom, sizeof(InstanceNormalXform), j++);
    }

    bufferStartOffset += count * sizeof(InstanceNormalXform);
    bufferIndexOffset += count;
    ++i;
  }

  return hr;
}

HRESULT HelloDXRApp::CreateBottomLevelAS(const std::vector<MeshBuffer *> &aMeshBuffers,
                                         AccelerationStructureBuffers &buffers) {

  /// BLAS store geometry vertex buffers.
  HRESULT hr;
  ID3D12Resource *pVertexBuffer, *pIndexBuffer;
  UINT vertexCount, indexCount;
  UINT vertexStride, indexStride;
  DXGI_FORMAT indexFormat;
  BottomLevelASGenerator bottomASGenerator;

  //-----------------------------------------------------------------------------
  //
  // Create a bottom-level acceleration structure based on a list of vertex
  // buffers in GPU memory along with their vertex count. The build is then done
  // in 3 steps: gathering the geometry, computing the sizes of the required
  // buffers, and building the actual AS
  //
  for (auto ri : aMeshBuffers) {
    ri->GetVertexBufferInfo(&pVertexBuffer, &vertexCount, &vertexStride);
    ri->GetIndexBufferInfo(&pIndexBuffer, &indexCount, &indexStride, &indexFormat);
    bottomASGenerator.AddVertexBuffer(pVertexBuffer, 0, vertexCount, vertexStride, pIndexBuffer, 0, indexCount,
                                      indexFormat, nullptr, 0, TRUE);
    SAFE_RELEASE(pVertexBuffer);
    SAFE_RELEASE(pIndexBuffer);
  }

  UINT64 scratchSizeInBytes;
  UINT64 resultSizeInBytes;

  // The AS build requires some scratch space to store temporary information.
  // The amount of scratch memory is dependent on the scene complexity.
  bottomASGenerator.ComputeASBufferSizes(m_pd3dDevice, FALSE, &scratchSizeInBytes, &resultSizeInBytes);

  // Once the sizes are obtained, the application is responsible for allocating
  // the necessary buffers. Since the entire generation will be done on the GPU,
  // we can directly allocate those on the default heap
  V_RETURN(m_pd3dDevice->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Buffer(scratchSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
      D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(buffers.pScratch.GetAddressOf())));

  V_RETURN(m_pd3dDevice->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Buffer(resultSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
      D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(buffers.pResult.GetAddressOf())));

  // Build the acceleration structure. Note that this call integrates a barrier
  // on the generated AS, so that it can be used to compute a top-level AS right
  // after this method.
  V_RETURN(
      bottomASGenerator.Generate(m_pd3dCommandList, buffers.pScratch.Get(), buffers.pResult.Get(), FALSE, nullptr));

  return S_OK;
}

HRESULT HelloDXRApp::CreateTopLevelAS(_In_ const std::vector<InstanceBuffers> &instances,
                                      _In_ AccelerationStructureBuffers &buffers, _In_ BOOL updateOnly) {
  HRESULT hr = S_OK;
  TopLevelASGenerator topASGenerator;
  XMMATRIX matTransform;
  UINT i, j;

  //-----------------------------------------------------------------------------
  // Create the main acceleration structure that holds all instances of the scene.
  // Similarly to the bottom-level AS generation, it is done in 3 steps: gathering
  // the instances, computing the memory requirements for the AS, and building the
  // AS itself
  //

  // Gather all the instances into the builder helper
  topASGenerator.Reserve(instances.size());
  i = 0;
  for (auto &inst : instances) {
    j = 0;
    for (auto &xform : inst.Xforms) {
      matTransform = XMLoadFloat4x4(&xform);
      topASGenerator.AddInstance(inst.BottomASBuffers.pResult.Get(), matTransform, j, inst.HitGroupIndex);
      ++j;
    }
  }

  // As for the bottom-level AS, the building the AS requires some scratch space
  // to store temporary data in addition to the actual AS. In the case of the
  // top-level AS, the instance descriptors also need to be stored in GPU
  // memory. This call outputs the memory requirements for each (scratch,
  // results, instance descriptors) so that the application can allocate the
  // corresponding memory
  UINT64 scratchSize, resultSize, instanceDescsSize;

  topASGenerator.ComputeASBufferSizes(m_pd3dDevice, TRUE, &scratchSize, &resultSize, &instanceDescsSize);

  if (!updateOnly) {
    // Create the scratch and result buffers. Since the build is all done on GPU,
    // those can be allocated on the default heap
    V_RETURN(m_pd3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(scratchSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_PPV_ARGS(buffers.pScratch.GetAddressOf())));
    V_RETURN(m_pd3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(resultSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr, IID_PPV_ARGS(buffers.pResult.GetAddressOf())));

    // The buffer describing the instances: ID, shader binding information,
    // matrices ... Those will be copied into the buffer by the helper through
    // mapping, so the buffer has to be allocated on the upload heap.
    V_RETURN(m_pd3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(instanceDescsSize, D3D12_RESOURCE_FLAG_NONE), D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr, IID_PPV_ARGS(buffers.pInstanceDesc.GetAddressOf())));
  } else {
    // If this a request for an update,
    // then the TLAS was already used in a DispatchRay() call.
    // We need a UAV barrier to make sure the read operation ends before updating the buffer
    m_pd3dCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(buffers.pResult.Get()));
  }

  // After all the buffers are allocated, or if only an update is required, we
  // can build the acceleration structure. Note that in the case of the update
  // we also pass the existing AS as the 'previous' AS, so that it can be
  // refitted in place.
  V_RETURN(topASGenerator.Generate(m_pd3dCommandList, buffers.pScratch.Get(), buffers.pResult.Get(),
                                   buffers.pInstanceDesc.Get(), updateOnly, buffers.pResult.Get()));

  return hr;
}

HRESULT HelloDXRApp::CreateRaytracingPipeline() {

  //-----------------------------------------------------------------------------
  //
  // The raytracing pipeline binds the shader code, root signatures and pipeline
  // characteristics in a single structure used by DXR to invoke the shaders and
  // manage temporary memory during raytracing
  //
  //
  // The pipeline contains the DXIL code of all the shaders potentially executed
  // during the raytracing process. This section compiles the HLSL code into a
  // set of DXIL libraries. We chose to separate the code in several libraries
  // by semantic (ray generation, hit, miss) for clarity. Any code layout can be
  // used.
  HRESULT hr;
  d3dUtils::DxcCompilerWrapper compilerWrapper;
  ComPtr<IDxcBlob> pDxil;

  V_RETURN(compilerWrapper.CreateCompiler());

  V_RETURN(compilerWrapper.CompileFromFile(L"Shaders/HelloDXR_Simple.hlsl", L"", L"lib_6_3", nullptr, 0, nullptr, 0,
                                           nullptr, pDxil.GetAddressOf(), nullptr));

  RayTracingPipelineGenerator rtxpipeline;

  // In a way similar to DLLs, each library is associated with a number of
  // exported symbols. This
  // has to be done explicitly in the lines below. Note that a single library
  // can contain an arbitrary number of symbols, whose semantic is given in HLSL
  // using the [shader("xxx")] syntax
  rtxpipeline.AddLibrary(pDxil.Get(), {L"GenerateRay", L"Miss", L"ClosestHit", L"ShadowMiss"});
  // 3 different shaders can be invoked to obtain an intersection: an
  // intersection shader is called
  // when hitting the bounding box of non-triangular geometry. This is beyond
  // the scope of this tutorial. An any-hit shader is called on potential
  // intersections. This shader can, for example, perform alpha-testing and
  // discard some intersections. Finally, the closest-hit program is invoked on
  // the intersection point closest to the ray origin. Those 3 shaders are bound
  // together into a hit group.

  // Note that for triangular geometry the intersection shader is built-in. An
  // empty any-hit shader is also defined by default, so in our simple case each
  // hit group contains only the closest hit shader. Note that since the
  // exported symbols are defined above the shaders can be simply referred to by
  // name.

  // Hit group for the triangles, with a shader simply interpolating vertex
  // colors
  rtxpipeline.AddHitGroup(L"HitGroup", L"ClosestHit");
  rtxpipeline.AddHitGroup(L"ShadowHitGroup", L"");

  RootSignatureGenerator signatureGen;
  signatureGen.AddConstBufferView(0, 0);
  signatureGen.AddDescriptorTable(
    {
      CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_UAV,
      1,
      0),
      CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV,
      1,
      0
      )
    });
  V_RETURN(
      signatureGen.Generate(m_pd3dDevice, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE, &m_pRaytracingRootSignature));

  signatureGen.Reset();

  V_RETURN(signatureGen.Generate(m_pd3dDevice, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE, &m_pMissRootSignature));

  signatureGen.AddConstBufferView(0, 1);
  signatureGen.AddShaderResourceView(0, 1);
  signatureGen.AddShaderResourceView(1, 1);
  signatureGen.AddShaderResourceView(2, 1);
  ///  TLAS structure.
  signatureGen.AddDescriptorTable(
    {
      CD3DX12_DESCRIPTOR_RANGE1(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0)
    });
  V_RETURN(signatureGen.Generate(m_pd3dDevice, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE, &m_pHitRootSignature));

  // The following section associates the root signature to each shader. Note
  // that we can explicitly show that some shaders share the same root signature
  // (eg. Miss and ShadowMiss). Note that the hit shaders are now only referred
  // to as hit groups, meaning that the underlying intersection, any-hit and
  // closest-hit shaders share the same root signature.
  rtxpipeline.AddRootSignatureAssociation(m_pRaytracingRootSignature.Get(), {L"GenerateRay"});
  // rtxpipeline.AddRootSignatureAssociation(m_pMissRootSignature.Get(), { L"Miss", L"ShadowMiss" });
  rtxpipeline.AddRootSignatureAssociation(m_pHitRootSignature.Get(), {L"HitGroup"});

  // The payload size defines the maximum size of the data carried by the rays,
  // ie. the the data
  // exchanged between shaders, such as the HitInfo structure in the HLSL code.
  // It is important to keep this value as low as possible as a too high value
  // would result in unnecessary memory consumption and cache trashing.
  rtxpipeline.SetMaxPayloadSize(4 * sizeof(float));

  // Upon hitting a surface, DXR can provide several attributes to the hit. In
  // our sample we just use the barycentric coordinates defined by the weights
  // u,v of the last two vertices of the triangle. The actual barycentrics can
  // be obtained using float3 barycentrics = float3(1.f-u-v, u, v);
  rtxpipeline.SetMaxAttributeSize(2 * sizeof(float));

  // The raytracing process can shoot rays from existing hit points, resulting
  // in nested TraceRay calls. Our sample code traces only primary rays, which
  // then requires a trace depth of 1. Note that this recursion depth should be
  // kept to a minimum for best performance. Path tracing algorithms can be
  // easily flattened into a simple loop in the ray generation.
  rtxpipeline.SetMaxRecursionDepth(2);

  // Compile the pipeline for execution on the GPU
  V_RETURN(rtxpipeline.Generate(m_pd3dDevice, &m_pRaytracingStateObject));

  // Cast the state object into a properties object, allowing to later access
  // the shader pointers by name
  V_RETURN(m_pRaytracingStateObject->QueryInterface(IID_PPV_ARGS(&m_pRaytracingStateProps)));

  return hr;
}

HRESULT HelloDXRApp::CreateCbvSrvUavDescriptorHeap() {
  HRESULT hr = S_OK;

  if (!m_pCbvSrvUavHeap) {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    heapDesc.NumDescriptors = 2;
    heapDesc.NodeMask = 0;

    V_RETURN(m_pd3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(m_pCbvSrvUavHeap.GetAddressOf())));
  }

  CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDescriptorHandle;
  CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDescriptorHandle;

  hCpuDescriptorHandle = m_pCbvSrvUavHeap->GetCPUDescriptorHandleForHeapStart();
  hGpuDescriptorHandle = m_pCbvSrvUavHeap->GetGPUDescriptorHandleForHeapStart();

  D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
  uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  uavDesc.Texture2D.MipSlice = 0;
  uavDesc.Texture2D.PlaneSlice = 0;

  m_pd3dDevice->CreateUnorderedAccessView(m_pRaytracingOutputBuffer.Get(), nullptr, &uavDesc, hCpuDescriptorHandle);
  m_hRaytracingOutputUav = hGpuDescriptorHandle;

  hCpuDescriptorHandle.Offset(m_uCbvSrvUavDescriptorSize);
  hGpuDescriptorHandle.Offset(m_uCbvSrvUavDescriptorSize);

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
  srvDesc.Format = DXGI_FORMAT_UNKNOWN;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.RaytracingAccelerationStructure.Location = m_aTopASBuffers.pResult->GetGPUVirtualAddress();
  m_pd3dDevice->CreateShaderResourceView(nullptr, &srvDesc, hCpuDescriptorHandle);
  m_hRaytracingAccelSrv = hGpuDescriptorHandle;

  hCpuDescriptorHandle.Offset(m_uCbvSrvUavDescriptorSize);
  hGpuDescriptorHandle.Offset(m_uCbvSrvUavDescriptorSize);

  return hr;
}

HRESULT HelloDXRApp::CreateRaytracingOutputBuffer() {

  HRESULT hr;

  m_pRaytracingOutputBuffer = nullptr;
  V_RETURN(m_pd3dDevice->CreateCommittedResource(
      &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
      &CD3DX12_RESOURCE_DESC::Tex2D(m_BackBufferFormat, m_uFrameWidth, m_uFrameHeight, 1, 1, 1, 0,
                                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
      D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(m_pRaytracingOutputBuffer.GetAddressOf())));

  return hr;
}

//-----------------------------------------------------------------------------
//
// The Shader Binding Table (SBT) is the cornerstone of the raytracing setup:
// this is where the shader resources are bound to the shaders, in a way that
// can be interpreted by the raytracer on GPU. In terms of layout, the SBT
// contains a series of shader IDs with their resource pointers. The SBT
// contains the ray generation shader, the miss shaders, then the hit groups.
// Using the helper class, those can be specified in arbitrary order.
//
HRESULT HelloDXRApp::CreateRaytracingShaderBindingTable(FrameResources *) {

  HRESULT hr = S_OK;

  // The SBT helper class collects calls to Add*Program.  If called several
  // times, the helper must be emptied before re-adding shaders.
  m_aSBTGenerator.Reset();

  // The ray generation only uses heap data
  m_aSBTGenerator.AddRayGenerationProgram(
      L"GenerateRay",
      {reinterpret_cast<PVOID>(FrameResources::PerframeBuffer.GetConstBufferAddress(m_iCurrentFrameIndex)),
       reinterpret_cast<PVOID>(m_hRaytracingOutputUav.ptr)});

  // The miss and hit shaders do not access any external resources: instead they
  // communicate their results through the ray payload
  m_aSBTGenerator.AddMissProgram(L"Miss", {});
  m_aSBTGenerator.AddMissProgram(L"ShadowMiss", {});

  std::vector<void *> hitGroupBSs(5);
  ID3D12Resource *pVertexBuffer;
  ID3D12Resource *pIndexBuffer;
  D3D12_GPU_VIRTUAL_ADDRESS cbaInstanceXform;

  hitGroupBSs[0] = reinterpret_cast<void *>(FrameResources::SpotLightBuffer.GetConstBufferAddress(0));
  hitGroupBSs[4] = reinterpret_cast<void *>(m_hRaytracingAccelSrv.ptr);

  cbaInstanceXform = FrameResources::InstanceNormalXformBuffer.GetConstBufferAddress();

  for (auto &ri : m_aInstanceBuffers) {
    hitGroupBSs[1] = reinterpret_cast<void *>(cbaInstanceXform + ri.InstanceXformBufferStartOffset);

    ri.MeshBuffer->GetVertexBufferInfo(&pVertexBuffer, nullptr, nullptr);
    ri.MeshBuffer->GetIndexBufferInfo(&pIndexBuffer, nullptr, nullptr, nullptr);
    hitGroupBSs[2] = reinterpret_cast<void *>(pVertexBuffer->GetGPUVirtualAddress());
    hitGroupBSs[3] = reinterpret_cast<void *>(pIndexBuffer->GetGPUVirtualAddress());
    // Adding the triangle hit shader
    m_aSBTGenerator.AddHitGroup(L"HitGroup", hitGroupBSs);
    m_aSBTGenerator.AddHitGroup(L"ShadowHitGroup", {});
    SAFE_RELEASE(pVertexBuffer);
    SAFE_RELEASE(pIndexBuffer);
  }

  // Compute the size of the SBT given the number of shaders and their
  // parameters
  UINT64 sbtSize;
  sbtSize = m_aSBTGenerator.ComputeSBTSize();

  // Create the SBT on the upload heap. This is required as the helper will use
  // mapping to write the SBT contents. After the SBT compilation it could be
  // copied to the default heap for performance.
  if (!m_pSBTBuffer) {
    V_RETURN(m_pd3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(sbtSize),
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&m_pSBTBuffer)));
  }

  // Compile the SBT from the shader and parameters info
  m_aSBTGenerator.Generate(m_pSBTBuffer.Get(), m_pRaytracingStateProps.Get());

  return hr;
}

void HelloDXRApp::OnResizeFrame(int cx, int cy) {

  m_Camera.SetProjParams(0.25f * XM_PI, GetAspectRatio(), 1.0f, 1000.0f);
  m_Camera.SetWindow(cx, cy);

  HRESULT hr;

  V(CreateRaytracingOutputBuffer());
  V(CreateCbvSrvUavDescriptorHeap());
  // V(CreateRaytracingShaderBindingTable());
}

LRESULT HelloDXRApp::OnMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool *pbNoFurtherProcessing) {

  m_Camera.HandleMessages(hwnd, msg, wp, lp);

  return 0L;
}

void HelloDXRApp::OnFrameMoved(float fTime, float fElapsed) {
  HRESULT hr;

  m_Camera.FrameMove(fElapsed, this);

  FrameResources *pFrameResources;

  m_iCurrentFrameIndex = (m_iCurrentFrameIndex + 1) % s_uNumberOfFrames;
  pFrameResources = &m_aFrameResources[m_iCurrentFrameIndex];

  /// Sychronize it.
  V(m_pSyncFence->WaitForSyncPoint(pFrameResources->FenceCount));

  /// Update Camera parameters.
  FrameConstants frameRes;
  XMMATRIX V, P;

  V = m_Camera.GetViewMatrix();
  P = m_Camera.GetProjMatrix();

  XMStoreFloat4x4(&frameRes.InvProjection, XMMatrixTranspose(XMMatrixInverse(nullptr, P)));

  XMStoreFloat3(&frameRes.EyePosW, m_Camera.GetEyePt());

  XMMATRIX invV = XMMatrixTranspose(V);
  invV = XMMatrixInverse(nullptr, invV);

  XMStoreFloat4x4(&frameRes.ViewInverse, invV);

  FrameResources::PerframeBuffer.CopyData(&frameRes, sizeof(FrameConstants), m_iCurrentFrameIndex);

  /// Animate light movement and stength.
  m_fLightRotationAngle += 0.5f * fElapsed;

  XMMATRIX lightRot = XMMatrixRotationY(m_fLightRotationAngle);
  SpotLight spotLight;
  XMVECTOR vLightPos = XMVectorSet(7.0f, 6.0f, 8.0f, 1.0f);

  vLightPos = XMVector4Transform(vLightPos, lightRot);
  XMStoreFloat3(&spotLight.LightPos, vLightPos);

  spotLight.LightStenghth = {0.8f, 0.6f, 0.5f, 1.0f};

  FrameResources::SpotLightBuffer.CopyData(&spotLight, sizeof(SpotLight), 0);

  V(CreateRaytracingShaderBindingTable(pFrameResources));
}

VOID HelloDXRApp::AnimateInstances(float fTime, float fElapsed) {

  HRESULT hr;
  /// Animate the instance.
  // XMMATRIX planeMatrix;
  // XMMATRIX mengerMatrix;
  XMMATRIX tetrahedronMatrix[3];

  tetrahedronMatrix[0] = XMMatrixRotationY(-3.0f * static_cast<float>(fElapsed)) *
                         XMMatrixTranslation(0.f, 0.1f * cosf(0.5f * fElapsed), 0.f);
  tetrahedronMatrix[1] = XMMatrixRotationZ(1.0f * static_cast<float>(fElapsed)) *
                         XMMatrixTranslation(0.f, 0.1f * cosf(0.7f * fElapsed), 0.f) *
                         XMMatrixTranslation(-4.0f, .0f, .0f);
  tetrahedronMatrix[2] = XMMatrixRotationX(2.0f * static_cast<float>(fElapsed)) *
                         XMMatrixTranslation(0.f, 0.1f * cosf(1.2f * fElapsed), 0.f) *
                         XMMatrixTranslation(4.0f, .0f, .0f);

  // planeMatrix = XMMatrixTranslation(.0f, -1.0f, .0f) * XMMatrixScaling(6.0f, 1.0f, 6.0f);
  // mengerMatrix = XMMatrixScaling(2.0f, 2.f, 2.0f) * XMMatrixTranslation(.0f, -0.0f, 2.0f);

  XMStoreFloat4x4(&m_aInstanceBuffers[0].Xforms[0], tetrahedronMatrix[0]);
  XMStoreFloat4x4(&m_aInstanceBuffers[0].Xforms[1], tetrahedronMatrix[1]);
  XMStoreFloat4x4(&m_aInstanceBuffers[0].Xforms[2], tetrahedronMatrix[2]);

  V(CreateTopLevelAS(m_aInstanceBuffers, m_aTopASBuffers, TRUE));

  /// Copy data.
  UINT j = 0;
  InstanceNormalXform normalXform;
  for (auto &xform : m_aInstanceBuffers[0].Xforms) {

    XMStoreFloat4x4(&normalXform.WorldInvTranspose, NormalTranspseFromWorld(XMLoadFloat4x4(&xform)));

    FrameResources::InstanceNormalXformBuffer.CopyData(&normalXform, sizeof(InstanceNormalXform), j++);
  }
}

void HelloDXRApp::OnRenderFrame(float fTime, float fElapsed) {

  HRESULT hr;
  FrameResources *pFrameResources = &m_aFrameResources[m_iCurrentFrameIndex];

  // Reuse the memory associated with command recording.
  // We can only reset when the associated command lists have finished execution on the GPU.
  V(pFrameResources->CmdAllocator->Reset());

  // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
  // Reusing the command list reuses memory.
  V(m_pd3dCommandList->Reset(pFrameResources->CmdAllocator, nullptr));

  AnimateInstances(fTime, fElapsed);

  CD3DX12_RESOURCE_BARRIER resBarriers[2] = {CD3DX12_RESOURCE_BARRIER::Transition(
      m_pRaytracingOutputBuffer.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)};

  m_pd3dCommandList->ResourceBarrier(1, resBarriers);

  ID3D12DescriptorHeap *descriptorHeaps[] = {m_pCbvSrvUavHeap.Get()};
  m_pd3dCommandList->SetDescriptorHeaps(1, descriptorHeaps);

  // Setup the raytracing task
  D3D12_DISPATCH_RAYS_DESC dispatchRayDesc = {};
  UINT64 rayGenerationSectionSizeInBytes;
  UINT64 missSectionSizeInBytes;
  UINT64 hitGroupsSectionSize;

  // The ray generation shaders are always at the beginning of the SBT.
  // The layout of the SBT is as follows: ray generation shader, miss
  // shaders, hit groups. As described in the CreateShaderBindingTable method,
  // all SBT entries of a given type have the same size to allow a fixed stride.

  // The ray generation shaders are always at the beginning of the SBT.
  rayGenerationSectionSizeInBytes = m_aSBTGenerator.GetRayGenSectionSize();
  dispatchRayDesc.RayGenerationShaderRecord.StartAddress =
      m_pSBTBuffer->GetGPUVirtualAddress() + m_aSBTGenerator.GetRayGenEntryOffset();
  dispatchRayDesc.RayGenerationShaderRecord.SizeInBytes = rayGenerationSectionSizeInBytes;

  // The miss shaders are in the second SBT section, right after the ray
  // generation shader. We have one miss shader for the camera rays and one
  // for the shadow rays, so this section has a size of 2*m_sbtEntrySize. We
  // also indicate the stride between the two miss shaders, which is the size
  // of a SBT entry
  missSectionSizeInBytes = m_aSBTGenerator.GetMissSectionSize();
  dispatchRayDesc.MissShaderTable.StartAddress =
      m_pSBTBuffer->GetGPUVirtualAddress() + m_aSBTGenerator.GetMissEntryOffset();
  dispatchRayDesc.MissShaderTable.SizeInBytes = missSectionSizeInBytes;
  dispatchRayDesc.MissShaderTable.StrideInBytes = m_aSBTGenerator.GetMissEntrySize();

  // The hit groups section start after the miss shaders. In this sample we
  // have one 1 hit group for the triangle
  hitGroupsSectionSize = m_aSBTGenerator.GetHitGroupSectionSize();
  dispatchRayDesc.HitGroupTable.StartAddress =
      m_pSBTBuffer->GetGPUVirtualAddress() + m_aSBTGenerator.GetHitGroupEntryOffset();
  dispatchRayDesc.HitGroupTable.SizeInBytes = hitGroupsSectionSize;
  dispatchRayDesc.HitGroupTable.StrideInBytes = m_aSBTGenerator.GetHitGroupEntrySize();

  // Dimensions of the image to render, identical to a kernel launch dimension
  dispatchRayDesc.Width = m_uFrameWidth;
  dispatchRayDesc.Height = m_uFrameHeight;
  dispatchRayDesc.Depth = 1;

  // Bind the raytracing pipeline
  m_pd3dCommandList->SetPipelineState1(m_pRaytracingStateObject.Get());
  m_pd3dCommandList->DispatchRays(&dispatchRayDesc);

  resBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
      m_pRaytracingOutputBuffer.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
  resBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT,
                                                        D3D12_RESOURCE_STATE_COPY_DEST);
  m_pd3dCommandList->ResourceBarrier(2, resBarriers);

  m_pd3dCommandList->CopyResource(CurrentBackBuffer(), m_pRaytracingOutputBuffer.Get());

  resBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(), D3D12_RESOURCE_STATE_COPY_DEST,
                                                        D3D12_RESOURCE_STATE_PRESENT);
  m_pd3dCommandList->ResourceBarrier(1, resBarriers);

  // Done recording commands.
  V(m_pd3dCommandList->Close());

  // Add the command list to the queue for execution.
  ID3D12CommandList *cmdsLists[] = {m_pd3dCommandList};
  m_pd3dCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

  m_pSyncFence->Signal(m_pd3dCommandQueue, &pFrameResources->FenceCount);

  // swap the back and front buffers
  Present();
}

template <class Vertex>
void GenerateMengerSponge(int32_t level, float probability, std::vector<Vertex> &outputVertices,
                          std::vector<UINT> &outputIndices) {
  struct Cube {
    Cube(CXMVECTOR tlf, float s) : m_topLeftFront(tlf), m_size(s) {}
    XMVECTOR m_topLeftFront;
    float m_size;

    void enqueueQuad(std::vector<Vertex> &vertices, std::vector<UINT> &indices, const XMVECTOR &bottomLeft4,
                     const XMVECTOR &dx, const XMVECTOR &dy, bool flip) {
      UINT currentIndex = static_cast<UINT>(vertices.size());
      XMVECTOR normal = XMVector3Cross(XMVector3Normalize(dy), XMVector3Normalize(dx));
      if (flip) {
        normal = -normal;

        indices.push_back(currentIndex + 0);
        indices.push_back(currentIndex + 2);
        indices.push_back(currentIndex + 1);

        indices.push_back(currentIndex + 3);
        indices.push_back(currentIndex + 1);
        indices.push_back(currentIndex + 2);
      } else {

        indices.push_back(currentIndex + 0);
        indices.push_back(currentIndex + 1);
        indices.push_back(currentIndex + 2);

        indices.push_back(currentIndex + 2);
        indices.push_back(currentIndex + 1);
        indices.push_back(currentIndex + 3);
      }

      DirectX::XMFLOAT4 n;
      DirectX::XMFLOAT4 bottomLeft, bottomRight, topLeft, topRight;
      XMStoreFloat4(&n, normal);

      XMStoreFloat4(&bottomLeft, bottomLeft4);
      XMStoreFloat4(&bottomRight, XMVectorAdd(bottomLeft4, dx));
      XMStoreFloat4(&topLeft, XMVectorAdd(bottomLeft4, dy));
      XMStoreFloat4(&topRight, XMVectorAdd(XMVectorAdd(bottomLeft4, dx), dy));

      // vertices.push_back( { bottomLeft, n, {1.f, 0.f, 0.f, 1.f}  });
      // vertices.push_back( { bottomRight, n, {0.5f, 1.f, 0.f, 1.f} });
      // vertices.push_back( { topLeft, n, {0.5f, 0.f, 1.f, 1.f} } );
      // vertices.push_back( { topRight, n, {0.f, 1.f, 0.f, 1.f} } );

      vertices.push_back({bottomLeft, n, {1.f, 0.f, 0.f, 1.f}});
      vertices.push_back({bottomRight, n, {1.f, 0.f, 0.f, 1.f}});
      vertices.push_back({topLeft, n, {1.f, 0.f, 0.f, 1.f}});
      vertices.push_back({topRight, n, {1.f, 0.f, 0.f, 1.f}});
    }
    void enqueueVertices(std::vector<Vertex> &vertices, std::vector<UINT> &indices) {

      XMVECTOR current = m_topLeftFront;
      enqueueQuad(vertices, indices, current, {m_size, 0, 0}, {0, m_size, 0}, false);
      enqueueQuad(vertices, indices, current, {m_size, 0, 0}, {0, 0, m_size}, true);
      enqueueQuad(vertices, indices, current, {0, m_size, 0}, {0, 0, m_size}, false);

      current = XMVectorAdd(current, XMVectorSet(m_size, m_size, m_size, .0f));
      enqueueQuad(vertices, indices, current, {-m_size, 0, 0}, {0, -m_size, 0}, true);
      enqueueQuad(vertices, indices, current, {-m_size, 0, 0}, {0, 0, -m_size}, false);
      enqueueQuad(vertices, indices, current, {0, -m_size, 0}, {0, 0, -m_size}, true);
    }
    void split(std::vector<Cube> &cubes) {
      float size = m_size / 3.f;
      XMVECTOR topLeftFront = m_topLeftFront;
      for (int x = 0; x < 3; x++) {
        topLeftFront.m128_f32[0] = m_topLeftFront.m128_f32[0] + static_cast<float>(x) * size;
        for (int y = 0; y < 3; y++) {
          if (x == 1 && y == 1)
            continue;
          topLeftFront.m128_f32[1] = m_topLeftFront.m128_f32[1] + static_cast<float>(y) * size;
          for (int z = 0; z < 3; z++) {
            if (x == 1 && z == 1)
              continue;
            if (y == 1 && z == 1)
              continue;

            topLeftFront.m128_f32[2] = m_topLeftFront.m128_f32[2] + static_cast<float>(z) * size;
            cubes.push_back({topLeftFront, size});
          }
        }
      }
    }

    void splitProb(std::vector<Cube> &cubes, float prob) {

      float size = m_size / 3.f;
      XMVECTOR topLeftFront = m_topLeftFront;
      for (int x = 0; x < 3; x++) {
        topLeftFront.m128_f32[0] = m_topLeftFront.m128_f32[0] + static_cast<float>(x) * size;
        for (int y = 0; y < 3; y++) {
          topLeftFront.m128_f32[1] = m_topLeftFront.m128_f32[1] + static_cast<float>(y) * size;
          for (int z = 0; z < 3; z++) {
            float sample = rand() / static_cast<float>(RAND_MAX);
            if (sample > prob)
              continue;
            topLeftFront.m128_f32[2] = m_topLeftFront.m128_f32[2] + static_cast<float>(z) * size;
            cubes.push_back({topLeftFront, size});
          }
        }
      }
    }
  };

  XMVECTOR orig = XMVectorSet(-0.5f, -0.5f, -0.5f, 1.0f);

  Cube cube(orig, 1.f);

  std::vector<Cube> cubes1 = {cube};
  std::vector<Cube> cubes2 = {};

  auto previous = &cubes1;
  auto next = &cubes2;

  for (int i = 0; i < level; i++) {
    for (Cube &c : *previous) {
      if (probability < 0.f)
        c.split(*next);
      else
        c.splitProb(*next, 20.f / 27.f);
    }
    auto temp = previous;
    previous = next;
    next = temp;
    next->clear();
  }

  outputVertices.reserve(24 * previous->size());
  outputIndices.reserve(24 * previous->size());
  for (Cube &c : *previous) {
    c.enqueueVertices(outputVertices, outputIndices);
  }
}