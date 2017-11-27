// Copyright 2016 Google Inc. All Rights Reserved.
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

#include "starboard/storage.h"

#include <algorithm>

#include "starboard/file.h"
#include "starboard/shared/starboard/file_storage/storage_internal.h"

bool SbStorageWriteRecord(SbStorageRecord record,
                          const char* data,
                          int64_t data_size) {
  if (!SbStorageIsValidRecord(record) || !data || data_size < 0) {
    return false;
  }

  int64_t position = SbFileSeek(record->file, kSbFileFromBegin, 0);
  if (position != 0) {
    return false;
  }

  SbFileTruncate(record->file, 0);

  const char* source = data;
  int64_t to_write = data_size;
  while (to_write > 0) {
    int to_write_max =
        static_cast<int>(std::min(to_write, static_cast<int64_t>(kSbInt32Max)));
    int bytes_written = SbFileWrite(record->file, source, to_write_max);
    if (bytes_written < 0) {
      SbFileTruncate(record->file, 0);
      return false;
    }

    source += bytes_written;
    to_write -= bytes_written;
  }

  SbFileFlush(record->file);
  return true;
}
