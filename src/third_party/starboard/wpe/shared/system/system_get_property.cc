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

#include "starboard/common/log.h"
#include "starboard/common/string.h"

#ifdef HAS_PROVISION
#include <provisionproxy/AccessProvision.h>
#endif

#include <cstring>
#include <deviceinfo/deviceinfo.h>

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
    case kSbSystemPropertyBrandName: {
      auto* property_name = std::getenv("COBALT_OPERATOR_NAME");
      if (property_name) {
        return CopyStringAndTestIfSuccess(out_value, value_length, property_name);
      } else {
        return CopyStringAndTestIfSuccess(out_value, value_length,
            SB_PLATFORM_OPERATOR_NAME);
      }
    }
    case kSbSystemPropertyChipsetModelNumber: {
      char property_name[128];
      uint8_t property_length = sizeof(property_name);
      std::memset(property_name, 0, property_length);
      uint32_t ret = deviceinfo_chipset(property_name, &property_length);
      if ((ret == 0) && (property_length > 0)) {
        return CopyStringAndTestIfSuccess(out_value, value_length, property_name);
      } else {
        return CopyStringAndTestIfSuccess(out_value, value_length,
            SB_PLATFORM_CHIPSET_MODEL_NUMBER_STRING);
      }
    }
    case kSbSystemPropertyFirmwareVersion: {
      char property_name[128];
      uint8_t property_length = sizeof(property_name);
      std::memset(property_name, 0, property_length);
      uint32_t ret = deviceinfo_firmware_version(property_name, &property_length);
      if ((ret == 0) && (property_length > 0)) {
        return CopyStringAndTestIfSuccess(out_value, value_length, property_name);
      } else {
        return CopyStringAndTestIfSuccess(out_value, value_length,
            SB_PLATFORM_FIRMWARE_VERSION_STRING);
      }
    }
    case kSbSystemPropertyModelName: {
      char property_name[128];
      uint8_t property_length = sizeof(property_name);
      std::memset(property_name, 0, property_length);
      uint32_t ret = deviceinfo_model_name(property_name, &property_length);
      if ((ret == 0) && (property_length > 0)) {
        return CopyStringAndTestIfSuccess(out_value, value_length, property_name);
      } else {
        return CopyStringAndTestIfSuccess(out_value, value_length,
            SB_PLATFORM_MODEL_NAME);
      }
    }
    case kSbSystemPropertyModelYear: {
      char property_name[128];
      uint8_t property_length = sizeof(property_name);
      std::memset(property_name, 0, property_length);
      uint32_t ret = deviceinfo_model_year(property_name, &property_length);
      if ((ret == 0) && (property_length > 0)) {
        return CopyStringAndTestIfSuccess(out_value, value_length, property_name);
      } else {
        return CopyStringAndTestIfSuccess(out_value, value_length,
            std::to_string(SB_PLATFORM_MODEL_YEAR).c_str());
      }
    }
#if SB_API_VERSION >= 12
    case kSbSystemPropertySystemIntegratorName:
#elif SB_API_VERSION == 11
    case kSbSystemPropertyOriginalDesignManufacturerName:
#else
    case kSbSystemPropertyNetworkOperatorName:
#endif
    {
      char property_name[128];
      uint8_t property_length = sizeof(property_name);
      std::memset(property_name, 0, property_length);
      uint32_t ret = deviceinfo_system_integrator_name(property_name, &property_length);
      if ((ret == 0) && (property_length > 0)) {
        return CopyStringAndTestIfSuccess(out_value, value_length, property_name);
      } else {
        return CopyStringAndTestIfSuccess(out_value, value_length,
            SB_PLATFORM_MANUFACTURER_NAME);
      }
    }
    case kSbSystemPropertySpeechApiKey:
      return false;

    case kSbSystemPropertyFriendlyName: {
      char property_name[128];
      uint8_t property_length = sizeof(property_name);
      std::memset(property_name, 0, property_length);
      uint32_t ret = deviceinfo_friendly_name(property_name, &property_length);
      if ((ret == 0) && (property_length > 0)) {
        return CopyStringAndTestIfSuccess(out_value, value_length, property_name);
      } else {
        return CopyStringAndTestIfSuccess(out_value, value_length,
            SB_PLATFORM_FRIENDLY_NAME);
      }
    }
    case kSbSystemPropertyPlatformName: {
      char property_name[128];
      uint8_t property_length = sizeof(property_name);
      std::memset(property_name, 0, property_length);
      uint32_t ret = deviceinfo_platform_name(property_name, &property_length);
      if ((ret == 0) && (property_length > 0)) {
        return CopyStringAndTestIfSuccess(out_value, value_length, property_name);
      } else {
        return CopyStringAndTestIfSuccess(out_value, value_length, kPlatformName);
      }
    }

#if SB_API_VERSION >= 11
    case kSbSystemPropertyCertificationScope: {

#ifdef HAS_PROVISION
      char property_name[1024];
      int length;
      if ( (length = GetDRMId("cobalt", sizeof(property_name), property_name)) > 0) {
        std::string text (property_name, length);
        // Find the ','
        size_t index = text.find(',', 0);
        if (index != std::string::npos) {
            return CopyStringAndTestIfSuccess(out_value, value_length, text.substr(index+1).c_str());
        }
      }
      return (false);
#else
      auto* property_name = std::getenv("COBALT_CERTIFICATION_SCOPE");

      if (property_name) {
        return CopyStringAndTestIfSuccess(out_value, value_length, property_name);
      } else {
        return (false);
      }
#endif
    }
    case kSbSystemPropertyBase64EncodedCertificationSecret: {
#ifdef HAS_PROVISION
      char property_name[1024];
      int length;
      if ( (length = GetDRMId("cobalt", sizeof(property_name), property_name)) > 0) {
        std::string text (property_name, length);
        // Find the ','
        size_t index = text.find(',', 0);
        if (index != std::string::npos) {
            return CopyStringAndTestIfSuccess(out_value, value_length, text.substr(0, index).c_str());
        }
      }
      return (false);
#else
      auto* property_name = std::getenv("COBALT_CERTIFICATION_SECRET");
      if (property_name) {
        return CopyStringAndTestIfSuccess(out_value, value_length, property_name);
      } else {
        return (false);
      }
#endif
    }
#endif  // SB_API_VERSION >= 11

    default:
      SB_DLOG(WARNING) << __FUNCTION__
                       << ": Unrecognized property: " << property_id;
      break;
  }

  return false;
}
