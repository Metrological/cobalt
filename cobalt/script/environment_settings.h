// Copyright 2014 The Cobalt Authors. All Rights Reserved.
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

#ifndef COBALT_SCRIPT_ENVIRONMENT_SETTINGS_H_
#define COBALT_SCRIPT_ENVIRONMENT_SETTINGS_H_

#include <memory>

#include "base/memory/ref_counted.h"
#include "cobalt/base/debugger_hooks.h"
#include "url/gurl.h"

namespace cobalt {
namespace script {

// Environment Settings object as described in
// https://html.spec.whatwg.org/commit-snapshots/465a6b672c703054de278b0f8133eb3ad33d93f4/#environment-settings-objects
class EnvironmentSettings {
 public:
  explicit EnvironmentSettings(
      const base::DebuggerHooks& debugger_hooks = null_debugger_hooks_)
      : debugger_hooks_(debugger_hooks) {}
  virtual ~EnvironmentSettings() {}

  // The API base URL.
  //   https://html.spec.whatwg.org/commit-snapshots/465a6b672c703054de278b0f8133eb3ad33d93f4/#api-base-url
  void set_base_url(const GURL& url) { base_url_ = url; }
  const GURL& base_url() const { return base_url_; }

  // The API creation URL
  //   https://html.spec.whatwg.org/commit-snapshots/465a6b672c703054de278b0f8133eb3ad33d93f4/#concept-environment-creation-url
  const GURL& creation_url() const { return base_url(); }

  const base::DebuggerHooks& debugger_hooks() const { return debugger_hooks_; }

 protected:
  friend std::unique_ptr<EnvironmentSettings>::deleter_type;

 private:
  DISALLOW_COPY_AND_ASSIGN(EnvironmentSettings);

  static const base::NullDebuggerHooks null_debugger_hooks_;
  const base::DebuggerHooks& debugger_hooks_;
  GURL base_url_;
};

}  // namespace script
}  // namespace cobalt

#endif  // COBALT_SCRIPT_ENVIRONMENT_SETTINGS_H_
