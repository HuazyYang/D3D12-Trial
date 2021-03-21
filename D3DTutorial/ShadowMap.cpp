#include "ShadowMap.h"


ShadowMap::ShadowMap(
    ID3D12Device *pd3dDevice,
    ID3D12GraphicsCommandList *pd3dCommandList,
    UINT uWidth,
    UINT uHeight)
{
    HRESULT hr;

    m_pd3dDevice = pd3dDevice;
    SAFE_ADDREF(m_pd3dDevice);

    m_LightViewport = { 0.0f, 0.0f, (float)uWidth, (float)uHeight, 0.0f, 1.0f};
    m_LightScissorRect = { (LONG)0, (LONG)0, (LONG)uWidth, (LONG)uHeight };

    V(CreateResources(pd3dDevice,
        pd3dCommandList,
        uWidth,
        uHeight));
}

const D3D12_VIEWPORT &ShadowMap::Viewport() const {
    return m_LightViewport;
}

const D3D12_RECT &ShadowMap::ScissorRect() const {
    return m_LightScissorRect;
}

ID3D12Resource *ShadowMap::Resource() const {
    return m_pd3dDepthMap;
}

std::pair<UINT, UINT> ShadowMap::GetShadowMapDimension() const {
    D3D12_RESOURCE_DESC texDesc;
    texDesc = m_pd3dDepthMap->GetDesc();

    return { (UINT)texDesc.Width, (UINT)texDesc.Height };
}

HRESULT ShadowMap::CreateResources(
    ID3D12Device *pd3dDevice,
    ID3D12GraphicsCommandList *pd3dCommandList,
    UINT uWidth,
    UINT uHeight
) {
    HRESULT hr;
    D3D12_RESOURCE_DESC texDesc = {};
    D3D12_CLEAR_VALUE clrValue;

    texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texDesc.Alignment = 0;
    texDesc.Width = uWidth;
    texDesc.Height = uHeight;
    texDesc.DepthOrArraySize = 1;
    texDesc.MipLevels = 1;
    texDesc.Format = DXGI_FORMAT_R24G8_TYPELESS;
    texDesc.SampleDesc.Count = 1;
    texDesc.SampleDesc.Quality = 0;
    texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    clrValue.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    clrValue.DepthStencil.Depth = 1.0f;
    clrValue.DepthStencil.Stencil = 0;

    V_RETURN(pd3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &texDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        &clrValue,
        IID_PPV_ARGS(&m_pd3dDepthMap)
    ));

    //pd3dCommandList->ResourceBarrier(1,
    //    &CD3DX12_RESOURCE_BARRIER::Transition(m_pd3dDepthMap,
    //        D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_GENERIC_READ));

    return hr;
}

DXGI_FORMAT ShadowMap::GetDepthFormat() const {
    return DXGI_FORMAT_D24_UNORM_S8_UINT;
}

ShadowMap::~ShadowMap()
{
    SAFE_RELEASE(m_pd3dDevice);
    SAFE_RELEASE(m_pd3dDepthMap);
}

VOID ShadowMap::CreateDescriptors(
    D3D12_CPU_DESCRIPTOR_HANDLE DsvHeapStart,
    D3D12_CPU_DESCRIPTOR_HANDLE CpuSrvHeapStart,
    D3D12_GPU_DESCRIPTOR_HANDLE GpuSrvHeapStart
) {
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    dsvDesc.Texture2D.MipSlice = 0;
    m_pd3dDevice->CreateDepthStencilView(m_pd3dDepthMap, &dsvDesc, DsvHeapStart);
    m_hShadowDSV = DsvHeapStart;

    srvDesc.Format = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    m_pd3dDevice->CreateShaderResourceView(m_pd3dDepthMap, &srvDesc, CpuSrvHeapStart);
    m_hShadowSRV = GpuSrvHeapStart;
}

D3D12_CPU_DESCRIPTOR_HANDLE ShadowMap::Dsv() const {
    return m_hShadowDSV;
}

D3D12_GPU_DESCRIPTOR_HANDLE ShadowMap::Srv() const {
    return m_hShadowSRV;
}



