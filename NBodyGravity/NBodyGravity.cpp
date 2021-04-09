#include "d3dUtils.h"
#include "D3D12App.h"
#include "MeshBuffer.h"
#include "Texture.h"
#include "UploadBuffer.h"
#include "Camera.h"
#include <DirectXColors.h>
#include "RootSignatureGenerator.h"

static D3D12RendererContext *CreateNBodyGravityApp(HINSTANCE hInstance);

int main() {

    HRESULT hr;

    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    D3D12RendererContext *pTheApp = CreateNBodyGravityApp(NULL);
    if (FAILED(hr = pTheApp->Initialize())) {
        SAFE_DELETE(pTheApp);
        return hr;
    }

    hr = pTheApp->Run();
    SAFE_DELETE(pTheApp);

    return hr;
}

using namespace DirectX;
using namespace Microsoft::WRL;

struct ParticlePos {
    XMFLOAT3 Pos;
    FLOAT    Mass;
    XMFLOAT3 Vel;
    FLOAT    AccelScalar;
};

struct ParticelParams {
    int ParticleCount;
    int DimX;
    float DeltaTime;
    float Dumping;
};

struct DrawPassConstants {
    XMFLOAT4X4 ViewProj;
    XMFLOAT4X4 InvView;
};


class FrameResources {
public:
    ~FrameResources() {
        SAFE_RELEASE(CmdListAlloc);
    }

    HRESULT CreateCommandAllocator(ID3D12Device *pd3dDevice) {
        HRESULT hr;

        V_RETURN(pd3dDevice->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&CmdListAlloc)
        ));

        return hr;
    }

    static HRESULT CreateBuffers(ID3D12Device *pd3dDevice, int NumFrames) {
        HRESULT hr;

        V_RETURN(ParticleParamCB.CreateBuffer(pd3dDevice, NumFrames, sizeof(ParticelParams), TRUE));
        V_RETURN(DrawParticleCB.CreateBuffer(pd3dDevice, NumFrames, sizeof(DrawPassConstants), TRUE));
        return hr;
    }

    ID3D12CommandAllocator *CmdListAlloc;
    static UploadBuffer ParticleParamCB;
    static UploadBuffer DrawParticleCB;

    INT64 FencePoint = 0;
};

UploadBuffer FrameResources::ParticleParamCB;
UploadBuffer FrameResources::DrawParticleCB;

class NBodyGravityApp : public D3D12RendererContext {
public:
    NBodyGravityApp(HINSTANCE hInstance);
    ~NBodyGravityApp();

    HRESULT Initialize();

private:
    void PostInitialize();

    virtual void Update(float fTime, float fElapsedTime) override;
    virtual void RenderFrame(float fTime, float fElapsedTime) override;

    virtual LRESULT OnResize();
    virtual LRESULT OnMouseEvent(UINT uMsg, WPARAM wParam, int x, int y);
    virtual LRESULT OnKeyEvent(WPARAM wParam, LPARAM lParam);

    HRESULT LoadTexture();
    HRESULT CreateParticleBuffers();
    HRESULT CreateParticleDrawBuffer();
    HRESULT CreatePSOs();
    HRESULT CreateCbvSrvUavHeap();
    HRESULT CreateConstantBuffers();

    void LoadParticles(ParticlePos *pParticles, XMVECTOR Center, XMVECTOR Velocity,
        float fSpread, UINT NumParticles);

    void UpdatePaticles(FrameResources *pFrameResources);

    enum { MAX_PARTICLES = 10000 };

    ID3D12Resource *m_pParticleBufferUpload = nullptr;
    ID3D12Resource *m_pParticleBuffer1 = nullptr;
    ID3D12Resource *m_pParticleBuffer2 = nullptr;

    Texture *m_pParticleDiffuseMap = nullptr;

    MeshBuffer *m_pParticleDrawBuffer = nullptr;

    ID3D12DescriptorHeap *m_pCbvUavSrvHeap = nullptr;

    int m_iParticleBuffersSwapState = 0;
    ID3D12RootSignature *m_pCSRootSignaure = nullptr;
    ID3D12RootSignature *m_pDrawRootSignature = nullptr;
    ID3D12PipelineState *m_pCSPSO = nullptr;
    ID3D12PipelineState *m_pDrawPSO = nullptr;

    /// Ping-Pong the particle postion buffers.
    D3D12_GPU_DESCRIPTOR_HANDLE m_hParticleSrvBuffer1;
    D3D12_GPU_DESCRIPTOR_HANDLE m_hParticleUavBuffer1;
    D3D12_GPU_DESCRIPTOR_HANDLE m_hParticleSrvBuffer2;
    D3D12_GPU_DESCRIPTOR_HANDLE m_hParticleUavBuffer2;
    D3D12_GPU_DESCRIPTOR_HANDLE m_hDiffuseMapSrv;

    FrameResources m_FrameResources[3];
    int m_iCurrentFrameIndex = 0;

    UploadBuffer m_CSCB;
    UploadBuffer m_DrawCB;

    FLOAT m_fSpread = 400.0f;

    SimpleOrbitCamera m_Camera;
    POINT m_ptLastMousePos;
};


D3D12RendererContext *CreateNBodyGravityApp(HINSTANCE hInstance) {
    return new NBodyGravityApp(hInstance);
}

NBodyGravityApp::NBodyGravityApp(HINSTANCE hInstance)
    : D3D12RendererContext(hInstance) {
}

NBodyGravityApp::~NBodyGravityApp() {
    SAFE_RELEASE(m_pParticleBufferUpload);
    SAFE_RELEASE(m_pParticleBuffer1);
    SAFE_RELEASE(m_pParticleBuffer2);
    SAFE_RELEASE(m_pParticleDiffuseMap);
    SAFE_RELEASE(m_pParticleDrawBuffer);
    SAFE_RELEASE(m_pCbvUavSrvHeap);
    SAFE_RELEASE(m_pCSRootSignaure);
    SAFE_RELEASE(m_pDrawRootSignature);
    SAFE_RELEASE(m_pCSPSO);
    SAFE_RELEASE(m_pDrawPSO);
}

HRESULT NBodyGravityApp::Initialize() {
    HRESULT hr;

    V_RETURN(__super::Initialize());

    /// Reset the command list to prepare for initalization commands.
    m_pd3dCommandList->Reset(m_pd3dDirectCmdAlloc, nullptr);

    V_RETURN(CreateParticleBuffers());
    V_RETURN(CreateParticleDrawBuffer());
    V_RETURN(CreatePSOs());
    V_RETURN(CreateConstantBuffers());
    V_RETURN(LoadTexture());
    V_RETURN(CreateCbvSrvUavHeap());

    // Execute the initialization commands.
    m_pd3dCommandList->Close();
    ID3D12CommandList *cmdList[] = { m_pd3dCommandList };
    m_pd3dCommandQueue->ExecuteCommandLists(1, cmdList);

    FlushCommandQueue();

    PostInitialize();

    m_Camera.SetViewParams(
        {-m_fSpread*2, m_fSpread*4, -m_fSpread*3},
        {.0f, .0f, .0f}
    );
    m_Camera.SetProjMatrix(XM_PIDIV4, GetAspectRatio(), 10.0f, 500000.0f);

    return hr;
}

void NBodyGravityApp::PostInitialize() {
    m_pParticleDrawBuffer->DisposeUploaders();
    m_pParticleDiffuseMap->DisposeUploaders();
}

HRESULT NBodyGravityApp::LoadTexture() {

    HRESULT hr;

    m_pParticleDiffuseMap = new Texture("ParticleDiffuseMap");
    V_RETURN(m_pParticleDiffuseMap->CreateTextureFromDDSFile(m_pd3dDevice, m_pd3dCommandList,
        L"Media/NBodyGravity/Textures/particle.dds"));

    return hr;
}

HRESULT NBodyGravityApp::CreateParticleBuffers() {
    HRESULT hr;
    UINT uBufferSize;

    srand(GetTickCount());

    ParticlePos *pParticles = new ParticlePos[MAX_PARTICLES];
    float fCenterSpread = m_fSpread * 0.5f;
    LoadParticles(pParticles,
        XMVectorSet(fCenterSpread, .0f, .0f, .0f),
        XMVectorSet(.0f, .0f, -20.0f, .0f),
        m_fSpread,
        MAX_PARTICLES / 2);
    LoadParticles(&pParticles[MAX_PARTICLES / 2],
        XMVectorSet(-fCenterSpread, .0f, .0f, .0f),
        XMVectorSet(.0f, .0f, 20.0f, .0f),
        m_fSpread,
        MAX_PARTICLES / 2);

    uBufferSize = sizeof(ParticlePos) * MAX_PARTICLES;

    if (!m_pParticleBuffer1) {
        V(m_pd3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&m_pParticleBuffer1)));
        if (FAILED(hr)) {
            SAFE_DELETE_ARRAY(pParticles);
            return hr;
        }

        V(m_pd3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uBufferSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&m_pParticleBuffer2)));
        if (FAILED(hr)) {
            SAFE_DELETE_ARRAY(pParticles);
            return hr;
        }

        //
        // In order to copy CPU memory data into our default buffer, we need to create
        // an intermediate upload heap. 
        //
        V(m_pd3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(uBufferSize),
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_pParticleBufferUpload)));
        if (FAILED(hr)) {
            SAFE_DELETE_ARRAY(pParticles);
            return hr;
        }
    } else {

        hr = S_OK;

        m_pd3dCommandList->ResourceBarrier(
            1,
            &CD3DX12_RESOURCE_BARRIER::Transition(m_pParticleBuffer1,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                D3D12_RESOURCE_STATE_COPY_DEST));
    }

    D3D12_SUBRESOURCE_DATA initData;
    initData.pData = pParticles;
    initData.RowPitch = uBufferSize;
    initData.SlicePitch = 1;

    UpdateSubresources<1>(m_pd3dCommandList,
        m_pParticleBuffer1, m_pParticleBufferUpload, 0, 0, 1, &initData);
    SAFE_DELETE_ARRAY(pParticles);

    m_pd3dCommandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_pParticleBuffer1,
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_GENERIC_READ));

    return hr;
}

static float RPercent() {
    float ret = (float)( ( rand() % 10000 ) - 5000 );
    return ret / 5000.0f;
}

void NBodyGravityApp::LoadParticles(ParticlePos *pParticles, XMVECTOR Center,
    XMVECTOR Velocity, float Spread, UINT NumParticles) {

    XMVECTOR Delta;
    XMVECTOR Pos;
    XMFLOAT3 Vel;

    XMStoreFloat3(&Vel, Velocity);

    for (UINT i = 0; i < NumParticles; ++i) {

        Delta = XMVectorSet(Spread, Spread, Spread, 0.0f);

        while (XMVectorGetX(XMVector3LengthSq(Delta)) > Spread * Spread) {
            Delta = XMVectorSet(
                    RPercent()*Spread,
                    RPercent()*Spread,
                    RPercent()*Spread,
                    0.0f);
        }

        Pos = XMVectorAdd(Center, Delta);
        XMStoreFloat3(&pParticles[i].Pos, Pos);

        pParticles[i].Mass = (RPercent() + 3.0f) / 2.0f;

        pParticles[i].Vel = Vel;
    }
}

HRESULT NBodyGravityApp::CreateParticleDrawBuffer() {
    struct Vertex {
        XMFLOAT4 Color;
    };
    HRESULT hr;

    Vertex *pVertices = new Vertex[MAX_PARTICLES];

    for (UINT i = 0; i < MAX_PARTICLES; ++i) {
        pVertices[i].Color = XMFLOAT4(1, 1, 0.5, 1);
    }

    V_RETURN(CreateMeshBuffer(&m_pParticleDrawBuffer));
    m_pParticleDrawBuffer->CreateVertexBuffer(
      m_pd3dDevice,
      m_pd3dCommandList,
      pVertices,
      MAX_PARTICLES,
      sizeof(Vertex)
    );
    SAFE_DELETE(pVertices);

    return hr;
}

HRESULT NBodyGravityApp::CreatePSOs() {
    HRESULT hr;
    UINT compileFlags = D3DCOMPILE_ENABLE_STRICTNESS;
    ComPtr<ID3DBlob> CSBuffer, VSBuffer, GSBuffer, PSBuffer;
    ComPtr<ID3DBlob> ErrorBuffer;

#if defined(_DEBUG)
    compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    V_RETURN(d3dUtils::CompileShaderFromFile(L"Shaders/NBodyGravityCS.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "NBodyGravityCS", "cs_5_0", compileFlags, 0, CSBuffer.GetAddressOf(), ErrorBuffer.GetAddressOf()));
    if (ErrorBuffer) {
        DXOutputDebugStringA((const char *)ErrorBuffer->GetBufferPointer());
        V_RETURN(E_FAIL);
    }

    V_RETURN(d3dUtils::CompileShaderFromFile(L"Shaders/ParticleDraw.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "VS", "vs_5_0", compileFlags, 0, VSBuffer.GetAddressOf(), ErrorBuffer.GetAddressOf()));
    if (ErrorBuffer) {
        DXOutputDebugStringA((const char *)ErrorBuffer->GetBufferPointer());
        V_RETURN(E_FAIL);
    }

    V_RETURN(d3dUtils::CompileShaderFromFile(L"Shaders/ParticleDraw.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "GS", "gs_5_0", compileFlags, 0, GSBuffer.GetAddressOf(), ErrorBuffer.GetAddressOf()));
    if (ErrorBuffer) {
        DXOutputDebugStringA((const char *)ErrorBuffer->GetBufferPointer());
        V_RETURN(E_FAIL);
    }

    V_RETURN(d3dUtils::CompileShaderFromFile(L"Shaders/ParticleDraw.hlsl", nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        "PS", "ps_5_0", compileFlags, 0, PSBuffer.GetAddressOf(), ErrorBuffer.GetAddressOf()));
    if (ErrorBuffer) {
        DXOutputDebugStringA((const char *)ErrorBuffer->GetBufferPointer());
        V_RETURN(E_FAIL);
    }

    /// Root signatures.
    RootSignatureGenerator signatureGen;

    signatureGen.AddConstBufferView(0, 0);
    signatureGen.AddDescriptorTable(
      { RootSignatureGenerator::ComposeShaderResourceViewRange(1, 0) });
    signatureGen.AddDescriptorTable(
      { RootSignatureGenerator::ComposeUnorderedAccessViewRange(1, 0) });

    signatureGen.Generate(m_pd3dDevice, D3D12_ROOT_SIGNATURE_FLAG_NONE, &m_pCSRootSignaure);

    D3D12_COMPUTE_PIPELINE_STATE_DESC cpsoDess = {};
    cpsoDess.pRootSignature = m_pCSRootSignaure;
    cpsoDess.CS = {
        CSBuffer->GetBufferPointer(),
        CSBuffer->GetBufferSize()
    };
    V_RETURN(m_pd3dDevice->CreateComputePipelineState(&cpsoDess, IID_PPV_ARGS(&m_pCSPSO)));

    signatureGen.Reset();
    signatureGen.AddConstBufferView(0, 0, D3D12_SHADER_VISIBILITY_GEOMETRY);
    signatureGen.AddDescriptorTable(
      { RootSignatureGenerator::ComposeShaderResourceViewRange(1, 0) },
      D3D12_SHADER_VISIBILITY_VERTEX);
    signatureGen.AddDescriptorTable(
      { RootSignatureGenerator::ComposeShaderResourceViewRange(1, 1) },
      D3D12_SHADER_VISIBILITY_PIXEL);

    signatureGen.AddStaticSamples(
      {
        RootSignatureGenerator::ComposeStaticSampler(
            0,
            D3D12_FILTER_MIN_MAG_MIP_LINEAR,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
            .0f,
            16)
      });

    signatureGen.Generate(m_pd3dDevice, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT,
      &m_pDrawRootSignature);

    /// Layout.
    D3D12_INPUT_ELEMENT_DESC aInputLayout[] = {
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };

    CD3DX12_DEPTH_STENCIL_DESC ddsDesc{D3D12_DEFAULT};
    CD3DX12_BLEND_DESC blendDesc{D3D12_DEFAULT};

    ddsDesc.DepthEnable = FALSE;
    ddsDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = 0x07;

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_pDrawRootSignature;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    psoDesc.InputLayout = { aInputLayout, 1 };
    psoDesc.VS = {
        VSBuffer->GetBufferPointer(),
        VSBuffer->GetBufferSize()
    };
    psoDesc.GS = {
        GSBuffer->GetBufferPointer(),
        GSBuffer->GetBufferSize()
    };
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.PS = {
        PSBuffer->GetBufferPointer(),
        PSBuffer->GetBufferSize()
    };
    psoDesc.DepthStencilState = ddsDesc;
    psoDesc.BlendState = blendDesc;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = m_BackBufferFormat;
    psoDesc.DSVFormat = m_DepthStencilBufferFormat;
    psoDesc.SampleDesc.Count = m_aDeviceConfig.MsaaEnabled ? 4 : 1;
    psoDesc.SampleDesc.Quality = m_aDeviceConfig.MsaaEnabled ? m_aDeviceConfig.MsaaQaulityLevel - 1 : 0;
    psoDesc.SampleMask = UINT_MAX;

    V_RETURN(m_pd3dDevice->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pDrawPSO)));

    return hr;
}

HRESULT NBodyGravityApp::CreateConstantBuffers() {
    HRESULT hr;
    int i;

    for (i = 0; i < 3; ++i) {
        V_RETURN(m_FrameResources[i].CreateCommandAllocator(m_pd3dDevice));
    }

    V_RETURN(FrameResources::CreateBuffers(m_pd3dDevice, 3));

    return hr;
}

HRESULT NBodyGravityApp::CreateCbvSrvUavHeap() {
    HRESULT hr;
    UINT uNumDescriptors = 5;
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc;
    CD3DX12_CPU_DESCRIPTOR_HANDLE heapHandle, heapHandle2;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;
    CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle;

    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NodeMask = 0;
    heapDesc.NumDescriptors = uNumDescriptors;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    V_RETURN(m_pd3dDevice->CreateDescriptorHeap(
        &heapDesc,
        IID_PPV_ARGS(&m_pCbvUavSrvHeap)
    ));

    heapHandle = m_pCbvUavSrvHeap->GetCPUDescriptorHandleForHeapStart();

    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Format = DXGI_FORMAT_UNKNOWN;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.StructureByteStride = sizeof(ParticlePos);
    srvDesc.Buffer.NumElements = MAX_PARTICLES;
    srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

    ZeroMemory(&uavDesc, sizeof(uavDesc));
    uavDesc.Format = DXGI_FORMAT_UNKNOWN;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uavDesc.Buffer.StructureByteStride = sizeof(ParticlePos);
    uavDesc.Buffer.NumElements = MAX_PARTICLES;
    uavDesc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;

    heapHandle2 = heapHandle;
    m_pd3dDevice->CreateShaderResourceView(m_pParticleBuffer1,
        &srvDesc, heapHandle2);

    heapHandle2.Offset(1, m_uCbvSrvUavDescriptorSize);
    m_pd3dDevice->CreateUnorderedAccessView(m_pParticleBuffer1,
        nullptr, &uavDesc, heapHandle2);

    heapHandle2.Offset(1, m_uCbvSrvUavDescriptorSize);
    m_pd3dDevice->CreateShaderResourceView(m_pParticleBuffer2,
        &srvDesc, heapHandle2);

    heapHandle2.Offset(1, m_uCbvSrvUavDescriptorSize);
    m_pd3dDevice->CreateUnorderedAccessView(m_pParticleBuffer2,
        nullptr, &uavDesc, heapHandle2);

    heapHandle2.Offset(1, m_uCbvSrvUavDescriptorSize);
    m_pd3dDevice->CreateShaderResourceView(m_pParticleDiffuseMap->Resource,
        nullptr, heapHandle2);

    gpuHandle = m_pCbvUavSrvHeap->GetGPUDescriptorHandleForHeapStart();

    m_hParticleSrvBuffer1 = gpuHandle;
    gpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);
    m_hParticleUavBuffer1 = gpuHandle;
    gpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);
    m_hParticleSrvBuffer2 = gpuHandle;
    gpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);
    m_hParticleUavBuffer2 = gpuHandle;
    gpuHandle.Offset(1, m_uCbvSrvUavDescriptorSize);
    m_hDiffuseMapSrv = gpuHandle;

    return hr;
}

void NBodyGravityApp::Update(float fTime, float fElapsedTime) {

    FrameResources *pFrameResources;

    m_iCurrentFrameIndex = (m_iCurrentFrameIndex + 1) % 3;
    pFrameResources = &m_FrameResources[m_iCurrentFrameIndex];

    /// Sychronize it.
    if (pFrameResources->FencePoint != 0 && m_pd3dFence->GetCompletedValue() <
        pFrameResources->FencePoint) {

        if (!m_hFenceEvent)
            m_hFenceEvent = CreateEventEx(NULL, NULL, 0, EVENT_ALL_ACCESS);

        m_pd3dFence->SetEventOnCompletion(pFrameResources->FencePoint, m_hFenceEvent);
        WaitForSingleObject(m_hFenceEvent, INFINITE);
    }

    /// Update the buffers up to this time point.

    ParticelParams pp;

    pp.ParticleCount = MAX_PARTICLES;
    pp.DimX = (int)ceil(MAX_PARTICLES / 128.0);
    pp.DeltaTime = 0.1f;
    pp.Dumping = 0.99f;

    FrameResources::ParticleParamCB.CopyData(&pp, sizeof(pp), m_iCurrentFrameIndex);

    DrawPassConstants dpc;
    XMMATRIX M;

    M = XMLoadFloat4x4(&m_Camera.GetViewProj());
    XMStoreFloat4x4(&dpc.ViewProj, XMMatrixTranspose(M));

    M = XMLoadFloat4x4(&m_Camera.GetView());
    M = XMMatrixInverse(nullptr, M);
    XMStoreFloat4x4(&dpc.InvView, XMMatrixTranspose(M));

    FrameResources::DrawParticleCB.CopyData(&dpc, sizeof(dpc), m_iCurrentFrameIndex);
}

void NBodyGravityApp::UpdatePaticles(FrameResources *pFrameResources) {

    UINT cbCBByteSize = d3dUtils::CalcConstantBufferByteSize(sizeof(ParticelParams));
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress;

    int DimX = (int)ceilf(MAX_PARTICLES / 128.0f);

    cbAddress = FrameResources::ParticleParamCB.GetConstBufferAddress();
    cbAddress += m_iCurrentFrameIndex * cbCBByteSize;

    m_pd3dCommandList->SetComputeRootSignature(m_pCSRootSignaure);
    m_pd3dCommandList->SetComputeRootConstantBufferView(0, cbAddress);
    m_pd3dCommandList->SetComputeRootDescriptorTable(1, m_hParticleSrvBuffer1);
    m_pd3dCommandList->SetComputeRootDescriptorTable(2, m_hParticleUavBuffer2);

    m_pd3dCommandList->Dispatch(DimX, 1, 1);

    std::swap(m_pParticleBuffer1, m_pParticleBuffer2);
    std::swap(m_hParticleSrvBuffer1, m_hParticleSrvBuffer2);
    std::swap(m_hParticleUavBuffer1, m_hParticleUavBuffer2);
    m_iParticleBuffersSwapState ^= 1;

    D3D12_RESOURCE_BARRIER Barriers[] = {
        CD3DX12_RESOURCE_BARRIER::Transition(m_pParticleBuffer1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 
        D3D12_RESOURCE_STATE_GENERIC_READ),
        CD3DX12_RESOURCE_BARRIER::Transition(m_pParticleBuffer2, D3D12_RESOURCE_STATE_GENERIC_READ,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
    };

    m_pd3dCommandList->ResourceBarrier(2, 
        Barriers);
}

void NBodyGravityApp::RenderFrame(float fTime, float fElapsedTime) {
    HRESULT hr;
    FrameResources *pFrameResources = &m_FrameResources[m_iCurrentFrameIndex];
    D3D12_GPU_VIRTUAL_ADDRESS cbAddress;
    UINT cbCBByteSize = d3dUtils::CalcConstantBufferByteSize(sizeof(DrawPassConstants));

    V(pFrameResources->CmdListAlloc->Reset());
    V(m_pd3dCommandList->Reset(pFrameResources->CmdListAlloc, m_pCSPSO));

    m_pd3dCommandList->SetDescriptorHeaps(1, &m_pCbvUavSrvHeap);

    UpdatePaticles(pFrameResources);

    /// Render the particles.
    m_pd3dCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET
    ));

    m_pd3dCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::Black,
        1, &m_ScissorRect);

    m_pd3dCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), FALSE, nullptr);

    m_pd3dCommandList->SetGraphicsRootSignature(m_pDrawRootSignature);
    m_pd3dCommandList->SetPipelineState(m_pDrawPSO);

    m_pd3dCommandList->RSSetViewports(1, &m_ScreenViewport);
    m_pd3dCommandList->RSSetScissorRects(1, &m_ScissorRect);

    cbAddress = FrameResources::DrawParticleCB.GetConstBufferAddress();
    cbAddress += m_iCurrentFrameIndex * cbCBByteSize;

    m_pd3dCommandList->SetGraphicsRootConstantBufferView(0, cbAddress);
    m_pd3dCommandList->SetGraphicsRootDescriptorTable(1, m_hParticleSrvBuffer1);
    m_pd3dCommandList->SetGraphicsRootDescriptorTable(2, m_hDiffuseMapSrv);

    m_pd3dCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
    m_pd3dCommandList->IASetVertexBuffers(0, 1, &m_pParticleDrawBuffer->VertexBufferView());
    m_pd3dCommandList->DrawInstanced(MAX_PARTICLES, 1, 0, 0);

    m_pd3dCommandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
            D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    // Done recording commands.
    m_pd3dCommandList->Close();

    // Add the command list to the queue for execution.
    ID3D12CommandList *cmdList[] = { m_pd3dCommandList };
    m_pd3dCommandQueue->ExecuteCommandLists(1, cmdList);

    Present();

    pFrameResources->FencePoint = ++m_FenceCount;

    m_pd3dCommandQueue->Signal(m_pd3dFence, m_FenceCount);
}

LRESULT NBodyGravityApp::OnResize() {
    __super::OnResize();

    m_Camera.SetProjMatrix(XM_PIDIV4, GetAspectRatio(), 10.0f, 500000.0f);

    return 0;
}

LRESULT NBodyGravityApp::OnMouseEvent(UINT uMsg, WPARAM wParam, int x, int y) {
    switch (uMsg) {
    case WM_LBUTTONDOWN:
        m_ptLastMousePos.x = x;
        m_ptLastMousePos.y = y;
        SetCapture(this->m_hMainWnd);
        break;
    case WM_LBUTTONUP:
        ReleaseCapture();
    case WM_MOUSEMOVE:
        if (wParam & MK_LBUTTON) {
            // Make each pixel correspond to a quarter of a degree.
            float dx = XMConvertToRadians(0.25f*static_cast<float>(x - m_ptLastMousePos.x));
            float dy = XMConvertToRadians(0.25f*static_cast<float>(y - m_ptLastMousePos.y));
            m_Camera.Rotate(dx, dy);
        } else if (wParam & MK_RBUTTON) {
            // Make each pixel correspond to 0.005 unit in the scene.
            float dx = 5.0f*static_cast<float>(x - m_ptLastMousePos.x);
            float dy = 5.0f*static_cast<float>(y - m_ptLastMousePos.y);

            m_Camera.Scale(dx - dy, 10.0f, 500000.0f);
        }

        m_ptLastMousePos.x = x;
        m_ptLastMousePos.y = y;
        break;
    }

    return 0;
}

LRESULT NBodyGravityApp::OnKeyEvent(WPARAM wParam, LPARAM lParam) {
    if (wParam == 'R' || wParam == 'r') {
        /// Reset the particle buffers.

        m_pd3dDirectCmdAlloc->Reset();
        m_pd3dCommandList->Reset(m_pd3dDirectCmdAlloc, nullptr);

        if (m_iParticleBuffersSwapState) {
            std::swap(m_pParticleBuffer1, m_pParticleBuffer2);
            std::swap(m_hParticleSrvBuffer1, m_hParticleSrvBuffer2);
            std::swap(m_hParticleUavBuffer1, m_hParticleUavBuffer2);

            m_iParticleBuffersSwapState ^= 1;
        }

        CreateParticleBuffers();

        m_pd3dCommandList->Close();

        ID3D12CommandList *cmdList[] = { m_pd3dCommandList };
        m_pd3dCommandQueue->ExecuteCommandLists(1, cmdList);

        FlushCommandQueue();
    }

    return 0;
}

