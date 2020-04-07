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

#ifndef THIRD_PARTY_STARBOARD_WPE_SHARED_EVENTS_SYSTEM_EVENTS_H_
#define THIRD_PARTY_STARBOARD_WPE_SHARED_EVENTS_SYSTEM_EVENTS_H_

#include "starboard/configuration.h"
#include "starboard/input.h"
#include "starboard/shared/internal_only.h"
#include "starboard/types.h"
#include <mutex>              
#include <fcntl.h> 
#include <vector>
#include <poll.h>

namespace third_party {
namespace starboard {
namespace wpe {
namespace shared {
  class SystemEvents
  {
  public:
    static SystemEvents& Get();

    void AddEventSource(int event_fd);
    void WakeEventWait();
    void WaitForEvent(SbTime max_time);
  private:   
    SystemEvents();

    int ToMiliseconds(SbTime time);
    
    int wake_pipe_ends_[2];
    std::vector<int> observed_fds_;
    std::vector<pollfd> polled_fds_;
    bool polled_fds_need_update_;
    std::mutex lock_;
  };

}  // namespace shared
}  // namespace wpe
}  // namespace starboard
}  // namespace third_party

#endif  // THIRD_PARTY_STARBOARD_WPE_SHARED_EVENTS_SYSTEM_EVENTS_H_
