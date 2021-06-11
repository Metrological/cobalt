// Copyright 2021 The Cobalt Authors. All Rights Reserved.
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

#include "cobalt/dom/performance_resource_timing.h"
#include "cobalt/dom/performance.h"

namespace cobalt {
namespace dom {

namespace {
  const std::string kPerformanceResourceTimingCacheMode = "local";
}

PerformanceResourceTiming::PerformanceResourceTiming(
    const std::string& name, DOMHighResTimeStamp start_time,
    DOMHighResTimeStamp end_time)
    : PerformanceEntry(name, start_time, end_time), transfer_size_(0) {}

PerformanceResourceTiming::PerformanceResourceTiming(
    const net::LoadTimingInfo& timing_info, const std::string& initiator_type,
    const std::string& requested_url, Performance* performance)
    : PerformanceEntry(
          requested_url, performance->Now(),
          ConvertTimeDeltaToDOMHighResTimeStamp(
              timing_info.receive_headers_end.since_origin(),
              Performance::kPerformanceTimerMinResolutionInMicroseconds)),
      initiator_type_(initiator_type),
      cache_mode_(kPerformanceResourceTimingCacheMode),
      transfer_size_(0),
      timing_info_(timing_info) {}

std::string PerformanceResourceTiming::initiator_type() const {
  return initiator_type_;
}

DOMHighResTimeStamp PerformanceResourceTiming::fetch_start() const {
  // There is no worker in Cobalt, thus we need to return start_time()
  // instead of worker ready time.
  return PerformanceEntry::start_time();
}

DOMHighResTimeStamp PerformanceResourceTiming::connect_start() const {
  if (timing_info_.connect_timing.connect_start.is_null()) {
    return PerformanceEntry::start_time();
  }
  return ConvertTimeDeltaToDOMHighResTimeStamp(
      timing_info_.connect_timing.connect_start.since_origin(),
      Performance::kPerformanceTimerMinResolutionInMicroseconds);
}

DOMHighResTimeStamp PerformanceResourceTiming::connect_end() const {
  if (timing_info_.connect_timing.connect_end.is_null()) {
    return PerformanceEntry::start_time();
  }
  return ConvertTimeDeltaToDOMHighResTimeStamp(
      timing_info_.connect_timing.connect_end.since_origin(),
      Performance::kPerformanceTimerMinResolutionInMicroseconds);
}

DOMHighResTimeStamp PerformanceResourceTiming::secure_connection_start() const {
  if (timing_info_.connect_timing.ssl_start.is_null()) {
    return 0.0;
  }
  return ConvertTimeDeltaToDOMHighResTimeStamp(
      timing_info_.connect_timing.ssl_start.since_origin(),
      Performance::kPerformanceTimerMinResolutionInMicroseconds);
}

DOMHighResTimeStamp PerformanceResourceTiming::request_start() const {
  if (timing_info_.send_start.is_null()) {
    return PerformanceEntry::start_time();
  }
  return ConvertTimeDeltaToDOMHighResTimeStamp(
      timing_info_.send_start.since_origin(),
      Performance::kPerformanceTimerMinResolutionInMicroseconds);
}

DOMHighResTimeStamp PerformanceResourceTiming::response_start() const {
  if (timing_info_.receive_headers_end.is_null()) {
    PerformanceEntry::start_time();
  }
  return ConvertTimeDeltaToDOMHighResTimeStamp(
      timing_info_.receive_headers_end.since_origin(),
      Performance::kPerformanceTimerMinResolutionInMicroseconds);
}

DOMHighResTimeStamp PerformanceResourceTiming::response_end() const {
  return response_start();
}

unsigned long long PerformanceResourceTiming::transfer_size() const {
  return transfer_size_;
}

void PerformanceResourceTiming::SetResourceTimingEntry(
    const net::LoadTimingInfo& timing_info, const std::string& initiator_type,
    const std::string& requested_url, const std::string& cache_mode) {
  // To setup the resource timing entry for PerformanceResourceTiming entry
  // given DOMString initiatorType, DOMString requestedURL, fetch timing info
  // timingInfo, and a DOMString cacheMode, perform the following steps:
  //   https://www.w3.org/TR/2021/WD-resource-timing-2-20210414/#dfn-setup-the-resource-timing-entry
  // 1. Assert that cacheMode is the empty string or "local".
  DCHECK(cache_mode.empty() || cache_mode == "local");
  // 2. Set entry's initiator type to initiatorType.
  initiator_type_ = initiator_type;
  // 3. Set entry's requested URL to requestedURL.
  requested_url_ = requested_url;
  // 4. Set entry's timing info to timingInfo.
  timing_info_ = timing_info;
  // 5. Set entry's cache mode to cacheMode.
  cache_mode_ = cache_mode;
}

}  // namespace dom
}  // namespace cobalt
