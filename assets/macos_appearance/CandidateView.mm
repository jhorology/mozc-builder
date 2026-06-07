// Copyright 2010-2026, Google LLC. All rights reserved.
// This file has been modified from the original Mozc source code.

#import "renderer/mac/CandidateView.h"
#import <Foundation/Foundation.h>
#include <algorithm>
#include "absl/strings/str_format.h"
#include "protocol/commands.pb.h"
#include "client/client_interface.h"
#include "renderer/mac/mac_view_util.h"
#include "renderer/table_layout.h"

using mozc::client::SendCommandInterface;
using mozc::commands::CandidateWindow;
using mozc::commands::Output;
using mozc::commands::SessionCommand;
using mozc::renderer::ColumnType;
using mozc::renderer::kColumnShortcut;
using mozc::renderer::kColumnGap1;
using mozc::renderer::kColumnCandidate;
using mozc::renderer::kColumnDescription;
using mozc::renderer::kNumberOfColumns;
using mozc::renderer::TableLayout;
using mozc::renderer::mac::MacViewUtil;

// === adjust layout ====
#define MOZC_CANDIDATE_FONT_SIZE     18.0
#define MOZC_SHORTCUT_FONT_SIZE      11.0
#define MOZC_DESCRIPTION_FONT_SIZE   14.0
#define MOZC_FOOTER_FONT_SIZE        13.0

#define MOZC_WINDOW_PADDING_X        8.0   // horizontal padding
#define MOZC_MARGIN_NO_TO_CAND       8.0   // margin between No. and candidate
#define MOZC_MARGIN_CAND_TO_DESC     10.0  // margin between candidate and description
#define MOZC_ROW_PADDING_Y           2.0   // vertical row margin
#define MOZC_HIGHLIGHT_INSET_X       4.0   // highlight horizontal inset
#define MOZC_HIGHLIGHT_INSET_Y       1.0   // highlight vertical inset
// ======================

@implementation CandidateView {
  const NSImage *logoImage_;
  int columnMinimumWidth_;
  mozc::commands::CandidateWindow candidate_window_;
  mozc::renderer::TableLayout tableLayout_;
  int focusedRow_;
  NSArray *candidateStringsCache_;
  mozc::client::SendCommandInterface *command_sender_;
}

- (id)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    [self initializeDefaultStyle];
  }
  return self;
}

- (void)initializeDefaultStyle {
  logoImage_ = [NSImage imageNamed:@"candidate_window_logo.tiff"];

  NSDictionary *attr = @{ NSFontAttributeName : [NSFont systemFontOfSize:MOZC_CANDIDATE_FONT_SIZE] };
  NSAttributedString *defaultMessage = [[NSAttributedString alloc] initWithString:@"そのほかの文字種" attributes:attr];
  columnMinimumWidth_ = [defaultMessage size].width + MOZC_MARGIN_CAND_TO_DESC + MOZC_WINDOW_PADDING_X;

  [NSBezierPath setDefaultLineWidth:1.0];
  [NSBezierPath setDefaultLineJoinStyle:NSLineJoinStyleMiter];
}

- (NSSize)updateLayout {
  candidateStringsCache_ = nil;
  tableLayout_.Initialize(candidate_window_.candidate_size(), kNumberOfColumns);
  tableLayout_.SetWindowBorder(0);
  tableLayout_.SetRowRectPadding(MOZC_ROW_PADDING_Y);

  if (candidate_window_.candidate_size() < candidate_window_.size()) {
    tableLayout_.SetVScrollBar(4);
  }

  if (candidate_window_.has_focused_index() && candidate_window_.candidate_size() > 0) {
    focusedRow_ = candidate_window_.focused_index() - candidate_window_.candidate(0).index();
  } else {
    focusedRow_ = -1;
  }

  NSDictionary *attrShortcut = @{ NSFontAttributeName: [NSFont systemFontOfSize:MOZC_SHORTCUT_FONT_SIZE],
                                  NSForegroundColorAttributeName: [NSColor secondaryLabelColor] };
  NSDictionary *attrCandidate = @{ NSFontAttributeName: [NSFont systemFontOfSize:MOZC_CANDIDATE_FONT_SIZE],
                                   NSForegroundColorAttributeName: [NSColor labelColor] };
  NSDictionary *attrDesc = @{ NSFontAttributeName: [NSFont systemFontOfSize:MOZC_DESCRIPTION_FONT_SIZE],
                              NSForegroundColorAttributeName: [NSColor secondaryLabelColor] };

  NSMutableArray *newCache = [NSMutableArray array];
  for (int i = 0; i < candidate_window_.candidate_size(); ++i) {
    const auto &c = candidate_window_.candidate(i);
    std::string val = c.value();
    if (c.annotation().has_prefix()) val.insert(0, c.annotation().prefix());
    if (c.annotation().has_suffix()) val.append(c.annotation().suffix());

    NSAttributedString *s = [[NSAttributedString alloc] initWithString:[NSString stringWithUTF8String:c.annotation().shortcut().c_str()] attributes:attrShortcut];
    NSAttributedString *v = [[NSAttributedString alloc] initWithString:[NSString stringWithUTF8String:val.c_str()] attributes:attrCandidate];
    NSAttributedString *d = [[NSAttributedString alloc] initWithString:[NSString stringWithUTF8String:c.annotation().description().c_str()] attributes:attrDesc];

    NSSize sSize = [s size]; sSize.width += MOZC_WINDOW_PADDING_X;
    NSSize vSize = [v size]; vSize.width += MOZC_MARGIN_NO_TO_CAND;
    NSSize dSize = [d size]; dSize.width += MOZC_MARGIN_CAND_TO_DESC + MOZC_WINDOW_PADDING_X;

    tableLayout_.EnsureCellSize(kColumnShortcut, MacViewUtil::ToSize(sSize));
    tableLayout_.EnsureCellSize(kColumnCandidate, MacViewUtil::ToSize(vSize));
    tableLayout_.EnsureCellSize(kColumnDescription, MacViewUtil::ToSize(dSize));

    NSAttributedString *empty = [[NSAttributedString alloc] initWithString:@""];
    [newCache addObject:@[s, empty, v, d]];
  }

  tableLayout_.EnsureColumnsWidth(kColumnCandidate, kColumnDescription, columnMinimumWidth_);
  candidateStringsCache_ = newCache;

  if (candidate_window_.has_footer()) {
    tableLayout_.EnsureFooterSize(MacViewUtil::ToSize(NSMakeSize(0, 28)));
  }

  tableLayout_.FreezeLayout();
  return MacViewUtil::ToNSSize(tableLayout_.GetTotalSize());
}

- (void)drawRect:(NSRect)rect {
  [[NSColor clearColor] set];
  NSRectFill(rect);

  [[[NSColor windowBackgroundColor] colorWithAlphaComponent:0.96] set];
  NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:rect xRadius:10.0 yRadius:10.0];
  [path addClip];
  [path fill];

  for (int i = 0; i < candidate_window_.candidate_size(); ++i) {
    [self drawRow:i];
  }
  if (candidate_window_.candidate_size() < candidate_window_.size()) {
    [self drawVScrollBar];
  }
  [self drawFooter];
}

- (void)drawRow:(int)row {
  NSRect rowRect = MacViewUtil::ToNSRect(tableLayout_.GetRowRect(row));
  BOOL isFocused = (row == focusedRow_);

  if (isFocused) {
    [[NSColor selectedContentBackgroundColor] set];
    NSRect highlightRect = NSInsetRect(rowRect, MOZC_HIGHLIGHT_INSET_X, MOZC_HIGHLIGHT_INSET_Y);
    [[NSBezierPath bezierPathWithRoundedRect:highlightRect xRadius:5.0 yRadius:5.0] fill];
  }

  NSArray *cells = [candidateStringsCache_ objectAtIndex:row];
  for (int i = kColumnShortcut; i < kNumberOfColumns; ++i) {
    if (i >= [cells count]) break;
    NSAttributedString *text = [cells objectAtIndex:i];
    if ([text length] == 0) continue;

    if (isFocused) {
      NSMutableAttributedString *mut = [text mutableCopy];
      [mut addAttribute:NSForegroundColorAttributeName value:[NSColor alternateSelectedControlTextColor] range:NSMakeRange(0, mut.length)];
      text = mut;
    }

    NSRect cellRect = MacViewUtil::ToNSRect(tableLayout_.GetCellRect(row, i));
    NSPoint p = cellRect.origin;

    if (i == kColumnShortcut) {
      p.x += MOZC_WINDOW_PADDING_X;
    } else if (i == kColumnCandidate) {
      p.x += MOZC_MARGIN_NO_TO_CAND;
    } else if (i == kColumnDescription) {
      p.x += MOZC_MARGIN_CAND_TO_DESC;
    }

    p.y += (cellRect.size.height - [text size].height) / 2;
    [text drawAtPoint:p];
  }

  if (candidate_window_.candidate(row).has_information_id()) {
    [[NSColor controlAccentColor] set];
    NSRect infoRect = NSMakeRect(rowRect.origin.x + rowRect.size.width - 8.0,
                                 rowRect.origin.y + 4.0, 3.0, rowRect.size.height - 8.0);
    [[NSBezierPath bezierPathWithRoundedRect:infoRect xRadius:1.5 yRadius:1.5] fill];
  }
}

- (void)drawVScrollBar {
  const mozc::Rect vscrollRect = tableLayout_.GetVScrollBarRect();
  if (vscrollRect.IsRectEmpty()) return;

  const int beginIndex = candidate_window_.candidate(0).index();
  const int candidatesTotal = candidate_window_.size();
  const int endIndex = candidate_window_.candidate(candidate_window_.candidate_size() - 1).index();

  [[NSColor tertiaryLabelColor] set];
  NSRect scrollBg = MacViewUtil::ToNSRect(vscrollRect);
  scrollBg.origin.x -= 2.0;
  [[NSBezierPath bezierPathWithRoundedRect:scrollBg xRadius:2.0 yRadius:2.0] fill];

  const mozc::Rect indicatorRect = tableLayout_.GetVScrollIndicatorRect(beginIndex, endIndex, candidatesTotal);
  NSRect indicatorNsRect = MacViewUtil::ToNSRect(indicatorRect);
  indicatorNsRect.origin.x -= 2.0;
  [[NSColor secondaryLabelColor] set];
  [[NSBezierPath bezierPathWithRoundedRect:indicatorNsRect xRadius:2.0 yRadius:2.0] fill];
}

- (void)drawFooter {
  if (!candidate_window_.has_footer()) return;
  const auto &footer = candidate_window_.footer();
  NSRect footerRect = MacViewUtil::ToNSRect(tableLayout_.GetFooterRect());

  NSDictionary *attr = @{ NSFontAttributeName: [NSFont systemFontOfSize:MOZC_FOOTER_FONT_SIZE],
                          NSForegroundColorAttributeName: [NSColor secondaryLabelColor] };

  if (footer.has_label()) {
    NSAttributedString *s = [[NSAttributedString alloc] initWithString:[NSString stringWithUTF8String:footer.label().c_str()] attributes:attr];
    [s drawAtPoint:NSMakePoint(footerRect.origin.x + MOZC_WINDOW_PADDING_X, footerRect.origin.y + (footerRect.size.height - [s size].height)/2)];
  }

  if (footer.index_visible()) {
    NSString *idx = [NSString stringWithFormat:@"%d/%d", candidate_window_.focused_index() + 1, candidate_window_.size()];
    NSAttributedString *s = [[NSAttributedString alloc] initWithString:idx attributes:attr];
    [s drawAtPoint:NSMakePoint(footerRect.origin.x + footerRect.size.width - [s size].width - MOZC_WINDOW_PADDING_X, footerRect.origin.y + (footerRect.size.height - [s size].height)/2)];
  }
}

- (void)mouseDown:(NSEvent *)theEvent {
  NSPoint p = [self convertPoint:[theEvent locationInWindow] fromView:nil];

  int clickedRow = -1;
  for (int i = 0; i < candidate_window_.candidate_size(); ++i) {
    NSRect rowRect = MacViewUtil::ToNSRect(tableLayout_.GetRowRect(i));
    if (NSPointInRect(p, rowRect)) {
      clickedRow = i;
      break;
    }
  }

  if (clickedRow >= 0 && clickedRow < candidate_window_.candidate_size()) {
    if (command_sender_) {
      mozc::commands::SessionCommand command;
      command.set_type(mozc::commands::SessionCommand::SELECT_CANDIDATE);
      command.set_id(candidate_window_.candidate(clickedRow).id());
      mozc::commands::Output dummy_output;
      command_sender_->SendCommand(command, &dummy_output);
    }
  }
}

- (BOOL)isFlipped { return YES; }
- (void)setCandidateWindow:(const mozc::commands::CandidateWindow *)cw { candidate_window_ = *cw; }
- (void)setSendCommandInterface:(mozc::client::SendCommandInterface *)cs { command_sender_ = cs; }
- (const mozc::renderer::TableLayout *)tableLayout { return &tableLayout_; }
@end
