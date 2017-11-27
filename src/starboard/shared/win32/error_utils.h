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

// Not breaking these functions up because however one is implemented, the
// others should be implemented similarly.

#ifndef STARBOARD_SHARED_WIN32_ERROR_UTILS_H_
#define STARBOARD_SHARED_WIN32_ERROR_UTILS_H_

#include <windows.h>

#include <iostream>
#include <string>

#include "starboard/log.h"

namespace starboard {
namespace shared {
namespace win32 {

class Win32ErrorCode {
 public:
  explicit Win32ErrorCode(DWORD error_code) : error_code_(error_code) {}

  HRESULT GetHRESULT() const { return HRESULT_FROM_WIN32(error_code_); }

 private:
  DWORD error_code_;
};

std::ostream& operator<<(std::ostream& os, const Win32ErrorCode& error_code);

// Checks for system errors and logs a human-readable error if GetLastError()
// returns an error code. Noops on non-debug builds.
void DebugLogWinError();

std::string HResultToString(HRESULT hr);

inline void CheckResult(HRESULT hr) {
  SB_DCHECK(SUCCEEDED(hr)) << "HRESULT was " << std::hex << hr
      << " which translates to\n---> \"" << HResultToString(hr) << "\"";
}

}  // namespace win32
}  // namespace shared
}  // namespace starboard

#endif  // STARBOARD_SHARED_WIN32_ERROR_UTILS_H_
