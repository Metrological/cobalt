// Copyright 2019 The Cobalt Authors. All Rights Reserved.
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

#include "cobalt/dom/abort_controller.h"

namespace cobalt {
namespace dom {

AbortController::AbortController() {
  abort_signal_ = new AbortSignal();
}

void AbortController::Abort() {
  abort_signal_->SignalAbort();
}

void AbortController::TraceMembers(script::Tracer* tracer) {
  tracer->Trace(abort_signal_);
}

}  // namespace dom
}  // namespace cobalt