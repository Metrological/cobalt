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

std::mutex Application::g_lock_;
std::condition_variable Application::g_finished_init_;

Application::Application() {}

Application::~Application() {}

void Application::Initialize() {
  SbAudioSinkPrivate::Initialize();

  g_finished_init_.notify_all();
}

void Application::Teardown() {
  SbAudioSinkPrivate::TearDown();
}

bool Application::MayHaveSystemEvents() {
  return true;
}

::starboard::shared::starboard::Application::Event*
Application::PollNextSystemEvent() {
  {
    ::starboard::ScopedLock lock(window_lock_);
    if (window_) {
      window_->PollNextSystemEvent();
    }
  }
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
  {
    ::starboard::ScopedLock lock(window_lock_);
    window_ = window;
  }
  return window;
}

bool Application::DestroyWindow(SbWindow window) {
  {
    ::starboard::ScopedLock lock(window_lock_);
    SB_DCHECK(window_ == window);
    delete window;
    window_ = kSbWindowInvalid;
  }
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

void Application::DeepLink(const char* link_data) {
  if (link_data != nullptr) {
    ::starboard::shared::starboard::Application::State state = ::starboard::shared::starboard::Application::state();
    // Only fire the link event when it is started, other states will fire during suspended -> resume transition.
    if (state == ::starboard::shared::starboard::Application::State::kStateStarted) {
        ::starboard::shared::starboard::Application::Link(link_data);
    }
    deep_link_ = std::string(link_data);
  }
}

void Application::Suspend()
{
  suspend_lock_.lock();
  ::starboard::shared::starboard::Application::Blur(this, nullptr);
  ::starboard::shared::starboard::Application::Freeze(this,
    [](void* application) {
      reinterpret_cast<Application*>(application)->suspend_lock_.unlock();
    });
}

void Application::OnSuspend()
{
  ::starboard::ScopedLock lock(window_lock_);
  if (window_) {
    window_->DestroyDisplay();
    display_released_ = true;
  }
}

void Application::Resume()
{
  suspend_lock_.lock();

  ::starboard::shared::starboard::Application::Focus(this,
    [](void* application) {
      // Send OnDeepLink event
      auto deep_link =  reinterpret_cast<Application*>(application)->deep_link_;
      if (!deep_link.empty())
        ::starboard::shared::starboard::Application::Get()->Link(deep_link.c_str());
    });

  ::starboard::shared::starboard::Application::Reveal(this,
    [](void* application) {
      reinterpret_cast<Application*>(application)->suspend_lock_.unlock();
    });
}

void Application::OnResume()
{
  ::starboard::ScopedLock lock(window_lock_);
  if (window_ && display_released_) {
    window_->CreateDisplay();
    display_released_ = false;
  }
}

void Application::WaitForInit() {
  std::unique_lock<std::mutex> lk(g_lock_);

  g_finished_init_.wait(lk, [&]{return Get() != nullptr;});
}

void Application::Inject(Event* e) {
  QueueApplication::Inject(e);
}

void Application::Stop() {
  SbSystemRequestStop(0);
}

}  // namespace shared
}  // namespace wpe
}  // namespace starboard
}  // namespace third_party
