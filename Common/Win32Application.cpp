#include "Common.h"
#include "D3D12RendererContext.hpp"
#include "Win32Application.hpp"
#include <Windows.h>
#include <memory>
#include <string.h>

WindowInteractor::WindowInteractor() {
  m_hWnd = NULL;
  m_bEnabled = true;
  m_FrameStat.FPS = .0;
  m_FrameStat.LastFrameCount = 0;
  m_FrameStat.TotalFrameCount = 0;

  Reset();
}

HWND WindowInteractor::GetHwnd() const {
  return m_hWnd;
}

WindowInteractor::~WindowInteractor() {
}

void WindowInteractor::Reset() {
  m_GlobalTimer.Reset();
  m_FrameStat.LastTimeStamp = .0;
}

bool WindowInteractor::IsPaused() const {
  return m_GlobalTimer.IsPaused();
}

void WindowInteractor::SetPaused(bool paused) {
  paused ? m_GlobalTimer.Stop() : m_GlobalTimer.Resume();
}

void WindowInteractor::Tick() {
  m_GlobalTimer.Tick();

  if(!m_GlobalTimer.IsPaused()) {
    double sampleInterval;
    m_FrameStat.LastFrameCount += 1;
    m_FrameStat.TotalFrameCount += 1;

    if ((sampleInterval = (m_GlobalTimer.GetTime() - m_FrameStat.LastTimeStamp)) >= 1.0) {
      m_FrameStat.FPS = m_FrameStat.LastFrameCount / sampleInterval;
      m_FrameStat.LastTimeStamp = m_GlobalTimer.GetTime();
      m_FrameStat.LastFrameCount = 0;
    }
  }
}

double WindowInteractor::GetFPS() const {
  return m_FrameStat.FPS;
}

double WindowInteractor::GetTotalTime() const {
  return m_GlobalTimer.GetTime();
}
double WindowInteractor::GetElapsedTime() const {
  return m_GlobalTimer.GetElapsedTime();
}

LRESULT WindowInteractor::OnMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool *pbNoFurtherProcessing) {

  *pbNoFurtherProcessing = false;
  return 0;
}

struct _WindowContext {
  D3D12RendererContext *pRenderer;
  WindowInteractor *pInteractor;
  BOOL Resizing;
  std::wstring Title;
};

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wp, LPARAM lp);
static void ReportFrameStats(HWND hwnd, float fTime, float fElapsed);

int RunSample(_In_ D3D12RendererContext *pRenderer, _In_ WindowInteractor *pInteractor, _In_ int iWindowWidth,
              _In_ int iWindowHeight, _In_ const wchar_t *pWindowTitle) {

  HRESULT hr;
  WNDCLASSEX wcx = {sizeof(wcx)};
  RECT rect;
  int width, height;
  HICON hIcon = NULL;
  WCHAR szResourcePath[MAX_PATH];
  HINSTANCE hAppInst = GetModuleHandleW(NULL);
  HWND hMainWnd;
  std::unique_ptr<_WindowContext> pWndContext;

  V(FindDemoMediaFileAbsPath(L"Media/Icons/DX12.ico", _countof(szResourcePath), szResourcePath));
  if (SUCCEEDED(hr)) {
    hIcon = (HICON)LoadImage(hAppInst, szResourcePath, IMAGE_ICON, 0, 0, LR_LOADFROMFILE);
  }

  wcx.style = CS_HREDRAW | CS_VREDRAW;
  wcx.lpfnWndProc = MainWndProc;
  wcx.cbClsExtra = 0;
  wcx.cbWndExtra = 0;
  wcx.hInstance = hAppInst;
  wcx.hIcon = hIcon;
  wcx.hIconSm = hIcon;
  wcx.hCursor = LoadCursor(NULL, IDC_ARROW);
  wcx.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
  wcx.lpszMenuName = NULL;
  wcx.lpszClassName = L"D3D12WNDCLASS";

  if (!RegisterClassEx(&wcx)) {
    V_RETURN(E_FAIL);
  }

  rect = {0, 0, (LONG)iWindowWidth, (LONG)iWindowHeight};
  AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, false);
  width = rect.right - rect.left;
  height = rect.bottom - rect.top;

  pWndContext = std::make_unique<_WindowContext>();
  pWndContext->pRenderer = pRenderer;
  pWndContext->pInteractor = pInteractor;
  pWndContext->Resizing = FALSE;
  pWndContext->Title = pWindowTitle;

  hMainWnd = CreateWindowW(wcx.lpszClassName, pWindowTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, wcx.hInstance,
                           (LPVOID)pWndContext.get());
  if (!hMainWnd) {
    V_RETURN(E_FAIL);
  }
  pWndContext->pInteractor->m_hWnd = hMainWnd;

  GetClientRect(hMainWnd, &rect);
  V_RETURN(pWndContext->pRenderer->Initialize(hMainWnd, rect.right, rect.bottom));
  PostMessage(hMainWnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(rect.right, rect.bottom));

  ShowWindow(hMainWnd, SW_SHOW);
  UpdateWindow(hMainWnd);

  MSG msg = {0};

  float fTime, fElapsed;
  pWndContext->pInteractor->Reset();

  while (msg.message != WM_QUIT) {
    // If there are Window messsages, then process them
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    } else if(!pWndContext->pInteractor->IsPaused()) {

      fTime = (float)pWndContext->pInteractor->GetTotalTime();
      fElapsed = (float)pWndContext->pInteractor->GetElapsedTime();
      pWndContext->pInteractor->Tick();

      ReportFrameStats(hMainWnd, fTime, fElapsed);
      pRenderer->Update(fTime, fElapsed);
      pRenderer->RenderFrame(fTime, fElapsed);
    } else
      Sleep(10);
  }

  pRenderer->Destroy();

  return msg.message == WM_QUIT ? 0 : -1;
}

void ReportFrameStats(HWND hwnd, float fTime, float fElapsed) {
  static double tmInterval = .0;

  _WindowContext *pWndContext = reinterpret_cast<_WindowContext *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

  tmInterval += fElapsed;

  if (tmInterval > 1.0) {
    wchar_t buff[128];
    _snwprintf_s(buff, _countof(buff), L"%s, FPS:%3.1f, MSPF:%.3f", pWndContext->Title.c_str(),
      static_cast<float>(pWndContext->pInteractor->GetFPS()),
      static_cast<float>(1000.0 / pWndContext->pInteractor->GetFPS())
    );
    SetWindowTextW(hwnd, buff);
    tmInterval = .0;
  }
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {

  _WindowContext *pWndContext = reinterpret_cast<_WindowContext *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  switch (msg) {
  case WM_CREATE: {
    LPCREATESTRUCT pCreateStruct = (LPCREATESTRUCT)lp;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pCreateStruct->lpCreateParams);
    pWndContext = reinterpret_cast<_WindowContext *>(pCreateStruct->lpCreateParams);
    break;
  }
    // WM_ACTIVATE is sent when the window is activated or deactivated.
    // We pause the game when the window is deactivated and unpause it
    // when it becomes active.
  case WM_ACTIVATE:
    if (LOWORD(wp) == WA_INACTIVE) {
      pWndContext->pInteractor->SetPaused(true);
    } else {
      pWndContext->pInteractor->SetPaused(false);
    }
    break;
    // WM_SIZE is sent when the user resizes the window.
  case WM_SIZE:
    LONG cx, cy;
    cx = LOWORD(lp);
    cy = HIWORD(lp);
    if (wp == SIZE_MINIMIZED) {
      pWndContext->pInteractor->SetPaused(true);
    } else if (wp == SIZE_MAXIMIZED) {
      pWndContext->pRenderer->ResizeFrame(cx, cy);
    } else if(wp == SIZE_RESTORED && !pWndContext->Resizing) {
      pWndContext->pRenderer->ResizeFrame(cx, cy);
    }
    break;
    // WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
  case WM_ENTERSIZEMOVE:
    pWndContext->pInteractor->SetPaused(true);
    pWndContext->Resizing = true;
    break;
    // WM_EXITSIZEMOVE is sent when the user releases the resize bars.
    // Here we reset everything based on the new window dimensions.
  case WM_EXITSIZEMOVE:
    RECT rect;
    GetClientRect(hwnd, &rect);
    pWndContext->Resizing = false;
    pWndContext->pRenderer->ResizeFrame(rect.right, rect.bottom);
    pWndContext->pInteractor->SetPaused(false);
    break;
    // WM_DESTROY is sent when the window is being destroyed.
  case WM_DESTROY:
    PostQuitMessage(0);
    break;
    // The WM_MENUCHAR message is sent when a menu is active and the user presses
    // a key that does not correspond to any mnemonic or accelerator key.
  case WM_MENUCHAR:
    // Don't beep when we alt-enter.
    return MAKELRESULT(0, MNC_CLOSE);

    // Catch this message so to prevent the window from becoming too small.
  case WM_GETMINMAXINFO:
    ((MINMAXINFO *)lp)->ptMinTrackSize.x = 200;
    ((MINMAXINFO *)lp)->ptMinTrackSize.y = 200;
    return 0;
  case WM_KEYUP:
    if (wp == VK_ESCAPE) {
      PostMessage(hwnd, WM_CLOSE, 0, 0);
      return 0;
    }
    break;
  }

  LRESULT ret = 0;
  bool bNoFurtherProcessing = false;
  if(pWndContext)
    ret = pWndContext->pInteractor->OnMsgProc(hwnd, msg, wp, lp, &bNoFurtherProcessing);

  if(!bNoFurtherProcessing) {
    ret = DefWindowProc(hwnd, msg, wp, lp);
  }

  return ret;
}
