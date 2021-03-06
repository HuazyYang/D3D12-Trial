#pragma once
#include "d3dUtils.h"
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include "D3D12MemAllocator.hpp"
#include "SyncFence.hpp"

class D3D12RendererContext {

public:
  D3D12RendererContext();
  virtual ~D3D12RendererContext();

  HRESULT Initialize(HWND hwnd, int cx, int cy);
  void Update(float fTime, float fElapsedTime);
  void RenderFrame(float fTime, float fElapsedTime);
  void Destroy();
  HRESULT ResizeFrame(int cx, int cy);
  HRESULT SetFullscreenMode(BOOL bFullScreen);
  RECT GetSwapchainContainingOutputDesktopCoordinates() const;

  LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

protected:
  void ModifyPreset();
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
  virtual LRESULT OnMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) { return 0; }
  /// Override the RTV and DSV descriptor size.
  virtual UINT GetExtraRTVDescriptorCount() const;
  virtual UINT GetExtraDSVDescriptorCount() const;

  ID3D12Resource *CurrentBackBuffer() const;
  D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
  D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

  void CheckTearingSupport(IDXGIFactory5 *pFactory);
  HRESULT GetHardwareAdapter(_In_ IDXGIFactory1 *pDXGIFactory, _Out_ IDXGIAdapter1 **ppAdapter);

  HRESULT CreateDevice();
  HRESULT CreateMemAllocator();
  HRESULT CreateCommandObjects();
  HRESULT CreateSwapChain(HWND hwnd);
  HRESULT CreateRtvAndDsvDescriptorHeaps();
  HRESULT CreateMsaaRenderBuffer();

  VOID PrepareNextFrame(_In_opt_ ID3D12GraphicsCommandList *pCommandList = nullptr);
  VOID EndRenderFrame(_In_opt_ ID3D12GraphicsCommandList *pCommandList = nullptr);

  HRESULT Present();
  void FlushCommandQueue();

  DXGI_SAMPLE_DESC GetMsaaSampleDesc() const;
  BOOL Get4xMsaaEnabled() const;
  void Set4xMsaaEnabled(BOOL state);

  float GetAspectRatio() const;

  BOOL IsMsaaEnabled() const;

  // The following configuration must be specified
  // before Initialize() invokes.
  struct DeviceFeatureConfig {
    // Request decrete Gpu
    BOOL RequestHighPerformanceGpu;
    /// Request device feature level.
    D3D_FEATURE_LEVEL FeatureLevel;

    /// Request DXR Ray Tracing support (mandatory)
    BOOL RaytracingEnabled;

    /// Request MSAA (non-mandatory)
    BOOL MsaaEnabled;
    UINT MsaaSampleCount;
    UINT MsaaQaulityLevel;

    /// Vertical Synchronization (non-mandatory)
    BOOL VsyncEnabled;

    // Back color buffer SRGB settings (mandatory)
    // Default is FALSE
    BOOL SwapChainBackBufferFormatSRGB;

  } m_aDeviceConfig;

  // The following settings is mutable when the renderer context
  // is running
  struct DeviceRuntimeSettings {
    // Check whether tearing is support for output
    BOOL TearingSupport;
  } m_aDeviceRuntimeSettings;

  uint32_t m_uFrameWidth, m_uFrameHeight;
  // Check whether current render output window is in full screen mode
  BOOL m_bFullscreenMode;

  // Back buffer format.
  DXGI_FORMAT m_BackBufferFormat;
  DXGI_FORMAT m_SwapChainFormat;
  DXGI_FORMAT m_DepthStencilBufferFormat;

  /// MSAA support.
  ID3D12Resource *m_pd3dMsaaRenderTargetBuffer;

  // D3D12 resources.
  IDXGIFactory4 *m_pDXGIFactory;
  IDXGIAdapter1 *m_pDXGIAdapter;
  ID3D12Device5 *m_pd3dDevice;
  D3D12MAAllocator m_MemAllocator;

  UINT m_uRtvDescriptorSize;
  UINT m_uDsvDescriptorSize;
  UINT m_uCbvSrvUavDescriptorSize;

  ID3D12CommandQueue *m_pd3dCommandQueue;
  ID3D12CommandAllocator *m_pd3dDirectCmdAlloc;
  ID3D12GraphicsCommandList4 *m_pd3dCommandList;

  SyncFence *m_pSyncFence;

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
