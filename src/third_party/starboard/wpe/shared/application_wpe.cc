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

#include "third_party/starboard/wpe/shared/application_wpe.h"

#include <fcntl.h>

#include "starboard/common/log.h"
#include "starboard/event.h"
#include "starboard/shared/starboard/audio_sink/audio_sink_internal.h"

#include "third_party/starboard/wpe/shared/window/window_internal.h"

namespace third_party {
namespace starboard {
namespace wpe {
namespace shared {

Application::Application() {}

Application::~Application() {}

void Application::Initialize() {
  SbAudioSinkPrivate::Initialize();
}

void Application::Teardown() {
  SbAudioSinkPrivate::TearDown();
}

bool Application::MayHaveSystemEvents() {
  return true;
}

// This is a bit of the shortcut. We could implement PollNextSystemEvent(),
// WaitForSystemEventWithTimeout() and WakeSystemEventWait() properly
// but instead PollNextSystemEvent() injects a new event which will be processed
// just after PollNextSystemEvent() if it returns nullptr
::starboard::shared::starboard::Application::Event*
Application::PollNextSystemEvent() {
  auto* display = window::GetDisplay();
  int fd = display->FileDescriptor();
  int flags = fcntl(fd, F_GETFL, 0);
  fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  display->Process(1);
  return NULL;
}

::starboard::shared::starboard::Application::Event*
Application::WaitForSystemEventWithTimeout(SbTime time) {
  SB_UNREFERENCED_PARAMETER(time);
  return NULL;
}

void Application::WakeSystemEventWait() {}

SbWindow Application::CreateWindow(const SbWindowOptions* options) {
  SbWindow window = new SbWindowPrivate(options);
  return window;
}

bool Application::DestroyWindow(SbWindow window) {
  delete window;
  return true;
}

void Application::InjectInputEvent(SbInputData* data) {
  Inject(new Event(kSbEventTypeInput, data,
                   &Application::DeleteDestructor<SbInputData>));
}

void Application::Inject(Event* e) {
  if (e->event->type == kSbEventTypeSuspend) {
    e->destructor = nullptr;
  }

  QueueApplication::Inject(e);
}

}  // namespace shared
}  // namespace wpe
}  // namespace starboard
}  // namespace third_party
