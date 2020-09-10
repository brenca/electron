// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_offscreen_window.h"

#include <memory>

#include "base/threading/thread_task_runner_handle.h"
#include "content/browser/renderer_host/render_widget_host_impl.h"  // nogncheck
#include "content/browser/renderer_host/render_widget_host_owner_delegate.h"  // nogncheck
#include "content/browser/web_contents/web_contents_impl.h"  // nogncheck
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "gin/converter.h"
#include "native_mate/dictionary.h"
#include "shell/browser/browser.h"
#include "shell/browser/unresponsive_suppressor.h"
#include "shell/browser/web_contents_preferences.h"
#include "shell/browser/window_list.h"
#include "shell/common/api/constructor.h"
#include "shell/common/color_util.h"
#include "shell/common/gin_helper/dictionary.h"
#include "shell/common/native_mate_converters/value_converter.h"
#include "shell/common/node_includes.h"
#include "shell/common/options_switches.h"

namespace electron {

namespace api {

OffscreenWindow::OffscreenWindow(gin::Arguments* args,
                                 const mate::Dictionary& options)
    : weak_factory_(this) {
  mate::Handle<class WebContents> web_contents;

  // Use options.webPreferences in WebContents.
  v8::Isolate* isolate = args->isolate();
  mate::Dictionary web_preferences = mate::Dictionary::CreateEmpty(isolate);
  options.Get(options::kWebPreferences, &web_preferences);

  // Force offscreen to be true
  web_preferences.Set(options::kOffscreen, true);

  // Copy the backgroundColor to webContents.
  v8::Local<v8::Value> value;
  if (options.Get(options::kBackgroundColor, &value))
    web_preferences.Set(options::kBackgroundColor, value);

  // Copy the transparent setting to webContents
  transparent_ = false;
  if (options.Get("transparent", &transparent_)) {
    web_preferences.Set("transparent", transparent_);
  }

  width_ = 800;
  height_ = 600;
  options.Get(options::kWidth, &width_);
  options.Get(options::kHeight, &height_);

  // Copy the show setting to webContents, but only if we don't want to paint
  // when initially hidden
  bool paint_when_initially_hidden = true;
  options.Get("paintWhenInitiallyHidden", &paint_when_initially_hidden);
  if (!paint_when_initially_hidden) {
    bool show = true;
    options.Get(options::kShow, &show);
    web_preferences.Set(options::kShow, show);
  }

  if (options.Get("webContents", &web_contents) && !web_contents.IsEmpty()) {
    // Set webPreferences from options if using an existing webContents.
    // These preferences will be used when the webContent launches new
    // render processes.
    auto* existing_preferences =
        WebContentsPreferences::From(web_contents->web_contents());
    base::DictionaryValue web_preferences_dict;
    if (mate::ConvertFromV8(isolate, web_preferences.GetHandle(),
                            &web_preferences_dict)) {
      existing_preferences->Clear();
      existing_preferences->Merge(web_preferences_dict);
    }
  } else {
    // Creates the WebContents used by OffscreenWindow.
    web_contents = WebContents::Create(isolate, web_preferences);
  }

  web_contents_.Reset(isolate, web_contents.ToV8());
  api_web_contents_ = web_contents->GetWeakPtr();
  api_web_contents_->AddObserver(this);
  Observe(api_web_contents_->web_contents());

  // Keep a copy of the options for later use.
  mate::Dictionary(isolate, web_contents->GetWrapper())
      .Set("browserWindowOptions", options);

  // Associate with OffscreenWindow.
  web_contents->SetOffscreenWindow(this);

  auto* host = web_contents->web_contents()->GetRenderViewHost();
  if (host)
    host->GetWidget()->AddInputEventObserver(this);

  InitWithArgs(args);
}

OffscreenWindow::~OffscreenWindow() {
  for (Observer& observer : observers_)
    observer.OnWindowClosed();
  // FIXME This is a hack rather than a proper fix preventing shutdown crashes.
  if (api_web_contents_)
    api_web_contents_->RemoveObserver(this);
  // Note that the OnWindowClosed will not be called after the destructor runs,
  // since the window object is managed by the TopLevelWindow class.
  if (web_contents())
    Cleanup();
}

void OffscreenWindow::OnInputEvent(const blink::WebInputEvent& event) {
  switch (event.GetType()) {
    case blink::WebInputEvent::kGestureScrollBegin:
    case blink::WebInputEvent::kGestureScrollUpdate:
    case blink::WebInputEvent::kGestureScrollEnd:
      Emit("scroll-touch-edge");
      break;
    default:
      break;
  }
}

void OffscreenWindow::RenderViewHostChanged(content::RenderViewHost* old_host,
                                            content::RenderViewHost* new_host) {
  if (old_host)
    old_host->GetWidget()->RemoveInputEventObserver(this);
  if (new_host)
    new_host->GetWidget()->AddInputEventObserver(this);
}

void OffscreenWindow::RenderViewCreated(
    content::RenderViewHost* render_view_host) {
  if (!transparent_)
    return;

  content::RenderWidgetHostImpl* impl = content::RenderWidgetHostImpl::FromID(
      render_view_host->GetProcess()->GetID(),
      render_view_host->GetRoutingID());
  if (impl)
    impl->owner_delegate()->SetBackgroundOpaque(false);
}

void OffscreenWindow::DidFirstVisuallyNonEmptyPaint() {
  // Emit the ReadyToShow event in next tick in case of pending drawing work.
  base::ThreadTaskRunnerHandle::Get()->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::WeakPtr<OffscreenWindow> self) {
                       if (self)
                         self->Emit("ready-to-show");
                     },
                     GetWeakPtr()));
}

void OffscreenWindow::BeforeUnloadDialogCancelled() {
  // Cancel unresponsive event when window close is cancelled.
  window_unresponsive_closure_.Cancel();
}

void OffscreenWindow::OnRendererUnresponsive(content::RenderProcessHost*) {
  // Schedule the unresponsive shortly later, since we may receive the
  // responsive event soon. This could happen after the whole application had
  // blocked for a while.
  // Also notice that when closing this event would be ignored because we have
  // explicitly started a close timeout counter. This is on purpose because we
  // don't want the unresponsive event to be sent too early when user is closing
  // the window.
  ScheduleUnresponsiveEvent(50);
}

void OffscreenWindow::OnCloseContents() {
  // On some machines it may happen that the window gets destroyed for twice,
  // checking web_contents() can effectively guard against that.
  // https://github.com/electron/electron/issues/16202.
  //
  // TODO(zcbenz): We should find out the root cause and improve the closing
  // procedure of OffscreenWindow.
  if (!web_contents())
    return;

  // Do not sent "unresponsive" event after window is closed.
  window_unresponsive_closure_.Cancel();

  delete this;
}

void OffscreenWindow::OnRendererResponsive() {
  window_unresponsive_closure_.Cancel();
  Emit("responsive");
}

void OffscreenWindow::Close() {
  Emit("closed");
  delete this;
}

void OffscreenWindow::Focus() {
  Emit("focus");
  web_contents()->GetRenderViewHost()->GetWidget()->Focus();
}

void OffscreenWindow::Blur() {
  Emit("blur");
  web_contents()->GetRenderViewHost()->GetWidget()->Blur();
}

bool OffscreenWindow::IsFocused() {
  auto* host_view = web_contents()->GetRenderViewHost()->GetWidget()->GetView();
  return host_view && host_view->HasFocus();
}

void OffscreenWindow::SetSize(int width, int height) {
  width_ = width;
  height_ = height;
  for (Observer& observer : observers_)
    observer.OnWindowResize();
}

std::vector<int> OffscreenWindow::GetSize() {
  std::vector<int> result(2);
  result[0] = width_;
  result[1] = height_;
  return result;
}

gfx::Size OffscreenWindow::GetInternalSize() const {
  return gfx::Size(width_, height_);
}

void OffscreenWindow::SetBackgroundColor(const std::string& color_name) {
  auto* view = web_contents()->GetRenderWidgetHostView();
  if (view)
    view->SetBackgroundColor(ParseHexColor(color_name));
  // Also update the web preferences object otherwise the view will be reset on
  // the next load URL call
  if (api_web_contents_) {
    auto* web_preferences =
        WebContentsPreferences::From(api_web_contents_->web_contents());
    if (web_preferences) {
      web_preferences->preference()->SetStringKey(options::kBackgroundColor,
                                                  color_name);
    }
  }
}

void OffscreenWindow::SetOwnerWindow(v8::Local<v8::Value> value) {
  mate::Handle<BrowserWindow> browser_window;
  if (value->IsObject() &&
      gin::ConvertFromV8(isolate(), value, &browser_window)) {
    api_web_contents_->SetOwnerWindow(browser_window->window());
  }
}

int32_t OffscreenWindow::GetID() const {
  return weak_map_id();
}

v8::Local<v8::Value> OffscreenWindow::GetWebContents(v8::Isolate* isolate) {
  if (web_contents_.IsEmpty())
    return v8::Null(isolate);
  return v8::Local<v8::Value>::New(isolate, web_contents_);
}

void OffscreenWindow::ScheduleUnresponsiveEvent(int ms) {
  if (!window_unresponsive_closure_.IsCancelled())
    return;

  window_unresponsive_closure_.Reset(base::BindRepeating(
      &OffscreenWindow::NotifyWindowUnresponsive, GetWeakPtr()));
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, window_unresponsive_closure_.callback(),
      base::TimeDelta::FromMilliseconds(ms));
}

void OffscreenWindow::NotifyWindowUnresponsive() {
  window_unresponsive_closure_.Cancel();
  if (!IsUnresponsiveEventSuppressed()) {
    Emit("unresponsive");
  }
}

void OffscreenWindow::Cleanup() {
  auto* host = web_contents()->GetRenderViewHost();
  if (host)
    host->GetWidget()->RemoveInputEventObserver(this);

  // Destroy WebContents asynchronously unless app is shutting down,
  // because destroy() might be called inside WebContents's event handler.
  api_web_contents_->DestroyWebContents(!Browser::Get()->is_shutting_down());
  Observe(nullptr);
}

// static
mate::WrappableBase* OffscreenWindow::New(gin_helper::ErrorThrower thrower,
                                          gin::Arguments* args) {
  if (!Browser::Get()->is_ready()) {
    thrower.ThrowError("Cannot create OffscreenWindow before app is ready");
    return nullptr;
  }

  if (args->Length() > 1) {
    args->ThrowError();
    return nullptr;
  }

  mate::Dictionary options;
  if (!(args->Length() == 1 && args->GetNext(&options))) {
    options = mate::Dictionary::CreateEmpty(args->isolate());
  }

  return new OffscreenWindow(args, options);
}

// static
void OffscreenWindow::BuildPrototype(
    v8::Isolate* isolate,
    v8::Local<v8::FunctionTemplate> prototype) {
  prototype->SetClassName(mate::StringToV8(isolate, "OffscreenWindow"));
  gin_helper::Destroyable::MakeDestroyable(isolate, prototype);
  mate::ObjectTemplateBuilder(isolate, prototype->PrototypeTemplate())
      .SetMethod("close", &OffscreenWindow::Close)
      .SetMethod("focus", &OffscreenWindow::Focus)
      .SetMethod("blur", &OffscreenWindow::Blur)
      .SetMethod("isFocused", &OffscreenWindow::IsFocused)
      .SetMethod("setSize", &OffscreenWindow::SetSize)
      .SetMethod("getSize", &OffscreenWindow::GetSize)
      .SetMethod("setBackgroundColor", &OffscreenWindow::SetBackgroundColor)
      .SetMethod("setOwnerWindow", &OffscreenWindow::SetOwnerWindow)
      .SetProperty("webContents", &OffscreenWindow::GetWebContents)
      .SetProperty("id", &OffscreenWindow::GetID);
}

}  // namespace api

// static
void OffscreenWindowRelay::CreateForWebContents(
    content::WebContents* web_contents,
    base::WeakPtr<api::OffscreenWindow> window) {
  DCHECK(web_contents);
  if (!web_contents->GetUserData(UserDataKey())) {
    web_contents->SetUserData(
        UserDataKey(), base::WrapUnique(new OffscreenWindowRelay(window)));
  }
}

OffscreenWindowRelay::OffscreenWindowRelay(
    base::WeakPtr<api::OffscreenWindow> window)
    : offscreen_window_(window) {}

OffscreenWindowRelay::~OffscreenWindowRelay() = default;

WEB_CONTENTS_USER_DATA_KEY_IMPL(OffscreenWindowRelay)

}  // namespace electron

namespace {

using electron::api::OffscreenWindow;

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  OffscreenWindow::SetConstructor(isolate,
                                  base::BindRepeating(&OffscreenWindow::New));

  gin_helper::Dictionary constructor(isolate,
                                     OffscreenWindow::GetConstructor(isolate)
                                         ->GetFunction(context)
                                         .ToLocalChecked());
  constructor.SetMethod("fromId", &OffscreenWindow::FromWeakMapID);
  constructor.SetMethod("getAllWindows", &OffscreenWindow::GetAll);

  gin_helper::Dictionary dict(isolate, exports);
  dict.Set("OffscreenWindow", constructor);
}

}  // namespace

NODE_LINKED_MODULE_CONTEXT_AWARE(atom_browser_offscreen_window, Initialize)
