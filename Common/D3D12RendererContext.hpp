#pragma once
#include "d3dUtils.h"
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

class D3D12RendererContext {
public:
  D3D12RendererContext();
  virtual ~D3D12RendererContext();

  HRESULT Initialize(HWND hwnd, int cx, int cy);
  void Update(float fTime, float fElapsedTime);
  void RenderFrame(float fTime, float fElapsedTime);
  void Destroy();
  HRESULT ResizeFrame(int cx, int cy);

protected:
///
/// Check device feature support for a given device.
/// This entry is used for selecting adapter.
///
virtual HRESULT CheckDeviceFeatureSupport(ID3D12Device5 *pDevice);
  virtual HRESULT OnInitPipelines() { return S_OK; }
  virtual void OnFrameMoved(float fTime, float fElapsedTime) {}
  virtual void OnRenderFrame(float fTime, float fElapsedTime) {}
  virtual void OnResizeFrame(int cx, int cy) {}
  virtual void OnDestroy() {}
  /// Override the RTV and DSV descriptor size.
  virtual UINT GetExtraRTVDescriptorCount() const;
  virtual UINT GetExtraDSVDescriptorCount() const;

  ID3D12Resource *CurrentBackBuffer() const;
  D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
  D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

  HRESULT GetHardwareAdapter(_In_ IDXGIFactory1 *pDXGIFactory, _Out_ IDXGIAdapter1 **ppAdapter);


  HRESULT CreateDevice();
  HRESULT CreateCommandObjects();
  HRESULT CreateSwapChain(HWND hwnd);
  HRESULT CreateRtvAndDsvDescriptorHeaps();
  HRESULT CreateMsaaRenderBuffer();

  VOID PrepareNextFrame();
  VOID EndRenderFrame();

  HRESULT Present();
  void FlushCommandQueue();

  DXGI_SAMPLE_DESC GetMsaaSampleDesc() const;
  BOOL Get4xMsaaEnabled() const;
  void Set4xMsaaEnabled(BOOL state);

  float GetAspectRatio() const;

  BOOL IsMsaaEnabled() const;

  struct DeviceFeatureConfig {
    // Request decrete Gpu
    BOOL RequestHighPerformanceGpu;
    /// Device feature level.
    D3D_FEATURE_LEVEL FeatureLevel;

    /// DXR Ray Tracing support.
    BOOL RaytracingEnabled;

    /// MSAA
    BOOL MsaaEnabled;
    UINT MsaaSampleCount;
    UINT MsaaQaulityLevel;

    /// Vertical Synchronization.
    BOOL VsyncEnabled;
  } m_aDeviceConfig;

  uint32_t m_uFrameWidth, m_uFrameHeight;

  // Back buffer format.
  DXGI_FORMAT m_BackBufferFormat;
  DXGI_FORMAT m_DepthStencilBufferFormat;

  /// MSAA support.
  ID3D12Resource *m_pd3dMsaaRenderTargetBuffer;

  // D3D12 resources.
  IDXGIFactory4 *m_pDXGIFactory;
  ID3D12Device5 *m_pd3dDevice;

  UINT m_uRtvDescriptorSize;
  UINT m_uDsvDescriptorSize;
  UINT m_uCbvSrvUavDescriptorSize;

  ID3D12CommandQueue *m_pd3dCommandQueue;
  ID3D12CommandAllocator *m_pd3dDirectCmdAlloc;
  ID3D12GraphicsCommandList4 *m_pd3dCommandList;

  ID3D12Fence *m_pd3dFence;
  UINT m_FenceCount;
  HANDLE m_hFenceEvent;

  IDXGISwapChain *m_pSwapChain;

  ID3D12DescriptorHeap *m_pRTVDescriptorHeap;
  ID3D12DescriptorHeap *m_pDSVDescriptorHeap;

  static const int s_iSwapChainBufferCount = 2;

  ID3D12Resource *m_pd3dSwapChainBuffer[s_iSwapChainBufferCount];
  ID3D12Resource *m_pd3dDepthStencilBuffer;

  int m_iCurrentBackBuffer;

  D3D12_VIEWPORT m_ScreenViewport;
  D3D12_RECT m_ScissorRect;

  D3D12_CLEAR_VALUE m_aRTVDefaultClearValue;
};
