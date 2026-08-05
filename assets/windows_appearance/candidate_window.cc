// Copyright 2010-2026, Google LLC. All rights reserved.
// This file has been modified for modern Windows appearance.

#include "renderer/win32/candidate_window.h"

#include <atlbase.h>
#include <atltypes.h>
#include <atlwin.h>
#include <wil/resource.h>
#include <windows.h>
#include <windowsx.h>

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "absl/log/check.h"
#include "absl/log/log.h"
#include "base/coordinates.h"
#include "base/win32/wide_char.h"
#include "client/client_interface.h"
#include "protocol/candidate_window.pb.h"
#include "protocol/renderer_command.pb.h"
#include "renderer/renderer_style_handler.h"
#include "renderer/table_layout.h"
#include "renderer/win32/resource.h"
#include "renderer/win32/text_renderer.h"
#include "renderer/win32/win32_dpi_util.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

constexpr int kIndicatorWidthInDefaultDPI = 4;

enum COLUMN_TYPE {
  COLUMN_SHORTCUT = 0,
  COLUMN_GAP1,
  COLUMN_CANDIDATE,
  COLUMN_GAP2,
  COLUMN_DESCRIPTION,
  NUMBER_OF_COLUMNS,
};

CRect ToCRect(const Rect& rect) {
  return CRect(rect.Left(), rect.Top(), rect.Right(), rect.Bottom());
}

int GetCandidateArrayIndexByCandidateIndex(const commands::CandidateWindow& candidate_window,
                                           int candidate_index) {
  for (size_t i = 0; i < candidate_window.candidate_size(); ++i) {
    const commands::CandidateWindow::Candidate& candidate = candidate_window.candidate(i);
    if (candidate.index() == candidate_index) {
      return i;
    }
  }
  return candidate_window.candidate_size();
}

std::string GetIndexGuideString(const commands::CandidateWindow& candidate_window) {
  if (!candidate_window.has_footer() || !candidate_window.footer().index_visible()) {
    return "";
  }
  const int focused_index = candidate_window.focused_index();
  const int total_items = candidate_window.size();

  std::stringstream footer_string;
  footer_string << focused_index + 1 << "/" << total_items << " ";
  return footer_string.str();
}

int GetFocusedArrayIndex(const commands::CandidateWindow& candidate_window) {
  const int invalid_index = candidate_window.candidate_size();
  if (!candidate_window.has_focused_index()) {
    return invalid_index;
  }
  return GetCandidateArrayIndexByCandidateIndex(candidate_window, candidate_window.focused_index());
}

std::wstring GetDisplayStringByColumn(const commands::CandidateWindow::Candidate& candidate,
                                      COLUMN_TYPE column_type) {
  std::wstring display_string;

  switch (column_type) {
    case COLUMN_SHORTCUT:
      if (candidate.has_annotation()) {
        const commands::Annotation& annotation = candidate.annotation();
        if (annotation.has_shortcut()) {
          display_string = mozc::win32::Utf8ToWide(annotation.shortcut());
        }
      }
      break;
    case COLUMN_CANDIDATE:
      if (candidate.has_value()) {
        display_string = mozc::win32::Utf8ToWide(candidate.value());
      }
      if (candidate.has_annotation()) {
        const commands::Annotation& annotation = candidate.annotation();
        if (annotation.has_prefix()) {
          display_string = mozc::win32::Utf8ToWide(annotation.prefix()) + display_string;
        }
        if (annotation.has_suffix()) {
          display_string += mozc::win32::Utf8ToWide(annotation.suffix());
        }
      }
      break;
    case COLUMN_DESCRIPTION:
      if (candidate.has_annotation()) {
        const commands::Annotation& annotation = candidate.annotation();
        if (annotation.has_description()) {
          display_string = mozc::win32::Utf8ToWide(annotation.description());
        }
      }
      break;
    default:
      break;
  }
  return display_string;
}

HBITMAP LoadBitmapFromResource(HMODULE module, int resource_id) {
  return reinterpret_cast<HBITMAP>(
      ::LoadImage(module, MAKEINTRESOURCE(resource_id), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION));
}

void FillSolidRect(HDC dc, const RECT* rect, COLORREF color) {
  COLORREF old_color = ::SetBkColor(dc, color);
  if (old_color != CLR_INVALID) {
    ::ExtTextOut(dc, 0, 0, ETO_OPAQUE, rect, nullptr, 0, nullptr);
    ::SetBkColor(dc, old_color);
  }
}

COLORREF ToColorRef(const RendererStyle::RGBAColor& color) {
  return RGB(color.r(), color.g(), color.b());
}

void ApplyDwmAttributes(HWND hwnd) {
  HMODULE hDwm = ::GetModuleHandleW(L"dwmapi.dll");
  if (!hDwm) {
    hDwm = ::LoadLibraryW(L"dwmapi.dll");
  }
  if (hDwm) {
    typedef HRESULT(WINAPI * PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
    auto pDwmSetWindowAttribute = reinterpret_cast<PFN_DwmSetWindowAttribute>(
        ::GetProcAddress(hDwm, "DwmSetWindowAttribute"));
    if (pDwmSetWindowAttribute) {
      DWORD preference = 2;  // DWMWCP_ROUNDED
      pDwmSetWindowAttribute(hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &preference,
                             sizeof(preference));

      BOOL darkMode = SystemTheme::GetInstance().IsDarkMode() ? TRUE : FALSE;
      pDwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &darkMode,
                             sizeof(darkMode));
    }
  }
}

}  // namespace

CandidateWindow::CandidateWindow()
    : candidate_window_(std::make_unique<commands::CandidateWindow>()),
      footer_logo_display_size_(0, 0),
      send_command_interface_(nullptr),
      table_layout_(std::make_unique<TableLayout>()),
      dpi_(::GetDpiForSystem()),
      text_renderer_(TextRenderer::Create(dpi_)),
      indicator_width_(0),
      metrics_changed_(false),
      mouse_moving_(true) {
  UpdateDpiDependentResources();
}

CandidateWindow::~CandidateWindow() = default;

void CandidateWindow::UpdateDpiDependentResources() {
  GetScaledRendererStyle(&style_, dpi_);
  const double scale_factor = GetDPIScalingFactor(dpi_);
  double image_scale_factor = 1.0;
  if (scale_factor < 1.125) {
    footer_logo_.reset(
        LoadBitmapFromResource(::GetModuleHandle(nullptr), IDB_FOOTER_LOGO_COLOR_100));
    image_scale_factor = 1.0;
  } else if (scale_factor < 1.375) {
    footer_logo_.reset(
        LoadBitmapFromResource(::GetModuleHandle(nullptr), IDB_FOOTER_LOGO_COLOR_125));
    image_scale_factor = 1.25;
  } else if (scale_factor < 1.75) {
    footer_logo_.reset(
        LoadBitmapFromResource(::GetModuleHandle(nullptr), IDB_FOOTER_LOGO_COLOR_150));
    image_scale_factor = 1.5;
  } else {
    footer_logo_.reset(
        LoadBitmapFromResource(::GetModuleHandle(nullptr), IDB_FOOTER_LOGO_COLOR_200));
    image_scale_factor = 2.0;
  }

  footer_logo_display_size_ = Size(0, 0);
  if (footer_logo_.is_valid()) {
    BITMAP bm = {};
    if (::GetObject(footer_logo_.get(), sizeof(bm), &bm)) {
      footer_logo_display_size_ = Size(bm.bmWidth * (scale_factor / image_scale_factor),
                                       bm.bmHeight * (scale_factor / image_scale_factor));
    }
  }
  indicator_width_ = kIndicatorWidthInDefaultDPI * scale_factor;
}

LRESULT CandidateWindow::OnCreate(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  EnableOrDisableWindowForWorkaround();
  ApplyDwmAttributes(m_hWnd);
  return 0;
}

void CandidateWindow::UpdateDpi(uint32_t dpi) {
  if (dpi == dpi_) return;
  dpi_ = dpi;
  UpdateDpiDependentResources();
  text_renderer_->OnDpiChanged(dpi_);
}

void CandidateWindow::EnableOrDisableWindowForWorkaround() {
  BOOL is_tracking_enabled = FALSE;
  if (::SystemParametersInfo(SPI_GETACTIVEWINDOWTRACKING, 0, &is_tracking_enabled, 0)) {
    EnableWindow(!is_tracking_enabled);
  }
}

LRESULT CandidateWindow::OnDestroy(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  ::PostQuitMessage(0);
  return 0;
}

LRESULT CandidateWindow::OnEraseBkgnd(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  return TRUE;
}

LRESULT CandidateWindow::OnGetMinMaxInfo(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  MINMAXINFO* min_max_info = reinterpret_cast<MINMAXINFO*>(lParam);
  if (min_max_info) {
    min_max_info->ptMinTrackSize.x = 1;
    min_max_info->ptMinTrackSize.y = 1;
  }
  bHandled = TRUE;
  return 0;
}

void CandidateWindow::HandleMouseEvent(UINT nFlags, const CPoint& point,
                                       bool close_candidatewindow) {
  if (send_command_interface_ == nullptr) return;

  for (size_t i = 0; i < candidate_window_->candidate_size(); ++i) {
    const commands::CandidateWindow::Candidate& candidate = candidate_window_->candidate(i);
    const CRect rect = ToCRect(table_layout_->GetRowRect(i));
    if (rect.PtInRect(point)) {
      commands::SessionCommand command;
      command.set_type(close_candidatewindow ? commands::SessionCommand::SELECT_CANDIDATE
                                             : commands::SessionCommand::HIGHLIGHT_CANDIDATE);
      command.set_id(candidate.id());
      commands::Output output;
      send_command_interface_->SendCommand(command, &output);
      return;
    }
  }
}

LRESULT CandidateWindow::OnLButtonDown(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  UINT nFlags = static_cast<UINT>(wParam);
  CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
  HandleMouseEvent(nFlags, point, false);
  return 0;
}

LRESULT CandidateWindow::OnLButtonUp(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  UINT nFlags = static_cast<UINT>(wParam);
  CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
  HandleMouseEvent(nFlags, point, true);
  return 0;
}

LRESULT CandidateWindow::OnMouseMove(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  if (!mouse_moving_ || (wParam & MK_LBUTTON) != MK_LBUTTON) return 0;
  UINT nFlags = static_cast<UINT>(wParam);
  CPoint point(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
  HandleMouseEvent(nFlags, point, false);
  return 0;
}

LRESULT CandidateWindow::OnPaint(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  HDC dc = reinterpret_cast<HDC>(wParam);
  CRect client_rect;
  this->GetClientRect(&client_rect);

  wil::unique_hdc_paint paint_dc;
  if (dc == nullptr) paint_dc = wil::BeginPaint(this->m_hWnd);
  HDC target_dc = paint_dc.is_valid() ? paint_dc.get() : dc;

  wil::unique_hdc memdc(::CreateCompatibleDC(target_dc));
  wil::unique_hbitmap bitmap(
      ::CreateCompatibleBitmap(target_dc, client_rect.Width(), client_rect.Height()));
  wil::unique_select_object old_bitmap = wil::SelectObject(memdc.get(), bitmap.get());
  DoPaint(memdc.get());
  ::BitBlt(target_dc, client_rect.left, client_rect.top, client_rect.Width(), client_rect.Height(),
           memdc.get(), 0, 0, SRCCOPY);
  return 0;
}

LRESULT CandidateWindow::OnPrintClient(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
  return OnPaint(uMsg, wParam, lParam, bHandled);
}

void CandidateWindow::DoPaint(HDC dc) {
  switch (candidate_window_->category()) {
    case commands::CONVERSION:
    case commands::PREDICTION:
    case commands::TRANSLITERATION:
    case commands::SUGGESTION:
    case commands::USAGE:
      break;
    default:
      return;
  }

  if (!table_layout_->IsLayoutFrozen()) return;

  ::SetBkMode(dc, TRANSPARENT);

  DrawBackground(dc);
  DrawShortcutBackground(dc);
  DrawSelectedRect(dc);
  DrawCells(dc);
  DrawInformationIcon(dc);
  DrawVScrollBar(dc);
  DrawFooter(dc);
  DrawFrame(dc);
}

LRESULT CandidateWindow::OnSettingChange(UINT uMsg, WPARAM wParam, LPARAM lParam, BOOL& bHandled) {
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
    case SPI_SETACTIVEWINDOWTRACKING:
      EnableOrDisableWindowForWorkaround();
      [[fallthrough]];
    default:
      break;
  }
  return 0;
}

LRESULT CandidateWindow::OnThemeOrColorChanged(UINT uMsg, WPARAM wParam, LPARAM lParam,
                                               BOOL& bHandled) {
  SystemTheme::GetInstance().Update();
  text_renderer_->OnThemeChanged();
  if (m_hWnd) {
    ApplyDwmAttributes(m_hWnd);
    Invalidate();
  }
  return 0;
}

void CandidateWindow::UpdateLayout(const commands::CandidateWindow& candidates) {
  *candidate_window_ = candidates;

  if (metrics_changed_) {
    text_renderer_->OnThemeChanged();
    metrics_changed_ = false;
  }

  table_layout_->Initialize(candidate_window_->candidate_size(), NUMBER_OF_COLUMNS);
  table_layout_->SetWindowBorder(style_.window_border());

  if (candidate_window_->candidate_size() < candidate_window_->size()) {
    table_layout_->SetVScrollBar(indicator_width_);
  }

  if (candidate_window_->has_footer()) {
    Size footer_size(0, 0);
    if (candidate_window_->footer().has_label()) {
      const std::wstring footer_label =
          mozc::win32::Utf8ToWide(candidate_window_->footer().label());
      const Size label_string_size = text_renderer_->MeasureString(
          TextRenderer::FONTSET_FOOTER_LABEL, L" " + footer_label + L" ");
      footer_size.width += label_string_size.width;
      footer_size.height = std::max(footer_size.height, label_string_size.height);
    } else if (candidate_window_->footer().has_sub_label()) {
      const std::wstring footer_sub_label =
          mozc::win32::Utf8ToWide(candidate_window_->footer().sub_label());
      const Size label_string_size = text_renderer_->MeasureString(
          TextRenderer::FONTSET_FOOTER_SUBLABEL, L" " + footer_sub_label + L" ");
      footer_size.width += label_string_size.width;
      footer_size.height = std::max(footer_size.height, label_string_size.height);
    }

    if (candidate_window_->footer().index_visible()) {
      const std::wstring index_guide_string =
          mozc::win32::Utf8ToWide(GetIndexGuideString(*candidate_window_));
      const Size index_guide_size =
          text_renderer_->MeasureString(TextRenderer::FONTSET_FOOTER_INDEX, index_guide_string);
      footer_size.width += index_guide_size.width;
      footer_size.height = std::max(footer_size.height, index_guide_size.height);
    }

    if (footer_logo_.is_valid() && candidate_window_->footer().logo_visible()) {
      footer_size.width += footer_logo_display_size_.width;
      footer_size.height = std::max(footer_size.height, footer_logo_display_size_.height);
    }

    if (candidate_window_->candidate_size() < candidate_window_->size()) {
      const std::wstring minimum_width_as_wstring =
          mozc::win32::Utf8ToWide(style_.column_minimum_width_string());
      const Size minimum_size = text_renderer_->MeasureString(TextRenderer::FONTSET_CANDIDATE,
                                                              minimum_width_as_wstring.c_str());
      table_layout_->EnsureColumnsWidth(COLUMN_CANDIDATE, COLUMN_DESCRIPTION, minimum_size.width);
    }

    footer_size.height += style_.footer_border_colors_size();
    table_layout_->EnsureFooterSize(footer_size);
  }

  table_layout_->SetRowRectPadding(style_.row_rect_padding());
  const Size gap1_size = text_renderer_->MeasureString(TextRenderer::FONTSET_CANDIDATE, L" ");
  table_layout_->EnsureCellSize(COLUMN_GAP1, gap1_size);

  bool description_found = false;
  for (size_t i = 0; i < candidate_window_->candidate_size(); ++i) {
    const commands::CandidateWindow::Candidate& candidate = candidate_window_->candidate(i);
    const std::wstring shortcut = GetDisplayStringByColumn(candidate, COLUMN_SHORTCUT);
    const std::wstring description = GetDisplayStringByColumn(candidate, COLUMN_DESCRIPTION);
    const std::wstring candidate_string = GetDisplayStringByColumn(candidate, COLUMN_CANDIDATE);

    if (!shortcut.empty()) {
      const Size rendering_size =
          text_renderer_->MeasureString(TextRenderer::FONTSET_SHORTCUT, L" " + shortcut + L" ");
      table_layout_->EnsureCellSize(COLUMN_SHORTCUT, rendering_size);
    }
    if (!candidate_string.empty()) {
      const Size rendering_size =
          text_renderer_->MeasureString(TextRenderer::FONTSET_CANDIDATE, candidate_string);
      table_layout_->EnsureCellSize(COLUMN_CANDIDATE, rendering_size);
    }
    if (!description.empty()) {
      const Size rendering_size =
          text_renderer_->MeasureString(TextRenderer::FONTSET_DESCRIPTION, description + L" ");
      table_layout_->EnsureCellSize(COLUMN_DESCRIPTION, rendering_size);
      description_found = true;
    }
  }

  const wchar_t* gap2_string = (description_found ? L"   " : L" ");
  const Size gap2_size =
      text_renderer_->MeasureString(TextRenderer::FONTSET_CANDIDATE, gap2_string);
  table_layout_->EnsureCellSize(COLUMN_GAP2, gap2_size);

  table_layout_->FreezeLayout();
}

void CandidateWindow::SetSendCommandInterface(
    client::SendCommandInterface* send_command_interface) {
  send_command_interface_ = send_command_interface;
}

Size CandidateWindow::GetLayoutSize() const { return table_layout_->GetTotalSize(); }

Rect CandidateWindow::GetSelectionRectInScreenCord() const {
  const int focused_array_index = GetFocusedArrayIndex(*candidate_window_);
  if (0 <= focused_array_index && focused_array_index < candidate_window_->candidate_size()) {
    CRect rect = ToCRect(table_layout_->GetRowRect(focused_array_index));
    ClientToScreen(&rect);
    return Rect(rect.left, rect.top, rect.Width(), rect.Height());
  }
  return Rect();
}

Rect CandidateWindow::GetCandidateColumnInClientCord() const {
  return table_layout_->GetCellRect(0, COLUMN_CANDIDATE);
}
Rect CandidateWindow::GetFirstRowInClientCord() const { return table_layout_->GetRowRect(0); }

void CandidateWindow::DrawCells(HDC dc) {
  COLUMN_TYPE kColumnTypes[] = {COLUMN_SHORTCUT, COLUMN_CANDIDATE, COLUMN_DESCRIPTION};
  TextRenderer::FONT_TYPE kNormalFontTypes[] = {TextRenderer::FONTSET_SHORTCUT,
                                                TextRenderer::FONTSET_CANDIDATE,
                                                TextRenderer::FONTSET_DESCRIPTION};
  TextRenderer::FONT_TYPE kFocusedFontTypes[] = {TextRenderer::FONTSET_SHORTCUT_FOCUSED,
                                                 TextRenderer::FONTSET_CANDIDATE_FOCUSED,
                                                 TextRenderer::FONTSET_DESCRIPTION_FOCUSED};

  const int focused_array_index = GetFocusedArrayIndex(*candidate_window_);

  for (size_t type_index = 0; type_index < std::size(kColumnTypes); ++type_index) {
    const COLUMN_TYPE column_type = kColumnTypes[type_index];

    std::vector<TextRenderingInfo> normal_list;
    std::vector<TextRenderingInfo> focused_list;

    for (size_t i = 0; i < candidate_window_->candidate_size(); ++i) {
      const commands::CandidateWindow::Candidate& candidate = candidate_window_->candidate(i);
      const std::wstring display_string = GetDisplayStringByColumn(candidate, column_type);
      if (display_string.empty()) continue;

      const Rect text_rect = table_layout_->GetCellRect(i, column_type);
      if (static_cast<int>(i) == focused_array_index) {
        focused_list.push_back(TextRenderingInfo(display_string, text_rect));
      } else {
        normal_list.push_back(TextRenderingInfo(display_string, text_rect));
      }
    }

    if (!normal_list.empty()) {
      text_renderer_->RenderTextList(dc, normal_list, kNormalFontTypes[type_index]);
    }
    if (!focused_list.empty()) {
      text_renderer_->RenderTextList(dc, focused_list, kFocusedFontTypes[type_index]);
    }
  }
}

void CandidateWindow::DrawVScrollBar(HDC dc) {
  const Rect& vscroll_rect = table_layout_->GetVScrollBarRect();
  if (!vscroll_rect.IsRectEmpty() && candidate_window_->candidate_size() > 0) {
    const int begin_index = candidate_window_->candidate(0).index();
    const int candidates_in_page = candidate_window_->candidate_size();
    const int candidates_total = candidate_window_->size();
    const int end_index = candidate_window_->candidate(candidates_in_page - 1).index();
    const double scale_factor = GetDPIScalingFactor(dpi_);

    CRect bg_rect = ToCRect(vscroll_rect);
    bg_rect.left -= static_cast<int>(2.0 * scale_factor);

    const COLORREF bg_color = ToColorRef(style_.scrollbar_background_color());
    wil::unique_hbrush bg_brush(::CreateSolidBrush(bg_color));
    wil::unique_hpen bg_pen(::CreatePen(PS_SOLID, 1, bg_color));
    {
      const auto old_brush = wil::SelectObject(dc, bg_brush.get());
      const auto old_pen = wil::SelectObject(dc, bg_pen.get());
      const int r = static_cast<int>(2.0 * scale_factor);
      ::RoundRect(dc, bg_rect.left, bg_rect.top, bg_rect.right, bg_rect.bottom, r * 2, r * 2);
    }

    const mozc::Rect& indicator_rect =
        table_layout_->GetVScrollIndicatorRect(begin_index, end_index, candidates_total);
    CRect ind_rect = ToCRect(indicator_rect);
    ind_rect.left -= static_cast<int>(2.0 * scale_factor);

    const COLORREF ind_color = ToColorRef(style_.scrollbar_indicator_color());
    wil::unique_hbrush ind_brush(::CreateSolidBrush(ind_color));
    wil::unique_hpen ind_pen(::CreatePen(PS_SOLID, 1, ind_color));
    {
      const auto old_brush = wil::SelectObject(dc, ind_brush.get());
      const auto old_pen = wil::SelectObject(dc, ind_pen.get());
      const int r = static_cast<int>(2.0 * scale_factor);
      ::RoundRect(dc, ind_rect.left, ind_rect.top, ind_rect.right, ind_rect.bottom, r * 2, r * 2);
    }
  }
}

void CandidateWindow::DrawShortcutBackground(HDC dc) {
  if (table_layout_->number_of_columns() > 0) {
    Rect shortcut_colmun_rect = table_layout_->GetColumnRect(0);
    if (!shortcut_colmun_rect.IsRectEmpty()) {
      const Rect row_rect = table_layout_->GetRowRect(0);
      const int width = shortcut_colmun_rect.Right() - row_rect.Left();
      shortcut_colmun_rect.origin.x = row_rect.Left();
      shortcut_colmun_rect.size.width = width;
      const CRect shortcut_colmun_crect = ToCRect(shortcut_colmun_rect);
      const bool dark_mode = SystemTheme::GetInstance().IsDarkMode();
      const COLORREF bg_color = dark_mode ? RGB(32, 32, 36) : RGB(252, 252, 254);
      FillSolidRect(dc, &shortcut_colmun_crect, bg_color);
    }
  }
}

void CandidateWindow::DrawFooter(HDC dc) {
  const Rect& footer_rect = table_layout_->GetFooterRect();
  if (!candidate_window_->has_footer() || footer_rect.IsRectEmpty()) return;

  const int footer_separator_height = style_.footer_border_colors_size();
  {
    wil::unique_select_object prev_pen =
        wil::SelectObject(dc, static_cast<HPEN>(::GetStockObject(DC_PEN)));
    const bool dark_mode = SystemTheme::GetInstance().IsDarkMode();
    const COLORREF sep_color = dark_mode ? RGB(60, 60, 65) : RGB(225, 225, 230);
    ::SetDCPenColor(dc, sep_color);
    for (size_t i = 0, y = footer_rect.Top(); i < footer_separator_height; y++, i++) {
      ::MoveToEx(dc, footer_rect.Left(), y, nullptr);
      ::LineTo(dc, footer_rect.Right(), y);
    }
  }

  const Rect footer_content_rect(footer_rect.Left(), footer_rect.Top() + footer_separator_height,
                                 footer_rect.Width(),
                                 footer_rect.Height() - footer_separator_height);

  int left_used = 0;
  if (candidate_window_->footer().logo_visible() && footer_logo_.is_valid()) {
    const int top_offset = (footer_content_rect.Height() - footer_logo_display_size_.height) / 2;
    wil::unique_hdc src_dc(::CreateCompatibleDC(dc));
    wil::unique_select_object old_bitmap = wil::SelectObject(src_dc.get(), footer_logo_.get());
    BITMAP bm = {};
    ::GetObject(footer_logo_.get(), sizeof(bm), &bm);
    const CSize src_size(bm.bmWidth, bm.bmHeight);
    const BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    ::AlphaBlend(dc, footer_content_rect.Left(), footer_content_rect.Top() + top_offset,
                 footer_logo_display_size_.width, footer_logo_display_size_.height, src_dc.get(), 0,
                 0, src_size.cx, src_size.cy, bf);
    left_used = footer_content_rect.Left() + footer_logo_display_size_.width;
  }

  int right_used = 0;
  if (candidate_window_->footer().index_visible()) {
    const std::wstring index_guide_string =
        mozc::win32::Utf8ToWide(GetIndexGuideString(*candidate_window_));
    const Size index_guide_size =
        text_renderer_->MeasureString(TextRenderer::FONTSET_FOOTER_INDEX, index_guide_string);
    const Rect index_rect(footer_content_rect.Right() - index_guide_size.width,
                          footer_content_rect.Top(), index_guide_size.width,
                          footer_content_rect.Height());
    text_renderer_->RenderText(dc, index_guide_string, index_rect,
                               TextRenderer::FONTSET_FOOTER_INDEX);
    right_used = index_guide_size.width;
  }

  if (candidate_window_->footer().has_label()) {
    const Rect label_rect(left_used, footer_content_rect.Top(),
                          footer_content_rect.Width() - left_used - right_used,
                          footer_content_rect.Height());
    const std::wstring footer_label = mozc::win32::Utf8ToWide(candidate_window_->footer().label());
    text_renderer_->RenderText(dc, L" " + footer_label + L" ", label_rect,
                               TextRenderer::FONTSET_FOOTER_LABEL);
  }
}

void CandidateWindow::DrawSelectedRect(HDC dc) {
  DCHECK(table_layout_->IsLayoutFrozen()) << "Table layout is not frozen.";
  const int focused_array_index = GetFocusedArrayIndex(*candidate_window_);

  if (0 <= focused_array_index && focused_array_index < candidate_window_->candidate_size()) {
    CRect selected_rect = ToCRect(table_layout_->GetRowRect(focused_array_index));
    const double scale_factor = GetDPIScalingFactor(dpi_);
    selected_rect.DeflateRect(static_cast<int>(3 * scale_factor),
                              static_cast<int>(2 * scale_factor));

    const COLORREF bg_color = SystemTheme::GetInstance().GetAccentColor();
    wil::unique_hbrush brush(::CreateSolidBrush(bg_color));
    wil::unique_hpen pen(::CreatePen(PS_SOLID, 1, bg_color));
    const auto old_brush = wil::SelectObject(dc, brush.get());
    const auto old_pen = wil::SelectObject(dc, pen.get());

    const int corner_radius = static_cast<int>(5 * scale_factor);
    ::RoundRect(dc, selected_rect.left, selected_rect.top, selected_rect.right,
                selected_rect.bottom, corner_radius * 2, corner_radius * 2);
  }
}

void CandidateWindow::DrawInformationIcon(HDC dc) {
  DCHECK(table_layout_->IsLayoutFrozen()) << "Table layout is not frozen.";
  const double scale_factor = GetDPIScalingFactor(dpi_);
  const COLORREF accent_color = SystemTheme::GetInstance().GetAccentColor();

  for (size_t i = 0; i < candidate_window_->candidate_size(); ++i) {
    if (candidate_window_->candidate(i).has_information_id()) {
      CRect rect = ToCRect(table_layout_->GetRowRect(i));
      rect.left = rect.right - static_cast<int>(8.0 * scale_factor);
      rect.right = rect.left + static_cast<int>(3.0 * scale_factor);
      rect.top += static_cast<int>(4.0 * scale_factor);
      rect.bottom -= static_cast<int>(4.0 * scale_factor);

      wil::unique_hbrush brush(::CreateSolidBrush(accent_color));
      wil::unique_hpen pen(::CreatePen(PS_SOLID, 1, accent_color));
      const auto old_brush = wil::SelectObject(dc, brush.get());
      const auto old_pen = wil::SelectObject(dc, pen.get());

      const int r = static_cast<int>(1.5 * scale_factor);
      ::RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, r * 2, r * 2);
    }
  }
}

void CandidateWindow::DrawBackground(HDC dc) {
  const Rect client_rect(Point(0, 0), table_layout_->GetTotalSize());
  const CRect client_crect = ToCRect(client_rect);
  const bool dark_mode = SystemTheme::GetInstance().IsDarkMode();
  const COLORREF bg_color = dark_mode ? RGB(32, 32, 36) : RGB(252, 252, 254);
  FillSolidRect(dc, &client_crect, bg_color);
}

void CandidateWindow::DrawFrame(HDC dc) {
  const Rect client_rect(Point(0, 0), table_layout_->GetTotalSize());
  const CRect client_crect = ToCRect(client_rect);
  const bool dark_mode = SystemTheme::GetInstance().IsDarkMode();
  const COLORREF border_color = dark_mode ? RGB(65, 65, 70) : RGB(220, 220, 225);

  wil::unique_hbrush brush(::CreateSolidBrush(border_color));
  ::FrameRect(dc, &client_crect, brush.get());
}

void CandidateWindow::set_mouse_moving(bool moving) { mouse_moving_ = moving; }

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
