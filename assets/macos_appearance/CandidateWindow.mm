// Copyright 2010-2026, Google LLC. All rights reserved.
// This file has been modified from the original Mozc source code.

#include <Carbon/Carbon.h>
#include <objc/message.h>

#import "CandidateView.h"

#include "absl/log/log.h"
#include "base/coordinates.h"
#include "protocol/commands.pb.h"
#include "renderer/mac/CandidateWindow.h"

namespace mozc {
  namespace renderer {
    namespace mac {

      CandidateWindow::CandidateWindow() : command_sender_(nullptr) {}

      CandidateWindow::~CandidateWindow() {}

      void CandidateWindow::SetSendCommandInterface(
                                                    client::SendCommandInterface* send_command_interface) {
        DLOG(INFO) << "CandidateWindow::SetSendCommandInterface()";
        command_sender_ = send_command_interface;

        const CandidateView* candidate_view = (CandidateView*)view_;
        [candidate_view setSendCommandInterface:send_command_interface];
      }

      void CandidateWindow::InitWindow() {
        RendererBaseWindow::InitWindow();
        const CandidateView* candidate_view = (CandidateView*)view_;

        // ---- support macos appearance ----
        [window_ setAppearance:nil];
        [window_ setOpaque:NO];
        [window_ setBackgroundColor:[NSColor clearColor]];
        [window_ setHasShadow:YES];
        // ----------------------------------

        [candidate_view setSendCommandInterface:command_sender_];
      }
      const mozc::renderer::TableLayout* CandidateWindow::GetTableLayout() const {
        const CandidateView* candidate_view = (CandidateView*)view_;
        return [candidate_view tableLayout];
      }

      void CandidateWindow::SetCandidateWindow(const commands::CandidateWindow& candidate_window) {
        DLOG(INFO) << "CandidateWindow::SetCandidateWindow";
        if (candidate_window.candidate_size() == 0) {
          return;
        }

        if (!window_) {
          InitWindow();
        }
        CandidateView* candidate_view = (CandidateView*)view_;
        [candidate_view setCandidateWindow:&candidate_window];
        [candidate_view setNeedsDisplay:YES];
        NSSize size = [candidate_view updateLayout];
        ResizeWindow(size.width, size.height);
      }

      void CandidateWindow::ResetView() {
        DLOG(INFO) << "CandidateWindow::ResetView()";
        view_ = [[CandidateView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
      }

    }  // namespace mac
  }  // namespace renderer
}  // namespace mozc
