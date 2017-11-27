// Copyright 2017 Google Inc. All Rights Reserved.
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

#include "starboard/system.h"

#include <windows.h>

#include "starboard/log.h"

int64_t SbSystemGetUsedCPUMemory() {
  MEMORYSTATUSEX statex = {0};
  statex.dwLength = sizeof(statex);
  GlobalMemoryStatusEx(&statex);
  int64_t remaining_bytes = static_cast<int64_t>(statex.ullTotalPhys) -
                            static_cast<int64_t>(statex.ullAvailPhys);
  SB_DCHECK(remaining_bytes >= 0);
  return remaining_bytes;
}
