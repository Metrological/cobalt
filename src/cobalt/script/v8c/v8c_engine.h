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

#ifndef COBALT_SCRIPT_V8C_V8C_ENGINE_H_
#define COBALT_SCRIPT_V8C_V8C_ENGINE_H_

#include <vector>

#include "base/threading/thread_checker.h"
#include "base/timer.h"
#include "cobalt/script/javascript_engine.h"
#include "v8/include/libplatform/libplatform.h"
#include "v8/include/v8.h"

namespace cobalt {
namespace script {
namespace v8c {

class V8cEngine : public JavaScriptEngine {
 public:
  explicit V8cEngine(const Options& options);
  ~V8cEngine() override;

  scoped_refptr<GlobalEnvironment> CreateGlobalEnvironment() override;
  void CollectGarbage() override;
  void ReportExtraMemoryCost(size_t bytes) override;
  bool RegisterErrorHandler(JavaScriptEngine::ErrorHandler handler) override;
  void SetGcThreshold(int64_t bytes) override;

  v8::Isolate* isolate() const { return isolate_; }

 private:
  base::ThreadChecker thread_checker_;

  // An isolated instance of the V8 engine.
  v8::Isolate* isolate_;

  // The amount of externally allocated memory since last forced GC.
  size_t accumulated_extra_memory_cost_;

  // Used to handle javascript errors.
  ErrorHandler error_handler_;

  JavaScriptEngine::Options options_;
};

}  // namespace v8c
}  // namespace script
}  // namespace cobalt

#endif  // COBALT_SCRIPT_V8C_V8C_ENGINE_H_
