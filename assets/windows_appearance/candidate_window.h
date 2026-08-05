// Copyright 2010-2026, Google LLC. All rights reserved.
// This file has been modified for modern Windows appearance.

#ifndef MOZC_RENDERER_WIN32_CANDIDATE_WINDOW_H_
#define MOZC_RENDERER_WIN32_CANDIDATE_WINDOW_H_

#include <atlbase.h>
#include <atltypes.h>
#include <atlwin.h>
#include <wil/resource.h>
#include <windows.h>
#include <windowsx.h>

#include <cstdint>
#include <memory>

#include "base/const.h"
#include "base/coordinates.h"
#include "client/client_interface.h"
#include "protocol/candidate_window.pb.h"
#include "protocol/commands.pb.h"
#include "protocol/renderer_style.pb.h"
#include "renderer/table_layout.h"
#include "renderer/win32/text_renderer.h"

namespace mozc {
namespace renderer {
namespace win32 {

typedef ATL::CWinTraits<WS_POPUP | WS_DISABLED,
                        WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE>
    CandidateWindowTraits;

class CandidateWindow : public ATL::CWindowImpl<CandidateWindow, ATL::CWindow,
                                                CandidateWindowTraits> {
 public:
  DECLARE_WND_CLASS_EX(kCandidateWindowClassName, CS_SAVEBITS | CS_DROPSHADOW,
                       COLOR_WINDOW);

  BEGIN_MSG_MAP(CandidateWindow)
  MESSAGE_HANDLER(WM_CREATE, OnCreate)
  MESSAGE_HANDLER(WM_DESTROY, OnDestroy)
  MESSAGE_HANDLER(WM_ERASEBKGND, OnEraseBkgnd)
  MESSAGE_HANDLER(WM_GETMINMAXINFO, OnGetMinMaxInfo)
  MESSAGE_HANDLER(WM_LBUTTONDOWN, OnLButtonDown)
  MESSAGE_HANDLER(WM_LBUTTONUP, OnLButtonUp)
  MESSAGE_HANDLER(WM_MOUSEMOVE, OnMouseMove)
  MESSAGE_HANDLER(WM_SETTINGCHANGE, OnSettingChange)
  MESSAGE_HANDLER(WM_DWMCOLORIZATIONCOLORCHANGED, OnThemeOrColorChanged)
  MESSAGE_HANDLER(WM_THEMECHANGED, OnThemeOrColorChanged)
  MESSAGE_HANDLER(WM_SYSCOLORCHANGE, OnThemeOrColorChanged)
  MESSAGE_HANDLER(WM_PAINT, OnPaint)
  MESSAGE_HANDLER(WM_PRINTCLIENT, OnPrintClient)
  END_MSG_MAP()

  CandidateWindow();
  CandidateWindow(const CandidateWindow&) = delete;
  CandidateWindow& operator=(const CandidateWindow&) = delete;
  ~CandidateWindow();

  LRESULT OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnGetMinMaxInfo(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnThemeOrColorChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);
  LRESULT OnPrintClient(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled);

  void UpdateDpi(uint32_t dpi);
  void UpdateLayout(const commands::CandidateWindow& candidates);
  void SetSendCommandInterface(
      client::SendCommandInterface* send_command_interface);

  Size GetLayoutSize() const;
  Rect GetSelectionRectInScreenCord() const;
  Rect GetCandidateColumnInClientCord() const;
  Rect GetFirstRowInClientCord() const;
  void set_mouse_moving(bool moving);

 private:
  void DoPaint(HDC dc);
  void DrawBackground(HDC dc);
  void DrawShortcutBackground(HDC dc);
  void DrawSelectedRect(HDC dc);
  void DrawCells(HDC dc);
  void DrawInformationIcon(HDC dc);
  void DrawVScrollBar(HDC dc);
  void DrawFooter(HDC dc);
  void DrawFrame(HDC dc);
  void EnableOrDisableWindowForWorkaround();
  void HandleMouseEvent(UINT nFlags, const CPoint& point,
                        bool close_candidatewindow);
  void UpdateDpiDependentResources();

  std::unique_ptr<commands::CandidateWindow> candidate_window_;
  wil::unique_hbitmap footer_logo_;
  Size footer_logo_display_size_;
  client::SendCommandInterface* send_command_interface_;
  std::unique_ptr<TableLayout> table_layout_;
  RendererStyle style_;
  uint32_t dpi_;
  std::unique_ptr<TextRenderer> text_renderer_;
  int indicator_width_;
  bool metrics_changed_;
  bool mouse_moving_;
};

}  // namespace win32
}  // namespace renderer
}  // namespace mozc

#endif  // MOZC_RENDERER_WIN32_CANDIDATE_WINDOW_H_
