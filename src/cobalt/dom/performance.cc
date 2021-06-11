// Copyright 2015 The Cobalt Authors. All Rights Reserved.
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

#include "cobalt/dom/performance.h"

#include "base/time/time.h"
#include "cobalt/browser/stack_size_constants.h"
#include "cobalt/dom/memory_info.h"
#include "cobalt/dom/performance_entry.h"

namespace cobalt {
namespace dom {

Performance::Performance(script::EnvironmentSettings* settings,
                         const scoped_refptr<base::BasicClock>& clock)
    : EventTarget(settings),
      timing_(new PerformanceTiming(clock)),
      memory_(new MemoryInfo()),
      time_origin_(base::Time::Now() - base::Time::UnixEpoch()),
      resource_timing_buffer_size_limit_(
          Performance::kMaxResourceTimingBufferSize),
      resource_timing_buffer_current_size_(0),
      resource_timing_buffer_full_event_pending_flag_(false),
      resource_timing_secondary_buffer_current_size_(0),
      performance_observer_task_queued_flag_(false),
      add_to_performance_entry_buffer_flag_(false) {}

DOMHighResTimeStamp Performance::Now() const {
  base::TimeDelta now = base::Time::Now() - base::Time::UnixEpoch();
  return ConvertTimeDeltaToDOMHighResTimeStamp(
      now - time_origin_,
      Performance::kPerformanceTimerMinResolutionInMicroseconds);
}

scoped_refptr<PerformanceTiming> Performance::timing() const { return timing_; }

scoped_refptr<MemoryInfo> Performance::memory() const { return memory_; }

DOMHighResTimeStamp Performance::time_origin() const {
  // The algorithm for calculating time origin timestamp.
  //   https://www.w3.org/TR/2019/REC-hr-time-2-20191121/#dfn-time-origin-timestamp
  // Assert that global's time origin is not undefined.
  DCHECK(!time_origin_.is_zero());

  // Let t1 be the DOMHighResTimeStamp representing the high resolution
  // time at which the global monotonic clock is zero.
  base::TimeDelta t1 = base::Time::UnixEpoch().ToDeltaSinceWindowsEpoch();

  // Let t2 be the DOMHighResTimeStamp representing the high resolution
  // time value of the global monotonic clock at global's time origin.
  base::TimeDelta t2 = time_origin_;

  // Return the sum of t1 and t2.
  return ConvertTimeDeltaToDOMHighResTimeStamp(
      t1 + t2, Performance::kPerformanceTimerMinResolutionInMicroseconds);
}

void Performance::UnregisterPerformanceObserver(
    const scoped_refptr<PerformanceObserver>& old_observer) {
  auto iter = registered_performance_observers_.begin();
  while (iter != registered_performance_observers_.end()) {
    if (iter->observer == old_observer) {
      iter = registered_performance_observers_.erase(iter);
    } else {
      ++iter;
    }
  }
}

void Performance::RegisterPerformanceObserver(
    const scoped_refptr<PerformanceObserver>& observer,
    const PerformanceObserverInit& options) {
  std::list<PerformanceObserverInit> options_list;
  options_list.push_back(options);
  registered_performance_observers_.emplace_back(observer,
                                                 options_list);
}

void Performance::ReplaceRegisteredPerformanceObserverOptionsList(
    const scoped_refptr<PerformanceObserver>& observer,
    const PerformanceObserverInit& options) {
  auto iter = registered_performance_observers_.begin();
  while (iter != registered_performance_observers_.end()) {
    if (iter->observer == observer) {
      iter->options_list.clear();
      iter->options_list.push_back(options);
    }
    ++iter;
  }
}

void Performance::UpdateRegisteredPerformanceObserverOptionsList(
    const scoped_refptr<PerformanceObserver>& observer,
    const PerformanceObserverInit& options) {
  auto iter = registered_performance_observers_.begin();
  while (iter != registered_performance_observers_.end()) {
    if (iter->observer == observer) {
      bool is_replaced = false;
      for (auto& registered_options : iter->options_list) {
        if (registered_options.type() == options.type()) {
          registered_options = options;
          is_replaced = true;
        }
      }
      if (!is_replaced) iter->options_list.push_back(options);
    }
    ++iter;
  }
}

void Performance::TraceMembers(script::Tracer* tracer) {
  tracer->Trace(timing_);
  tracer->Trace(memory_);
}

PerformanceEntryList Performance::GetEntries() {
  return PerformanceEntryListImpl::GetEntries(performance_entry_buffer_);
}

PerformanceEntryList Performance::GetEntriesByType(
    const std::string& entry_type) {
  return PerformanceEntryListImpl::GetEntriesByType(performance_entry_buffer_,
                                                      entry_type);
}

PerformanceEntryList Performance::GetEntriesByName(
    const std::string& name, const base::StringPiece& type) {
  return PerformanceEntryListImpl::GetEntriesByName(performance_entry_buffer_,
                                                      name, type);
}

void Performance::ClearResourceTimings() {
  // The method clearResourceTimings runs the following steps:
  //   https://www.w3.org/TR/2021/WD-resource-timing-2-20210414/#dom-performance-clearresourcetimings
  // 1. Remove all PerformanceResourceTiming objects in the performance
  // entry buffer.
  PerformanceEntryList performance_entry_buffer;
  for (const auto& entry : performance_entry_buffer_) {
    if (!base::polymorphic_downcast<PerformanceResourceTiming*>(entry.get())) {
      performance_entry_buffer.push_back(entry);
    }
  }
  performance_entry_buffer_.swap(performance_entry_buffer);

  // 2. Set resource timing buffer current size to 0.
  resource_timing_buffer_current_size_ = 0;
}

void Performance::SetResourceTimingBufferSize(unsigned long max_size) {
  // The method runs the following steps:
  //   https://www.w3.org/TR/2021/WD-resource-timing-2-20210414/#dom-performance-setresourcetimingbuffersize
  // 1. Set resource timing buffer size limit to the maxSize parameter.
  // If the maxSize parameter is less than resource timing buffer current
  // size, no PerformanceResourceTiming objects are to be removed from
  // the performance entry buffer.
  resource_timing_buffer_size_limit_ = max_size;
}

bool Performance::CanAddResourceTimingEntry() {
  // THe method runs the following steps:
  //   https://www.w3.org/TR/2021/WD-resource-timing-2-20210414/#dfn-can-add-resource-timing-entry
  // 1. If resource timing buffer current size is smaller than resource
  // timing buffer size limit, return true.
  // 2. Return false.
  return resource_timing_buffer_current_size_ <
         resource_timing_buffer_size_limit_;
}

void Performance::CopySecondaryBuffer() {
  //   https://www.w3.org/TR/2021/WD-resource-timing-2-20210414/#dfn-copy-secondary-buffer
  // While resource timing secondary buffer is not empty and can add
  // resource timing entry returns true, run the following substeps:
  PerformanceEntryList entry_list;
  while (!resource_timing_secondary_buffer_.empty() &&
         CanAddResourceTimingEntry()) {
    // 1. Let entry be the oldest PerformanceResourceTiming in resource timing
    // secondary buffer.
    scoped_refptr<PerformanceResourceTiming> entry =
        resource_timing_secondary_buffer_.front();
    // 2. Add entry to the end of performance entry buffer.
    DCHECK(entry);
    performance_entry_buffer_.push_back(entry);
    // 3. Increment resource timing buffer current size by 1.
    resource_timing_buffer_current_size_++;
    // 4. Remove entry from resource timing secondary buffer.
    resource_timing_secondary_buffer_.pop_front();
    // 5. Decrement resource timing secondary buffer current size by 1.
    resource_timing_secondary_buffer_current_size_--;
  }
}

void Performance::set_onresourcetimingbufferfull(
    const EventTarget::EventListenerScriptValue& event_listener) {
  SetAttributeEventListener(base::Tokens::resourcetimingbufferfull(),
                            event_listener);
}

const EventTarget::EventListenerScriptValue*
Performance::onresourcetimingbufferfull() const {
  return GetAttributeEventListener(base::Tokens::resourcetimingbufferfull());
}

void Performance::FireResourceTimingBufferFullEvent() {
  //   https://www.w3.org/TR/2021/WD-resource-timing-2-20210414/#dfn-fire-a-buffer-full-event
  // 1. While resource timing secondary buffer is not empty, run the
  // following substeps:
  while (!resource_timing_secondary_buffer_.empty()) {
    // 1.1 Let number of excess entries before be resource timing secondary
    // buffer current size.
    unsigned long excess_entries_before =
        resource_timing_secondary_buffer_current_size_;
    // 1.2 If can add resource timing entry returns false, then fire an event
    // named resourcetimingbufferfull at the Performance object.
    if (!CanAddResourceTimingEntry()) {
      DispatchEvent(new Event(base::Tokens::resourcetimingbufferfull()));
    }
    // 1.3 Run copy secondary buffer.
    CopySecondaryBuffer();
    // 1.4 Let number of excess entries after be resource timing secondary
    // buffer current size.
    unsigned long excess_entries_after =
        resource_timing_secondary_buffer_current_size_;
    // 1.5 If number of excess entries before is lower than or equals number of
    // excess entries after, then remove all entries from resource timing
    // secondary buffer, set resource timing secondary buffer current size to 0,
    // and abort these steps.
    if (excess_entries_before <= excess_entries_after) {
      resource_timing_secondary_buffer_.clear();
      resource_timing_secondary_buffer_current_size_ = 0;
      break;
    }
  }

  // 2. Set resource timing buffer full event pending flag to false.
  resource_timing_buffer_full_event_pending_flag_ = false;
}

void Performance::AddPerformanceResourceTimingEntry(
    const scoped_refptr<PerformanceResourceTiming>& resource_timing_entry) {
  //   https://www.w3.org/TR/2021/WD-resource-timing-2-20210414/#dfn-add-a-performanceresourcetiming-entry
  // To add a PerformanceResourceTiming entry into the performance entry buffer,
  // run the following steps:
  // 1. Let new entry be the input PerformanceEntry to be added.
  // 2. If can add resource timing entry returns true and resource timing buffer
  // full event pending flag is false, run the following substeps:
  if (CanAddResourceTimingEntry() &&
      !resource_timing_buffer_full_event_pending_flag_) {
    // 2.1 Add new entry to the performance entry buffer.
    performance_entry_buffer_.push_back(resource_timing_entry);
    // 2.2 Increase resource timing buffer current size by 1.
    resource_timing_buffer_current_size_++;
    // 2.3 Return.
    return;
  }

  // 3. If resource timing buffer full event pending flag is false, run the
  // following substeps:
  if (!resource_timing_buffer_full_event_pending_flag_) {
    // 3.1 Set resource timing buffer full event pending flag to true.
    resource_timing_buffer_full_event_pending_flag_ = true;
    // 3.2 Queue a task on the performance timeline task source to run fire
    // a buffer full event.
    FireResourceTimingBufferFullEvent();
  }
  // 4. Add new entry to the resource timing secondary buffer.
  resource_timing_secondary_buffer_.push_back(resource_timing_entry);
  // 5. Increase resource timing secondary buffer current size by 1.
  resource_timing_secondary_buffer_current_size_++;
  DCHECK_EQ(resource_timing_secondary_buffer_current_size_,
            resource_timing_secondary_buffer_.size());
}

void Performance::QueuePerformanceEntry(
    const scoped_refptr<PerformanceEntry>& entry) {
  // To queue a PerformanceEntry (new entry) with an optional add to
  // performance entry buffer flag, which is unset by default, run these steps:
  //   https://www.w3.org/TR/2019/WD-performance-timeline-2-20191024/#queue-a-performanceentry
  // 1. Let interested observers be an initially empty set of
  // PerformanceObserver objects.
  std::list<scoped_refptr<PerformanceObserver>> interested_observers;
  // 2. Let entryType be new entry’s entryType value.
  const std::string entry_type = entry->entry_type();
  // 3. For each registered performance observer (regObs):
  for (const auto& reg_obs : registered_performance_observers_) {
    // 3.1 If regObs's options list contains a PerformanceObserverInit item
    // whose entryTypes member include entryType or whose type member equals to
    // entryType, append regObs's observer to interested observers.
    for (const auto& item : reg_obs.options_list) {
      if (item.has_type() && item.type() == entry_type) {
        interested_observers.push_back(reg_obs.observer);
      }
      if (item.has_entry_types()) {
        for (const auto& type : item.entry_types()) {
          if (type == entry_type) {
            interested_observers.push_back(reg_obs.observer);
          }
        }
      }
    }
  }
  // 4. For each observer in interested observers:
  for (const auto& observer : interested_observers) {
    // 4.1 Append new entry to observer's observer buffer.
    observer->EnqueuePerformanceEntry(entry);
  }
  // 5. If the add to performance entry buffer flag is set, add new entry to the
  // performance entry buffer.
  if (add_to_performance_entry_buffer_flag_) {
    performance_entry_buffer_.push_back(entry);
  }
  // 6. If the performance observer task queued flag is set, terminate these
  // steps.
  if (performance_observer_task_queued_flag_) return;
  // 7. Set performance observer task queued flag.
  performance_observer_task_queued_flag_ = true;
  // 8. Queue a task that consists of running the following substeps.
  // The task source for the queued task is the performance timeline task
  // source.
  QueuePerformanceTimelineTask();
}

void Performance::QueuePerformanceTimelineTask() {
  // 8.1 Unset performance observer task queued flag for the relevant global
  // object.
  performance_observer_task_queued_flag_ = false;
  // 8.2 Let notify list be a copy of relevant global object's list of
  // registered performance observer objects.
  RegisteredPerformanceObserverList notify_list =
      registered_performance_observers_;
  // 8.3 For each PerformanceObserver object po in notify list, run these steps:
  for (const auto& reg_obs : notify_list) {
    // 8.3.1 Let entries be a copy of po’s observer buffer.
    scoped_refptr<PerformanceObserver> po = reg_obs.observer;
    PerformanceEntryList entries = po->GetObserverBuffer();
    // 8.3.2 Empty po’s observer buffer.
    po->EmptyObserverBuffer();
    // If entries is non-empty, call po’s callback with entries as first
    // argument and po as the second argument and callback this value. If this
    // throws an exception, report the exception.
    if (!entries.empty()) {
      scoped_refptr<PerformanceObserverEntryList> observer_entry_list(
          new PerformanceObserverEntryList(entries));
      po->GetPerformanceObserverCallback()->RunCallback(observer_entry_list,
                                                        po);
    }
  }
}

void Performance::CreatePerformanceResourceTiming(
    const net::LoadTimingInfo& timing_info, const std::string& initiator_type,
    const std::string& requested_url) {
  // To mark resource timing given a fetch timing info timingInfo, a DOMString
  // requestedURL, a DOMString initiatorType a global object global, and a
  // string cacheMode, perform the following steps:
  //   https://www.w3.org/TR/2021/WD-resource-timing-2-20210414/#marking-resource-timing
  // 1. Create a PerformanceResourceTiming object entry in global's realm.
  // 2.Setup the resource timing entry for entry, given initiatorType,
  // requestedURL, timingInfo, and cacheMode.
  scoped_refptr<PerformanceResourceTiming> resource_timing(
      new PerformanceResourceTiming(timing_info, initiator_type,
                                    requested_url, this));
  // 2. Queue entry.
  QueuePerformanceEntry(resource_timing);
  // 3. Add entry to global's performance entry buffer.
  AddPerformanceResourceTimingEntry(resource_timing);
}

}  // namespace dom
}  // namespace cobalt
