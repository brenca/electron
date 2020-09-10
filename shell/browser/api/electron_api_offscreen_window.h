// Copyright (c) 2013 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#ifndef SHELL_BROWSER_API_ELECTRON_API_OFFSCREEN_WINDOW_H_
#define SHELL_BROWSER_API_ELECTRON_API_OFFSCREEN_WINDOW_H_

#include <memory>
#include <string>
#include <vector>

#include "base/cancelable_callback.h"
#include "shell/browser/api/electron_api_browser_window.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/api/trackable_object.h"
#include "shell/common/gin_helper/error_thrower.h"
#include "shell/common/gin_helper/event_emitter.h"

namespace electron {

namespace api {

class OffscreenWindow
    : public mate::TrackableObject<
          OffscreenWindow,
          gin_helper::EventEmitter<mate::Wrappable<OffscreenWindow>>>,
      public content::RenderWidgetHost::InputEventObserver,
      public content::WebContentsObserver,
      public ExtendedWebContentsObserver {
 public:
  static mate::WrappableBase* New(gin_helper::ErrorThrower thrower,
                                  gin::Arguments* args);

  static void BuildPrototype(v8::Isolate* isolate,
                             v8::Local<v8::FunctionTemplate> prototype);

  base::WeakPtr<OffscreenWindow> GetWeakPtr() {
    return weak_factory_.GetWeakPtr();
  }

  class Observer : public base::CheckedObserver {
   public:
    ~Observer() override {}

    virtual void OnWindowResize() {}
    virtual void OnWindowClosed() {}
  };

  void AddObserver(Observer* obs) { observers_.AddObserver(obs); }
  void RemoveObserver(Observer* obs) { observers_.RemoveObserver(obs); }

  void Close();
  void Focus();
  void Blur();
  bool IsFocused();
  void SetSize(int width, int height);
  std::vector<int> GetSize();
  gfx::Size GetInternalSize() const;
  void SetBackgroundColor(const std::string& color_name);
  void SetOwnerWindow(v8::Local<v8::Value> value);
  int32_t GetID() const;

 protected:
  OffscreenWindow(gin::Arguments* args, const mate::Dictionary& options);
  ~OffscreenWindow() override;

  // content::RenderWidgetHost::InputEventObserver:
  void OnInputEvent(const blink::WebInputEvent& event) override;

  // content::WebContentsObserver:
  void RenderViewHostChanged(content::RenderViewHost* old_host,
                             content::RenderViewHost* new_host) override;
  void RenderViewCreated(content::RenderViewHost* render_view_host) override;
  void DidFirstVisuallyNonEmptyPaint() override;
  void BeforeUnloadDialogCancelled() override;
  void OnRendererUnresponsive(content::RenderProcessHost*) override;

  // ExtendedWebContentsObserver:
  void OnCloseContents() override;
  void OnRendererResponsive() override;

  // OffscreenWindow APIs.
  v8::Local<v8::Value> GetWebContents(v8::Isolate* isolate);

 private:
  // Schedule a notification unresponsive event.
  void ScheduleUnresponsiveEvent(int ms);

  // Dispatch unresponsive event to observers.
  void NotifyWindowUnresponsive();

  // Cleanup our WebContents observers.
  void Cleanup();

  // Closure that would be called when window is unresponsive when closing,
  // it should be cancelled when we can prove that the window is responsive.
  base::CancelableClosure window_unresponsive_closure_;

  v8::Global<v8::Value> web_contents_;
  base::WeakPtr<api::WebContents> api_web_contents_;

  bool transparent_;
  int width_;
  int height_;

  // Observers of this window.
  base::ObserverList<Observer> observers_;

  base::WeakPtrFactory<OffscreenWindow> weak_factory_;

  DISALLOW_COPY_AND_ASSIGN(OffscreenWindow);
};

}  // namespace api

// This class provides a hook to get a OffscreenWindow from a WebContents.
class OffscreenWindowRelay
    : public content::WebContentsUserData<OffscreenWindowRelay> {
 public:
  static void CreateForWebContents(content::WebContents*,
                                   base::WeakPtr<api::OffscreenWindow>);

  ~OffscreenWindowRelay() override;

  api::OffscreenWindow* GetOffscreenWindow() const {
    return offscreen_window_.get();
  }

  WEB_CONTENTS_USER_DATA_KEY_DECL();

 private:
  friend class content::WebContentsUserData<api::OffscreenWindow>;
  explicit OffscreenWindowRelay(base::WeakPtr<api::OffscreenWindow> window);

  base::WeakPtr<api::OffscreenWindow> offscreen_window_;
};

}  // namespace electron

#endif  // SHELL_BROWSER_API_ELECTRON_API_OFFSCREEN_WINDOW_H_
