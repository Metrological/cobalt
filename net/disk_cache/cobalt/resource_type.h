// Copyright 2022 The Cobalt Authors. All Rights Reserved.
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

#ifndef RESOURCE_TYPE_H_
#define RESOURCE_TYPE_H_

namespace disk_cache {

/* Note: If adding a new resource type, add corresponding metadata below. */
enum ResourceType {
  kOther = 0,
  kHTML = 1,
  kCSS = 2,
  kImage = 3,
  kFont = 4,
  kSplashScreen = 5,
  kUncompiledScript = 6,
  kCompiledScript = 7,
  kTypeCount = 8
};

struct ResourceTypeMetadata {
  std::string directory;
  int64_t max_size_mb;
};

// TODO: Store sizes on disk.
static ResourceTypeMetadata kTypeMetadata[] = {
    {"other", 3}, {"html", 3},   {"css", 3},           {"image", 3},
    {"font", 3},  {"splash", 3}, {"uncompiled_js", 3}, {"compiled_js", 3},
};

}  // namespace disk_cache

#endif  // RESOURCE_TYPE_H_
