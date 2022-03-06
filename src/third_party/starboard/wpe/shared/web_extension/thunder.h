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

#ifndef COBALT_WEBAPI_EXTENSION_THUNDER_TOKEN_H_
#define COBALT_WEBAPI_EXTENSION_THUNDER_TOKEN_H_

#include "base/message_loop/message_loop.h"
#include "cobalt/base/polymorphic_downcast.h"
#include "cobalt/dom/dom_settings.h"
#include "cobalt/script/global_environment.h"

namespace cobalt {
namespace webapi_extension {

class Thunder : public script::Wrappable {
 public:
  Thunder(script::GlobalEnvironment* environment);
  ~Thunder();

  static std::string Token(script::EnvironmentSettings* settings);

  DEFINE_WRAPPABLE_TYPE(Thunder);

 private:
  script::GlobalEnvironment* environment_;
  scoped_refptr<base::SingleThreadTaskRunner> const main_message_loop_;
  base::WeakPtrFactory<Thunder> weak_ptr_factory_;
  base::WeakPtr<Thunder> weak_this_;
};
}  // namespace webapi_extension
}  // namespace cobalt

#endif  // COBALT_WEBAPI_EXTENSION_THUNDER_TOKEN_H_
