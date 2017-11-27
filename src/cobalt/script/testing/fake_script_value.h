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

#include "cobalt/base/polymorphic_downcast.h"
#include "cobalt/script/script_value.h"

#ifndef COBALT_SCRIPT_TESTING_FAKE_SCRIPT_VALUE_H_
#define COBALT_SCRIPT_TESTING_FAKE_SCRIPT_VALUE_H_

namespace cobalt {
namespace script {
namespace testing {

template <class T>
class FakeScriptValue : public cobalt::script::ScriptValue<T> {
 public:
  typedef cobalt::script::ScriptValue<T> BaseClass;
  explicit FakeScriptValue(const T* listener)
      : value_(listener) {}

  void RegisterOwner(script::Wrappable*) OVERRIDE {}
  void DeregisterOwner(script::Wrappable*) OVERRIDE {}
  void PreventGarbageCollection() OVERRIDE {}
  void AllowGarbageCollection() OVERRIDE {}
  const T* GetScriptValue(void) const OVERRIDE {
    return value_;
  }
  scoped_ptr<BaseClass> MakeCopy() const OVERRIDE {
    return make_scoped_ptr<BaseClass>(new FakeScriptValue(value_));
  }

  bool EqualTo(const BaseClass& other) const OVERRIDE {
    const FakeScriptValue* other_script_object =
        base::polymorphic_downcast<const FakeScriptValue*>(&other);
    return value_ == other_script_object->value_;
  }

 private:
  const T* value_;
};

}  // namespace testing
}  // namespace script
}  // namespace cobalt

#endif  // COBALT_SCRIPT_TESTING_FAKE_SCRIPT_VALUE_H_
