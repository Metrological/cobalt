// Copyright 2022 The Cobalt Authors. All Rights Reserved.
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

#ifndef COBALT_WATCHDOG_WATCHDOG_H_
#define COBALT_WATCHDOG_WATCHDOG_H_

#include <memory>
#include <queue>
#include <string>
#include <unordered_map>

#include "cobalt/base/application_state.h"
#include "cobalt/watchdog/singleton.h"
#include "starboard/mutex.h"
#include "starboard/thread.h"
#include "starboard/time.h"

namespace cobalt {
namespace watchdog {

// Client to monitor
typedef struct Client {
  std::string name;
  std::string description;
  // List of strings optionally provided with each Ping.
  std::queue<std::string> ping_infos;
  // Application state to continue monitoring client up to.
  base::ApplicationState monitor_state;
  int64_t time_interval_microseconds;
  int64_t time_wait_microseconds;
  int64_t time_registered_microseconds;                    // since epoch
  SbTimeMonotonic time_registered_monotonic_microseconds;  // since (relative)
  SbTimeMonotonic time_last_pinged_microseconds;           // since (relative)
} Client;

// Watchdog violation
typedef struct Violation {
  std::string name;
  std::string description;
  // List of strings optionally provided with each Ping.
  std::queue<std::string> ping_infos;
  // Application state to continue monitoring client up to.
  base::ApplicationState monitor_state;
  int64_t time_interval_microseconds;
  int64_t time_wait_microseconds;
  int64_t time_registered_microseconds;  // since epoch
  int64_t violation_time_microseconds;   // since epoch
  int64_t violation_delta_microseconds;  // over time_interval
  int64_t violation_count;
  // Watchdog index as a serialized json string
  std::string serialized_watchdog_index;

  void operator=(const Client& c) {
    name = c.name;
    description = c.description;
    ping_infos = c.ping_infos;
    monitor_state = c.monitor_state;
    time_interval_microseconds = c.time_wait_microseconds;
    time_wait_microseconds = c.time_wait_microseconds;
    time_registered_microseconds = c.time_registered_microseconds;
  }
} Violation;

// Register behavior with previously registered clients of the same name.
enum Replace {
  NONE = 0,
  PING = 1,
  ALL = 2,
};

class Watchdog : public Singleton<Watchdog> {
 public:
  bool Initialize();
  bool InitializeStub();
  void Uninitialize();
  void UpdateState(base::ApplicationState state);
  bool Register(std::string name, base::ApplicationState monitor_state,
                int64_t time_interval, int64_t time_wait = 0,
                Replace replace = NONE);
  bool Register(std::string name, std::string description,
                base::ApplicationState monitor_state, int64_t time_interval,
                int64_t time_wait = 0, Replace replace = NONE);
  bool Unregister(const std::string& name, bool lock = true);
  bool Ping(const std::string& name);
  bool Ping(const std::string& name, const std::string& info);
  std::string GetWatchdogViolations(bool current = false);

 private:
  std::string GetWatchdogFilePaths(bool current);
  void PreservePreviousWatchdogViolations();
  static void* Monitor(void* context);
  std::string GetSerializedWatchdogIndex();
  static void SerializeWatchdogViolations(void* context);

  // Current Watchdog violations file path.
  std::string watchdog_file_;
  // Previous Watchdog violations file path.
  std::string watchdog_old_file_;
  // Creates a lock which ensures that each loop of monitor is atomic in that
  // modifications to is_monitoring_, state_, smallest_time_interval_, and most
  // importantly to the dictionaries containing Watchdog clients,
  // watchdog_index_ and watchdog_violations_, only occur in between loops of
  // monitor. API functions like Register(), Unregister(), Ping(), and
  // GetWatchdogViolations() will be called by various threads and interact
  // with these class variables.
  SbMutex mutex_;
  // Time interval between monitor loops.
  int64_t smallest_time_interval_;
  // Monitor thread.
  SbThread watchdog_thread_;
  // Tracks application state.
  base::ApplicationState state_ = base::kApplicationStateStarted;
  // Dictionary of registered Watchdog clients.
  std::unordered_map<std::string, std::unique_ptr<Client>> watchdog_index_;
  // Dictionary of Watchdog violations.
  std::unordered_map<std::string, std::unique_ptr<Violation>>
      watchdog_violations_;
  // Flag to stub out Watchdog.
  bool is_stub_ = false;
  // Flag to stop monitor thread.
  bool is_monitoring_;
};

}  // namespace watchdog
}  // namespace cobalt

#endif  // COBALT_WATCHDOG_WATCHDOG_H_
