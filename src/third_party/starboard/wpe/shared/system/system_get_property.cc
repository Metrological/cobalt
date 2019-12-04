// Copyright 2017 The Cobalt Authors. All Rights Reserved.
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

#include <string>

#if SB_API_VERSION >= 11
#include "starboard/format_string.h"
#endif  // SB_API_VERSION >= 11
#include "starboard/common/log.h"
#include "starboard/common/string.h"

namespace {

const char kPlatformName[] = "Linux";

bool CopyStringAndTestIfSuccess(char* out_value,
                                int value_length,
                                const char* from_value) {
  if (SbStringGetLength(from_value) + 1 > value_length)
    return false;
  SbStringCopy(out_value, from_value, value_length);
  return true;
}

}  // namespace

bool SbSystemGetProperty(SbSystemPropertyId property_id,
                         char* out_value,
                         int value_length) {
  if (!out_value || !value_length) {
    return false;
  }

  switch (property_id) {
    case kSbSystemPropertyBrandName:
    return CopyStringAndTestIfSuccess(out_value, value_length,
                                      SB_PLATFORM_OPERATOR_NAME);
    case kSbSystemPropertyChipsetModelNumber:
      return CopyStringAndTestIfSuccess(
            out_value, value_length, SB_PLATFORM_CHIPSET_MODEL_NUMBER_STRING);
    case kSbSystemPropertyFirmwareVersion:
      return CopyStringAndTestIfSuccess(out_value, value_length,
                                        SB_PLATFORM_FIRMWARE_VERSION_STRING);
    case kSbSystemPropertyModelName:
      return CopyStringAndTestIfSuccess(out_value, value_length,
                                        SB_PLATFORM_MODEL_NAME);
    case kSbSystemPropertyModelYear:
      return CopyStringAndTestIfSuccess(out_value, value_length,
          std::to_string(SB_PLATFORM_MODEL_YEAR).c_str());
#if SB_API_VERSION >= 11
    case kSbSystemPropertyOriginalDesignManufacturerName:
#else
    case kSbSystemPropertyNetworkOperatorName:
#endif
      return CopyStringAndTestIfSuccess(out_value, value_length,
                                        SB_PLATFORM_MANUFACTURER_NAME);
    case kSbSystemPropertySpeechApiKey:
      return false;

    case kSbSystemPropertyFriendlyName:
      return CopyStringAndTestIfSuccess(out_value, value_length,
                                        SB_PLATFORM_FRIENDLY_NAME);

    case kSbSystemPropertyPlatformName:
      return CopyStringAndTestIfSuccess(out_value, value_length, kPlatformName);

#if SB_API_VERSION >= 11
    case kSbSystemPropertyCertificationScope:
    case kSbSystemPropertyBase64EncodedCertificationSecret:
#endif  // SB_API_VERSION >= 11

    default:
      SB_DLOG(WARNING) << __FUNCTION__
                       << ": Unrecognized property: " << property_id;
      break;
  }

  return false;
}
