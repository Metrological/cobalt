// Copyright (c) 2010 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/disk_cache/blockfile/disk_format.h"
#include "starboard/memory.h"

namespace disk_cache {

IndexHeader::IndexHeader() {
  SbMemorySet(this, 0, sizeof(*this));
  magic = kIndexMagic;
  version = kCurrentVersion;
}

}  // namespace disk_cache
