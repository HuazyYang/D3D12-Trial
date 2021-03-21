#ifndef D3D12_APP_H
#define D3D12_APP_H
#include <windows.h>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include "d3dUtils.h"
#include "GameTimer.h"



class D3D12App {
public:
    D3D12App(HINSTANCE hInstance = nullptr);
    virtual ~D3D12App();

    virtual HRESULT Initialize();
    HRESULT Run();

protected:
    /// Override the RTV and DSV descriptor size.
    virtual UINT GetExtraRTVDescriptorCount() const;
    virtual UINT GetExtraDSVDescriptorCount() const;

    static LRESULT CALLBACK MainWndProc( HWND hwnd, UINT uMsg, WPARAM wp, LPARAM lp );
    virtual void Update( float fTime, float fElapsedTime );
    virtual void RenderFrame( float fTime, float fElapsedTime );

    ID3D12Resource *CurrentBackBuffer() const;
    D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
    D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

    HRESULT InitWindow();
    HRESULT InitDirect3D();

    HRESULT GetHardwareAdapter(_In_ IDXGIFactory4 *pDXGIFactory, _Out_ IDXGIAdapter **ppAdapter);
    ///
    /// Check device feature support for a given device.
    /// This entry is used for selecting adapter.
    ///
    virtual HRESULT CheckDeviceFeatureSupport(ID3D12Device5 *pDevice);

    void LogAdapters();
    void LogAdapterOutputs( IDXGIAdapter *pAdapter );
    void LogOutputDisplayModes( IDXGIOutput *pOutput, DXGI_FORMAT dxgiFormat );

    HRESULT CreateCommandObjects();
    HRESULT CreateSwapChain();
    HRESULT CreateRtvAndDsvDescriptorHeaps();
    HRESULT CreateMsaaRenderBuffer();

    VOID PrepareNextFrame();
    VOID EndRenderFrame();

    HRESULT Present();
    void FlushCommandQueue();

    DXGI_SAMPLE_DESC GetMsaaSampleDesc() const;
    BOOL Get4xMsaaEnabled() const;
    void Set4xMsaaEnabled(BOOL state );

    float GetAspectRatio() const;

    virtual LRESULT OnResize();
    virtual LRESULT OnMouseEvent( UINT uMsg, WPARAM wParam, int x, int y );
    virtual LRESULT OnKeyEvent( WPARAM wParam, LPARAM lParam );
    LRESULT MsgProc( HWND hwnd, UINT uMsg, WPARAM wp, LPARAM lp );

    void CalcFrameStats();

    BOOL IsMsaaEnabled() const;

    struct DeviceFeatureConfig {
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

    // App handle
    HINSTANCE m_hAppInst;
    // Windows misc
    HWND m_hMainWnd;
    UINT m_iClientWidth;
    UINT m_iClientHeight;
    std::wstring m_MainWndCaption;

    UINT m_uWndSizeState;       /* Window sizing state */
    bool m_bAppPaused;          /* Is window paused */
    bool m_bFullScreen;         /* Is window full screen displaying */

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

    ID3D12CommandQueue          *m_pd3dCommandQueue;
    ID3D12CommandAllocator      *m_pd3dDirectCmdAlloc;
    ID3D12GraphicsCommandList4   *m_pd3dCommandList;

    ID3D12Fence             *m_pd3dFence;
    UINT                    m_FenceCount;
    HANDLE                  m_hFenceEvent;

    IDXGISwapChain          *m_pSwapChain;

    ID3D12DescriptorHeap    *m_pRTVDescriptorHeap;
    ID3D12DescriptorHeap    *m_pDSVDescriptorHeap;

    static const int        s_iSwapChainBufferCount = 2;

    ID3D12Resource          *m_pd3dSwapChainBuffer[s_iSwapChainBufferCount];
    ID3D12Resource          *m_pd3dDepthStencilBuffer;

    int                     m_iCurrentBackBuffer;

    D3D12_VIEWPORT          m_ScreenViewport;
    D3D12_RECT              m_ScissorRect;

    D3D12_CLEAR_VALUE       m_aRTVDefaultClearValue;

    GameTimer               m_GameTimer;
};

#endif /* D3D12_APP_H */
