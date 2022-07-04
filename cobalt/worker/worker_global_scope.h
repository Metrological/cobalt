// Copyright 2022 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef COBALT_WORKER_WORKER_GLOBAL_SCOPE_H_
#define COBALT_WORKER_WORKER_GLOBAL_SCOPE_H_

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "base/memory/ref_counted.h"
#include "cobalt/base/tokens.h"
#include "cobalt/script/environment_settings.h"
#include "cobalt/script/exception_state.h"
#include "cobalt/script/sequence.h"
#include "cobalt/script/value_handle.h"
#include "cobalt/script/wrappable.h"
#include "cobalt/web/event_target.h"
#include "cobalt/web/event_target_listener_info.h"
#include "cobalt/web/user_agent_platform_info.h"
#include "cobalt/web/window_or_worker_global_scope.h"
#include "cobalt/web/window_timers.h"
#include "cobalt/worker/worker_location.h"
#include "cobalt/worker/worker_navigator.h"
#include "net/url_request/url_request.h"
#include "url/gurl.h"

namespace cobalt {
namespace worker {
// Implementation of the WorkerGlobalScope common interface.
//   https://html.spec.whatwg.org/commit-snapshots/465a6b672c703054de278b0f8133eb3ad33d93f4/#the-workerglobalscope-common-interface

using ScriptResourceMap = std::map<GURL, std::unique_ptr<std::string>>;

class WorkerGlobalScope : public web::WindowOrWorkerGlobalScope {
 public:
  using TimerCallback = web::WindowTimers::TimerCallback;
  using URLLookupCallback =
      base::Callback<std::string*(const GURL&, script::ExceptionState*)>;
  using ResponseCallback =
      base::Callback<std::string*(const GURL& url, std::string*)>;

  explicit WorkerGlobalScope(script::EnvironmentSettings* settings);
  WorkerGlobalScope(const WorkerGlobalScope&) = delete;
  WorkerGlobalScope& operator=(const WorkerGlobalScope&) = delete;

  // https://html.spec.whatwg.org/commit-snapshots/465a6b672c703054de278b0f8133eb3ad33d93f4/#dom-workerglobalscope-closing
  // THe closing flag.
  bool closing_flag() { return closing_flag_; }
  void set_closing_flag(bool value) { closing_flag_ = value; }

  virtual void Initialize() {}

  // Web API: WorkerGlobalScope
  //
  scoped_refptr<WorkerGlobalScope> self() { return this; }
  const scoped_refptr<WorkerLocation>& location() const { return location_; }
  const scoped_refptr<WorkerNavigator>& navigator() const { return navigator_; }

  virtual void ImportScripts(const std::vector<std::string>& urls,
                             script::ExceptionState* exception_state);

  void set_url(const GURL& url) { url_ = url; }

  const GURL Url() const { return url_; }

  const web::EventTargetListenerInfo::EventListenerScriptValue*
  onlanguagechange() {
    return GetAttributeEventListener(base::Tokens::languagechange());
  }
  void set_onlanguagechange(
      const web::EventTargetListenerInfo::EventListenerScriptValue&
          event_listener) {
    SetAttributeEventListener(base::Tokens::languagechange(), event_listener);
  }

  const web::EventTargetListenerInfo::EventListenerScriptValue*
  onrejectionhandled() {
    return GetAttributeEventListener(base::Tokens::rejectionhandled());
  }
  void set_onrejectionhandled(
      const web::EventTargetListenerInfo::EventListenerScriptValue&
          event_listener) {
    SetAttributeEventListener(base::Tokens::rejectionhandled(), event_listener);
  }
  const web::EventTargetListenerInfo::EventListenerScriptValue*
  onunhandledrejection() {
    return GetAttributeEventListener(base::Tokens::unhandledrejection());
  }
  void set_onunhandledrejection(
      const web::EventTargetListenerInfo::EventListenerScriptValue&
          event_listener) {
    SetAttributeEventListener(base::Tokens::unhandledrejection(),
                              event_listener);
  }

  // Custom, not in any spec.
  //
  bool LoadImportsAndReturnIfUpdated(
      const ScriptResourceMap& previous_resource_map,
      ScriptResourceMap* new_resource_map);

  DEFINE_WRAPPABLE_TYPE(WorkerGlobalScope);

 protected:
  virtual ~WorkerGlobalScope() {}

  virtual void ImportScriptsInternal(const std::vector<std::string>& urls,
                                     script::ExceptionState* exception_state,
                                     URLLookupCallback url_lookup_callback,
                                     ResponseCallback response_callback);

 private:
  // WorkerGlobalScope url
  //   https://html.spec.whatwg.org/commit-snapshots/465a6b672c703054de278b0f8133eb3ad33d93f4/#concept-workerglobalscope-url
  GURL url_;

  scoped_refptr<WorkerLocation> location_;
  scoped_refptr<WorkerNavigator> navigator_;

  bool closing_flag_ = false;
};

}  // namespace worker
}  // namespace cobalt

#endif  // COBALT_WORKER_WORKER_GLOBAL_SCOPE_H_
