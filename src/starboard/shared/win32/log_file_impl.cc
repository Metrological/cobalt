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

#include "starboard/shared/win32/log_file_impl.h"

#include <string>

#include "starboard/file.h"
#include "starboard/memory.h"
#include "starboard/mutex.h"
#include "starboard/once.h"
#include "starboard/shared/win32/file_internal.h"
#include "starboard/string.h"

namespace {

SbMutex log_mutex = SB_MUTEX_INITIALIZER;
SbFile log_file = kSbFileInvalid;

// SbMutex is not reentrant, so factor out close log file functionality for use
// by other functions.
void CloseLogFileWithoutLock() {
  if (SbFileIsValid(log_file)) {
    SbFileFlush(log_file);
    SbFileClose(log_file);
    log_file = kSbFileInvalid;
  }
}

}  // namespace

namespace starboard {
namespace shared {
namespace win32 {

void CloseLogFile() {
  SbMutexAcquire(&log_mutex);
  CloseLogFileWithoutLock();
  SbMutexRelease(&log_mutex);
}

void OpenLogInCacheDirectory(const char* log_file_name, int creation_flags) {
  SB_DCHECK((creation_flags & kSbFileOpenAlways) ||
            (creation_flags & kSbFileCreateAlways));
  SB_DCHECK(SbStringGetLength(log_file_name) != 0);
  SB_DCHECK(SbStringFindCharacter(log_file_name, SB_FILE_SEP_CHAR) == nullptr);
  char out_path[SB_FILE_MAX_PATH + 1];
  out_path[0] = '\0';

  if (!SbSystemGetPath(kSbSystemPathCacheDirectory, out_path,
                       SB_ARRAY_SIZE_INT(out_path))) {
    return;
  }

  const int path_size = SB_ARRAY_SIZE_INT(out_path);
  if (SbStringConcat(out_path, SB_FILE_SEP_STRING, path_size) >= path_size) {
    return;
  }
  if (SbStringConcat(out_path, log_file_name, path_size) >= path_size) {
    return;
  }

  OpenLogFile(out_path, creation_flags);
}

void OpenLogFile(const char* path, const int creation_flags) {
  SB_DCHECK((creation_flags & kSbFileOpenAlways) ||
            (creation_flags & kSbFileCreateAlways));
  SB_DLOG(INFO) << "Logging to [" << path << "]";

  int flags = creation_flags | kSbFileWrite;

  SbMutexAcquire(&log_mutex);
  CloseLogFileWithoutLock();
  if ((path != nullptr) && (path[0] != '\0')) {
    log_file = SbFileOpen(path, flags, nullptr, nullptr);
    SB_DCHECK(SbFileIsValid(log_file));
  }

  SbMutexRelease(&log_mutex);
}

void WriteToLogFile(const char* text, const int text_length) {
  if (text_length <= 0) {
    return;
  }
  SbMutexAcquire(&log_mutex);
  if (!SbFileIsValid(log_file)) {
    SbMutexRelease(&log_mutex);
    return;
  }

  int bytes_written = SbFileWriteAll(log_file, text, text_length);
  SB_DCHECK(text_length == bytes_written);

  SbFileFlush(log_file);
  SbMutexRelease(&log_mutex);
}

}  // namespace win32
}  // namespace shared
}  // namespace starboard
