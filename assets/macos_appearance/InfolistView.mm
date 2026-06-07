// Copyright 2010-2026, Google LLC. All rights reserved.
// This file has been modified from the original Mozc source code.

#import "renderer/mac/InfolistView.h"

#include "absl/log/log.h"
#include "client/client_interface.h"
#include "protocol/commands.pb.h"
#include "renderer/mac/mac_view_util.h"
#include "renderer/table_layout.h"

using mozc::commands::CandidateWindow;
using mozc::commands::Information;
using mozc::commands::InformationList;
using mozc::renderer::mac::MacViewUtil;

// === adjust layout ====
#define MOZC_INFO_WIDTH           320.0  // window width
#define MOZC_INFO_PADDING_X       8.0    // horizontal padding
#define MOZC_INFO_PADDING_Y       6.0    // vertical padding
#define MOZC_INFO_HEADER_MARGIN_Y 8.0    // margin between header and content
#define MOZC_INFO_ROW_MARGIN_Y    8.0    // vertical margin
#define MOZC_INFO_TITLE_FONT      16.0   // font size for candidate
#define MOZC_INFO_DESC_FONT       13.0   // font size for descripotion
#define MOZC_INFO_CAPTION_FONT    12.0   // font size for "用例" in header
#define MOZC_INFO_CORNER_RADIUS   10.0   // window corner radius
// ======================

@interface InfolistView ()
- (CGFloat)drawRow:(int)row ypos:(CGFloat)ypos draw_flag:(bool)draw_flag;
- (NSSize)drawView:(bool)draw_flag;
@end

@implementation InfolistView

- (id)initWithFrame:(NSRect)frame {
  self = [super initWithFrame:frame];
  return self;
}

- (void)setCandidateWindow:(const CandidateWindow *)candidate_window {
  candidate_window_.CopyFrom(*candidate_window);
}

- (BOOL)isFlipped {
  return YES;
}

- (CGFloat)drawRow:(int)row ypos:(CGFloat)ypos draw_flag:(bool)draw_flag {
  const InformationList &usages = candidate_window_.usages();
  const Information &info = usages.information(row);
  BOOL isFocused = (usages.has_focused_index() && row == usages.focused_index());

  NSColor *titleColor = isFocused ? [NSColor alternateSelectedControlTextColor] : [NSColor labelColor];
  NSColor *descColor = isFocused ? [[NSColor alternateSelectedControlTextColor] colorWithAlphaComponent:0.9] : [NSColor secondaryLabelColor];

  NSDictionary *titleAttr = @{ NSFontAttributeName: [NSFont boldSystemFontOfSize:MOZC_INFO_TITLE_FONT],
                               NSForegroundColorAttributeName: titleColor };
  NSDictionary *descAttr = @{ NSFontAttributeName: [NSFont systemFontOfSize:MOZC_INFO_DESC_FONT],
                              NSForegroundColorAttributeName: descColor };

  NSAttributedString *titleStr = [[NSAttributedString alloc] initWithString:[NSString stringWithUTF8String:info.title().c_str()] attributes:titleAttr];
  NSAttributedString *descStr = [[NSAttributedString alloc] initWithString:[NSString stringWithUTF8String:info.description().c_str()] attributes:descAttr];

  CGFloat textWidth = MOZC_INFO_WIDTH - (MOZC_INFO_PADDING_X * 2);
  NSRect titleRect = [titleStr boundingRectWithSize:NSMakeSize(textWidth, 1000) options:NSStringDrawingUsesLineFragmentOrigin];
  NSRect descRect = [descStr boundingRectWithSize:NSMakeSize(textWidth, 1000) options:NSStringDrawingUsesLineFragmentOrigin];

  CGFloat rowHeight = titleRect.size.height + descRect.size.height + (MOZC_INFO_ROW_MARGIN_Y * 2) + 4.0;

  if (draw_flag) {
    if (isFocused) {
      NSRect fullRowRect = NSMakeRect(0, ypos, MOZC_INFO_WIDTH, rowHeight);
      [[NSColor selectedContentBackgroundColor] set];
      NSRect highlightRect = NSInsetRect(fullRowRect, 6.0, 1.0);
      [[NSBezierPath bezierPathWithRoundedRect:highlightRect xRadius:5.0 yRadius:5.0] fill];
    }

    NSPoint titlePos = NSMakePoint(MOZC_INFO_PADDING_X, ypos + MOZC_INFO_ROW_MARGIN_Y);
    [titleStr drawAtPoint:titlePos];

    NSRect drawDescRect = NSMakeRect(MOZC_INFO_PADDING_X, titlePos.y + titleRect.size.height + 4.0, textWidth, descRect.size.height);
    [descStr drawWithRect:drawDescRect options:NSStringDrawingUsesLineFragmentOrigin];
  }

  return rowHeight;
}

- (NSSize)drawView:(bool)draw_flag {
  if (!candidate_window_.has_usages()) {
    return NSMakeSize(0, 0);
  }

  CGFloat ypos = MOZC_INFO_PADDING_Y;

  NSDictionary *captionAttr = @{ NSFontAttributeName: [NSFont systemFontOfSize:MOZC_INFO_CAPTION_FONT weight:NSFontWeightBold],
                                 NSForegroundColorAttributeName: [NSColor tertiaryLabelColor] };
  NSAttributedString *captionStr = [[NSAttributedString alloc] initWithString:@"用例" attributes:captionAttr];

  if (draw_flag) {
    [captionStr drawAtPoint:NSMakePoint(MOZC_INFO_PADDING_X, ypos)];
  }
  ypos += [captionStr size].height + MOZC_INFO_HEADER_MARGIN_Y;

  const InformationList &usages = candidate_window_.usages();
  for (int i = 0; i < usages.information_size(); ++i) {
    ypos += [self drawRow:i ypos:ypos draw_flag:draw_flag];
  }

  ypos += MOZC_INFO_PADDING_Y;

  return NSMakeSize(MOZC_INFO_WIDTH, ypos);
}

- (NSSize)updateLayout {
  return [self drawView:false];
}

- (void)drawRect:(NSRect)rect {
  [[NSColor clearColor] set];
  NSRectFill(rect);

  [[[NSColor windowBackgroundColor] colorWithAlphaComponent:0.96] set];
  NSBezierPath *path = [NSBezierPath bezierPathWithRoundedRect:rect xRadius:MOZC_INFO_CORNER_RADIUS yRadius:MOZC_INFO_CORNER_RADIUS];
  [path addClip];
  [path fill];

  [self drawView:true];
}

@end
