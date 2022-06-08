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

#ifndef COBALT_WEB_NAVIGATOR_BASE_H_
#define COBALT_WEB_NAVIGATOR_BASE_H_

#include <string>

#include "base/memory/ref_counted.h"
#include "cobalt/script/script_value_factory.h"
#include "cobalt/script/wrappable.h"
#include "cobalt/web/navigator_ua_data.h"
#include "cobalt/worker/service_worker_container.h"

namespace cobalt {
namespace web {

// The NavigatorBase object contains the shared logic between Navigator and
// WorkerNavigator.
// https://html.spec.whatwg.org/commit-snapshots/9fe65ccaf193c8c0d93fd3638b4c8e935fc28213/#the-navigatorbase-object
// https://www.w3.org/TR/html50/webappapis.html#navigator

class NavigatorBase : public script::Wrappable {
 public:
  NavigatorBase(script::EnvironmentSettings* settings,
                const std::string& user_agent,
                UserAgentPlatformInfo* platform_info,
                const std::string& language,
                script::ScriptValueFactory* script_value_factory);

  // Web API: NavigatorID
  const std::string& user_agent() const;

  // Web API: NavigatorUA
  const scoped_refptr<NavigatorUAData>& user_agent_data() const;

  // Web API: NavigatorLanguage
  const std::string& language() const;
  script::Sequence<std::string> languages() const;

  // Web API: NavigatorOnline
  bool on_line() const;

  // Web API: ServiceWorker
  scoped_refptr<worker::ServiceWorkerContainer> service_worker();

  // Set maybe freeze callback.
  void set_maybefreeze_callback(const base::Closure& maybe_freeze_callback) {
    maybe_freeze_callback_ = maybe_freeze_callback;
  }

  script::ScriptValueFactory* script_value_factory() {
    return script_value_factory_;
  }

 private:
  std::string user_agent_;
  scoped_refptr<NavigatorUAData> user_agent_data_;
  std::string language_;
  scoped_refptr<worker::ServiceWorkerContainer> service_worker_;
  script::ScriptValueFactory* script_value_factory_;

  base::Closure maybe_freeze_callback_;
};

}  // namespace web
}  // namespace cobalt

#endif  // COBALT_WEB_NAVIGATOR_BASE_H_
