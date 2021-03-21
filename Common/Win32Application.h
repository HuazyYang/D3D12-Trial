#pragma once
#include <Windows.h>
#include <string>
#include "GameTimer.h"

class RendererSample;

class Win32Application {

public:
  int Run(RendererSample *render, HINSTANCE hInstance, UINT nCmdShow);

private:
    int InitWindow();
    static LRESULT CALLBACK MainWndProc( HWND hwnd, UINT uMsg, WPARAM wp, LPARAM lp );
    LRESULT MsgProc( HWND hwnd, UINT uMsg, WPARAM wp, LPARAM lp );
    void OnSize(int nType, int cx, int cy);
    LRESULT OnMouseEvent( UINT uMsg, WPARAM wParam, int x, int y );
    LRESULT OnKeyDownEvent( WPARAM wParam, LPARAM lParam );
    LRESULT OnKeyUpEvent(WPARAM wParam, LPARAM lParam);

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

  GameTimer m_GameTimer;
}