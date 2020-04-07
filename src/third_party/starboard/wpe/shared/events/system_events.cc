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

#include "system_events.h"
#include <unistd.h>

namespace third_party {
namespace starboard {
namespace wpe {
namespace shared {
  SystemEvents::SystemEvents() {
    pipe2(wake_pipe_ends_, O_NONBLOCK | O_CLOEXEC);
  }

  SystemEvents& SystemEvents::Get() {
    static SystemEvents instance;

    return instance;
  }

  void SystemEvents::AddEventSource(int event_fd) {
    std::unique_lock<std::mutex> lk(lock_);
    observed_fds_.push_back(event_fd);
    polled_fds_need_update_ = true;
  }

  void SystemEvents::WakeEventWait() {
    // Write a dummy message to pipe, just to wake up polling
    char message = 1;
    write(wake_pipe_ends_[1], reinterpret_cast<void*>(&message), sizeof(char));
  }

  void SystemEvents::WaitForEvent(SbTime max_time) { 
    { 
      std::unique_lock<std::mutex> lk(lock_);

      if (polled_fds_need_update_) {
        polled_fds_.clear();

        pollfd poll_desc;
        poll_desc.fd = wake_pipe_ends_[0];
        poll_desc.events = POLLIN;
        polled_fds_.push_back(poll_desc);

        for (int fd : observed_fds_) {
          poll_desc.fd = fd;
          polled_fds_.push_back(poll_desc);
        }

        polled_fds_need_update_ = false;
      }
    }

    int time_to_wait = ToMiliseconds(max_time);
    int result = poll(reinterpret_cast<pollfd*>(polled_fds_.data()), polled_fds_.size(), time_to_wait);
    
    if (result > 0) {
      // Clear wakeup pipe
      char buffer[1];
      for (; read(wake_pipe_ends_[0], buffer, sizeof(buffer)) > 0;) {};
    }
  }

  int SystemEvents::ToMiliseconds(SbTime time) {
    auto time_chrono = std::chrono::microseconds(time);
 
    return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(time_chrono).count());
  }
}  // namespace shared
}  // namespace wpe
}  // namespace starboard
}  // namespace third_party
