#pragma once
#include "d3dUtils.h"
#include <utility>

class ShadowMap
{
public:
    ShadowMap(
        ID3D12Device *pd3dDevice,
        ID3D12GraphicsCommandList *pd3dCommandList,
        UINT uWidth,
        UINT uHeight);
    ~ShadowMap();

    DXGI_FORMAT GetDepthFormat() const;

    VOID CreateDescriptors(
        D3D12_CPU_DESCRIPTOR_HANDLE DsvHeapStart,
        D3D12_CPU_DESCRIPTOR_HANDLE CpuSrvHeapStart,
        D3D12_GPU_DESCRIPTOR_HANDLE GpuSrvHeapStart
    );

    const D3D12_VIEWPORT &Viewport() const;
    const D3D12_RECT &ScissorRect() const;

    D3D12_CPU_DESCRIPTOR_HANDLE Dsv() const;
    D3D12_GPU_DESCRIPTOR_HANDLE Srv() const;

    ID3D12Resource *Resource() const;

    std::pair<UINT, UINT> GetShadowMapDimension() const;

private:
    HRESULT CreateResources(
        ID3D12Device *pd3dDevice,
        ID3D12GraphicsCommandList *pd3dCommandList,
        UINT uWidth,
        UINT uHeight
    );

    ID3D12Device *m_pd3dDevice;

    D3D12_VIEWPORT m_LightViewport;
    D3D12_RECT m_LightScissorRect;

    ID3D12Resource *m_pd3dDepthMap;

    D3D12_CPU_DESCRIPTOR_HANDLE m_hShadowDSV;
    D3D12_GPU_DESCRIPTOR_HANDLE m_hShadowSRV;
};

