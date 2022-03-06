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

#include "third_party/starboard/wpe/shared/web_extension/thunder.h"
#include <WPEFramework/securityagent/securityagent.h>

namespace cobalt {
namespace webapi_extension {

// static
std::string Thunder::Token(
    script::EnvironmentSettings* settings) {
  DCHECK(settings);
  dom::DOMSettings* dom_settings =
      base::polymorphic_downcast<dom::DOMSettings*>(settings);
  auto* global_environment = dom_settings->global_environment();
  DCHECK(global_environment);

  scoped_refptr<Thunder> extension = new Thunder(global_environment);

  uint8_t buffer[2 * 1024];
  std::string url("http://localhost/");
  std::string tokenAsString;
  if (url.length() < sizeof(buffer)) {
    ::memset(buffer, 0, sizeof(buffer));
    ::memcpy(buffer, url.c_str(), url.length());
    int length =
        GetToken(static_cast<uint16_t>(sizeof(buffer)), url.length(), buffer);
    if (length > 0) {
      tokenAsString =
          std::string(reinterpret_cast<const char*>(buffer), length);
    }
  }
  return tokenAsString;
}

Thunder::Thunder(
    script::GlobalEnvironment* environment)
    : environment_(environment),
      main_message_loop_(base::MessageLoop::current()->task_runner()),
      ALLOW_THIS_IN_INITIALIZER_LIST(weak_ptr_factory_(this)),
      ALLOW_THIS_IN_INITIALIZER_LIST(
          weak_this_(weak_ptr_factory_.GetWeakPtr())) {
  DCHECK(main_message_loop_);
}

Thunder::~Thunder() {
}
}  // namespace webapi_extension
}  // namespace cobalt
