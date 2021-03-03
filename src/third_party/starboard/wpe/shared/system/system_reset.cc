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

#include "third_party/starboard/wpe/shared/system/system_reset.h"
#include "starboard/configuration_constants.h"
#include "starboard/common/file.h"
#include "starboard/common/string.h"
#include "starboard/directory.h"
#include "starboard/user.h"

#include <stdio.h>

namespace third_party {
namespace starboard {
namespace wpe {
namespace shared {
namespace system {

bool ResetFactory()
{
  bool status =  false;
  if (ResetCache() == true) {
    status = ResetCredentials();
  }

  return status;
}

bool ResetCache()
{
  bool status = false;
  char cache_path[kSbFileMaxPath];
  if (SbUserGetProperty(SbUserGetCurrent(), kSbUserPropertyHomeDirectory,
                       cache_path, sizeof(cache_path))) {
    if (SbStringConcat(cache_path, "/.cache", sizeof(cache_path))) {
      status = true;
      if (::starboard::SbFileDeleteRecursive(cache_path, false)) {
        status = true;
      }
    }
  }

  return status;
}

bool ResetCredentials()
{
  bool status = true;
  char cred_prefix[] = ".starboard.";
  char home_path[kSbFileMaxPath];
  if (SbUserGetProperty(SbUserGetCurrent(), kSbUserPropertyHomeDirectory,
                       home_path, sizeof(home_path))) {
    SbFileError err = kSbFileOk;
    SbDirectory dir = SbDirectoryOpen(home_path, &err);
    if (err == kSbFileOk) {
#if SB_API_VERSION >= 12
      std::vector<char> entry(kSbFileMaxName);

      while (SbDirectoryGetNext(dir, entry.data(), kSbFileMaxName)) {
        if (!SbStringCompare(entry.data(), cred_prefix, sizeof(cred_prefix) - 1)) {

          char cred_path[kSbFileMaxPath];
          SbStringFormatF(cred_path, kSbFileMaxPath, "%s/%s", home_path, entry.data());
          if (!SbFileDelete(cred_path)) {
             status = false;
          }
        }
#else
      SbDirectoryEntry entry;

      while (SbDirectoryGetNext(dir, &entry)) {
        if (!SbStringCompare(entry.name, cred_prefix, sizeof(cred_prefix) - 1)) {
          char cred_path[kSbFileMaxPath];
          SbStringFormatF(cred_path, kSbFileMaxPath, "%s/%s", home_path, entry.data());
          if (!SbFileDelete(cred_path)) {
             status = false;
          }
        }
#endif
      }
    }
  } else {
    status = false;
  }

  return status;
}

}  // namespace system
}  // namespace shared
}  // namespace wpe
}  // namespace starboard
}  // namespace third_party
