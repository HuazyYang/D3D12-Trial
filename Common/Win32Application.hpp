#pragma once
#include <sal.h>

class D3D12RendererContext;
struct IUIController;

extern
int RunSample(
  _In_ D3D12RendererContext *pRenderer,
  _In_ IUIController *pUIState,
  _In_ int iWindowWidth,
  _In_ int iWindowHeight,
  _In_ const wchar_t *pWindowTitle
);

extern
void BeginCaptureWindowInput();
extern
void EndCaptureWindowInput();
