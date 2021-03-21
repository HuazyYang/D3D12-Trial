#include "CubeMapRenderTarget.h"



CubeMapRenderTarget::CubeMapRenderTarget(
    ID3D12Device *pd3dDevice,
    ID3D12GraphicsCommandList *pd3dCommandList,
    UINT uWidth, UINT uHeight,
    DXGI_FORMAT RtvFormat,
    const FLOAT RtvInitClearValue[2],
    FLOAT DsvInitClearValue)
{
    HRESULT hr;

    m_pd3dDevice = pd3dDevice;
    SAFE_ADDREF(m_pd3dDevice);

    m_uWidth = uWidth;
    m_uHeight = uHeight;
    m_RtvFormat = RtvFormat;

    m_pCubeTexture = nullptr;
    m_pCubeDepthStencil = nullptr;

    V(CreateResources(pd3dDevice, pd3dCommandList, uWidth, uHeight,
        RtvFormat, RtvInitClearValue, DsvInitClearValue));

    m_Viewport.Width = 1.0f*m_uWidth;
    m_Viewport.Height = 1.0f*m_uHeight;
    m_Viewport.TopLeftX = 0.0f;
    m_Viewport.TopLeftY = 0.0f;
    m_Viewport.MinDepth = 0.0f;
    m_Viewport.MaxDepth = 1.0f;

    m_ScissorRect = { 0, 0, (LONG)m_uWidth, (LONG)m_uHeight };
}

CubeMapRenderTarget::~CubeMapRenderTarget()
{
    SAFE_RELEASE(m_pd3dDevice);
    SAFE_RELEASE(m_pCubeTexture);
    SAFE_RELEASE(m_pCubeDepthStencil);
}

HRESULT CubeMapRenderTarget::CreateResources(
    ID3D12Device *pd3dDevice,
    ID3D12GraphicsCommandList *pd3dCommandList,
    UINT uWidth, UINT uHeight,
    DXGI_FORMAT RtvFormat,
    const FLOAT RtvInitClearValue[2],
    FLOAT DsvInitClearValue) {
    HRESULT hr;
    D3D12_RESOURCE_DESC texDesc;
    D3D12_CLEAR_VALUE clrValue;

    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = m_uWidth;
    texDesc.Height = m_uHeight;
    texDesc.DepthOrArraySize = 6;
    texDesc.MipLevels = 1;
    texDesc.Format = m_RtvFormat;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    clrValue.Format = texDesc.Format;
    memcpy(clrValue.Color, RtvInitClearValue, sizeof(clrValue.Color));

    V_RETURN(m_pd3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        &clrValue,
        IID_PPV_ARGS(&m_pCubeTexture)
        ));

    texDesc.DepthOrArraySize = 1;
    texDesc.Format = DXGI_FORMAT_D32_FLOAT;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    clrValue.Format = texDesc.Format;
    clrValue.DepthStencil.Depth = DsvInitClearValue;
    clrValue.DepthStencil.Stencil = 0;

    V_RETURN(m_pd3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_COMMON,
        &clrValue,
        IID_PPV_ARGS(&m_pCubeDepthStencil)
        ));

    pd3dCommandList->ResourceBarrier(1,
        &CD3DX12_RESOURCE_BARRIER::Transition(m_pCubeDepthStencil,
            D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE));

    return hr;
}

HRESULT CubeMapRenderTarget::BuildDescriptorHandles(
    D3D12_CPU_DESCRIPTOR_HANDLE hCpuRtv,
    UINT uRtvHeapIncrementSize,
    D3D12_CPU_DESCRIPTOR_HANDLE hCpuDsv,
    D3D12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
    D3D12_GPU_DESCRIPTOR_HANDLE hGpuSrv
) {
    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc;
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
    D3D12_RESOURCE_DESC texDesc;
    //HRESULT hr;
    UINT16 i;

    texDesc = m_pCubeTexture->GetDesc();

    for (i = 0; i < texDesc.DepthOrArraySize; ++i) {
        rtvDesc.Format = texDesc.Format;
        rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
        // Render target to ith element.
        rtvDesc.Texture2DArray.FirstArraySlice = i;
        // Only view one element of the array.
        rtvDesc.Texture2DArray.ArraySize = 1;
        rtvDesc.Texture2DArray.MipSlice = 0;
        rtvDesc.Texture2DArray.PlaneSlice = 0;

        m_hRtv[i].ptr = hCpuRtv.ptr + i * uRtvHeapIncrementSize;

        m_pd3dDevice->CreateRenderTargetView(m_pCubeTexture, &rtvDesc, m_hRtv[i]);
    }

    m_pd3dDevice->CreateDepthStencilView(m_pCubeDepthStencil, nullptr, hCpuDsv);
    m_hDsv = hCpuDsv;

    texDesc = m_pCubeTexture->GetDesc();
    srvDesc.Format = texDesc.Format;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
    srvDesc.TextureCube.MipLevels = 1;
    srvDesc.TextureCube.MostDetailedMip = 0;
    srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

    m_pd3dDevice->CreateShaderResourceView(m_pCubeTexture, &srvDesc, hCpuSrv);
    m_hSrv = hGpuSrv;

    return S_OK;
}


ID3D12Resource *CubeMapRenderTarget::Resource() const {
    return m_pCubeTexture;
}

D3D12_CPU_DESCRIPTOR_HANDLE const (&CubeMapRenderTarget::Rtv() const)[6]{
    return m_hRtv;
}

D3D12_CPU_DESCRIPTOR_HANDLE CubeMapRenderTarget::Dsv() const {
    return m_hDsv;
}

D3D12_GPU_DESCRIPTOR_HANDLE CubeMapRenderTarget::Srv() const {
    return m_hSrv;
}

D3D12_VIEWPORT CubeMapRenderTarget::Viewport() const {
    return m_Viewport;
}

D3D12_RECT CubeMapRenderTarget::ScissorRect() const {
    return m_ScissorRect;
}

DXGI_FORMAT CubeMapRenderTarget::DepthStencilFormat() const {
    D3D12_RESOURCE_DESC texDesc;
    texDesc = m_pCubeDepthStencil->GetDesc();
    return texDesc.Format;
}

DXGI_FORMAT CubeMapRenderTarget::RenderTargetFormat() const {
    D3D12_RESOURCE_DESC texDesc;
    texDesc = m_pCubeTexture->GetDesc();
    return texDesc.Format;
}

LRESULT CubeMapRenderTarget::OnResize(UINT cx, UINT cy) {
    return S_OK;
}
