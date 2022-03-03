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

#include "cobalt/extension/platform_service.h"
#include "starboard/common/log.h"

typedef struct CobaltExtensionPlatformServicePrivate {
  void* context;
  ReceiveMessageCallback receive_callback;
  const char* name;

  ~CobaltExtensionPlatformServicePrivate() {
    if (name) {
      delete name;
    }
  }
} CobaltExtensionPlatformServicePrivate;

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
    const char* name,
    ReceiveMessageCallback receive_callback) {

  SB_DCHECK(context);
  if (!HasPlatformService(name)) {
    SB_LOG(ERROR) << "Can't open Service " << name;
    return kCobaltExtensionPlatformServiceInvalid;
  }

  CobaltExtensionPlatformService service =
      new CobaltExtensionPlatformServicePrivate(
          {context, receive_callback, name});
  return service;
}

void ClosePlatformService(CobaltExtensionPlatformService service) {
  delete static_cast<CobaltExtensionPlatformServicePrivate*>(service);
}

void* SendToPlatformService(CobaltExtensionPlatformService service,
                            void* data,
                            uint64_t length,
                            uint64_t* output_length,
                            bool* invalid_state) {
  SB_DCHECK(data);
  SB_DCHECK(output_length);
  SB_DCHECK(invalid_state);

  *invalid_state = false;
  *output_length = length;
  uint8_t* output = new uint8_t[*output_length];
  for (int i = 0; i < length; i++) {
    output[i] = static_cast<uint8_t*>(data)[i] * 2;
  }
  return output;
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
