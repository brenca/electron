// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <algorithm>
#include <memory>

#include <utility>
#include <vector>

#include "base/i18n/rtl.h"
#include "base/time/time.h"
#include "base/threading/thread_task_runner_handle.h"
#include "chrome/browser/ui/views/autofill/autofill_popup_view_utils.h"
#include "content/browser/renderer_host/render_widget_host_view_child_frame.h"
#include "electron/buildflags/buildflags.h"
#include "mojo/public/cpp/bindings/associated_remote.h"
#include "shell/browser/native_window_views.h"
#include "shell/browser/ui/autofill_popup.h"
#include "shell/common/api/api.mojom.h"
#include "third_party/blink/public/common/associated_interfaces/associated_interface_provider.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/text_utils.h"

#if BUILDFLAG(ENABLE_OSR)
#include "shell/browser/osr/osr_render_widget_host_view.h"
#include "shell/browser/osr/osr_view_proxy.h"
#endif

namespace electron {

AutofillPopup::AutofillPopup() {
  bold_font_list_ = gfx::FontList().DeriveWithWeight(gfx::Font::Weight::BOLD);
  smaller_font_list_ =
      gfx::FontList().DeriveWithSizeDelta(kSmallerFontSizeDelta);
}

AutofillPopup::~AutofillPopup() {
  Hide();
}

void AutofillPopup::CreateView(content::RenderFrameHost* frame_host,
                               bool offscreen,
                               views::View* parent,
                               const gfx::RectF& r) {
  if (view_)
    return;

  Hide();

  frame_host_ = frame_host;
  element_bounds_ = gfx::ToEnclosedRect(r);

  gfx::Vector2d height_offset(0, element_bounds_.height());
  gfx::Point menu_position(element_bounds_.origin() + height_offset);
  views::View::ConvertPointToScreen(parent, &menu_position);
  popup_bounds_ = gfx::Rect(menu_position, element_bounds_.size());

  offscreen_ = offscreen;

  parent_ = parent;
  parent_->AddObserver(this);

  view_ = new AutofillPopupView(this, parent->GetWidget());

#if BUILDFLAG(ENABLE_OSR)
  if (offscreen) {
    auto* rwhv = frame_host->GetView();

    auto* rwhv_base = static_cast<content::RenderWidgetHostViewBase*>(rwhv);
    if (rwhv_base->IsRenderWidgetHostViewChildFrame()) {
      auto* child_rwhv =
          static_cast<content::RenderWidgetHostViewChildFrame*>(rwhv_base);
      rwhv = child_rwhv->GetRootView();
    }

    auto* osr_rwhv = static_cast<OffScreenRenderWidgetHostView*>(rwhv);
    view_->view_proxy_ = std::make_unique<OffscreenViewProxy>(
        view_, osr_rwhv->GetScaleFactor());
    osr_rwhv->AddViewProxy(view_->view_proxy_.get());
  }
#endif

  // Do this after OSR setup, we check for view_proxy_ when showing
  view_->Show();

  if (offscreen) {
    time_source_ = std::make_unique<viz::DelayBasedTimeSource>(
        base::ThreadTaskRunnerHandle::Get().get());
    time_source_->SetClient(this);
    time_source_->SetTimebaseAndInterval(
      base::TimeTicks::Now(),
      base::TimeDelta::FromSeconds(1) / 20.0);
    time_source_->SetActive(true);
  }
}

void AutofillPopup::Hide() {
  if (time_source_) {
    time_source_->SetActive(false);
  }
  if (parent_) {
    parent_->RemoveObserver(this);
    parent_ = nullptr;
  }
  if (view_) {
    view_->Hide();
    view_ = nullptr;
  }
}

void AutofillPopup::SetItems(const std::vector<base::string16>& values,
                             const std::vector<base::string16>& labels) {
  DCHECK(view_);
  values_ = values;
  labels_ = labels;
  UpdatePopupBounds();
  view_->OnSuggestionsChanged();
  if (view_)  // could be hidden after the change
    view_->DoUpdateBoundsAndRedrawPopup();
}

void AutofillPopup::AcceptSuggestion(int index) {
  mojo::AssociatedRemote<mojom::ElectronAutofillAgent> autofill_agent;
  frame_host_->GetRemoteAssociatedInterfaces()->GetInterface(&autofill_agent);
  autofill_agent->AcceptDataListSuggestion(GetValueAt(index));
}

void AutofillPopup::UpdatePopupBounds() {
  DCHECK(parent_);
  gfx::Point origin(element_bounds_.origin());
  views::View::ConvertPointToScreen(parent_, &origin);

  gfx::Rect bounds(origin, element_bounds_.size());
  gfx::Rect window_bounds = parent_->GetBoundsInScreen();

  // Provide fake parent bounds for offscreen case
  if (offscreen_) {
    window_bounds = gfx::Rect(0, 0, 99999, 99999);
  }

  gfx::Size preferred_size =
      gfx::Size(GetDesiredPopupWidth(), GetDesiredPopupHeight());
  popup_bounds_ = CalculatePopupBounds(preferred_size, window_bounds, bounds,
                                       base::i18n::IsRTL());
}

void AutofillPopup::OnTimerTick() {
  if (view_) {
    view_->Invalidate();
  }
}

gfx::Rect AutofillPopup::popup_bounds_in_view() {
  gfx::Vector2d height_offset(0, element_bounds_.height());
  gfx::Point menu_position(element_bounds_.origin() + height_offset);

  return gfx::Rect(menu_position, popup_bounds_.size());
}

void AutofillPopup::OnViewBoundsChanged(views::View* view) {
  UpdatePopupBounds();
  view_->DoUpdateBoundsAndRedrawPopup();
}

void AutofillPopup::OnViewIsDeleting(views::View* view) {
  Hide();
}

int AutofillPopup::GetDesiredPopupHeight() {
  return 2 * kPopupBorderThickness + values_.size() * kRowHeight;
}

int AutofillPopup::GetDesiredPopupWidth() {
  int popup_width = element_bounds_.width();

  for (size_t i = 0; i < values_.size(); ++i) {
    int row_size =
        kEndPadding + 2 * kPopupBorderThickness +
        gfx::GetStringWidth(GetValueAt(i), GetValueFontListForRow(i)) +
        gfx::GetStringWidth(GetLabelAt(i), GetLabelFontListForRow(i));
    if (!GetLabelAt(i).empty())
      row_size += kNamePadding + kEndPadding;

    popup_width = std::max(popup_width, row_size);
  }

  return popup_width;
}

gfx::Rect AutofillPopup::GetRowBounds(int index) {
  int top = kPopupBorderThickness + index * kRowHeight;

  return gfx::Rect(kPopupBorderThickness, top,
                   popup_bounds_.width() - 2 * kPopupBorderThickness,
                   kRowHeight);
}

const gfx::FontList& AutofillPopup::GetValueFontListForRow(int index) const {
  return bold_font_list_;
}

const gfx::FontList& AutofillPopup::GetLabelFontListForRow(int index) const {
  return smaller_font_list_;
}

ui::NativeTheme::ColorId AutofillPopup::GetBackgroundColorIDForRow(
    int index) const {
  return (view_ && index == view_->GetSelectedLine())
             ? ui::NativeTheme::kColorId_ResultsTableHoveredBackground
             : ui::NativeTheme::kColorId_ResultsTableNormalBackground;
}

int AutofillPopup::GetLineCount() {
  return values_.size();
}

base::string16 AutofillPopup::GetValueAt(int i) {
  return values_.at(i);
}

base::string16 AutofillPopup::GetLabelAt(int i) {
  return labels_.at(i);
}

int AutofillPopup::LineFromY(int y) const {
  int current_height = kPopupBorderThickness;

  for (size_t i = 0; i < values_.size(); ++i) {
    current_height += kRowHeight;

    if (y <= current_height)
      return i;
  }

  return values_.size() - 1;
}

}  // namespace electron
