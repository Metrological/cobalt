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

#include <sstream>

#include "starboard/shared/win32/error_utils.h"
#include "starboard/string.h"

int SbSystemGetErrorString(SbSystemError error,
                           char* out_string,
                           int string_length) {
  std::ostringstream out;
  out << starboard::shared::win32::Win32ErrorCode(error);
  if (out_string != nullptr) {
    SbStringCopy(out_string, out.str().c_str(), string_length);
  }
  return static_cast<int>(out.str().size());
}
