#include "D3D12App.h"
#include <WindowsX.h>

// Link necessary d3d12 libraries.
#pragma comment(lib,"d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib") // for WKPDID_D3DDebugObjectName

// One and only app instance
static D3D12App *s_pd3dApp;

//
// D3D12App implementation.
//

D3D12App::D3D12App( HINSTANCE hInstance )
    : m_hAppInst( hInstance )
    , m_hMainWnd(nullptr)
    , m_iClientWidth(800)
    , m_iClientHeight(600)
    , m_uWndSizeState(SIZE_RESTORED)
    , m_bAppPaused(false)
    , m_bFullScreen(false)
    , m_BackBufferFormat( DXGI_FORMAT_R8G8B8A8_UNORM )
    , m_DepthStencilBufferFormat( DXGI_FORMAT_D24_UNORM_S8_UINT )
    , m_pd3dMsaaRenderTargetBuffer(nullptr)
    , m_pDXGIFactory(nullptr)
    , m_pd3dDevice(nullptr)
    , m_uRtvDescriptorSize(0)
    , m_uDsvDescriptorSize(0)
    , m_uCbvSrvUavDescriptorSize(0)
    , m_pd3dCommandQueue(nullptr)
    , m_pd3dDirectCmdAlloc(nullptr)
    , m_pd3dCommandList(nullptr)
    , m_pd3dFence(nullptr)
    , m_FenceCount(0)
    , m_hFenceEvent(nullptr)
    , m_pSwapChain(nullptr)
    , m_pRTVDescriptorHeap(nullptr)
    , m_pDSVDescriptorHeap(nullptr)
    , m_pd3dDepthStencilBuffer(nullptr)
    , m_iCurrentBackBuffer(0)
    , m_ScreenViewport{0, 0, 0, 0, 0, 0}
    , m_ScissorRect{0, 0, 0, 0}
{
  /// Device configuration.
  m_aDeviceConfig.FeatureLevel = D3D_FEATURE_LEVEL_12_0;

  m_aDeviceConfig.VsyncEnabled = (FALSE);
  m_aDeviceConfig.RaytracingEnabled = (FALSE);

  m_aDeviceConfig.MsaaSampleCount = (4);
  m_aDeviceConfig.MsaaQaulityLevel = (1);
  m_aDeviceConfig.MsaaEnabled = (false);
  ///

    m_MainWndCaption = L"Direct3D Window";

    memset( m_pd3dSwapChainBuffer , 0, sizeof( m_pd3dSwapChainBuffer ) );
    m_aRTVDefaultClearValue.Format = DXGI_FORMAT_UNKNOWN;
    memset(m_aRTVDefaultClearValue.Color, 0, sizeof(m_aRTVDefaultClearValue.Color));
    m_aRTVDefaultClearValue.Format = m_BackBufferFormat;

    _ASSERT( s_pd3dApp == nullptr );
    s_pd3dApp = this;
}


D3D12App::~D3D12App()
{
    SAFE_RELEASE( m_pDXGIFactory );
    SAFE_RELEASE( m_pd3dDevice );

    SAFE_RELEASE( m_pd3dCommandQueue );
    SAFE_RELEASE( m_pd3dDirectCmdAlloc );
    SAFE_RELEASE( m_pd3dCommandList );

    SAFE_RELEASE(m_pd3dMsaaRenderTargetBuffer);

    SAFE_RELEASE( m_pd3dFence );
    CloseHandle( m_hFenceEvent );

    SAFE_RELEASE( m_pSwapChain );

    SAFE_RELEASE( m_pRTVDescriptorHeap );
    SAFE_RELEASE( m_pDSVDescriptorHeap );

    for( auto &item : m_pd3dSwapChainBuffer ) {
        SAFE_RELEASE(item);
    }
    SAFE_RELEASE( m_pd3dDepthStencilBuffer );
}

HRESULT D3D12App::Run() {

    MSG msg = {0};
    float fTime, fElapsed;

    m_GameTimer.Reset();

    while(msg.message != WM_QUIT) {
        // If there are Window messsages, then process them
        if(PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage( &msg );
            DispatchMessage( &msg );
        } else {
            m_GameTimer.Tick();
            fElapsed = m_GameTimer.TotalTime();
            fTime = m_GameTimer.DeltaTime();

            if( !m_bAppPaused ) {
                CalcFrameStats();
                Update(fTime, fElapsed);
                RenderFrame(fTime, fElapsed);
            } else {
                Sleep( 10 );
            }
        }
    }

    // Wait for command queue complete.
    FlushCommandQueue();

    return msg.message == WM_QUIT ? S_OK : E_FAIL;
}

HRESULT D3D12App::Initialize() {

    HRESULT hr;

    V_RETURN(InitWindow());

    V_RETURN(InitDirect3D());

    PostMessage(m_hMainWnd, WM_SIZE, (WPARAM)SIZE_RESTORED, MAKEWPARAM(m_iClientWidth, m_iClientHeight));

    return hr;
}

HRESULT D3D12App::InitWindow() {

    HRESULT hr;
    WNDCLASSEX wcx = { sizeof(wcx) };
    RECT rect;
    int width, height;
    HICON hIcon = NULL;
    WCHAR szResourcePath[MAX_PATH];

    V(FindDemoMediaFileAbsPath(L"Media/Icons/DX12.ico", _countof(szResourcePath), szResourcePath));
    if (SUCCEEDED(hr)) {
      hIcon = (HICON)LoadImage(m_hAppInst, szResourcePath, IMAGE_ICON, 0, 0, LR_LOADFROMFILE);
    }

    wcx.style = CS_HREDRAW | CS_VREDRAW;
    wcx.lpfnWndProc = MainWndProc;
    wcx.cbClsExtra = 0;
    wcx.cbWndExtra = 0;
    wcx.hInstance = m_hAppInst;
    wcx.hIcon = hIcon;
    wcx.hIconSm = hIcon;
    wcx.hCursor = LoadCursor(NULL, IDC_ARROW );
    wcx.hbrBackground = (HBRUSH)GetStockObject( NULL_BRUSH );
    wcx.lpszMenuName = NULL;
    wcx.lpszClassName = L"D3D12WNDCLASS";

    if ( !RegisterClassEx( &wcx ) ) {
        V_RETURN( E_FAIL );
    }

    rect = { 0, 0, (LONG)m_iClientWidth, (LONG)m_iClientHeight };
    AdjustWindowRect( &rect, WS_OVERLAPPEDWINDOW, false );
    width = rect.right - rect.left;
    height = rect.bottom - rect.top;

    m_hMainWnd = CreateWindowW( wcx.lpszClassName, m_MainWndCaption.c_str(),
                              WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, width, height,
                              NULL, NULL, wcx.hInstance, (LPVOID)this );
    if ( !m_hMainWnd ) {
        V_RETURN( E_FAIL );
    }

    ShowWindow( m_hMainWnd, SW_SHOW );
    UpdateWindow( m_hMainWnd );

    return S_OK;
}

HRESULT D3D12App::InitDirect3D() {
    HRESULT hr;
    DWORD dxgiFactoryFlags = 0;

#if defined(_DEBUG)
    ID3D12Debug *pd3d12DebugControl;
    // Enable debug layer.
    V( D3D12GetDebugInterface( IID_PPV_ARGS( &pd3d12DebugControl ) ) );
    if ( SUCCEEDED( hr ) ) {
        pd3d12DebugControl->EnableDebugLayer();
        SAFE_RELEASE( pd3d12DebugControl );

        dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
#endif

    /// Tearing must be enabled.
    V_RETURN( CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS( &m_pDXGIFactory ) ) );
    DX_SetDebugName( m_pDXGIFactory, "DXGIFactory" );

#ifdef _DEBUG
    LogAdapters();
#endif

    IDXGIAdapter *pAdapter;
    V_RETURN(GetHardwareAdapter(m_pDXGIFactory, &pAdapter));

    V(D3D12CreateDevice(
      pAdapter,
      m_aDeviceConfig.FeatureLevel,
      IID_PPV_ARGS(&m_pd3dDevice)
    ));
    SAFE_RELEASE(pAdapter);
    if (FAILED(hr)) {
      V_RETURN2("Can not create device for a given feature level and feature", E_NOTIMPL);
    }
    DX_SetDebugName( m_pd3dDevice, "D3D12Device" );

    m_FenceCount = 0;
    V_RETURN( m_pd3dDevice->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &m_pd3dFence ) ) );
    DX_SetDebugName( m_pd3dFence, "D3D12Fence" );

    m_uRtvDescriptorSize = m_pd3dDevice->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
    m_uDsvDescriptorSize = m_pd3dDevice->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_DSV );
    m_uCbvSrvUavDescriptorSize = m_pd3dDevice->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

    V_RETURN( CreateCommandObjects() );
    V_RETURN( CreateSwapChain() );
    V_RETURN( CreateRtvAndDsvDescriptorHeaps() );

    return hr;
}


HRESULT D3D12App::GetHardwareAdapter(IDXGIFactory4 *pDXGIFactory, IDXGIAdapter **ppAdapter) {

  HRESULT hr;
  UINT i;
  IDXGIAdapter1 *pAdapter;
  DXGI_ADAPTER_DESC1 adapterDesc;
  ID3D12Device5 *pDevice;

  *ppAdapter = nullptr;

  for (i = 0; DXGI_ERROR_NOT_FOUND != pDXGIFactory->EnumAdapters1(i, &pAdapter); ++i) {
    pAdapter->GetDesc1(&adapterDesc);

    if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) {
      /// This is a warp device.
      SAFE_RELEASE(pAdapter);
      continue;
    }

    if (FAILED(D3D12CreateDevice(pAdapter, m_aDeviceConfig.FeatureLevel, IID_PPV_ARGS(&pDevice)))) {
      SAFE_RELEASE(pAdapter);
      continue;
    }

    /// Check feature level support for the given device.
    hr = CheckDeviceFeatureSupport(pDevice);

    SAFE_RELEASE(pDevice);
    if (SUCCEEDED(hr)) {
      *ppAdapter = pAdapter;
      break;
    } else {
      SAFE_RELEASE(pAdapter);
    }
  }

  return *ppAdapter ? S_OK : E_NOT_SET;
}

HRESULT D3D12App::CheckDeviceFeatureSupport(ID3D12Device5 *pDevice) {

  HRESULT hr = S_OK;

  /// Check ray tracing support.
  if (m_aDeviceConfig.RaytracingEnabled) {
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
    V_RETURN(pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5)));

    if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0) {
      DXOutputDebugStringW(L"Ray tracing not support!\n");
      hr = E_NOTIMPL;
      return hr;
    }
  };

  /// Check MSAA support.
  if (m_aDeviceConfig.MsaaEnabled) {
    D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msQualityDesc;
    msQualityDesc.Format = m_BackBufferFormat;
    msQualityDesc.SampleCount = m_aDeviceConfig.MsaaSampleCount;
    msQualityDesc.Flags = D3D12_MULTISAMPLE_QUALITY_LEVELS_FLAG_NONE;
    msQualityDesc.NumQualityLevels = 0;
    V_RETURN(pDevice->CheckFeatureSupport(
      D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS,
      &msQualityDesc,
      sizeof(msQualityDesc)));

    if (msQualityDesc.NumQualityLevels >= 1)
      m_aDeviceConfig.MsaaQaulityLevel = min(msQualityDesc.NumQualityLevels - 1, m_aDeviceConfig.MsaaQaulityLevel);
    else {
      /// Not yet supported.
      hr = E_NOTIMPL;
      return hr;
    }
  } else {
    m_aDeviceConfig.MsaaSampleCount = 1;
    m_aDeviceConfig.MsaaQaulityLevel = 0;
  }

  /// Final return.
  return hr;
}

HRESULT D3D12App::CreateCommandObjects() {
    HRESULT hr;
    D3D12_COMMAND_QUEUE_DESC dqd = {};
    dqd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    dqd.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    V_RETURN( m_pd3dDevice->CreateCommandQueue( &dqd, IID_PPV_ARGS( &m_pd3dCommandQueue ) ) );
    DX_SetDebugName( m_pd3dCommandQueue, "D3DCommandQueue" );

    V_RETURN( m_pd3dDevice->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &m_pd3dDirectCmdAlloc ) ) );
    DX_SetDebugName( m_pd3dDirectCmdAlloc, "D3DCommandAllocator" );

    V_RETURN( m_pd3dDevice->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_pd3dDirectCmdAlloc,
        nullptr,
        IID_PPV_ARGS( &m_pd3dCommandList )
    ) );
    DX_SetDebugName( m_pd3dCommandList, "D3DCommandList" );


    // Start off in a closed state.  This is because the first time we refer 
    // to the command list we will Reset it, and it needs to be closed before
    // calling Reset.

    m_pd3dCommandList->Close();

    return hr;
}

HRESULT D3D12App::CreateSwapChain() {

    HRESULT hr;
    DXGI_SWAP_CHAIN_DESC dscd;

    // Release the previous Swap Chain so we can re-create new ones.
    SAFE_RELEASE( m_pSwapChain );

    dscd.BufferDesc.Width = m_iClientWidth;
    dscd.BufferDesc.Height = m_iClientHeight;
    dscd.BufferDesc.RefreshRate.Numerator = 60;
    dscd.BufferDesc.RefreshRate.Denominator = 1;
    dscd.BufferDesc.Format = m_BackBufferFormat;
    dscd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    dscd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    dscd.SampleDesc.Count = 1;
    dscd.SampleDesc.Quality = 0;
    dscd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    dscd.BufferCount = s_iSwapChainBufferCount;
    dscd.OutputWindow = m_hMainWnd;
    dscd.Windowed = TRUE;
    dscd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    dscd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    if (!m_aDeviceConfig.VsyncEnabled)
      dscd.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    V_RETURN( m_pDXGIFactory->CreateSwapChain(
        m_pd3dCommandQueue,
        &dscd,
        &m_pSwapChain
    ) );
    DX_SetDebugName( m_pSwapChain, "DXGISwapChain" );

    return hr;
}

UINT D3D12App::GetExtraRTVDescriptorCount() const {
    return 0;
}

UINT D3D12App::GetExtraDSVDescriptorCount() const {
    return 0;
}

HRESULT D3D12App::CreateMsaaRenderBuffer() {

  HRESULT hr;
  D3D12_RESOURCE_DESC texDesc = {};

  texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  texDesc.Alignment = 0;
  texDesc.Width = m_iClientWidth;
  texDesc.Height = m_iClientHeight;
  texDesc.DepthOrArraySize = 1;
  texDesc.MipLevels = 1;
  texDesc.Format = m_BackBufferFormat;
  texDesc.SampleDesc = GetMsaaSampleDesc();
  texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

  V_RETURN(m_pd3dDevice->CreateCommittedResource(
    &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
    D3D12_HEAP_FLAG_NONE,
    &texDesc,
    D3D12_RESOURCE_STATE_RESOLVE_SOURCE,
    &m_aRTVDefaultClearValue,
    IID_PPV_ARGS(&m_pd3dMsaaRenderTargetBuffer)
  ));

  return hr;
}

VOID D3D12App::PrepareNextFrame() {
  m_pd3dCommandList->ResourceBarrier(
    1, &CD3DX12_RESOURCE_BARRIER::Transition(
      CurrentBackBuffer(),
      IsMsaaEnabled() ? D3D12_RESOURCE_STATE_RESOLVE_SOURCE : D3D12_RESOURCE_STATE_PRESENT,
      D3D12_RESOURCE_STATE_RENDER_TARGET));
}

VOID D3D12App::EndRenderFrame() {

  if (IsMsaaEnabled()) {
    CD3DX12_RESOURCE_BARRIER barriers[] = {
      CD3DX12_RESOURCE_BARRIER::Transition(m_pd3dMsaaRenderTargetBuffer, D3D12_RESOURCE_STATE_RENDER_TARGET,
      D3D12_RESOURCE_STATE_RESOLVE_SOURCE),
      CD3DX12_RESOURCE_BARRIER::Transition(m_pd3dSwapChainBuffer[m_iCurrentBackBuffer],
      D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RESOLVE_DEST)
    };

    m_pd3dCommandList->ResourceBarrier(2, barriers);

    m_pd3dCommandList->ResolveSubresource(m_pd3dSwapChainBuffer[m_iCurrentBackBuffer],
      0, m_pd3dMsaaRenderTargetBuffer, 0, m_BackBufferFormat);

    barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(m_pd3dSwapChainBuffer[m_iCurrentBackBuffer],
      D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_PRESENT);

    m_pd3dCommandList->ResourceBarrier(1, barriers);

  } else {

    m_pd3dCommandList->ResourceBarrier(1,
      &CD3DX12_RESOURCE_BARRIER::Transition(m_pd3dSwapChainBuffer[m_iCurrentBackBuffer],
        D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

  }
}

HRESULT D3D12App::CreateRtvAndDsvDescriptorHeaps() {

    HRESULT hr;

    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
    rtvHeapDesc.NumDescriptors = s_iSwapChainBufferCount + GetExtraRTVDescriptorCount();
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    rtvHeapDesc.NodeMask = 0;
    V_RETURN( m_pd3dDevice->CreateDescriptorHeap( &rtvHeapDesc,
        IID_PPV_ARGS( &m_pRTVDescriptorHeap ) ) );
    DX_SetDebugName( m_pRTVDescriptorHeap, "RTVDescriptorHeap" );

    D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
    dsvHeapDesc.NumDescriptors = 1 + GetExtraDSVDescriptorCount();
    dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    dsvHeapDesc.NodeMask = 0;
    V_RETURN( m_pd3dDevice->CreateDescriptorHeap( &dsvHeapDesc, IID_PPV_ARGS( &m_pDSVDescriptorHeap ) ) );
    DX_SetDebugName( m_pDSVDescriptorHeap, "DSVDescriptorHeap" );

    return hr;
}

HRESULT D3D12App::Present() {
    HRESULT hr;
    V_RETURN(m_pSwapChain->Present(0, m_aDeviceConfig.VsyncEnabled ? 0 : DXGI_PRESENT_ALLOW_TEARING));
    m_iCurrentBackBuffer = (m_iCurrentBackBuffer + 1) % s_iSwapChainBufferCount;
    return hr;
}

void D3D12App::FlushCommandQueue() {

    HRESULT hr;
    // Advance the fence value to mark commands up to this fence point.
    m_FenceCount += 1;

    V( m_pd3dCommandQueue->Signal( m_pd3dFence, m_FenceCount ) );

    // Wait until the GPU has completed commands up to this fence point.
    if( m_pd3dFence->GetCompletedValue() < m_FenceCount ) {

        if( m_hFenceEvent == NULL )
            m_hFenceEvent = CreateEventEx( NULL, NULL, 0, EVENT_ALL_ACCESS );

        // Fire event when GPU hits current fence.  
        m_pd3dFence->SetEventOnCompletion( m_FenceCount, m_hFenceEvent );

        // Wait until the GPU hits current fence event is fired.
        WaitForSingleObject( m_hFenceEvent, INFINITE );
    }
}

DXGI_SAMPLE_DESC D3D12App::GetMsaaSampleDesc() const {
  DXGI_SAMPLE_DESC sampleDesc = { 1,0 };
  if (IsMsaaEnabled()) {
    sampleDesc.Count = m_aDeviceConfig.MsaaSampleCount;
    sampleDesc.Quality = m_aDeviceConfig.MsaaQaulityLevel;
  } else {
    sampleDesc.Count = 1;
    sampleDesc.Quality = 0;
  }
  return sampleDesc;
}

BOOL D3D12App::Get4xMsaaEnabled() const {
    return m_aDeviceConfig.MsaaEnabled;
}

void D3D12App::Set4xMsaaEnabled( BOOL state ) {
    if( m_aDeviceConfig.MsaaEnabled != state ) {
        m_aDeviceConfig.MsaaEnabled = state;

        CreateSwapChain();
        OnResize();
    }
}

float D3D12App::GetAspectRatio() const {
    return static_cast<float>(1.0 * m_iClientWidth / m_iClientHeight);
}

ID3D12Resource *D3D12App::CurrentBackBuffer() const {
    return IsMsaaEnabled() ? m_pd3dMsaaRenderTargetBuffer : m_pd3dSwapChainBuffer[m_iCurrentBackBuffer];
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12App::CurrentBackBufferView() const {
  if (IsMsaaEnabled()) {
      return CD3DX12_CPU_DESCRIPTOR_HANDLE{
        m_pRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), 0, m_uRtvDescriptorSize };
  } else {
    return CD3DX12_CPU_DESCRIPTOR_HANDLE{
      m_pRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
      m_iCurrentBackBuffer,
      m_uRtvDescriptorSize
    };
  }
}

D3D12_CPU_DESCRIPTOR_HANDLE D3D12App::DepthStencilView() const {
    return m_pDSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
}

void D3D12App::LogAdapters() {

    UINT i;
    std::wstring str;
    IDXGIAdapter *pAdapter;
    DXGI_ADAPTER_DESC adapterDesc;
    std::vector<IDXGIAdapter*> adapterList;

    i = 0;
    while ( m_pDXGIFactory->EnumAdapters( i, &pAdapter ) != DXGI_ERROR_NOT_FOUND ) {

        pAdapter->GetDesc( &adapterDesc );

        str = L"***Adapter: ";
        str += adapterDesc.Description;
        str += L'\n';

        OutputDebugStringW( str.c_str() );

        adapterList.push_back(pAdapter);
        ++i;
    }

    for ( auto iter = adapterList.begin(); iter != adapterList.end(); ++iter ) {
        LogAdapterOutputs(*iter);
        SAFE_RELEASE(*iter);
    }
}

void D3D12App::LogAdapterOutputs( IDXGIAdapter *pAdapter ) {

    IDXGIOutput *pOutput;
    DXGI_OUTPUT_DESC outputDesc;
    int i;
    std::wstring str;

    i = 0;
    while ( pAdapter->EnumOutputs( i, &pOutput ) != DXGI_ERROR_NOT_FOUND ) {

        pOutput->GetDesc( &outputDesc );

        str = L"***Output: ";
        str += outputDesc.DeviceName;
        str += L'\n';

        OutputDebugStringW( str.c_str() );

        LogOutputDisplayModes( pOutput, m_BackBufferFormat );
        SAFE_RELEASE( pOutput );
        ++i;
    }
}

void D3D12App::LogOutputDisplayModes( IDXGIOutput *pOutput, DXGI_FORMAT dxgiFormat ) {

    UINT count = 0;
    UINT flags = 0;
    std::unique_ptr<DXGI_MODE_DESC[]> modeList;
    UINT i;
    std::wstringstream wstrm;

    // Call with nullptr to get list count.
    pOutput->GetDisplayModeList( dxgiFormat, flags, &count, nullptr );
    modeList.reset( new DXGI_MODE_DESC[count] );
    pOutput->GetDisplayModeList( dxgiFormat, flags, &count, modeList.get() );

    for ( i = 0; i < count; ++i ) {

        UINT n = modeList[i].RefreshRate.Numerator;
        UINT d = modeList[i].RefreshRate.Denominator;

        wstrm << std::setfill(L' ')
            << std::right << std::setw(8)  << L"Width="       << std::left << std::setw( 12 ) << modeList[i].Width
            << std::right << std::setw(9)  << L"Height="      << std::left << std::setw( 12 ) << modeList[i].Height
            << std::right << std::setw(14) << L"RefreshRate=" << std::left << std::setw( 0 ) << n << L"/" << d
            << '(' << (int)((float)n/d) << ')' << std::endl;

        OutputDebugStringW( wstrm.str().c_str() );
        wstrm.str( L"" );
    }
}

BOOL D3D12App::IsMsaaEnabled() const {
  return m_aDeviceConfig.MsaaEnabled;
}

LRESULT D3D12App::OnResize() {

    HRESULT hr;
    int i;
    DXGI_SWAP_CHAIN_DESC scDesc;

    _ASSERT( m_pd3dDevice );
    _ASSERT( m_pd3dDirectCmdAlloc );
    _ASSERT( m_pSwapChain );

    // Flush before changing any resources.
    FlushCommandQueue();

    V_RETURN( m_pd3dCommandList->Reset( m_pd3dDirectCmdAlloc, nullptr ) );

    // Release the previous resources we will be recreating.
    for ( i = 0; i < s_iSwapChainBufferCount; ++i )
        SAFE_RELEASE( m_pd3dSwapChainBuffer[i] );
    SAFE_RELEASE( m_pd3dDepthStencilBuffer );

    m_pSwapChain->GetDesc(&scDesc);

    // Resize the swap chain.
    V_RETURN( m_pSwapChain->ResizeBuffers(
        s_iSwapChainBufferCount,
        m_iClientWidth,
        m_iClientHeight,
        m_BackBufferFormat,
        scDesc.Flags
    ) );

    m_iCurrentBackBuffer = 0;

    /// Create MSAA render target.
    if (IsMsaaEnabled()) {
      SAFE_RELEASE(m_pd3dMsaaRenderTargetBuffer);
      V_RETURN(CreateMsaaRenderBuffer());
      D3D12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle = m_pRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
      D3D12_RENDER_TARGET_VIEW_DESC rtvDesc;
      rtvDesc.Format = m_BackBufferFormat;
      rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;

      m_pd3dDevice->CreateRenderTargetView(m_pd3dMsaaRenderTargetBuffer, &rtvDesc, rtvHeapHandle);

      for (i = 0; i < s_iSwapChainBufferCount; ++i)
        m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&m_pd3dSwapChainBuffer[i]));
    } else {
      D3D12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle = { m_pRTVDescriptorHeap->GetCPUDescriptorHandleForHeapStart() };
      for (i = 0; i < s_iSwapChainBufferCount; ++i) {
        m_pSwapChain->GetBuffer(i, IID_PPV_ARGS(&m_pd3dSwapChainBuffer[i]));

        m_pd3dDevice->CreateRenderTargetView(m_pd3dSwapChainBuffer[i], nullptr, rtvHeapHandle);

        rtvHeapHandle.ptr += m_uRtvDescriptorSize;
      }
    }

    // Create depth-stencil buffer and the view.
    D3D12_RESOURCE_DESC dsd;
    dsd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    dsd.Alignment = 0;
    dsd.Width = m_iClientWidth;
    dsd.Height = m_iClientHeight;
    dsd.DepthOrArraySize = 1;
    dsd.MipLevels = 1;

    dsd.Format = DXGI_FORMAT_R24G8_TYPELESS;
    dsd.SampleDesc = GetMsaaSampleDesc();
    dsd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    dsd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;


    D3D12_CLEAR_VALUE optValue;
    optValue.Format = m_DepthStencilBufferFormat;
    optValue.DepthStencil.Depth = 1.0f;
    optValue.DepthStencil.Stencil = 0;
    V_RETURN( m_pd3dDevice->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES( D3D12_HEAP_TYPE_DEFAULT ),
        D3D12_HEAP_FLAG_NONE,
        &dsd,
        D3D12_RESOURCE_STATE_COMMON,
        &optValue,
        IID_PPV_ARGS( &m_pd3dDepthStencilBuffer )
    ) );

    // Create descriptor to mip level 0 of entire resource using the format of the resource.
    D3D12_DEPTH_STENCIL_VIEW_DESC ddsvd;
    ddsvd.Flags = D3D12_DSV_FLAG_NONE;
    ddsvd.ViewDimension = IsMsaaEnabled() ? D3D12_DSV_DIMENSION_TEXTURE2DMS : D3D12_DSV_DIMENSION_TEXTURE2D;
    ddsvd.Format = m_DepthStencilBufferFormat;
    ddsvd.Texture2D.MipSlice = 0;
    m_pd3dDevice->CreateDepthStencilView( m_pd3dDepthStencilBuffer, &ddsvd,
                                          m_pDSVDescriptorHeap->GetCPUDescriptorHandleForHeapStart() );

    // Transition the resource from its initial state to be used as a depth buffer.
    m_pd3dCommandList->ResourceBarrier( 1, &CD3DX12_RESOURCE_BARRIER::Transition( m_pd3dDepthStencilBuffer,
                                                                                  D3D12_RESOURCE_STATE_COMMON,
                                                                                  D3D12_RESOURCE_STATE_DEPTH_WRITE ) );

    // Execute the resize commands.
    V_RETURN( m_pd3dCommandList->Close() );
    ID3D12CommandList *cmdLists[] = { m_pd3dCommandList };
    m_pd3dCommandQueue->ExecuteCommandLists( 1, cmdLists );

    // Wait until resize is complete.
    FlushCommandQueue();

    // Update the viewport transform to cover the client area.
    m_ScreenViewport.TopLeftX = 0.f;
    m_ScreenViewport.TopLeftY = 0.f;
    m_ScreenViewport.Width = 1.f*m_iClientWidth;
    m_ScreenViewport.Height = 1.f*m_iClientHeight;
    m_ScreenViewport.MinDepth = 0.f;
    m_ScreenViewport.MaxDepth = 1.f;

    m_ScissorRect = { 0, 0, (LONG)m_iClientWidth, (LONG)m_iClientHeight };

    return hr;
}

LRESULT D3D12App::OnMouseEvent( UINT uMsg, WPARAM wParam, int x, int y ) {
    return S_OK;
}

LRESULT D3D12App::OnKeyEvent( WPARAM wParam, LPARAM lParam ) {
    return S_OK;
}

void D3D12App::CalcFrameStats() {
    // Code computes the average frames per second, and also the 
    // average time it takes to render one frame.  These stats 
    // are appended to the window caption bar.

    static int frameCnt = 0;
    static float timeElapsed = 0.0f;
    float timeInterval;

    frameCnt++;

    // Compute averages over one second period.
    if( (timeInterval = m_GameTimer.TotalTime() - timeElapsed) >= 1.0f ) {
        float fps = (float)frameCnt;
        float mfps = timeInterval * 1000.0f / fps;
        std::wstringstream ostrm;

        ostrm << std::fixed
            << m_MainWndCaption
            << L"    fps: " << std::setprecision( 0 ) << fps
            << L"   mspf: " << std::setprecision( 3 ) << mfps
            << std::ends;

        SetWindowText( m_hMainWnd, ostrm.str().c_str() );

        // Reset for next average
        frameCnt = 0;
        timeElapsed = m_GameTimer.TotalTime();
    }
}

void D3D12App::Update( float /*fTime*/, float /*fElapsedTime*/ ) {

}

void D3D12App::RenderFrame( float /*fTime*/, float /*fElaspedTime*/ ) {

}

// Forward declare message handler from imgui_impl_win32.cpp
extern LRESULT CALLBACK ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT D3D12App::MainWndProc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp ) {

  //D3D12App *pAppInst = reinterpret_cast<D3D12App *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  //switch (msg) {
  //case WM_CREATE: {
  //  LPCREATESTRUCT pCreateStruct = (LPCREATESTRUCT)lp;
  //  SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pCreateStruct->lpCreateParams);
  //  return 0;
  //}
  //default:
  //  return pAppInst->MsgProc(hwnd, msg, wp, lp);
  //}
  
  ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);

  return s_pd3dApp->MsgProc(hwnd, msg, wp, lp);
}

LRESULT D3D12App::MsgProc( HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam ) {

    switch( msg ) {
        // WM_ACTIVATE is sent when the window is activated or deactivated.  
        // We pause the game when the window is deactivated and unpause it 
        // when it becomes active.  
    case WM_ACTIVATE:
        if( LOWORD( wParam ) == WA_INACTIVE ) {
            m_bAppPaused = true;
            m_GameTimer.Stop();
        } else {
            m_bAppPaused = false;
            m_GameTimer.Start();
        }
        return 0;

        // WM_SIZE is sent when the user resizes the window.  
    case WM_SIZE:
        // Save the new client area dimensions.
        m_iClientWidth = LOWORD( lParam );
        m_iClientHeight = HIWORD( lParam );
        if( m_pd3dDevice ) {
            if( wParam == SIZE_MINIMIZED ) {
                m_bAppPaused = true;
                m_uWndSizeState = SIZE_MINIMIZED;
            } else if( wParam == SIZE_MAXIMIZED ) {
                m_bAppPaused = false;
                m_uWndSizeState = SIZE_MAXIMIZED;
                OnResize();
            } else if( wParam == SIZE_RESTORED ) {

                // Restoring from minimized state?
                if( m_uWndSizeState == SIZE_MINIMIZED ) {
                    m_bAppPaused = false;
                    m_uWndSizeState = SIZE_RESTORED;
                    OnResize();
                }

                // Restoring from maximized state?
                else if( m_uWndSizeState == SIZE_MAXIMIZED ) {
                    m_bAppPaused = false;
                    m_uWndSizeState = SIZE_RESTORED;
                    OnResize();
                } else if( m_uWndSizeState  & 0x10) {
                    // If user is dragging the resize bars, we do not resize 
                    // the buffers here because as the user continuously 
                    // drags the resize bars, a stream of WM_SIZE messages are
                    // sent to the window, and it would be pointless (and slow)
                    // to resize for each WM_SIZE message received from dragging
                    // the resize bars.  So instead, we reset after the user is 
                    // done resizing the window and releases the resize bars, which 
                    // sends a WM_EXITSIZEMOVE message.
                } else // API call such as SetWindowPos or mSwapChain->SetFullscreenState.
                {
                    OnResize();
                }
            }
        }
        return 0;

        // WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
    case WM_ENTERSIZEMOVE:
        m_bAppPaused = true;
        m_uWndSizeState = 0x10;
        m_GameTimer.Stop();
        return 0;

        // WM_EXITSIZEMOVE is sent when the user releases the resize bars.
        // Here we reset everything based on the new window dimensions.
    case WM_EXITSIZEMOVE:
        m_bAppPaused = false;
        m_uWndSizeState = SIZE_RESTORED;
        m_GameTimer.Start();
        OnResize();
        return 0;

        // WM_DESTROY is sent when the window is being destroyed.
    case WM_DESTROY:
        PostQuitMessage( 0 );
        return 0;

        // The WM_MENUCHAR message is sent when a menu is active and the user presses 
        // a key that does not correspond to any mnemonic or accelerator key. 
    case WM_MENUCHAR:
        // Don't beep when we alt-enter.
        return MAKELRESULT( 0, MNC_CLOSE );

        // Catch this message so to prevent the window from becoming too small.
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lParam)->ptMinTrackSize.x = 200;
        ((MINMAXINFO*)lParam)->ptMinTrackSize.y = 200;
        return 0;

    case WM_LBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_RBUTTONDOWN:
        OnMouseEvent(msg, wParam, GET_X_LPARAM( lParam ), GET_Y_LPARAM( lParam ) );
        return 0;
    case WM_LBUTTONUP:
    case WM_MBUTTONUP:
    case WM_RBUTTONUP:
        OnMouseEvent( msg, wParam, GET_X_LPARAM( lParam ), GET_Y_LPARAM( lParam ) );
        return 0;
    case WM_MOUSEMOVE:
        OnMouseEvent( msg, wParam, GET_X_LPARAM( lParam ), GET_Y_LPARAM( lParam ) );
        return 0;
    case WM_IME_KEYDOWN:
    case WM_KEYDOWN:
        OnKeyEvent( wParam, lParam );
        return 0;
    case WM_KEYUP:
        if( wParam == VK_ESCAPE ) {
            PostQuitMessage( 0 );
        } else if( (int)wParam == VK_F2 && !m_aDeviceConfig.RaytracingEnabled)
            Set4xMsaaEnabled( !m_aDeviceConfig.MsaaEnabled );

        return 0;
    }

    return DefWindowProc( hwnd, msg, wParam, lParam );
}


