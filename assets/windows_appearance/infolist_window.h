// Copyright 2010-2026, Google LLC. All rights reserved.
// This file has been modified for modern Windows appearance.

#ifndef MOZC_RENDERER_WIN32_INFOLIST_WINDOW_H_
#define MOZC_RENDERER_WIN32_INFOLIST_WINDOW_H_

#include <atlbase.h>
#include <atltypes.h>
#include <atlwin.h>
#include <windows.h>

#include <cstdint>
#include <memory>
#include <string>

#include "base/const.h"
#include "base/coordinates.h"
#include "client/client_interface.h"
#include "protocol/renderer_command.pb.h"
#include "protocol/renderer_style.pb.h"
#include "renderer/win32/text_renderer.h"

namespace mozc {
namespace renderer {
namespace win32 {

typedef ATL::CWinTraits<WS_POPUP | WS_DISABLED, WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE>
    InfolistWindowTraits;

class InfolistWindow : public ATL::CWindowImpl<InfolistWindow, ATL::CWindow, InfolistWindowTraits> {
 public:
  DECLARE_WND_CLASS_EX(kInfolistWindowClassName, CS_SAVEBITS | CS_DROPSHADOW, COLOR_WINDOW);

  BEGIN_MSG_MAP(InfolistWindow)
  MESSAGE_HANDLER(WM_CREATE, OnCreate)
  MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
  MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
  MESSAGE_HANDLER(WM_GETMINMAXINFO, OnGetMinMaxInfo)
  MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
  MESSAGE_HANDLER(WM_DWMCOLORIZATIONCOLORCHANGED, OnThemeOrColorChanged)
  MESSAGE_HANDLER(WM_THEMECHANGED, OnThemeOrColorChanged)
  MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnThemeOrColorChanged)
  MESSAGE_HANDLER(WM_PAINT, OnPaint)
  MESSAGE_HANDLER(WM_PRINTCLIENT, OnPrintClient)
  MESSAGE_HANDLER(WM_TIMER, OnTimer)
  END_MSG_MAP()

  InfolistWindow();
  InfolistWindow(const InfolistWindow&) = delete;
  InfolistWindow& operator=(const InfolistWindow&) = delete;
  ~InfolistWindow();

  LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnGetMinMaxInfo(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnThemeOrColorChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnPrintClient(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

  void UpdateDpi(uint32_t dpi);
  void UpdateLayout(const commands::CandidateWindow& candidates);
  void SetSendCommandInterface(client::SendCommandInterface* send_command_interface);

  Size GetLayoutSize();
  void DelayShow(UINT mseconds);
  void DelayHide(UINT mseconds);

 private:
  Size DoPaint(HDC dc);
  Size DoPaintRow(HDC dc, int row, int ypos);

  client::SendCommandInterface* send_command_interface_;
  std::unique_ptr<commands::CandidateWindow> candidate_window_;
  uint32_t dpi_;
  std::unique_ptr<TextRenderer> text_renderer_;
  std::unique_ptr<RendererStyle> style_;
  bool metrics_changed_;
  bool visible_;
};

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_INFOLIST_WINDOW_H_
