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

#ifndef COBALT_DOM_BUFFER_SOURCE_H_
#define COBALT_DOM_BUFFER_SOURCE_H_

#include "cobalt/script/union_type.h"

namespace cobalt {
namespace dom {

class ArrayBuffer;
class ArrayBufferView;

typedef script::UnionType2<scoped_refptr<ArrayBufferView>,
                           scoped_refptr<ArrayBuffer> > BufferSource;

void GetBufferAndSize(const BufferSource& buffer_source, const uint8** buffer,
                      int* buffer_size);

}  // namespace dom
}  // namespace cobalt

#endif  // COBALT_DOM_BUFFER_SOURCE_H_
