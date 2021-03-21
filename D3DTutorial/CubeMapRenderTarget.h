#pragma once
#include "d3dUtils.h"

class CubeMapRenderTarget
{
public:
    CubeMapRenderTarget(
        ID3D12Device *pd3dDevice,
        ID3D12GraphicsCommandList *pd3dCommandList,
        UINT uWidth, UINT uHeight,
        DXGI_FORMAT RtvFormat,
        const FLOAT RtvInitClearValue[2],
        FLOAT DsvInitClearValue
        );
    ~CubeMapRenderTarget();

    CubeMapRenderTarget(const CubeMapRenderTarget &) = delete;
    CubeMapRenderTarget& operator=(const CubeMapRenderTarget &) = delete;

    ID3D12Resource *Resource() const;

    D3D12_CPU_DESCRIPTOR_HANDLE const (&Rtv() const)[6];
    D3D12_CPU_DESCRIPTOR_HANDLE Dsv() const;
    D3D12_GPU_DESCRIPTOR_HANDLE Srv() const;

    D3D12_VIEWPORT Viewport() const;
    D3D12_RECT ScissorRect() const;

    DXGI_FORMAT DepthStencilFormat() const;
    DXGI_FORMAT RenderTargetFormat() const;

    HRESULT BuildDescriptorHandles(
        D3D12_CPU_DESCRIPTOR_HANDLE hCpuRtv,
        UINT uRtvHeapIncrementSize,
        D3D12_CPU_DESCRIPTOR_HANDLE hCpuDsv,
        D3D12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
        D3D12_GPU_DESCRIPTOR_HANDLE hGpuSrv
    );

    LRESULT OnResize(UINT uWidth, UINT uHeight);

private:
    HRESULT CreateResources(
        ID3D12Device *pd3dDevice,
        ID3D12GraphicsCommandList *pd3dCommandList,
        UINT uWidth, UINT uHeight,
        DXGI_FORMAT RtvFormat,
        const FLOAT RtvInitClearValue[2],
        FLOAT DsvInitClearValue);

    ID3D12Device *m_pd3dDevice;

    D3D12_VIEWPORT m_Viewport;
    D3D12_RECT m_ScissorRect;

    UINT m_uWidth;
    UINT m_uHeight;
    DXGI_FORMAT m_RtvFormat;

    ID3D12Resource *m_pCubeTexture;
    ID3D12Resource *m_pCubeDepthStencil;

    D3D12_CPU_DESCRIPTOR_HANDLE m_hRtv[6];
    D3D12_CPU_DESCRIPTOR_HANDLE m_hDsv;
    D3D12_GPU_DESCRIPTOR_HANDLE m_hSrv;
};

