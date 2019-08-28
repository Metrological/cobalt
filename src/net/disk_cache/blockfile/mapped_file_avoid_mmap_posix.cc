// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/blockfile/mapped_file.h"

#include <stdlib.h>

#include "base/files/file_path.h"
#include "base/logging.h"
#include "starboard/memory.h"
#include "starboard/types.h"

namespace disk_cache {

void* MappedFile::Init(const base::FilePath& name, size_t size) {
  DCHECK(!init_);
  if (init_ || !File::Init(name))
    return NULL;

  if (!size)
    size = GetLength();

  buffer_ = SbMemoryAllocate(size);
  snapshot_ = SbMemoryAllocate(size);
  if (buffer_ && snapshot_ && Read(buffer_, size, 0)) {
    SbMemoryCopy(snapshot_, buffer_, size);
  } else {
    SbMemoryDeallocate(buffer_);
    SbMemoryDeallocate(snapshot_);
    buffer_ = snapshot_ = 0;
  }

  init_ = true;
  view_size_ = size;
  return buffer_;
}

void MappedFile::Flush() {
  DCHECK(buffer_);
  DCHECK(snapshot_);
  const char* buffer_ptr = static_cast<const char*>(buffer_);
  char* snapshot_ptr = static_cast<char*>(snapshot_);
  const size_t block_size = 4096;
  for (size_t offset = 0; offset < view_size_; offset += block_size) {
    size_t size = std::min(view_size_ - offset, block_size);
    if (SbMemoryCompare(snapshot_ptr + offset, buffer_ptr + offset, size)) {
      SbMemoryCopy(snapshot_ptr + offset, buffer_ptr + offset, size);
      Write(snapshot_ptr + offset, size, offset);
    }
  }
}

MappedFile::~MappedFile() {
  if (!init_)
    return;

  if (buffer_ && snapshot_) {
    Flush();
  }
  SbMemoryDeallocate(buffer_);
  SbMemoryDeallocate(snapshot_);
}

}  // namespace disk_cache
