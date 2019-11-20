// Copyright 2016 The Cobalt Authors. All Rights Reserved.
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

#ifndef THIRD_PARTY_STARBOARD_WPE_SHARED_WINDOW_INTERNAL_H_
#define THIRD_PARTY_STARBOARD_WPE_SHARED_WINDOW_INTERNAL_H_

#include "base/macros.h"
#include "starboard/event.h"
#include "starboard/time.h"
#include "starboard/window.h"

#include "WPEFramework/compositor/Client.h"

namespace third_party {
namespace starboard {
namespace wpe {
namespace shared {
namespace window {

WPEFramework::Compositor::IDisplay* GetDisplay();
std::string DisplayName();

// FIXME PST: Move to separate files.
class KeyboardHandler : public WPEFramework::Compositor::IDisplay::IKeyboard {
 public:
  KeyboardHandler();
  ~KeyboardHandler() override {}

  void AddRef() const {}
  uint32_t Release() const override { return 0; }
  void KeyMap(const char information[], const uint16_t size) override {}
  void Key(const uint32_t key,
           const state action,
           const uint32_t time) override {
    Direct(key, action);
  }
  void Direct(const uint32_t key, const state action) override;
  void Modifiers(uint32_t mods_depressed,
                 uint32_t mods_latched,
                 uint32_t mods_locked,
                 uint32_t group) override;
  void Repeat(int32_t rate, int32_t delay) override;
  void SetWindow(SbWindow window);

 private:
  void CreateKey(int key, state action, bool is_repeat);
  void CreateRepeatKey();
  void DeleteRepeatKey();

  int key_repeat_key_{0};
  state key_repeat_state_{released};
  SbEventId key_repeat_event_id_{0};
  SbTime key_repeat_interval_{0};
  SbTime key_repeat_delay_{0};
  unsigned int key_modifiers_{0};
  SbWindow window_{};

  DISALLOW_COPY_AND_ASSIGN(KeyboardHandler);
};

}  // namespace window
}  // namespace shared
}  // namespace wpe
}  // namespace starboard
}  // namespace third_party

struct SbWindowPrivate {
  SbWindowPrivate(const SbWindowOptions* options);
  ~SbWindowPrivate();

  WPEFramework::Compositor::IDisplay::ISurface* CreateVideoOverlay();
  void DestroyVideoOverlay(WPEFramework::Compositor::IDisplay::ISurface*);

  WPEFramework::Compositor::IDisplay::ISurface* window_{nullptr};
  WPEFramework::Compositor::IDisplay::ISurface* video_overlay_{nullptr};
  third_party::starboard::wpe::shared::window::KeyboardHandler kb_handler_;
};

#endif  // THIRD_PARTY_STARBOARD_WPE_SHARED_WINDOW_INTERNAL_H_
