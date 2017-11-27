// Copyright 2017 Google Inc. All Rights Reserved.
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

#ifndef STARBOARD_SHARED_STARBOARD_PLAYER_JOB_QUEUE_H_
#define STARBOARD_SHARED_STARBOARD_PLAYER_JOB_QUEUE_H_

#include <map>

#include "starboard/common/scoped_ptr.h"
#include "starboard/condition_variable.h"
#include "starboard/log.h"
#include "starboard/mutex.h"
#include "starboard/queue.h"
#include "starboard/shared/internal_only.h"
#include "starboard/shared/starboard/player/closure.h"
#include "starboard/time.h"

#ifndef __cplusplus
#error "Only C++ files can include this header."
#endif

namespace starboard {
namespace shared {
namespace starboard {
namespace player {

// This class implements a job queue where closures can be posted to it on any
// thread and will be processed on one thread that this job queue is linked to.
// A thread can only have one job queue.
class JobQueue {
 public:
  class JobOwner {
   protected:
    explicit JobOwner(JobQueue* job_queue = JobQueue::current())
        : job_queue_(job_queue) {
      SB_DCHECK(job_queue);
    }
    ~JobOwner() { CancelPendingJobs(); }
    bool BelongsToCurrentThread() const {
      return job_queue_->BelongsToCurrentThread();
    }
    void Schedule(Closure closure, SbTimeMonotonic delay = 0) {
      job_queue_->Schedule(closure, this, delay);
    }
    void Remove(Closure closure) { job_queue_->Remove(closure); }
    void CancelPendingJobs() { job_queue_->RemoveJobsByToken(this); }

   private:
    JobQueue* job_queue_;
  };

  JobQueue();
  ~JobQueue();

  void Schedule(Closure closure, SbTimeMonotonic delay = 0);
  void Remove(Closure closure);

  // The processing of jobs may not be stopped when this function returns, but
  // it is guaranteed that the processing will be stopped very soon.  So it is
  // safe to join the thread after this call returns.
  void StopSoon();

  void RunUntilStopped();
  void RunUntilIdle();

  bool BelongsToCurrentThread() const;
  static JobQueue* current();

 private:
  struct Job {
    Closure closure;
    JobOwner* owner;
  };
  typedef std::multimap<SbTimeMonotonic, Job> TimeToJobMap;

  void Schedule(Closure closure, JobOwner* owner, SbTimeMonotonic delay);
  void RemoveJobsByToken(JobOwner* owner);
  // Return true if a job is run, otherwise return false.  When there is no job
  // ready to run currently and |wait_for_next_job| is true, the function will
  // wait to until a job is available or if the |queue_| is woken up.  Note that
  // set |wait_for_next_job| to true doesn't guarantee that one job will always
  // be run.
  bool TryToRunOneJob(bool wait_for_next_job);

  SbThreadId thread_id_;
  Mutex mutex_;
  ConditionVariable condition_;
  TimeToJobMap time_to_job_map_;
  bool stopped_;
};

}  // namespace player
}  // namespace starboard
}  // namespace shared
}  // namespace starboard

#endif  // STARBOARD_SHARED_STARBOARD_PLAYER_JOB_QUEUE_H_
