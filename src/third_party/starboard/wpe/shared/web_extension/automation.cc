// Copyright 2021 The Cobalt Authors. All Rights Reserved.
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

#include "third_party/starboard/wpe/shared/web_extension/automation.h"

namespace cobalt {
namespace webapi_extension {

// static
void Automation::Milestone(
    script::EnvironmentSettings* settings, 
    std::string arg1, std::string arg2, std::string arg3) {
  DCHECK(settings);
  dom::DOMSettings* dom_settings =
      base::polymorphic_downcast<dom::DOMSettings*>(settings);
  auto* global_environment = dom_settings->global_environment();
  DCHECK(global_environment);

  scoped_refptr<Automation> extension = new Automation(global_environment);
  printf("TEST TRACE: \"%s\" \"%s\" \"%s\"\n", arg1.c_str(), arg2.c_str(), arg3.c_str());
}

Automation::Automation(
    script::GlobalEnvironment* environment)
    : environment_(environment),
      main_message_loop_(base::MessageLoop::current()->task_runner()),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_ptr_factory_(this)),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          weak_this_(weak_ptr_factory_.GetWeakPtr())) {
  DCHECK(main_message_loop_);
}

Automation::~Automation() {
}
}  // namespace webapi_extension
}  // namespace cobalt
