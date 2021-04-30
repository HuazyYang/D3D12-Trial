#pragma once
#include <sal.h>
#include "DXUTmisc.h"

class D3D12RendererContext;

//
// WindowInteractor
//    Perform window interaction including picking and control.
class WindowInteractor {
public:
  WindowInteractor();
  virtual ~WindowInteractor();

  HWND GetHwnd() const;

  void Reset();

  bool IsPaused() const;
  void SetPaused(bool paused);

  void Tick();
  double GetTotalTime() const;
  double GetElapsedTime() const;
  double GetFPS() const;

  virtual LRESULT OnMsgProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, bool *pbNoFurtherProcessing);

protected:
  friend int RunSample(
    _In_ D3D12RendererContext *pRenderer,
    _In_ WindowInteractor *pInteractor,
    _In_ int iWindowWidth,
    _In_ int iWindowHeight,
    _In_ const wchar_t *pWindowTitle
  );

  HWND m_hWnd;
  bool m_bEnabled;

  struct FrameStatistics {
    uint32_t LastFrameCount;
    uint32_t TotalFrameCount;
    double LastTimeStamp;
    double FPS;
  } mutable m_FrameStat;

  DXUT::CDXUTTimer m_GlobalTimer;
};

extern
int RunSample(
  _In_ D3D12RendererContext *pRenderer,
  _In_ WindowInteractor *pInteractor,
  _In_ int iWindowWidth,
  _In_ int iWindowHeight,
  _In_ const wchar_t *pWindowTitle
);

