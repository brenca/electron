// Copyright (c) 2018 GitHub, Inc.
// Use of this source code is governed by the MIT license that can be
// found in the LICENSE file.

#include "shell/browser/api/electron_api_osr_host_view.h"

#include "content/public/browser/web_contents_user_data.h"
#include "native_mate/dictionary.h"
#include "shell/browser/api/electron_api_web_contents.h"
#include "shell/browser/browser.h"
#include "shell/browser/ui/inspectable_web_contents_view.h"
#include "shell/common/api/constructor.h"
#include "shell/common/node_includes.h"

#if defined(OS_MACOSX)
#include "shell/browser/ui/cocoa/delayed_native_view_host.h"
#endif

namespace electron {

namespace api {

OsrHostView::OsrHostView(v8::Isolate* isolate) : View() {
  set_delete_view(true);
}

OsrHostView::~OsrHostView() {
  view()->RemoveAllChildViews(true);
}

void OsrHostView::AddChildWebContents(mate::Handle<WebContentsView> child) {
  child_views_.push_back(
      v8::Global<v8::Object>(isolate(), child->GetWrapper()));
  view()->AddChildView(child->view());
}

void OsrHostView::RemoveChildWebContents(int32_t id) {
  v8::Isolate* isolate = v8::Isolate::GetCurrent();

  for (auto it = child_views_.begin(); it != child_views_.end(); ++it) {
    mate::Handle<WebContentsView> web_contents_view;
    if (gin::ConvertFromV8(isolate, it->Get(isolate), &web_contents_view) &&
        !web_contents_view.IsEmpty()) {
      if (web_contents_view->ID() == id) {
        view()->RemoveChildView(web_contents_view->view());
        child_views_.erase(it);
        return;
      }
    }
  }
}

size_t OsrHostView::ChildCount() {
  return child_views_.size();
}

// static
mate::WrappableBase* OsrHostView::New(gin::Arguments* args) {
  // Constructor call.
  auto* view = new OsrHostView(args->isolate());
  view->InitWithArgs(args);
  return view;
}

// static
void OsrHostView::BuildPrototype(v8::Isolate* isolate,
                                 v8::Local<v8::FunctionTemplate> prototype) {
  prototype->SetClassName(mate::StringToV8(isolate, "OsrHostView"));
  gin_helper::Destroyable::MakeDestroyable(isolate, prototype);
  mate::ObjectTemplateBuilder(isolate, prototype->PrototypeTemplate())
      .SetMethod("addWebContents", &OsrHostView::AddChildWebContents)
      .SetMethod("removeWebContents", &OsrHostView::RemoveChildWebContents)
      .SetProperty("childCount", &OsrHostView::ChildCount);
}

}  // namespace api

}  // namespace electron

namespace {

using electron::api::OsrHostView;

void Initialize(v8::Local<v8::Object> exports,
                v8::Local<v8::Value> unused,
                v8::Local<v8::Context> context,
                void* priv) {
  v8::Isolate* isolate = context->GetIsolate();
  mate::Dictionary dict(isolate, exports);
  dict.Set("OsrHostView", mate::CreateConstructor<OsrHostView>(
                              isolate, base::BindRepeating(&OsrHostView::New)));
}

}  // namespace

NODE_LINKED_MODULE_CONTEXT_AWARE(atom_browser_osr_host_view, Initialize)
