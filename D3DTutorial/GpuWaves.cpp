#include "GpuWaves.h"

GpuWaves::GpuWaves(
    ID3D12Device *pd3dDevice,
    ID3D12GraphicsCommandList *pCommandList,
    int m,
    int n,
    float dx,
    float dt,
    float speed,
    float damping
) {
    m_pd3dDevice = pd3dDevice;
    m_pd3dDevice->AddRef();

    m_uNumRows = m;
    m_uNumCols = n;

    _ASSERT(m * n % 256 == 0);
    m_uVertexCount = m * n;
    m_uTriangleCount = (m - 1) * (n - 1) * 2;

    m_fTimeStep = dt;
    m_fSpatialStep = dx;

    float d = damping*dt + 2.0f;
    float e = (speed*speed)*(dt*dt) / (dx*dx);
    m_fK[0] = (damping*dt - 2.0f) / d;
    m_fK[1] = (4.0f - 8.0f*e) / d;
    m_fK[2] = (2.0f*e) / d;

    m_pPrevSol = nullptr;
    m_pCurrSol = nullptr;
    m_pNextSol = nullptr;
    m_pPrevUploadBuffer = nullptr;
    m_pCurrUploadBuffer = nullptr;

    BuildResources(pCommandList);
}

GpuWaves::~GpuWaves()
{
    SAFE_RELEASE(m_pd3dDevice);
    SAFE_RELEASE(m_pPrevSol);
    SAFE_RELEASE(m_pCurrSol);
    SAFE_RELEASE(m_pNextSol);
    SAFE_RELEASE(m_pPrevUploadBuffer);
    SAFE_RELEASE(m_pCurrUploadBuffer);
}

UINT GpuWaves::VertexCount() const {
    return m_uVertexCount;
}

UINT GpuWaves::TriangleCount() const {
    return m_uTriangleCount;
}

UINT GpuWaves::ColumnCount() const {
    return m_uNumCols;
}

UINT GpuWaves::RowCount() const {
    return m_uNumRows;
}

FLOAT GpuWaves::SpatialStep() const {
    return m_fSpatialStep;
}

UINT GpuWaves::DescriptorHeapCount() const {
    return 6;
}

HRESULT GpuWaves::BuildResources(ID3D12GraphicsCommandList *pd3dCommandList) {
    HRESULT hr;

    D3D12_RESOURCE_DESC texDesc;
    ID3D12Resource **pSols[] = { &m_pPrevSol, &m_pCurrSol, &m_pNextSol };
    int i;

    // All the textures for the wave simulation will be bound as a shader resource and
    // unordered access view at some point since we ping-pong the buffers.
    ZeroMemory(&texDesc, sizeof(texDesc));
    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = m_uNumRows;
    texDesc.Height = m_uNumCols;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R32_FLOAT;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    for (i = 0; i < 3; ++i) {
        V_RETURN(m_pd3dDevice->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &texDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS(pSols[i])
        ));
    }

    //
    // In order to copy CPU memory data into our default buffer, we need to create
    // an intermediate upload heap. 
    //

    const UINT num2DSubresources = texDesc.DepthOrArraySize * texDesc.MipLevels;
    const UINT64 uploadBufferSize = GetRequiredIntermediateSize(m_pCurrSol, 0, num2DSubresources);

    V_RETURN(m_pd3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_pPrevUploadBuffer)
    ));

    V_RETURN(m_pd3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_pCurrUploadBuffer)
    ));


    // Describe the data we want to copy into the default buffer.
    FLOAT *pInitBuffer = new FLOAT[m_uNumRows * m_uNumCols];
    memset(pInitBuffer, 0, m_uNumCols * m_uNumCols * sizeof(FLOAT));

    D3D12_SUBRESOURCE_DATA initData;
    initData.pData = pInitBuffer;
    initData.RowPitch = m_uNumCols * sizeof(FLOAT);
    initData.SlicePitch = initData.RowPitch * m_uNumRows;

    //
    // Schedule to copy the data to the default resource, and change states.
    // Note that mCurrSol is put in the GENERIC_READ state so it can be 
    // read by a shader.
    //
    pd3dCommandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_pPrevSol,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
    UpdateSubresources(pd3dCommandList, m_pPrevSol, m_pPrevUploadBuffer, 0, 0, num2DSubresources,
        &initData);
    pd3dCommandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_pPrevSol,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

    pd3dCommandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_pCurrSol,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST));
    UpdateSubresources(pd3dCommandList, m_pCurrSol, m_pCurrUploadBuffer, 0, 0, num2DSubresources,
        &initData);
    pd3dCommandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_pCurrSol,
            D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_GENERIC_READ));

    pd3dCommandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_pNextSol,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

    delete[] pInitBuffer;

    return hr;
}

HRESULT GpuWaves::BuildDescriptors(
    D3D12_CPU_DESCRIPTOR_HANDLE hCpuHeapStart,
    D3D12_GPU_DESCRIPTOR_HANDLE hGpuHeapStart,
    UINT uCbvSrvUavDescriptorIncrementSize
) {
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc;
    CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDescriptor;
    CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuDescriptor;

    ZeroMemory(&srvDesc, sizeof(srvDesc));
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;

    ZeroMemory(&uavDesc, sizeof(uavDesc));
    uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uavDesc.Texture2D.MipSlice = 0;

    hCpuDescriptor = hCpuHeapStart;
    hGpuDescriptor = hGpuHeapStart;
    m_pd3dDevice->CreateShaderResourceView(m_pPrevSol, &srvDesc,
        hCpuDescriptor);
    m_pd3dDevice->CreateShaderResourceView(m_pCurrSol, &srvDesc,
        hCpuDescriptor.Offset(1, uCbvSrvUavDescriptorIncrementSize));
    m_pd3dDevice->CreateShaderResourceView(m_pNextSol, &srvDesc,
        hCpuDescriptor.Offset(1, uCbvSrvUavDescriptorIncrementSize));

    m_pd3dDevice->CreateUnorderedAccessView(m_pPrevSol, nullptr, &uavDesc,
        hCpuDescriptor.Offset(1, uCbvSrvUavDescriptorIncrementSize));
    m_pd3dDevice->CreateUnorderedAccessView(m_pCurrSol, nullptr, &uavDesc,
        hCpuDescriptor.Offset(1, uCbvSrvUavDescriptorIncrementSize));
    m_pd3dDevice->CreateUnorderedAccessView(m_pNextSol, nullptr, &uavDesc,
        hCpuDescriptor.Offset(1, uCbvSrvUavDescriptorIncrementSize));

    m_hPrevSolSrv = hGpuDescriptor;
    m_hCurrSolSrv = hGpuDescriptor.Offset(1, uCbvSrvUavDescriptorIncrementSize);
    m_hNextSolSrv = hGpuDescriptor.Offset(1, uCbvSrvUavDescriptorIncrementSize);
    m_hPrevSolUav = hGpuDescriptor.Offset(1, uCbvSrvUavDescriptorIncrementSize);
    m_hCurrSolUav = hGpuDescriptor.Offset(1, uCbvSrvUavDescriptorIncrementSize);
    m_hNextSolUav = hGpuDescriptor.Offset(1, uCbvSrvUavDescriptorIncrementSize);

    return S_OK;
}

void GpuWaves::InitCleanup() {
    SAFE_RELEASE(m_pPrevUploadBuffer);
    SAFE_RELEASE(m_pCurrUploadBuffer);
}

void GpuWaves::Update(
    FLOAT fTime,
    FLOAT fElapsedTime,
    ID3D12GraphicsCommandList *pd3dCommandList,
    ID3D12RootSignature *pRootSignature,
    ID3D12PipelineState *pso
) {
    static FLOAT t_base;

    t_base += fTime;

    if (t_base >= m_fTimeStep) {

        /// Reset the time step.
        t_base = 0.0f;

        // Only update the simulation at the specified time step.

        pd3dCommandList->SetPipelineState(pso);
        pd3dCommandList->SetComputeRootSignature(pRootSignature);

        /// Synchronize the resources.

        // Set the update constants.
        pd3dCommandList->SetComputeRoot32BitConstants(0, 3, m_fK, 0);

        pd3dCommandList->SetComputeRootDescriptorTable(1, m_hPrevSolSrv);
        pd3dCommandList->SetComputeRootDescriptorTable(2, m_hCurrSolSrv);
        pd3dCommandList->SetComputeRootDescriptorTable(3, m_hNextSolUav);

        pd3dCommandList->Dispatch(m_uNumCols / 16, m_uNumRows / 16, 1);

        //
        // Ping-pong buffers in preparation for the next update.
        // The previous solution is no longer needed and becomes the target of the next solution in the next update.
        // The current solution becomes the previous solution.
        // The next solution becomes the current solution.
        //
        D3D12_GPU_DESCRIPTOR_HANDLE hTemp = m_hPrevSolSrv;
        m_hPrevSolSrv = m_hCurrSolSrv;
        m_hCurrSolSrv = m_hNextSolSrv;
        m_hNextSolSrv = hTemp;

        hTemp = m_hPrevSolUav;
        m_hPrevSolUav = m_hCurrSolUav;
        m_hCurrSolUav = m_hNextSolUav;
        m_hNextSolUav = hTemp;

        ID3D12Resource *pTexTemp = m_pPrevSol;
        m_pPrevSol = m_pCurrSol;
        m_pCurrSol = m_pNextSol;
        m_pNextSol = pTexTemp;

        CD3DX12_RESOURCE_BARRIER syncBarrier[] = {
            CD3DX12_RESOURCE_BARRIER::Transition(m_pCurrSol,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ),
            CD3DX12_RESOURCE_BARRIER::Transition(m_pNextSol,
                D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
        };

        // The current solution needs to be able to be read by the vertex shader, so change its state to GENERIC_READ.
        pd3dCommandList->ResourceBarrier(2, syncBarrier);
    }
}

void GpuWaves::Disturb(
    ID3D12GraphicsCommandList *pd3dCommandList,
    ID3D12RootSignature *pRootSignature,
    ID3D12PipelineState *pso,
    UINT i, UINT j, FLOAT fMagnitude
) {

    pd3dCommandList->SetPipelineState(pso);
    pd3dCommandList->SetComputeRootSignature(pRootSignature);

    UINT uDisturbIndex[] = { i, j };
    pd3dCommandList->SetComputeRoot32BitConstants(0, 1, &fMagnitude, 3);
    pd3dCommandList->SetComputeRoot32BitConstants(0, 2, uDisturbIndex, 4);
    pd3dCommandList->SetComputeRootDescriptorTable(1, m_hCurrSolUav);

    pd3dCommandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_pCurrSol,
            D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));

    pd3dCommandList->Dispatch(1, 1, 1);

    pd3dCommandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_pCurrSol,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_GENERIC_READ));
}

D3D12_GPU_DESCRIPTOR_HANDLE GpuWaves::DisplacementMap() const {
    return m_hCurrSolSrv;
}

