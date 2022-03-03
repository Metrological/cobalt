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

#include "third_party/starboard/wpe/shared/ext/get_platform_service_api.h"

#include <algorithm>
#include <memory>
#include <vector>

#include "cobalt/extension/platform_service.h"
#include "starboard/common/log.h"
#include "starboard/common/mutex.h"
#include "starboard/common/string.h"
#include "starboard/event.h"
#include "starboard/memory.h"
#include "starboard/window.h"

#include "cobalt/extension/platform_service.h"

namespace third_party {
namespace starboard {
namespace wpe {
namespace shared {
namespace ext {

namespace {

bool HasPlatformService(const char* name) {
  return true;
}

CobaltExtensionPlatformService OpenPlatformService(
    void* context,
    const char* name_c_str,
    ReceiveMessageCallback receive_callback) {
  // name_c_str is allocated by Cobalt, but must be freed here.
  std::unique_ptr<const char[]> service_name(name_c_str);

  SB_DCHECK(context);
  SB_LOG(INFO) << "Open " << service_name.get();

  return reinterpret_cast<CobaltExtensionPlatformService>(~0);
}

void ClosePlatformService(CobaltExtensionPlatformService service) {
  SB_DCHECK(service);
}

void* SendToPlatformService(CobaltExtensionPlatformService service,
                            void* data,
                            uint64_t length,
                            uint64_t* output_length,
                            bool* invalid_state) {
  SB_DCHECK(service);
  SB_DCHECK(data);
  SB_DCHECK(output_length);
  SB_DCHECK(invalid_state);

  std::vector<uint8_t> buffer(static_cast<uint8_t*>(data),
                              static_cast<uint8_t*>(data) + length);
  std::vector<uint8_t> response = std::vector<uint8_t>();

  return response.data();
}

const CobaltExtensionPlatformServiceApi kPlatformServiceApi = {
    kCobaltExtensionPlatformServiceName,
    // API version that's implemented.
    1,
    &HasPlatformService,
    &OpenPlatformService,
    &ClosePlatformService,
    &SendToPlatformService,
};
}  // namespace

const void* GetPlatformServiceApi() {
  return &kPlatformServiceApi;
}

}  // namespace ext
}  // namespace shared
}  // namespace wpe
}  // namespace starboard
}  // namespace third_party
