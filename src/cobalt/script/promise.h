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

#ifndef COBALT_SCRIPT_PROMISE_H_
#define COBALT_SCRIPT_PROMISE_H_

#include "base/memory/ref_counted.h"
#include "cobalt/script/exception_message.h"
#include "cobalt/script/script_exception.h"
#include "cobalt/script/script_value.h"

namespace cobalt {
namespace script {

// Interface for interacting with a JavaScript Promise object that is resolved
// or rejected from native code.
template <typename T>
class Promise {
 public:
  // Call the |resolve| function that was passed as an argument to the Promise's
  // executor function supplying |result| as its argument.
  virtual void Resolve(const T& result) const = 0;

  // Call the |reject| function passed as an argument to the Promise's executor
  // function.
  virtual void Reject() const = 0;
  virtual void Reject(SimpleExceptionType exception) const = 0;
  virtual void Reject(const scoped_refptr<ScriptException>& result) const = 0;
  virtual ~Promise() {}
};

// Specialization of the Promise<T> class for Promise<void>, which does not take
// a value for the Resolve function.
template <>
class Promise<void> {
 public:
  // Call the |resolve| function passed as an argument to the Promise's executor
  // function.
  virtual void Resolve() const = 0;

  // Call the |reject| function passed as an argument to the Promise's executor
  // function.
  virtual void Reject() const = 0;
  virtual void Reject(SimpleExceptionType exception) const = 0;
  virtual void Reject(const scoped_refptr<ScriptException>& result) const = 0;
  virtual ~Promise() {}
};

}  // namespace script
}  // namespace cobalt

#endif  // COBALT_SCRIPT_PROMISE_H_
