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
#include "starboard/string.h"
#include "third_party/starboard/wpe/shared/window/window_internal.h"
#include "third_party/starboard/wpe/shared/events/system_events.h"

namespace third_party {
namespace starboard {
namespace wpe {
namespace shared {

std::mutex Application::g_lock;
std::condition_variable Application::g_finished_init;

Application::Application() {}

Application::~Application() {}

void Application::Initialize() {
  SbAudioSinkPrivate::Initialize();

  g_finished_init.notify_all();
}

void Application::Teardown() {
  SbAudioSinkPrivate::TearDown();
}

bool Application::MayHaveSystemEvents() {
  return true;
}

::starboard::shared::starboard::Application::Event*
Application::PollNextSystemEvent() {
  ::starboard::ScopedLock lock(mutex_);
  window_->PollNextSystemEvent();
  return NULL;
}

::starboard::shared::starboard::Application::Event*
Application::WaitForSystemEventWithTimeout(SbTime time) {
  SystemEvents::Get().WaitForEvent(time);

  return PollNextSystemEvent();
}

void Application::WakeSystemEventWait() {
  SystemEvents::Get().WakeEventWait();
}

SbWindow Application::CreateWindow(const SbWindowOptions* options) {
  SbWindow window = new SbWindowPrivate(options);
  window_ = window;

  return window;
}

bool Application::DestroyWindow(SbWindow window) {
  SB_DCHECK(window_ == window);
  delete window;
  window_ = kSbWindowInvalid;
  return true;
}

void Application::InjectInputEvent(SbInputData* data) {
  Inject(new Event(kSbEventTypeInput, data,
                   &Application::DeleteDestructor<SbInputData>));
}

void Application::NavigateTo(const char* url) {
  Inject(new Event(kSbEventTypeNavigate, SbStringDuplicate(url),
                  SbMemoryDeallocate));
}

void Application::Suspend()
{
  suspend_lock.lock();
  ::starboard::shared::starboard::Application::Suspend(this, 
    [](void* application) {
      reinterpret_cast<Application*>(application)->suspend_lock.unlock();
    });

  if (window_) {
    ::starboard::ScopedLock lock(mutex_);
    window_->DestroyDisplay();
  }
}

void Application::Resume()
{
  suspend_lock.lock();
  ::starboard::shared::starboard::Application::Resume(this, 
    [](void* application) {
      reinterpret_cast<Application*>(application)->suspend_lock.unlock();
    });
  if (window_) {
    ::starboard::ScopedLock lock(mutex_);
    window_->CreateDisplay();
  }

}

void Application::WaitForInit() {
  std::unique_lock<std::mutex> lk(g_lock);

  g_finished_init.wait(lk, [&]{return Get() != nullptr;});
}

void Application::Inject(Event* e) {
  QueueApplication::Inject(e);
}

}  // namespace shared
}  // namespace wpe
}  // namespace starboard
}  // namespace third_party
