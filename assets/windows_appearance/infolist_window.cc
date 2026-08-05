// Copyright 2010-2026, Google LLC. All rights reserved.
// This file has been modified for modern Windows appearance.

#include "renderer/win32/infolist_window.h"

#include <atlbase.h>
#include <atltypes.h>
#include <atlwin.h>
#include <wil/resource.h>
#include <windows.h>

#include <cstdint>
#include <string>

#include "base/coordinates.h"
#include "base/vlog.h"
#include "base/win32/wide_char.h"
#include "client/client_interface.h"
#include "protocol/candidate_window.pb.h"
#include "protocol/commands.pb.h"
#include "protocol/renderer_command.pb.h"
#include "protocol/renderer_style.pb.h"
#include "renderer/win32/text_renderer.h"
#include "renderer/win32/win32_dpi_util.h"

namespace mozc {
namespace renderer {
namespace win32 {

using mozc::commands::Information;
using mozc::commands::InformationList;
using mozc::commands::Output;
using mozc::commands::SessionCommand;
using mozc::renderer::RendererStyle;

namespace {
const UINT_PTR kIdDelayShowHideTimer = 100;

void FillSolidRect(HDC dc, const RECT* rect, COLORREF color) {
  COLORREF old_color = ::SetBkColor(dc, color);
  if (old_color != CLR_INVALID) {
    ::ExtTextOut(dc, 0, 0, ETO_OPAQUE, rect, nullptr, 0, nullptr);
    ::SetBkColor(dc, old_color);
  }
}

void ApplyDwmAttributes(HWND hwnd) {
  HMODULE hDwm = ::GetModuleHandleW(L"dwmapi.dll");
  if (!hDwm) {
    hDwm = ::LoadLibraryW(L"dwmapi.dll");
  }
  if (hDwm) {
    typedef HRESULT (WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
    auto pDwmSetWindowAttribute = reinterpret_cast<PFN_DwmSetWindowAttribute>(
        ::GetProcAddress(hDwm, "DwmSetWindowAttribute"));
    if (pDwmSetWindowAttribute) {
      DWORD preference = 2; // DWMWCP_ROUNDED
      pDwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &preference, sizeof(preference));

      BOOL darkMode = SystemTheme::GetInstance().IsDarkMode() ? TRUE : FALSE;
      pDwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkMode, sizeof(darkMode));
    }
  }
}

}  // namespace

InfolistWindow::InfolistWindow()
    : send_command_interface_(nullptr),
      candidate_window_(new commands::CandidateWindow),
      dpi_(::GetDpiForSystem()),
      text_renderer_(TextRenderer::Create(dpi_)),
      style_(new RendererStyle),
      metrics_changed_(false),
      visible_(false) {
  GetScaledRendererStyle(style_.get(), dpi_);
}

InfolistWindow::~InfolistWindow() {}

LRESULT InfolistWindow::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  ApplyDwmAttributes(m_hWnd);
  return 0;
}

void InfolistWindow::UpdateDpi(uint32_t dpi) {
  if (dpi == dpi_) return;
  dpi_ = dpi;
  GetScaledRendererStyle(style_.get(), dpi_);
  text_renderer_->OnDpiChanged(dpi_);
}

LRESULT InfolistWindow::OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  ::PostQuitMessage(0);
  return 0;
}

LRESULT InfolistWindow::OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  return TRUE;
}

LRESULT InfolistWindow::OnGetMinMaxInfo(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  MINMAXINFO* min_max_info = reinterpret_cast<MINMAXINFO*>(lParam);
  if (min_max_info) {
    min_max_info->ptMinTrackSize.x = 1;
    min_max_info->ptMinTrackSize.y = 1;
  }
  bHandled = TRUE;
  return 0;
}

LRESULT InfolistWindow::OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  HDC dc = reinterpret_cast<HDC>(wParam);
  CRect client_rect;
  this->GetClientRect(&client_rect);

  wil::unique_hdc_paint paint_dc;
  if (dc == nullptr) {
    paint_dc = wil::BeginPaint(this->m_hWnd);
  }
  HDC target_dc = paint_dc.is_valid() ? paint_dc.get() : dc;

  wil::unique_hdc memdc(::CreateCompatibleDC(target_dc));
  wil::unique_hbitmap bitmap(::CreateCompatibleBitmap(
      target_dc, client_rect.Width(), client_rect.Height()));
  wil::unique_select_object old_bitmap =
      wil::SelectObject(memdc.get(), bitmap.get());
  DoPaint(memdc.get());
  ::BitBlt(target_dc, client_rect.left, client_rect.top, client_rect.Width(),
           client_rect.Height(), memdc.get(), 0, 0, SRCCOPY);
  return 0;
}

LRESULT InfolistWindow::OnPrintClient(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  return OnPaint(uMsg, wParam, lParam, bHandled);
}

Size InfolistWindow::DoPaint(HDC dc) {
  const bool dark_mode = SystemTheme::GetInstance().IsDarkMode();
  if (dc != nullptr) {
    ::SetBkMode(dc, TRANSPARENT);
    const CRect bg_rect(0, 0, style_->infolist_style().window_width(), 1000);
    const COLORREF bg_color = dark_mode ? RGB(32, 32, 36) : RGB(252, 252, 254);
    FillSolidRect(dc, &bg_rect, bg_color);
  }
  const RendererStyle::InfolistStyle& infostyle = style_->infolist_style();
  const InformationList& usages = candidate_window_->usages();

  int ypos = infostyle.window_border() + 4;

  if ((dc != nullptr) && infostyle.has_caption_string()) {
    const RendererStyle::TextStyle& caption_style = infostyle.caption_style();
    const int caption_height = infostyle.caption_height();

    const Rect caption_rect(
        infostyle.window_border() + 8,
        ypos,
        infostyle.window_width() - infostyle.window_border() * 2 - 16,
        caption_height);
    const std::wstring caption_str =
        mozc::win32::Utf8ToWide(infostyle.caption_string());

    text_renderer_->RenderText(dc, caption_str, caption_rect,
                               TextRenderer::FONTSET_INFOLIST_CAPTION);
  }
  ypos += infostyle.caption_height();

  for (int i = 0; i < usages.information_size(); ++i) {
    Size size = DoPaintRow(dc, i, ypos);
    ypos += size.height;
  }
  ypos += infostyle.window_border() + 4;

  if (dc != nullptr) {
    const CRect rect(0, 0, infostyle.window_width(), ypos);
    const COLORREF border_color = dark_mode ? RGB(65, 65, 70) : RGB(220, 220, 225);
    wil::unique_hbrush brush(::CreateSolidBrush(border_color));
    ::FrameRect(dc, &rect, brush.get());
  }

  return Size(style_->infolist_style().window_width(), ypos);
}

Size InfolistWindow::DoPaintRow(HDC dc, int row, int ypos) {
  const RendererStyle::InfolistStyle& infostyle = style_->infolist_style();
  const InformationList& usages = candidate_window_->usages();
  const RendererStyle::TextStyle& title_style = infostyle.title_style();
  const RendererStyle::TextStyle& desc_style = infostyle.description_style();
  const double scale_factor = GetDPIScalingFactor(dpi_);

  const int title_width =
      infostyle.window_width() - 16 * scale_factor;
  const int desc_width = title_width;
  const Information& info = usages.information(row);

  const bool is_focused = (usages.has_focused_index() && (row == usages.focused_index()));

  const std::wstring title_str = mozc::win32::Utf8ToWide(info.title());
  const Size title_size = text_renderer_->MeasureStringMultiLine(
      is_focused ? TextRenderer::FONTSET_INFOLIST_TITLE_FOCUSED
                 : TextRenderer::FONTSET_INFOLIST_TITLE,
      title_str, title_width);

  const std::wstring desc_str = mozc::win32::Utf8ToWide(info.description());
  const Size desc_size = text_renderer_->MeasureStringMultiLine(
      is_focused ? TextRenderer::FONTSET_INFOLIST_DESCRIPTION_FOCUSED
                 : TextRenderer::FONTSET_INFOLIST_DESCRIPTION,
      desc_str, desc_width);

  int row_height =
      title_size.height + desc_size.height + static_cast<int>(12 * scale_factor);

  if (dc == nullptr) {
    return Size(0, row_height);
  }

  const Rect title_rect(
      static_cast<int>(8 * scale_factor),
      ypos + static_cast<int>(4 * scale_factor),
      title_width, title_size.height);
  const Rect desc_rect(
      static_cast<int>(8 * scale_factor),
      title_rect.Top() + title_rect.Height() + static_cast<int>(2 * scale_factor),
      desc_width, desc_size.height);

  if (is_focused) {
    CRect selected_rect(
        static_cast<int>(4 * scale_factor),
        ypos,
        infostyle.window_width() - static_cast<int>(4 * scale_factor),
        ypos + row_height);

    const COLORREF bg_color = SystemTheme::GetInstance().GetAccentColor();
    wil::unique_hbrush brush(::CreateSolidBrush(bg_color));
    wil::unique_hpen pen(::CreatePen(PS_SOLID, 1, bg_color));
    const auto old_brush = wil::SelectObject(dc, brush.get());
    const auto old_pen = wil::SelectObject(dc, pen.get());

    const int corner_radius = static_cast<int>(5 * scale_factor);
    ::RoundRect(dc, selected_rect.left, selected_rect.top,
               selected_rect.right, selected_rect.bottom,
               corner_radius * 2, corner_radius * 2);
  }

  text_renderer_->RenderText(
      dc, title_str, title_rect,
      is_focused ? TextRenderer::FONTSET_INFOLIST_TITLE_FOCUSED
                 : TextRenderer::FONTSET_INFOLIST_TITLE);

  text_renderer_->RenderText(
      dc, desc_str, desc_rect,
      is_focused ? TextRenderer::FONTSET_INFOLIST_DESCRIPTION_FOCUSED
                 : TextRenderer::FONTSET_INFOLIST_DESCRIPTION);

  return Size(0, row_height);
}

LRESULT InfolistWindow::OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  SystemTheme::GetInstance().Update();
  text_renderer_->OnThemeChanged();
  if (m_hWnd) {
    ApplyDwmAttributes(m_hWnd);
    Invalidate();
  }
  UINT uFlags = static_cast<UINT>(wParam);
  switch (uFlags) {
    case 0x1049:
    case SPI_SETFONTSMOOTHING:
    case SPI_SETFONTSMOOTHINGCONTRAST:
    case SPI_SETFONTSMOOTHINGORIENTATION:
    case SPI_SETFONTSMOOTHINGTYPE:
    case SPI_SETNONCLIENTMETRICS:
      metrics_changed_ = true;
      break;
    default:
      break;
  }
  return 0;
}

LRESULT InfolistWindow::OnThemeOrColorChanged(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  SystemTheme::GetInstance().Update();
  text_renderer_->OnThemeChanged();
  if (m_hWnd) {
    ApplyDwmAttributes(m_hWnd);
    Invalidate();
  }
  return 0;
}

LRESULT InfolistWindow::OnTimer(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  UINT_PTR nIDEvent = static_cast<UINT_PTR>(wParam);
  if (nIDEvent != kIdDelayShowHideTimer) return 0;
  if (visible_) {
    DelayShow(0);
  } else {
    DelayHide(0);
  }
  return 0;
}

void InfolistWindow::DelayShow(UINT mseconds) {
  visible_ = true;
  KillTimer(kIdDelayShowHideTimer);
  if (mseconds <= 0) {
    SetWindowPos(HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    SendMessageW(WM_NCACTIVATE, FALSE);
  } else {
    SetTimer(kIdDelayShowHideTimer, mseconds, nullptr);
  }
}

void InfolistWindow::DelayHide(UINT mseconds) {
  visible_ = false;
  KillTimer(kIdDelayShowHideTimer);
  if (mseconds <= 0) {
    ShowWindow(SW_HIDE);
  } else {
    SetTimer(kIdDelayShowHideTimer, mseconds, nullptr);
  }
}

void InfolistWindow::UpdateLayout(const commands::CandidateWindow& candidate_window) {
  *candidate_window_ = candidate_window;
  if (metrics_changed_) {
    text_renderer_->OnThemeChanged();
    metrics_changed_ = false;
  }
}

void InfolistWindow::SetSendCommandInterface(client::SendCommandInterface* send_command_interface) {
  send_command_interface_ = send_command_interface;
}

Size InfolistWindow::GetLayoutSize() { return DoPaint(nullptr); }

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
