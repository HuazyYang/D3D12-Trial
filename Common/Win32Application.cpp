#include "Common.h"
#include "D3D12RendererContext.hpp"
#include "GameTimer.hpp"
#include "UIController.hpp"
#include <Windows.h>
#include <memory>
#include <string.h>

struct FrameStatistics {
  uint32_t LastFrameCount;
  uint32_t TotalFrameCount;
  double LastTimeStamp;
  std::wstring Title;
};

struct _WindowContext {
  D3D12RendererContext *pRenderer;
  IUIController *pUIController;
  GameTimer Timer;
  BOOL Paused;

  FrameStatistics FrameStat;
};

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT uMsg, WPARAM wp, LPARAM lp);
static void ReportFrameStats(HWND hwnd, float fTime, float fElapsed);

int RunSample(_In_ D3D12RendererContext *pRenderer, _In_ IUIController *pUIState, _In_ int iWindowWidth,
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
  pWndContext->pUIController = pUIState;
  pWndContext->Paused = FALSE;
  pWndContext->FrameStat.Title = pWindowTitle;
  pWndContext->FrameStat.LastFrameCount = 0;
  pWndContext->FrameStat.TotalFrameCount = 0;
  pWndContext->FrameStat.LastTimeStamp = 0.0;

  hMainWnd = CreateWindowW(wcx.lpszClassName, pWindowTitle, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                           rect.right - rect.left, rect.bottom - rect.top, NULL, NULL, wcx.hInstance,
                           (LPVOID)pWndContext.get());
  if (!hMainWnd) {
    V_RETURN(E_FAIL);
  }
  GetClientRect(hMainWnd, &rect);
  V_RETURN(pWndContext->pRenderer->Initialize(hMainWnd, rect.right, rect.bottom));
  PostMessage(hMainWnd, WM_SIZE, SIZE_RESTORED, MAKELPARAM(rect.right, rect.bottom));

  ShowWindow(hMainWnd, SW_SHOW);
  UpdateWindow(hMainWnd);

  MSG msg = {0};

  float fTime, fElapsed;
  pWndContext->Timer.Reset();

  while (msg.message != WM_QUIT) {
    // If there are Window messsages, then process them
    if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    } else {
      pWndContext->Timer.Tick();
      fElapsed = (float)pWndContext->Timer.TotalElapsed();
      fTime = (float)pWndContext->Timer.DeltaElasped();

      if (!pWndContext->Paused) {
        ReportFrameStats(hMainWnd, fTime, fElapsed);
        pRenderer->Update(fTime, fElapsed);
        pRenderer->RenderFrame(fTime, fElapsed);
      } else {
        Sleep(10);
      }
    }
  }

  pRenderer->Destroy();

  return msg.message == WM_QUIT ? 0 : -1;
}

void ReportFrameStats(HWND hwnd, float fTime, float fElapsed) {
  double timeInterval = 0.0;

  _WindowContext *pWndContext = reinterpret_cast<_WindowContext *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

  pWndContext->FrameStat.LastFrameCount += 1;
  pWndContext->FrameStat.TotalFrameCount += 1;

  if ((timeInterval = (pWndContext->Timer.TotalElapsed() - pWndContext->FrameStat.LastTimeStamp)) >= 1.0) {
    wchar_t buff[128];
    _snwprintf_s(buff, _countof(buff), L"%s, FPS:%3.1f, MSPF:%.3f", pWndContext->FrameStat.Title.c_str(),
                 (float)(pWndContext->FrameStat.LastFrameCount / timeInterval),
                 (float)(timeInterval / pWndContext->FrameStat.LastFrameCount));
    pWndContext->FrameStat.LastTimeStamp = pWndContext->Timer.TotalElapsed();
    pWndContext->FrameStat.LastFrameCount = 0;
    SetWindowTextW(hwnd, buff);
  }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern LRESULT CALLBACK ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {

  ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);

  _WindowContext *pWndContext = reinterpret_cast<_WindowContext *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
  switch (msg) {
  case WM_CREATE: {
    LPCREATESTRUCT pCreateStruct = (LPCREATESTRUCT)lp;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)pCreateStruct->lpCreateParams);
    return 0;
  }
    // WM_ACTIVATE is sent when the window is activated or deactivated.
    // We pause the game when the window is deactivated and unpause it
    // when it becomes active.
  case WM_ACTIVATE:
    if (LOWORD(wp) == WA_INACTIVE) {
      pWndContext->Paused = TRUE;
      pWndContext->Timer.Stop();
    } else {
      pWndContext->Paused = FALSE;
      pWndContext->Timer.Resume();
    }
    return 0;
    // WM_SIZE is sent when the user resizes the window.
  case WM_SIZE:
    LONG cx, cy;
    cx = LOWORD(lp);
    cy = HIWORD(lp);
    if (wp == SIZE_MINIMIZED) {
      pWndContext->Paused = TRUE;
      pWndContext->Timer.Stop();
    } else if (!(pWndContext->Paused & 0x10)) {
      pWndContext->pRenderer->ResizeFrame(cx, cy);
      pWndContext->pUIController->OnResize(cx, cy);
    }
    break;
    // WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
  case WM_ENTERSIZEMOVE:
    pWndContext->Paused = 0x11;
    pWndContext->Timer.Stop();
    break;
    // WM_EXITSIZEMOVE is sent when the user releases the resize bars.
    // Here we reset everything based on the new window dimensions.
  case WM_EXITSIZEMOVE:
    RECT rect;
    GetClientRect(hwnd, &rect);
    pWndContext->Paused = 0x0;
    pWndContext->Timer.Resume();
    pWndContext->pRenderer->ResizeFrame(rect.right, rect.bottom);
    pWndContext->pUIController->OnResize(rect.right, rect.bottom);
    break;
    // WM_DESTROY is sent when the window is being destroyed.
  case WM_DESTROY:
    PostQuitMessage(0);
    return 0;
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

  case WM_LBUTTONDOWN:
  case WM_MBUTTONDOWN:
  case WM_RBUTTONDOWN:
  case WM_LBUTTONUP:
  case WM_MBUTTONUP:
  case WM_RBUTTONUP:
    pWndContext->pUIController->OnMouseButtonEvent((UI_MOUSE_BUTTON_EVENT)msg, (UI_MOUSE_VIRTUAL_KEY)wp, LOWORD(lp),
                                                   HIWORD(lp));
  case WM_MOUSEMOVE:
    pWndContext->pUIController->OnMouseMove((UI_MOUSE_VIRTUAL_KEY)wp, LOWORD(lp), HIWORD(lp));
    break;
  case WM_MOUSEWHEEL:
    pWndContext->pUIController->OnMouseWheel((UI_MOUSE_VIRTUAL_KEY)GET_KEYSTATE_WPARAM(wp), GET_WHEEL_DELTA_WPARAM(wp),
                                             LOWORD(lp), HIWORD(lp));
    break;
  case WM_IME_KEYDOWN:
  case WM_KEYDOWN:
    break;
  case WM_CHAR:
    pWndContext->pUIController->OnKeyEvent(0, (unsigned short)wp, LOWORD(lp));
  case WM_KEYUP:
    if (wp == VK_ESCAPE) {
      PostMessage(hwnd, WM_CLOSE, 0, 0);
      return 0;
    }
    pWndContext->pUIController->OnKeyEvent(1, (unsigned short)wp, 1);

    return 0;
  }

  return DefWindowProc(hwnd, msg, wp, lp);
}

void BeginCaptureWindowInput() {
  POINT pt;
  HWND hwnd;
  GetCursorPos(&pt);
  hwnd = WindowFromPoint(pt);

  SetCapture(hwnd);
}

void EndCaptureWindowInput() { ReleaseCapture(); }