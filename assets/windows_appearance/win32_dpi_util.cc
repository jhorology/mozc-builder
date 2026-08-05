// Copyright 2010-2026, Google LLC. All rights reserved.
// This file has been modified for modern Windows appearance.

#include "renderer/win32/win32_dpi_util.h"

#include <shellscalingapi.h>
#include <windows.h>

#include <cstdint>

#include "protocol/renderer_style.pb.h"
#include "renderer/renderer_style_handler.h"

namespace mozc {
namespace renderer {
namespace win32 {
namespace {

void ScaleTextStyle(RendererStyle::TextStyle* text_style, double scale_factor) {
  text_style->set_font_size(text_style->font_size() * scale_factor);
  text_style->set_left_padding(text_style->left_padding() * scale_factor);
  text_style->set_right_padding(text_style->right_padding() * scale_factor);
}

}  // namespace

double GetDPIScalingFactor(uint32_t dpi) {
  return static_cast<double>(dpi) / USER_DEFAULT_SCREEN_DPI;
}

uint32_t GetDpiForPoint(int x, int y) {
  const POINT point = {x, y};
  const HMONITOR monitor = ::MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
  UINT dpi_x = USER_DEFAULT_SCREEN_DPI;
  UINT dpi_y = USER_DEFAULT_SCREEN_DPI;
  if (FAILED(::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y))) {
    return USER_DEFAULT_SCREEN_DPI;
  }
  return dpi_x;
}

void GetScaledRendererStyle(::mozc::renderer::RendererStyle* style, uint32_t dpi) {
  const double scale_factor = GetDPIScalingFactor(dpi);

  RendererStyleHandler::GetRendererStyle(style);

  // Modern UI adjustments
  style->set_row_rect_padding(static_cast<int>(3 * scale_factor));
  style->set_scrollbar_width(static_cast<int>(4 * scale_factor));

  // Colors: Windows 11 Fluent style
  style->mutable_candidate_style()->set_font_size(18);
  style->mutable_candidate_style()->mutable_foreground_color()->set_r(28);
  style->mutable_candidate_style()->mutable_foreground_color()->set_g(28);
  style->mutable_candidate_style()->mutable_foreground_color()->set_b(30);

  style->mutable_shortcut_style()->set_font_size(11);
  style->mutable_shortcut_style()->mutable_foreground_color()->set_r(110);
  style->mutable_shortcut_style()->mutable_foreground_color()->set_g(110);
  style->mutable_shortcut_style()->mutable_foreground_color()->set_b(115);

  style->mutable_description_style()->set_font_size(14);
  style->mutable_description_style()->mutable_foreground_color()->set_r(110);
  style->mutable_description_style()->mutable_foreground_color()->set_g(110);
  style->mutable_description_style()->mutable_foreground_color()->set_b(115);

  // Selection accent blue (Windows 11 Accent: #0067C0)
  style->mutable_focused_background_color()->set_r(0);
  style->mutable_focused_background_color()->set_g(103);
  style->mutable_focused_background_color()->set_b(192);

  style->mutable_focused_border_color()->set_r(0);
  style->mutable_focused_border_color()->set_g(120);
  style->mutable_focused_border_color()->set_b(212);

  // Subtle border & background
  style->mutable_border_color()->set_r(218);
  style->mutable_border_color()->set_g(218);
  style->mutable_border_color()->set_b(222);

  style->mutable_scrollbar_background_color()->set_r(245);
  style->mutable_scrollbar_background_color()->set_g(245);
  style->mutable_scrollbar_background_color()->set_b(248);

  style->mutable_scrollbar_indicator_color()->set_r(160);
  style->mutable_scrollbar_indicator_color()->set_g(160);
  style->mutable_scrollbar_indicator_color()->set_b(165);

  ScaleTextStyle(style->mutable_shortcut_style(), scale_factor);
  ScaleTextStyle(style->mutable_gap1_style(), scale_factor);
  ScaleTextStyle(style->mutable_candidate_style(), scale_factor);
  ScaleTextStyle(style->mutable_description_style(), scale_factor);

  ScaleTextStyle(style->mutable_footer_style(), scale_factor);
  ScaleTextStyle(style->mutable_footer_sub_label_style(), scale_factor);

  RendererStyle::InfolistStyle* info_style = style->mutable_infolist_style();
  info_style->set_window_width(static_cast<int>(320 * scale_factor));
  info_style->set_row_rect_padding(static_cast<int>(4 * scale_factor));
  info_style->set_caption_height(static_cast<int>(24 * scale_factor));

  info_style->mutable_title_style()->set_font_size(16);
  info_style->mutable_description_style()->set_font_size(13);
  info_style->mutable_caption_style()->set_font_size(12);

  info_style->mutable_focused_background_color()->set_r(0);
  info_style->mutable_focused_background_color()->set_g(103);
  info_style->mutable_focused_background_color()->set_b(192);

  ScaleTextStyle(info_style->mutable_caption_style(), scale_factor);
  ScaleTextStyle(info_style->mutable_title_style(), scale_factor);
  ScaleTextStyle(info_style->mutable_description_style(), scale_factor);
}

}  // namespace win32
}  // namespace renderer
}  // namespace mozc
