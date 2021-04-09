#pragma once

enum UI_MOUSE_VIRTUAL_KEY {
  UI_MK_CONTROL = 0x0008,
  UI_MK_LBUTTON = 0x0001,
  UI_MK_MBUTTON = 0x0010,
  UI_MK_RBUTTON = 0x0002,
  UI_MK_SHIFT = 0x0004,
  UI_MK_XBUTTON1 = 0x0020,
  UI_MK_XBUTTON2 = 0x0040,
};

enum UI_MOUSE_BUTTON_EVENT {
  UI_WM_LBUTTONDOWN       = 0x0201,
  UI_WM_LBUTTONUP         = 0x0202,
  UI_WM_LBUTTONDBLCLK     = 0x0203,
  UI_WM_RBUTTONDOWN       = 0x0204,
  UI_WM_RBUTTONUP         = 0x0205,
  UI_WM_RBUTTONDBLCLK     = 0x0206,
  UI_WM_MBUTTONDOWN       = 0x0207,
  UI_WM_MBUTTONUP         = 0x0208,
  UI_WM_MBUTTONDBLCLK     = 0x0209,
  UI_WM_XBUTTONDOWN       = 0x020B,
  UI_WM_XBUTTONUP         = 0x020C,
  UI_WM_XBUTTONDBLCLK     = 0x020D,
};

struct IUIController {
  virtual void OnResize(int cx, int cy) {}
  virtual void OnMouseButtonEvent(UI_MOUSE_BUTTON_EVENT ev, UI_MOUSE_VIRTUAL_KEY keys, int x, int y) {}
  virtual void OnMouseMove(UI_MOUSE_VIRTUAL_KEY keys, int x, int y) {}
  virtual void OnMouseWheel(UI_MOUSE_VIRTUAL_KEY keys, int delta, int x, int y) {}
  virtual void OnKeyEvent(int downUp, unsigned short key, int repeatCnt) {}
};