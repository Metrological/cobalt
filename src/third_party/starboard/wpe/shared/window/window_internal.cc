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

#include "third_party/starboard/wpe/shared/window/window_internal.h"

#include <linux/input.h>

#include <algorithm>

#include "starboard/common/log.h"
#include "starboard/input.h"
#include "starboard/key.h"
#include "third_party/starboard/wpe/shared/application_wpe.h"

using namespace WPEFramework;
using namespace WPEFramework::Compositor;

namespace third_party {
namespace starboard {
namespace wpe {
namespace shared {
namespace window {

namespace {

constexpr int kDefaultWidth = 1280;
constexpr int kDefaultHeight = 720;

// YouTube Technical Requirement 2018 (2016/11/1 - Initial draft)
// 9.5 The device MUST dispatch the following key events, as appropriate:
//  * Window.keydown
//      * After a key is held down for 500ms, the Window.keydown event
//        MUST repeat every 50ms until a user stops holding the key down.
//  * Window.keyup
constexpr SbTime kKeyHoldTime = 500 * kSbTimeMillisecond;
constexpr SbTime kKeyRepeatTime = 50 * kSbTimeMillisecond;

#define KEY_INFO_BUTTON 0xbc

// Converts an input_event code into an SbKey.
SbKey KeyCodeToSbKey(uint16_t code) {
  switch (code) {
    case KEY_BACKSPACE:
      return kSbKeyBack;
    case KEY_DELETE:
      return kSbKeyDelete;
    case KEY_TAB:
      return kSbKeyTab;
    case KEY_LINEFEED:
    case KEY_ENTER:
    case KEY_KPENTER:
      return kSbKeyReturn;
    case KEY_CLEAR:
      return kSbKeyClear;
    case KEY_SPACE:
      return kSbKeySpace;
    case KEY_HOME:
      return kSbKeyHome;
    case KEY_END:
      return kSbKeyEnd;
    case KEY_PAGEUP:
      return kSbKeyPrior;
    case KEY_PAGEDOWN:
      return kSbKeyNext;
    case KEY_LEFT:
      return kSbKeyLeft;
    case KEY_RIGHT:
      return kSbKeyRight;
    case KEY_DOWN:
      return kSbKeyDown;
    case KEY_UP:
      return kSbKeyUp;
    case KEY_ESC:
      return kSbKeyEscape;
    case KEY_KATAKANA:
    case KEY_HIRAGANA:
    case KEY_KATAKANAHIRAGANA:
      return kSbKeyKana;
    case KEY_HANGEUL:
      return kSbKeyHangul;
    case KEY_HANJA:
      return kSbKeyHanja;
    case KEY_HENKAN:
      return kSbKeyConvert;
    case KEY_MUHENKAN:
      return kSbKeyNonconvert;
    case KEY_ZENKAKUHANKAKU:
      return kSbKeyDbeDbcschar;
    case KEY_A:
      return kSbKeyA;
    case KEY_B:
      return kSbKeyB;
    case KEY_C:
      return kSbKeyC;
    case KEY_D:
      return kSbKeyD;
    case KEY_E:
      return kSbKeyE;
    case KEY_F:
      return kSbKeyF;
    case KEY_G:
      return kSbKeyG;
    case KEY_H:
      return kSbKeyH;
    case KEY_I:
      return kSbKeyI;
    case KEY_J:
      return kSbKeyJ;
    case KEY_K:
      return kSbKeyK;
    case KEY_L:
      return kSbKeyL;
    case KEY_M:
      return kSbKeyM;
    case KEY_N:
      return kSbKeyN;
    case KEY_O:
      return kSbKeyO;
    case KEY_P:
      return kSbKeyP;
    case KEY_Q:
      return kSbKeyQ;
    case KEY_R:
      return kSbKeyR;
    case KEY_S:
      return kSbKeyS;
    case KEY_T:
      return kSbKeyT;
    case KEY_U:
      return kSbKeyU;
    case KEY_V:
      return kSbKeyV;
    case KEY_W:
      return kSbKeyW;
    case KEY_X:
      return kSbKeyX;
    case KEY_Y:
      return kSbKeyY;
    case KEY_Z:
      return kSbKeyZ;

    case KEY_0:
      return kSbKey0;
    case KEY_1:
      return kSbKey1;
    case KEY_2:
      return kSbKey2;
    case KEY_3:
      return kSbKey3;
    case KEY_4:
      return kSbKey4;
    case KEY_5:
      return kSbKey5;
    case KEY_6:
      return kSbKey6;
    case KEY_7:
      return kSbKey7;
    case KEY_8:
      return kSbKey8;
    case KEY_9:
      return kSbKey9;

    case KEY_NUMERIC_0:
    case KEY_NUMERIC_1:
    case KEY_NUMERIC_2:
    case KEY_NUMERIC_3:
    case KEY_NUMERIC_4:
    case KEY_NUMERIC_5:
    case KEY_NUMERIC_6:
    case KEY_NUMERIC_7:
    case KEY_NUMERIC_8:
    case KEY_NUMERIC_9:
      return static_cast<SbKey>(kSbKey0 + (code - KEY_NUMERIC_0));

    case KEY_KP0:
      return kSbKeyNumpad0;
    case KEY_KP1:
      return kSbKeyNumpad1;
    case KEY_KP2:
      return kSbKeyNumpad2;
    case KEY_KP3:
      return kSbKeyNumpad3;
    case KEY_KP4:
      return kSbKeyNumpad4;
    case KEY_KP5:
      return kSbKeyNumpad5;
    case KEY_KP6:
      return kSbKeyNumpad6;
    case KEY_KP7:
      return kSbKeyNumpad7;
    case KEY_KP8:
      return kSbKeyNumpad8;
    case KEY_KP9:
      return kSbKeyNumpad9;

    case KEY_KPASTERISK:
      return kSbKeyMultiply;
    case KEY_KPDOT:
      return kSbKeyDecimal;
    case KEY_KPSLASH:
      return kSbKeyDivide;
    case KEY_KPPLUS:
    case KEY_EQUAL:
      return kSbKeyOemPlus;
    case KEY_COMMA:
      return kSbKeyOemComma;
    case KEY_KPMINUS:
    case KEY_MINUS:
      return kSbKeyOemMinus;
    case KEY_DOT:
      return kSbKeyOemPeriod;
    case KEY_SEMICOLON:
      return kSbKeyOem1;
    case KEY_SLASH:
      return kSbKeyOem2;
    case KEY_GRAVE:
      return kSbKeyOem3;
    case KEY_LEFTBRACE:
      return kSbKeyOem4;
    case KEY_BACKSLASH:
      return kSbKeyOem5;
    case KEY_RIGHTBRACE:
      return kSbKeyOem6;
    case KEY_APOSTROPHE:
      return kSbKeyOem7;
    case KEY_LEFTSHIFT:
    case KEY_RIGHTSHIFT:
      return kSbKeyShift;
    case KEY_LEFTCTRL:
    case KEY_RIGHTCTRL:
      return kSbKeyControl;
    case KEY_LEFTMETA:
    case KEY_RIGHTMETA:
    case KEY_LEFTALT:
    case KEY_RIGHTALT:
      return kSbKeyMenu;
    case KEY_PAUSE:
      return kSbKeyPause;
    case KEY_CAPSLOCK:
      return kSbKeyCapital;
    case KEY_NUMLOCK:
      return kSbKeyNumlock;
    case KEY_SCROLLLOCK:
      return kSbKeyScroll;
    case KEY_SELECT:
      return kSbKeySelect;
    case KEY_PRINT:
      return kSbKeyPrint;
    case KEY_INSERT:
      return kSbKeyInsert;
    case KEY_HELP:
      return kSbKeyHelp;
    case KEY_MENU:
      return kSbKeyApps;
    case KEY_FN_F1:
    case KEY_FN_F2:
    case KEY_FN_F3:
    case KEY_FN_F4:
    case KEY_FN_F5:
    case KEY_FN_F6:
    case KEY_FN_F7:
    case KEY_FN_F8:
    case KEY_FN_F9:
    case KEY_FN_F10:
    case KEY_FN_F11:
    case KEY_FN_F12:
      return static_cast<SbKey>(kSbKeyF1 + (code - KEY_FN_F1));

    // For supporting multimedia buttons on a USB keyboard.
    case KEY_BACK:
      return kSbKeyBrowserBack;
    case KEY_FORWARD:
      return kSbKeyBrowserForward;
    case KEY_REFRESH:
      return kSbKeyBrowserRefresh;
    case KEY_STOP:
      return kSbKeyBrowserStop;
    case KEY_SEARCH:
      return kSbKeyBrowserSearch;
    case KEY_FAVORITES:
      return kSbKeyBrowserFavorites;
    case KEY_HOMEPAGE:
      return kSbKeyBrowserHome;
    case KEY_MUTE:
      return kSbKeyVolumeMute;
    case KEY_VOLUMEDOWN:
      return kSbKeyVolumeDown;
    case KEY_VOLUMEUP:
      return kSbKeyVolumeUp;
    case KEY_NEXTSONG:
      return kSbKeyMediaNextTrack;
    case KEY_PREVIOUSSONG:
      return kSbKeyMediaPrevTrack;
    case KEY_STOPCD:
      return kSbKeyMediaStop;
    case KEY_PLAYPAUSE:
      return kSbKeyMediaPlayPause;
    case KEY_MAIL:
      return kSbKeyMediaLaunchMail;
    case KEY_CALC:
      return kSbKeyMediaLaunchApp2;
    case KEY_WLAN:
      return kSbKeyWlan;
    case KEY_POWER:
      return kSbKeyPower;
    case KEY_BRIGHTNESSDOWN:
      return kSbKeyBrightnessDown;
    case KEY_BRIGHTNESSUP:
      return kSbKeyBrightnessUp;

    case KEY_INFO_BUTTON:
      return kSbKeyF1;
  }
  SB_DLOG(WARNING) << "Unknown code: 0x" << std::hex << code;
  return kSbKeyUnknown;
}  // NOLINT(readability/fn_size)

// Get a SbKeyLocation from an input_event.code.
SbKeyLocation KeyCodeToSbKeyLocation(uint16_t code) {
  switch (code) {
    case KEY_LEFTALT:
    case KEY_LEFTCTRL:
    case KEY_LEFTMETA:
    case KEY_LEFTSHIFT:
      return kSbKeyLocationLeft;
    case KEY_RIGHTALT:
    case KEY_RIGHTCTRL:
    case KEY_RIGHTMETA:
    case KEY_RIGHTSHIFT:
      return kSbKeyLocationRight;
  }

  return kSbKeyLocationUnspecified;
}

}  // namespace

IDisplay* GetDisplay() {
  static WPEFramework::Compositor::IDisplay* g_display = nullptr;
  if (!g_display)
    g_display = IDisplay::Instance(DisplayName());

  return g_display;
}

std::string DisplayName() {
  static std::string name = Compositor::IDisplay::SuggestedName();

  if (name.empty()) {
    name = "CobaltBrowser" + std::to_string(SbTimeToPosix(SbTimeGetNow()));
  }
  return (name);
}

KeyboardHandler::KeyboardHandler()
    : key_repeat_interval_(kKeyRepeatTime), key_repeat_delay_(kKeyHoldTime) {}

void KeyboardHandler::Modifiers(uint32_t mods_depressed,
                                uint32_t mods_latched,
                                uint32_t mods_locked,
                                uint32_t group) {
  // Convert to SbKeyModifiers.
  unsigned int modifiers = kSbKeyModifiersNone;

  if (mods_depressed & 1)
    modifiers |= kSbKeyModifiersShift;
  if (mods_depressed & 4)
    modifiers |= kSbKeyModifiersCtrl;
  if (mods_depressed & 8)
    modifiers |= kSbKeyModifiersAlt;

  SB_DLOG(INFO) << "[Key] Modifiers depressed " << mods_depressed
                << ", latched " << mods_latched << ", locked " << mods_locked
                << ", group " << group << ", key modifiers " << modifiers;

  key_modifiers_ = modifiers;
}

void KeyboardHandler::Repeat(int32_t rate, int32_t delay) {
  if (rate == 0) {
    DeleteRepeatKey();
  } else {
    key_repeat_interval_ = std::min(kKeyRepeatTime, static_cast<SbTime>(rate));
    key_repeat_delay_ = std::min(key_repeat_delay_, kKeyHoldTime);
  }
}

void KeyboardHandler::Direct(const uint32_t key, const state action) {
  bool repeatable =
      (key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN);
  SB_DLOG(INFO) << "[Key] Key :" << key << ", state:" << action
                << " repeatable " << repeatable << " key_repeat_key_ "
                << key_repeat_key_ << " key_repeat_state_ "
                << key_repeat_state_;
  if (action && repeatable && key == key_repeat_key_ && key_repeat_state_)
    return;

  if (repeatable) {
    CreateKey(key, action, true);
  } else {
    CreateKey(key, action, false);
  }
}

void KeyboardHandler::CreateKey(int key, state action, bool is_repeat) {
  SbInputData* data = new SbInputData();
  SbMemorySet(data, 0, sizeof(*data));
#if SB_API_VERSION >= 10
  data->timestamp = SbTimeGetMonotonicNow();
#endif  // SB_API_VERSION >= 10
  data->window = window_;
  data->type =
      (action == released ? kSbInputEventTypeUnpress : kSbInputEventTypePress);
  data->device_type = kSbInputDeviceTypeRemote;
  data->device_id = 1;  // kKeyboardDeviceId;
  data->key = KeyCodeToSbKey(key);
  data->key_location = KeyCodeToSbKeyLocation(key);
  data->key_modifiers = key_modifiers_;
  Application::Get()->InjectInputEvent(data);
  ;
  DeleteRepeatKey();

  if (is_repeat && action == pressed) {
    key_repeat_key_ = key;
    key_repeat_state_ = action;
    key_repeat_event_id_ = SbEventSchedule(
        [](void* data) {
          KeyboardHandler* dev_input = reinterpret_cast<KeyboardHandler*>(data);
          dev_input->CreateRepeatKey();
        },
        this, key_repeat_interval_);
  } else {
    key_repeat_interval_ = std::min(key_repeat_delay_, kKeyHoldTime);
  }
}

void KeyboardHandler::CreateRepeatKey() {
  if (!key_repeat_state_) {
    return;
  }

  key_repeat_interval_ = std::min(kKeyRepeatTime, key_repeat_interval_);

  CreateKey(key_repeat_key_, key_repeat_state_, true);
}

void KeyboardHandler::DeleteRepeatKey() {
  key_repeat_state_ = released;
  if (key_repeat_event_id_ != kSbEventIdInvalid) {
    SbEventCancel(key_repeat_event_id_);
    key_repeat_event_id_ = kSbEventIdInvalid;
  }
}

void KeyboardHandler::SetWindow(SbWindow window) {
  window_ = window;
  static_cast<SbWindowPrivate*>(window_)->window_->Keyboard(this);
}

}  // namespace window
}  // namespace shared
}  // namespace wpe
}  // namespace starboard
}  // namespace third_party

SbWindowPrivate::SbWindowPrivate(const SbWindowOptions* options) {
  auto window_width =
      options && options->size.width > 0
          ? options->size.width
          : third_party::starboard::wpe::shared::window::kDefaultWidth;
  auto window_height =
      options && options->size.height > 0
          ? options->size.height
          : third_party::starboard::wpe::shared::window::kDefaultHeight;
  auto* env_width  = std::getenv("COBALT_RESOLUTION_WIDTH");
  if (env_width) {
    window_width = atoi(env_width);
  }

  auto* env_height  = std::getenv("COBALT_RESOLUTION_HEIGHT");
  if (env_height) {
    window_height = atoi(env_height);
  }

#if defined(SB_NEEDS_VIDEO_OVERLAY_SURFACE)
  // The sufraces are stacked in order they are
  // created with by default so make sure video is under gfx by creating
  // it first.
  video_overlay_ =
      third_party::starboard::wpe::shared::window::GetDisplay()->Create(
          third_party::starboard::wpe::shared::window::DisplayName() + "-"
              + std::string("video"), window_width, window_height);
#endif

  window_ = third_party::starboard::wpe::shared::window::GetDisplay()->Create(
      third_party::starboard::wpe::shared::window::DisplayName() + "-"
          + std::string("graphics"), window_width, window_height);
  kb_handler_.SetWindow(this);
}

SbWindowPrivate::~SbWindowPrivate() {
  if (window_) {
    window_->Release();
  }
}

WPEFramework::Compositor::IDisplay::ISurface*
SbWindowPrivate::CreateVideoOverlay() {
  return video_overlay_;
}

void SbWindowPrivate::DestroyVideoOverlay(
    WPEFramework::Compositor::IDisplay::ISurface* surface) {}
