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

#include "cobalt/worker/service_worker_jobs.h"

#include <list>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/single_thread_task_runner.h"
#include "base/synchronization/lock.h"
#include "base/task_runner.h"
#include "base/trace_event/trace_event.h"
#include "cobalt/base/tokens.h"
#include "cobalt/dom/visibility_state.h"
#include "cobalt/loader/script_loader_factory.h"
#include "cobalt/network/network_module.h"
#include "cobalt/script/promise.h"
#include "cobalt/script/script_exception.h"
#include "cobalt/script/script_value.h"
#include "cobalt/web/context.h"
#include "cobalt/web/dom_exception.h"
#include "cobalt/web/environment_settings.h"
#include "cobalt/web/event.h"
#include "cobalt/web/window_or_worker_global_scope.h"
#include "cobalt/worker/client.h"
#include "cobalt/worker/client_query_options.h"
#include "cobalt/worker/client_type.h"
#include "cobalt/worker/frame_type.h"
#include "cobalt/worker/service_worker.h"
#include "cobalt/worker/service_worker_container.h"
#include "cobalt/worker/service_worker_global_scope.h"
#include "cobalt/worker/service_worker_registration.h"
#include "cobalt/worker/service_worker_registration_object.h"
#include "cobalt/worker/service_worker_update_via_cache.h"
#include "cobalt/worker/window_client.h"
#include "cobalt/worker/worker_type.h"
#include "net/base/url_util.h"
#include "starboard/atomic.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace cobalt {
namespace worker {

namespace {
bool PathContainsEscapedSlash(const GURL& url) {
  const std::string path = url.path();
  return (path.find("%2f") != std::string::npos ||
          path.find("%2F") != std::string::npos ||
          path.find("%5c") != std::string::npos ||
          path.find("%5C") != std::string::npos);
}

bool IsOriginPotentiallyTrustworthy(const GURL& url) {
  // Algorithm for potentially trustworthy origin:
  //   https://w3c.github.io/webappsec-secure-contexts/#potentially-trustworthy-origin

  const url::Origin origin(url::Origin::Create(url));
  // 1. If origin is an opaque origin, return "Not Trustworthy".
  if (origin.unique()) return false;

  // 2. Assert: origin is a tuple origin.
  DCHECK(!origin.unique());
  DCHECK(url.is_valid());

  // 3. If origin’s scheme is either "https" or "wss", return "Potentially
  // Trustworthy".
  if (url.SchemeIsCryptographic()) return true;

  // 4. If origin’s host matches one of the CIDR notations 127.0.0.0/8 or
  // ::1/128 [RFC4632], return "Potentially Trustworthy".
  if (net::IsLocalhost(url)) return true;

  // 5. If the user agent conforms to the name resolution rules in
  // [let-localhost-be-localhost] and one of the following is true:
  //    origin’s host is "localhost" or "localhost."
  //    origin’s host ends with ".localhost" or ".localhost."
  // then return "Potentially Trustworthy".
  // Covered by implementation of step 4.

  // 6. If origin’s scheme is "file", return "Potentially Trustworthy".
  if (url.SchemeIsFile()) return true;

  // 7. If origin’s scheme component is one which the user agent considers to be
  // authenticated, return "Potentially Trustworthy".
  if (url.SchemeIs("h5vcc-embedded")) return true;

  // 8. If origin has been configured as a trustworthy origin, return
  // "Potentially Trustworthy".

  // 9. Return "Not Trustworthy".
  return false;
}

bool PermitAnyURL(const GURL&, bool) { return true; }
}  // namespace

ServiceWorkerJobs::ServiceWorkerJobs(network::NetworkModule* network_module,
                                     base::MessageLoop* message_loop)
    : network_module_(network_module), message_loop_(message_loop) {
  DCHECK_EQ(message_loop_, base::MessageLoop::current());
  fetcher_factory_.reset(new loader::FetcherFactory(network_module));
  DCHECK(fetcher_factory_);

  script_loader_factory_.reset(new loader::ScriptLoaderFactory(
      "ServiceWorkerJobs", fetcher_factory_.get()));
  DCHECK(script_loader_factory_);
}

ServiceWorkerJobs::~ServiceWorkerJobs() {
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  while (!web_context_registrations_.empty()) {
    // Wait for web context registrations to be cleared.
    web_context_registrations_cleared_.Wait();
  }
  scope_to_registration_map_.HandleUserAgentShutdown(this);
}

void ServiceWorkerJobs::StartRegister(
    const base::Optional<GURL>& maybe_scope_url,
    const GURL& script_url_with_fragment,
    std::unique_ptr<script::ValuePromiseWrappable::Reference> promise_reference,
    web::EnvironmentSettings* client, const WorkerType& type,
    const ServiceWorkerUpdateViaCache& update_via_cache) {
  TRACE_EVENT2("cobalt::worker", "ServiceWorkerJobs::StartRegister()", "scope",
               maybe_scope_url.value_or(GURL()).spec(), "script",
               script_url_with_fragment.spec());
  DCHECK_NE(message_loop(), base::MessageLoop::current());
  DCHECK_EQ(client->context()->message_loop(), base::MessageLoop::current());
  // Algorithm for Start Register:
  //   https://w3c.github.io/ServiceWorker/#start-register-algorithm
  // 1. If scriptURL is failure, reject promise with a TypeError and abort these
  // steps.
  if (script_url_with_fragment.is_empty()) {
    promise_reference->value().Reject(script::kTypeError);
    return;
  }

  // 2. Set scriptURL’s fragment to null.
  url::Replacements<char> replacements;
  replacements.ClearRef();
  GURL script_url = script_url_with_fragment.ReplaceComponents(replacements);
  DCHECK(!script_url.has_ref() || script_url.ref().empty());
  DCHECK(!script_url.is_empty());

  // 3. If scriptURL’s scheme is not one of "http" and "https", reject promise
  // with a TypeError and abort these steps.
  if (!script_url.SchemeIsHTTPOrHTTPS()) {
    promise_reference->value().Reject(script::kTypeError);
    return;
  }

  // 4. If any of the strings in scriptURL’s path contains either ASCII
  // case-insensitive "%2f" or ASCII case-insensitive "%5c", reject promise with
  // a TypeError and abort these steps.
  if (PathContainsEscapedSlash(script_url)) {
    promise_reference->value().Reject(script::kTypeError);
    return;
  }

  // 5. If scopeURL is null, set scopeURL to the result of parsing the string
  // "./" with scriptURL.
  GURL scope_url = maybe_scope_url.value_or(script_url.Resolve("./"));

  // 6. If scopeURL is failure, reject promise with a TypeError and abort these
  // steps.
  if (scope_url.is_empty()) {
    promise_reference->value().Reject(script::kTypeError);
    return;
  }

  // 7. Set scopeURL’s fragment to null.
  scope_url = scope_url.ReplaceComponents(replacements);
  DCHECK(!scope_url.has_ref() || scope_url.ref().empty());
  DCHECK(!scope_url.is_empty());

  // 8. If scopeURL’s scheme is not one of "http" and "https", reject promise
  // with a TypeError and abort these steps.
  if (!scope_url.SchemeIsHTTPOrHTTPS()) {
    promise_reference->value().Reject(script::kTypeError);
    return;
  }

  // 9. If any of the strings in scopeURL’s path contains either ASCII
  // case-insensitive "%2f" or ASCII case-insensitive "%5c", reject promise with
  // a TypeError and abort these steps.
  if (PathContainsEscapedSlash(scope_url)) {
    promise_reference->value().Reject(script::kTypeError);
    return;
  }

  // 10. Let storage key be the result of running obtain a storage key given
  // client.
  url::Origin storage_key = client->ObtainStorageKey();

  // 11. Let job be the result of running Create Job with register, storage key,
  // scopeURL, scriptURL, promise, and client.
  std::unique_ptr<Job> job =
      CreateJob(kRegister, storage_key, scope_url, script_url,
                JobPromiseType::Create(std::move(promise_reference)), client);
  DCHECK(!promise_reference);

  // 12. Set job’s worker type to workerType.
  // Cobalt only supports 'classic' worker type.

  // 13. Set job’s update via cache mode to updateViaCache.
  job->update_via_cache = update_via_cache;

  // 14. Set job’s referrer to referrer.
  // This is the same value as set in CreateJob().

  // 15. Invoke Schedule Job with job.
  message_loop()->task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&ServiceWorkerJobs::ScheduleJob,
                                base::Unretained(this), std::move(job)));
  DCHECK(!job.get());
}

void ServiceWorkerJobs::PromiseErrorData::Reject(
    std::unique_ptr<JobPromiseType> promise) const {
  if (message_type_ != script::kNoError) {
    promise->Reject(GetSimpleExceptionType(message_type_));
  } else {
    promise->Reject(new web::DOMException(exception_code_, message_));
  }
}

std::unique_ptr<ServiceWorkerJobs::Job> ServiceWorkerJobs::CreateJob(
    JobType type, const url::Origin& storage_key, const GURL& scope_url,
    const GURL& script_url, std::unique_ptr<JobPromiseType> promise,
    web::EnvironmentSettings* client) {
  TRACE_EVENT2("cobalt::worker", "ServiceWorkerJobs::CreateJob()", "type", type,
               "script_url", script_url.spec());
  // Algorithm for Create Job:
  //   https://w3c.github.io/ServiceWorker/#create-job
  // 1. Let job be a new job.
  // 2. Set job’s job type to jobType.
  // 3. Set job’s storage key to storage key.
  // 4. Set job’s scope url to scopeURL.
  // 5. Set job’s script url to scriptURL.
  // 6. Set job’s job promise to promise.
  // 7. Set job’s client to client.
  std::unique_ptr<Job> job(new Job(type, storage_key, scope_url, script_url,
                                   client, std::move(promise)));
  // 8. If client is not null, set job’s referrer to client’s creation URL.
  if (client) {
    job->referrer = client->creation_url();
  }
  // 9. Return job.
  return job;
}

void ServiceWorkerJobs::ScheduleJob(std::unique_ptr<Job> job) {
  TRACE_EVENT0("cobalt::worker", "ServiceWorkerJobs::ScheduleJob()");
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  DCHECK(job);
  // Algorithm for Schedule Job:
  //   https://w3c.github.io/ServiceWorker/#schedule-job
  // 1. Let jobQueue be null.

  // 2. Let jobScope be job’s scope url, serialized.
  std::string job_scope = job->scope_url.spec();

  // 3. If scope to job queue map[jobScope] does not exist, set scope to job
  // queue map[jobScope] to a new job queue.
  if (job_queue_map_.find(job_scope) == job_queue_map_.end()) {
    auto insertion = job_queue_map_.emplace(
        job_scope, std::unique_ptr<JobQueue>(new JobQueue()));
    DCHECK(insertion.second);
  }

  // 4. Set jobQueue to scope to job queue map[jobScope].
  DCHECK(job_queue_map_.find(job_scope) != job_queue_map_.end());
  JobQueue* job_queue = job_queue_map_.find(job_scope)->second.get();

  // 5. If jobQueue is empty, then:
  if (job_queue->empty()) {
    // 5.1. Set job’s containing job queue to jobQueue, and enqueue job to
    // jobQueue.
    job->containing_job_queue = job_queue;
    job_queue->Enqueue(std::move(job));

    // 5.2. Invoke Run Job with jobQueue.
    RunJob(job_queue);
  } else {
    // 6. Else:
    // 6.1. Let lastJob be the element at the back of jobQueue.
    {
      auto last_item = job_queue->LastItem();
      Job* last_job = last_item.first;

      // 6.2. If job is equivalent to lastJob and lastJob’s job promise has not
      // settled, append job to lastJob’s list of equivalent jobs.
      DCHECK(last_job);
      base::AutoLock lock(last_job->equivalent_jobs_promise_mutex);
      if (EquivalentJobs(job.get(), last_job) && last_job->promise &&
          last_job->promise->State() == script::PromiseState::kPending) {
        last_job->equivalent_jobs.push_back(std::move(job));
        return;
      }
    }

    // 6.3. Else, set job’s containing job queue to jobQueue, and enqueue job to
    // jobQueue.
    job->containing_job_queue = job_queue;
    job_queue->Enqueue(std::move(job));
  }
  DCHECK(!job);
}

bool ServiceWorkerJobs::EquivalentJobs(Job* one, Job* two) {
  // Algorithm for Two jobs are equivalent:
  //   https://w3c.github.io/ServiceWorker/#dfn-job-equivalent
  DCHECK(one);
  DCHECK(two);
  if (!one || !two) {
    return false;
  }

  // Two jobs are equivalent when their job type is the same and:
  if (one->type != two->type) {
    return false;
  }

  // For register and update jobs, their scope url, script url, worker type, and
  // update via cache mode are the same.
  if ((one->type == kRegister || one->type == kUpdate) &&
      (one->scope_url == two->scope_url) &&
      (one->script_url == two->script_url) &&
      (one->update_via_cache == two->update_via_cache)) {
    return true;
  }

  // For unregister jobs, their scope url is the same.
  return (one->type == kUnregister) && (one->scope_url == two->scope_url);
}

void ServiceWorkerJobs::RunJob(JobQueue* job_queue) {
  TRACE_EVENT0("cobalt::worker", "ServiceWorkerJobs::RunJob()");
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  // Algorithm for Run Job:
  //   https://w3c.github.io/ServiceWorker/#run-job-algorithm

  // 1. Assert: jobQueue is not empty.
  DCHECK(job_queue && !job_queue->empty());
  if (!job_queue || job_queue->empty()) {
    return;
  }

  // 2. Queue a task to run these steps:
  message_loop()->task_runner()->PostTask(
      FROM_HERE, base::BindOnce(&ServiceWorkerJobs::RunJobTask,
                                base::Unretained(this), job_queue));
}

void ServiceWorkerJobs::RunJobTask(JobQueue* job_queue) {
  TRACE_EVENT0("cobalt::worker", "ServiceWorkerJobs::RunJobTask()");
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  // Task for "Run Job" to run in the service worker thread.
  //   https://w3c.github.io/ServiceWorker/#run-job-algorithm
  DCHECK(job_queue);
  if (!job_queue) return;
  DCHECK(!job_queue->empty());

  // 2.1 Let job be the first item in jobQueue.
  Job* job = job_queue->FirstItem();

  DCHECK(job);
  switch (job->type) {
    // 2.2 If job’s job type is register, run Register with job in parallel.
    case kRegister:
      Register(job);
      break;

    // 2.3 Else if job’s job type is update, run Update with job in parallel.
    case kUpdate:
      Update(job);
      break;

    // 2.4 Else if job’s job type is unregister, run Unregister with job in
    // parallel.
    case kUnregister:
      Unregister(job);
      break;
    default:
      NOTREACHED();
  }
}

void ServiceWorkerJobs::Register(Job* job) {
  TRACE_EVENT0("cobalt::worker", "ServiceWorkerJobs::Register()");
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  DCHECK(job);
  // Algorithm for Register:
  //   https://w3c.github.io/ServiceWorker/#register-algorithm

  // 1. If the result of running potentially trustworthy origin with the origin
  // of job’s script url as the argument is Not Trusted, then:
  if (!IsOriginPotentiallyTrustworthy(job->script_url)) {
    // 1.1. Invoke Reject Job Promise with job and "SecurityError" DOMException.
    RejectJobPromise(
        job, PromiseErrorData(
                 web::DOMException::kSecurityErr,
                 "Service Worker Register failed: Script URL is Not Trusted."));
    // 1.2. Invoke Finish Job with job and abort these steps.
    FinishJob(job);
    return;
  }

  // 2. If job’s script url's origin and job’s referrer's origin are not same
  // origin, then:
  const url::Origin job_script_origin(url::Origin::Create(job->script_url));
  const url::Origin job_referrer_origin(url::Origin::Create(job->referrer));
  if (!job_script_origin.IsSameOriginWith(job_referrer_origin)) {
    // 2.1. Invoke Reject Job Promise with job and "SecurityError" DOMException.
    RejectJobPromise(
        job, PromiseErrorData(
                 web::DOMException::kSecurityErr,
                 "Service Worker Register failed: Script URL and referrer "
                 "origin are not the same."));
    // 2.2. Invoke Finish Job with job and abort these steps.
    FinishJob(job);
    return;
  }

  // 3. If job’s scope url's origin and job’s referrer's origin are not same
  // origin, then:
  const url::Origin job_scope_origin(url::Origin::Create(job->scope_url));
  if (!job_scope_origin.IsSameOriginWith(job_referrer_origin)) {
    // 3.1. Invoke Reject Job Promise with job and "SecurityError" DOMException.
    RejectJobPromise(
        job, PromiseErrorData(
                 web::DOMException::kSecurityErr,
                 "Service Worker Register failed: Scope URL and referrer "
                 "origin are not the same."));

    // 3.2. Invoke Finish Job with job and abort these steps.
    FinishJob(job);
    return;
  }

  // 4. Let registration be the result of running Get Registration given job’s
  // storage key and job’s scope url.
  scoped_refptr<ServiceWorkerRegistrationObject> registration =
      scope_to_registration_map_.GetRegistration(job->storage_key,
                                                 job->scope_url);

  // 5. If registration is not null, then:
  if (registration) {
    // 5.1 Let newestWorker be the result of running the Get Newest Worker
    // algorithm passing registration as the argument.
    ServiceWorkerObject* newest_worker = registration->GetNewestWorker();

    // 5.2 If newestWorker is not null, job’s script url equals newestWorker’s
    // script url, job’s worker type equals newestWorker’s type, and job’s
    // update via cache mode's value equals registration’s update via cache
    // mode, then:
    if (newest_worker && job->script_url == newest_worker->script_url()) {
      // 5.2.1 Invoke Resolve Job Promise with job and registration.
      ResolveJobPromise(job, registration);

      // 5.2.2 Invoke Finish Job with job and abort these steps.
      FinishJob(job);
      return;
    }
  } else {
    // 6. Else:

    // 6.1 Invoke Set Registration algorithm with job’s storage key, job’s scope
    // url, and job’s update via cache mode.
    registration = scope_to_registration_map_.SetRegistration(
        job->storage_key, job->scope_url, job->update_via_cache);
  }

  // 7. Invoke Update algorithm passing job as the argument.
  Update(job);
}

void ServiceWorkerJobs::Update(Job* job) {
  TRACE_EVENT0("cobalt::worker", "ServiceWorkerJobs::Update()");
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  DCHECK(job);
  // Algorithm for Update:
  //   https://w3c.github.io/ServiceWorker/#update-algorithm

  // 1. Let registration be the result of running Get Registration given job’s
  //    storage key and job’s scope url.
  scoped_refptr<ServiceWorkerRegistrationObject> registration =
      scope_to_registration_map_.GetRegistration(job->storage_key,
                                                 job->scope_url);

  // 2. If registration is null, then:
  if (!registration) {
    // 2.1. Invoke Reject Job Promise with job and TypeError.
    RejectJobPromise(job, PromiseErrorData(script::kSimpleTypeError));

    // 2.2. Invoke Finish Job with job and abort these steps.
    FinishJob(job);
    return;
  }
  // 3. Let newestWorker be the result of running Get Newest Worker algorithm
  //    passing registration as the argument.
  ServiceWorkerObject* newest_worker = registration->GetNewestWorker();

  // 4. If job’s job type is update, and newestWorker is not null and its script
  //    url does not equal job’s script url, then:
  if ((job->type == kUpdate) && newest_worker &&
      (newest_worker->script_url() != job->script_url)) {
    // 4.1 Invoke Reject Job Promise with job and TypeError.
    RejectJobPromise(job, PromiseErrorData(script::kSimpleTypeError));

    // 4.2 Invoke Finish Job with job and abort these steps.
    FinishJob(job);
    return;
  }

  auto state(
      base::MakeRefCounted<UpdateJobState>(job, registration, newest_worker));

  // 5. Let referrerPolicy be the empty string.
  // 6. Let hasUpdatedResources be false.
  state->has_updated_resources = false;
  // 7. Let updatedResourceMap be an ordered map where the keys are URLs and the
  //    values are responses.
  // That is located in job->updated_resource_map.

  // 8. Switching on job’s worker type, run these substeps with the following
  //    options:
  //    - "classic"
  //        Fetch a classic worker script given job’s serialized script url,
  //        job’s client, "serviceworker", and the to-be-created environment
  //        settings object for this service worker.
  //    - "module"
  //        Fetch a module worker script graph given job’s serialized script
  //        url, job’s client, "serviceworker", "omit", and the to-be-created
  //        environment settings object for this service worker.
  // To perform the fetch given request, run the following steps:
  //   8.1.  Append `Service-Worker`/`script` to request’s header list.
  //   8.2.  Set request’s cache mode to "no-cache" if any of the following are
  //         true:
  //          - registration’s update via cache mode is not "all".
  //          - job’s force bypass cache flag is set.
  //          - newestWorker is not null and registration is stale.
  //   8.3.  Set request’s service-workers mode to "none".
  //   8.4.  If the is top-level flag is unset, then return the result of
  //         fetching request.
  //   8.5.  Set request’s redirect mode to "error".
  //   8.6.  Fetch request, and asynchronously wait to run the remaining steps
  //         as part of fetch’s process response for the response response.
  //   8.7.  Extract a MIME type from the response’s header list. If this MIME
  //         type (ignoring parameters) is not a JavaScript MIME type, then:
  //   8.7.1. Invoke Reject Job Promise with job and "SecurityError"
  //          DOMException.
  //   8.7.2. Asynchronously complete these steps with a network error.
  // TODO(b/235393876): Implement Service-Worker-Allowed.
  //   8.8.  Let serviceWorkerAllowed be the result of extracting header list
  //         values given `Service-Worker-Allowed` and response’s header list.
  //   8.9.  Set policyContainer to the result of creating a policy container
  //         from a fetch response given response.
  //   8.10. If serviceWorkerAllowed is failure, then:
  //   8.10.1  Asynchronously complete these steps with a network error.
  //   8.11. Let scopeURL be registration’s scope url.
  //   8.12. Let maxScopeString be null.
  //   8.13. If serviceWorkerAllowed is null, then:
  //   8.13.1. Let resolvedScope be the result of parsing "./" using job’s
  //           script url as the base URL.
  //   8.13.2. Set maxScopeString to "/", followed by the strings in
  //           resolvedScope’s path (including empty strings), separated from
  //           each other by "/".
  //   8.14. Else:
  //   8.14.1. Let maxScope be the result of parsing serviceWorkerAllowed using
  //           job’s script url as the base URL.
  //   8.14.2. If maxScope’s origin is job’s script url's origin, then:
  //   8.14.2.1. Set maxScopeString to "/", followed by the strings in
  //             maxScope’s path (including empty strings), separated from each
  //             other by "/".
  //   8.15. Let scopeString be "/", followed by the strings in scopeURL’s path
  //         (including empty strings), separated from each other by "/".
  //   8.16. If maxScopeString is null or scopeString does not start with
  //         maxScopeString, then:
  //   8.16.1. Invoke Reject Job Promise with job and "SecurityError"
  //           DOMException.
  //   8.16.2. Asynchronously complete these steps with a network error.

  // TODO(b/225037465): Implement CSP check.
  csp::SecurityCallback csp_callback = base::Bind(&PermitAnyURL);
  loader::Origin origin = loader::Origin(job->script_url.GetOrigin());
  job->loader = script_loader_factory_->CreateScriptLoader(
      job->script_url, origin, csp_callback,
      base::Bind(&ServiceWorkerJobs::UpdateOnContentProduced,
                 base::Unretained(this), state),
      base::Bind(&ServiceWorkerJobs::UpdateOnLoadingComplete,
                 base::Unretained(this), state));
}

void ServiceWorkerJobs::UpdateOnContentProduced(
    scoped_refptr<UpdateJobState> state, const loader::Origin& last_url_origin,
    std::unique_ptr<std::string> content) {
  TRACE_EVENT0("cobalt::worker",
               "ServiceWorkerJobs::UpdateOnContentProduced()");
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  // Note: There seems to be missing handling of network errors here.
  //   8.17. Let url be request’s url.
  //   8.18. Set updatedResourceMap[url] to response.
  auto result = state->updated_resource_map.insert(
      std::make_pair(state->job->script_url, std::move(content)));
  // Assert that the insert was successful.
  DCHECK(result.second);
  //   8.19. If response’s cache state is not "local", set registration’s last
  //         update check time to the current time.
  //   8.20. Set hasUpdatedResources to true if any of the following are true:
  //          - newestWorker is null.
  //          - newestWorker’s script url is not url or newestWorker’s type is
  //            not job’s worker type.
  // Note: Cobalt only supports 'classic' worker type.
  //          - newestWorker’s script resource map[url]'s body is not
  //            byte-for-byte identical with response’s body.
  if (state->newest_worker == nullptr) {
    state->has_updated_resources = true;
  } else {
    if (state->newest_worker->script_url() != state->job->script_url) {
      state->has_updated_resources = true;
    } else {
      std::string* script_resource =
          state->newest_worker->LookupScriptResource(state->job->script_url);
      if (script_resource && content && (*script_resource != *content)) {
        state->has_updated_resources = true;
      }
    }
  }
}

void ServiceWorkerJobs::UpdateOnLoadingComplete(
    scoped_refptr<UpdateJobState> state,
    const base::Optional<std::string>& error) {
  TRACE_EVENT0("cobalt::worker",
               "ServiceWorkerJobs::UpdateOnLoadingComplete()");
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  //   8.21. If hasUpdatedResources is false and newestWorker’s classic
  //         scripts imported flag is set, then:
  if (!state->has_updated_resources &&
      state->newest_worker->classic_scripts_imported()) {
    // This checks if there are any updates to already stored importScripts
    // resources.
    if (state->newest_worker->worker_global_scope()
            ->LoadImportsAndReturnIfUpdated(
                state->newest_worker->script_resource_map(),
                &state->updated_resource_map)) {
      state->has_updated_resources = true;
    }
  }
  //   8.22. Asynchronously complete these steps with response.

  // When the algorithm asynchronously completes, continue the rest of these
  // steps, with script being the asynchronous completion value.
  auto entry = state->updated_resource_map.find(state->job->script_url);
  auto* script = entry != state->updated_resource_map.end()
                     ? entry->second.get()
                     : nullptr;
  // 9. If script is null or Is Async Module with script’s record, script’s
  //    base URL, and {} it true, then:
  if (script == nullptr) {
    // 9.1. Invoke Reject Job Promise with job and TypeError.
    RejectJobPromise(state->job, PromiseErrorData(script::kSimpleTypeError));

    // 9.2. If newestWorker is null, then remove registration
    //      map[(registration’s storage key, serialized scopeURL)].
    if (state->newest_worker == nullptr) {
      scope_to_registration_map_.RemoveRegistration(state->job->storage_key,
                                                    state->job->scope_url);
    }
    // 9.3. Invoke Finish Job with job and abort these steps.
    FinishJob(state->job);
    return;
  }

  // 10. If hasUpdatedResources is false, then:
  if (!state->has_updated_resources) {
    // 10.1. Set registration’s update via cache mode to job’s update via cache
    //       mode.
    state->registration->set_update_via_cache_mode(
        state->job->update_via_cache);

    // 10.2. Invoke Resolve Job Promise with job and registration.
    ResolveJobPromise(state->job, state->registration);

    // 10.3. Invoke Finish Job with job and abort these steps.
    FinishJob(state->job);
    return;
  }

  // 11. Let worker be a new service worker.
  ServiceWorkerObject::Options options(
      "ServiceWorker", state->job->client->context()->network_module(),
      state->registration);
  options.web_options.platform_info =
      state->job->client->context()->platform_info();
  options.web_options.service_worker_jobs =
      state->job->client->context()->service_worker_jobs();
  scoped_refptr<ServiceWorkerObject> worker(new ServiceWorkerObject(options));
  // 12. Set worker’s script url to job’s script url, worker’s script
  //     resource to script, worker’s type to job’s worker type, and worker’s
  //     script resource map to updatedResourceMap.
  // -> The worker's script resource is set in the resource map at step 8.18.
  worker->set_script_url(state->job->script_url);
  worker->set_script_resource_map(std::move(state->updated_resource_map));
  // 13. Append url to worker’s set of used scripts.
  worker->AppendToSetOfUsedScripts(state->job->script_url);
  // 14. Set worker’s script resource’s policy container to policyContainer.
  // 15. Let forceBypassCache be true if job’s force bypass cache flag is
  //     set, and false otherwise.
  bool force_bypass_cache = state->job->force_bypass_cache_flag;
  // 16. Let runResult be the result of running the Run Service Worker
  //     algorithm with worker and forceBypassCache.
  auto* run_result = RunServiceWorker(worker.get(), force_bypass_cache);
  // 17. If runResult is failure or an abrupt completion, then:
  if (!run_result) {
    // 17.1. Invoke Reject Job Promise with job and TypeError.
    RejectJobPromise(state->job, PromiseErrorData(script::kSimpleTypeError));
    // 17.2. If newestWorker is null, then remove registration
    //       map[(registration’s storage key, serialized scopeURL)].
    if (state->newest_worker == nullptr) {
      scope_to_registration_map_.RemoveRegistration(state->job->storage_key,
                                                    state->job->scope_url);
    }
    // 17.3. Invoke Finish Job with job.
    FinishJob(state->job);
  } else {
    // 18. Else, invoke Install algorithm with job, worker, and registration
    //     as its arguments.
    Install(state->job, std::move(worker), state->registration);
  }
}

std::string* ServiceWorkerJobs::RunServiceWorker(ServiceWorkerObject* worker,
                                                 bool force_bypass_cache) {
  TRACE_EVENT0("cobalt::worker", "ServiceWorkerJobs::RunServiceWorker()");

  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  DCHECK(worker);
  // Algorithm for "Run Service Worker"
  //   https://w3c.github.io/ServiceWorker/#run-service-worker-algorithm

  // 1. Let unsafeCreationTime be the unsafe shared current time.
  auto unsafe_creation_time = base::TimeTicks::Now();
  // 2. If serviceWorker is running, then return serviceWorker’s start status.
  if (worker->is_running()) {
    return worker->start_status();
  }
  // 3. If serviceWorker’s state is "redundant", then return failure.
  if (worker->state() == kServiceWorkerStateRedundant) {
    return nullptr;
  }
  // 4. Assert: serviceWorker’s start status is null.
  DCHECK(worker->start_status() == nullptr);
  // 5. Let script be serviceWorker’s script resource.
  // 6. Assert: script is not null.
  DCHECK(worker->HasScriptResource());
  // 7. Let startFailed be false.
  worker->store_start_failed(false);
  // 8. Let agent be the result of obtaining a service worker agent, and run the
  //    following steps in that context:
  // 9. Wait for serviceWorker to be running, or for startFailed to be true.
  worker->ObtainWebAgentAndWaitUntilDone();
  // 10. If startFailed is true, then return failure.
  if (worker->load_start_failed()) {
    return nullptr;
  }
  // 11. Return serviceWorker’s start status.
  return worker->start_status();
}

void ServiceWorkerJobs::Install(
    Job* job, scoped_refptr<ServiceWorkerObject> worker,
    scoped_refptr<ServiceWorkerRegistrationObject> registration) {
  TRACE_EVENT0("cobalt::worker", "ServiceWorkerJobs::Install()");
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  // Algorithm for Install:
  //   https://w3c.github.io/ServiceWorker/#installation-algorithm

  // 1. Let installFailed be false.
  starboard::atomic_bool install_failed(false);

  // 2. Let newestWorker be the result of running Get Newest Worker algorithm
  //    passing registration as its argument.
  ServiceWorkerObject* newest_worker = registration->GetNewestWorker();

  // 3. Set registration’s update via cache mode to job’s update via cache mode.
  registration->set_update_via_cache_mode(job->update_via_cache);

  // 4. Run the Update Registration State algorithm passing registration,
  //    "installing" and worker as the arguments.
  UpdateRegistrationState(registration, kInstalling, worker);

  // 5. Run the Update Worker State algorithm passing registration’s installing
  //    worker and "installing" as the arguments.
  UpdateWorkerState(registration->installing_worker(),
                    kServiceWorkerStateInstalling);
  // 6. Assert: job’s job promise is not null.
  DCHECK(job->promise.get() != nullptr);
  // 7. Invoke Resolve Job Promise with job and registration.
  ResolveJobPromise(job, registration);
  // 8. Let settingsObjects be all environment settings objects whose origin is
  //    registration’s scope url's origin.
  auto registration_origin = registration->scope_url().GetOrigin();
  // 9. For each settingsObject of settingsObjects...
  for (auto& context : web_context_registrations_) {
    if (context->environment_settings()->GetOrigin() == registration_origin) {
      // 9. ... queue a task on settingsObject’s responsible event loop in the
      //    DOM manipulation task source to run the following steps:
      context->message_loop()->task_runner()->PostTask(
          FROM_HERE,
          base::BindOnce(
              [](web::Context* context,
                 scoped_refptr<ServiceWorkerRegistrationObject> registration) {
                // 9.1. Let registrationObjects be every
                //      ServiceWorkerRegistration object in settingsObject’s
                //      realm, whose service worker registration is
                //      registration.

                // There is at most one per web context, stored in the service
                // worker registration object map of the web context.

                // 9.2. For each registrationObject of registrationObjects, fire
                //      an event on registrationObject named updatefound.
                auto registration_object =
                    context->LookupServiceWorkerRegistration(registration);
                if (registration_object) {
                  context->message_loop()->task_runner()->PostTask(
                      FROM_HERE,
                      base::BindOnce(
                          [](scoped_refptr<ServiceWorkerRegistration>
                                 registration_object) {
                            registration_object->DispatchEvent(
                                new web::Event(base::Tokens::updatefound()));
                          },
                          registration_object));
                }
              },
              context, registration));
    }
  }
  // 10. Let installingWorker be registration’s installing worker.
  ServiceWorkerObject* installing_worker = registration->installing_worker();
  // 11. If the result of running the Should Skip Event algorithm with
  //     installingWorker and "install" is false, then:
  if (!ShouldSkipEvent(base::Tokens::install(), installing_worker)) {
    // 11.1. Let forceBypassCache be true if job’s force bypass cache flag is
    //       set, and false otherwise.
    bool force_bypass_cache = job->force_bypass_cache_flag;
    // 11.2. If the result of running the Run Service Worker algorithm with
    //       installingWorker and forceBypassCache is failure, then:
    auto* run_result = RunServiceWorker(installing_worker, force_bypass_cache);
    if (!run_result) {
      // 11.2.1. Set installFailed to true.
      install_failed.store(true);
      // 11.3. Else:
    } else {
      // 11.3.1. Queue a task task on installingWorker’s event loop using the
      //         DOM manipulation task source to run the following steps:
      installing_worker->web_agent()
          ->context()
          ->message_loop()
          ->task_runner()
          ->PostBlockingTask(
              FROM_HERE,
              base::Bind(
                  [](ServiceWorkerObject* installing_worker) {
                    // 11.3.1.1. Let e be the result of creating an event with
                    //           ExtendableEvent.
                    // TODO(b/228976500): implement this as ExtendableEvent.
                    // 11.3.1.2. Initialize e’s type attribute to install.
                    // 11.3.1.3. Dispatch e at installingWorker’s global object.
                    installing_worker->worker_global_scope()->DispatchEvent(
                        new web::Event(base::Tokens::install()));
                    // 11.3.1.4. WaitForAsynchronousExtensions: Run the
                    //           following substeps in parallel:
                    // 11.3.1.4.1. Wait until e is not active.
                    // 11.3.1.4.2. If e’s timed out flag is set, set
                    //             installFailed to true.
                    // 11.3.1.4.3. Let p be the result of getting a promise to
                    //             wait for all of e’s extend lifetime promises.
                    // 11.3.1.4.4. Upon rejection of p, set installFailed to
                    //             true.
                    //         If task is discarded, set installFailed to true.
                  },
                  base::Unretained(installing_worker)));
      // 11.3.2. Wait for task to have executed or been discarded.
      // Waiting is done inside PostBlockingTask above.
      // 11.3.3. Wait for the step labeled WaitForAsynchronousExtensions to
      //         complete.
      NOTIMPLEMENTED();
    }
  }
  // 12. If installFailed is true, then:
  if (install_failed.load()) {
    // 12.1. Run the Update Worker State algorithm passing registration’s
    //       installing worker and "redundant" as the arguments.
    UpdateWorkerState(registration->installing_worker(),
                      kServiceWorkerStateRedundant);
    // 12.2. Run the Update Registration State algorithm passing registration,
    //       "installing" and null as the arguments.
    UpdateRegistrationState(registration, kInstalling, nullptr);
    // 12.3. If newestWorker is null, then remove registration
    //       map[(registration’s storage key, serialized registration’s
    //       scope url)].
    if (newest_worker == nullptr) {
      scope_to_registration_map_.RemoveRegistration(registration->storage_key(),
                                                    registration->scope_url());
    }
    // 12.4. Invoke Finish Job with job and abort these steps.
    FinishJob(job);
    return;
  }
  // 13. Let map be registration’s installing worker's script resource map.
  // 14. Let usedSet be registration’s installing worker's set of used scripts.
  // 15. For each url of map:
  // 15.1. If usedSet does not contain url, then remove map[url].
  registration->installing_worker()->PurgeScriptResourceMap();

  // 16. If registration’s waiting worker is not null, then:
  if (registration->waiting_worker()) {
    // 16.1. Terminate registration’s waiting worker.
    TerminateServiceWorker(registration->waiting_worker());
    // 16.2. Run the Update Worker State algorithm passing registration’s
    //       waiting worker and "redundant" as the arguments.
    UpdateWorkerState(registration->waiting_worker(),
                      kServiceWorkerStateRedundant);
  }
  // 17. Run the Update Registration State algorithm passing registration,
  //     "waiting" and registration’s installing worker as the arguments.
  UpdateRegistrationState(registration, kWaiting,
                          registration->installing_worker());
  // 18. Run the Update Registration State algorithm passing registration,
  //     "installing" and null as the arguments.
  UpdateRegistrationState(registration, kInstalling, nullptr);
  // 19. Run the Update Worker State algorithm passing registration’s waiting
  //     worker and "installed" as the arguments.
  UpdateWorkerState(registration->waiting_worker(),
                    kServiceWorkerStateInstalled);
  // 20. Invoke Finish Job with job.
  FinishJob(job);
  // 21. Wait for all the tasks queued by Update Worker State invoked in this
  //     algorithm to have executed.
  // TODO(b/234788479): Wait for tasks.
  // 22. Invoke Try Activate with registration.
  TryActivate(registration);
}

bool ServiceWorkerJobs::IsAnyClientUsingRegistration(
    scoped_refptr<ServiceWorkerRegistrationObject> registration) {
  bool any_client_is_using = false;
  for (auto& context : web_context_registrations_) {
    // When a service worker client is controlled by a service worker, it is
    // said that the service worker client is using the service worker’s
    // containing service worker registration.
    //   https://w3c.github.io/ServiceWorker/#dfn-control
    if (context->is_controlled_by(registration->active_worker())) {
      any_client_is_using = true;
      break;
    }
  }
  return any_client_is_using;
}

void ServiceWorkerJobs::TryActivate(
    scoped_refptr<ServiceWorkerRegistrationObject> registration) {
  TRACE_EVENT0("cobalt::worker", "ServiceWorkerJobs::TryActivate()");
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  // Algorithm for Try Activate:
  //   https://w3c.github.io/ServiceWorker/#try-activate-algorithm

  // 1. If registration’s waiting worker is null, return.
  if (!registration) return;
  if (!registration->waiting_worker()) return;

  // 2. If registration’s active worker is not null and registration’s active
  //    worker's state is "activating", return.
  if (registration->active_worker() &&
      (registration->active_worker()->state() == kServiceWorkerStateActivating))
    return;

  // 3. Invoke Activate with registration if either of the following is true:

  //    - registration’s active worker is null.
  bool invoke_activate = registration->active_worker() == nullptr;

  if (!invoke_activate) {
    //    - The result of running Service Worker Has No Pending Events with
    //      registration’s active worker is true...
    if (ServiceWorkerHasNoPendingEvents(registration->active_worker())) {
      //      ... and no service worker client is using registration...
      bool any_client_using = IsAnyClientUsingRegistration(registration);
      invoke_activate = !any_client_using;
      //      ... or registration’s waiting worker's skip waiting flag is
      //      set.
      if (!invoke_activate && registration->waiting_worker()->skip_waiting())
        invoke_activate = true;
    }
  }

  if (invoke_activate) Activate(registration);
}

void ServiceWorkerJobs::Activate(
    scoped_refptr<ServiceWorkerRegistrationObject> registration) {
  TRACE_EVENT0("cobalt::worker", "ServiceWorkerJobs::Activate()");
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  // Algorithm for Activate:
  //   https://w3c.github.io/ServiceWorker/#activation-algorithm

  // 1. If registration’s waiting worker is null, abort these steps.
  if (registration->waiting_worker() == nullptr) return;
  // 2. If registration’s active worker is not null, then:
  if (registration->active_worker()) {
    // 2.1. Terminate registration’s active worker.
    TerminateServiceWorker(registration->active_worker());
    // 2.2. Run the Update Worker State algorithm passing registration’s active
    //      worker and "redundant" as the arguments.
    UpdateWorkerState(registration->active_worker(),
                      kServiceWorkerStateRedundant);
  }
  // 3. Run the Update Registration State algorithm passing registration,
  //    "active" and registration’s waiting worker as the arguments.
  UpdateRegistrationState(registration, kActive,
                          registration->waiting_worker());
  // 4. Run the Update Registration State algorithm passing registration,
  //    "waiting" and null as the arguments.
  UpdateRegistrationState(registration, kWaiting, nullptr);
  // 5. Run the Update Worker State algorithm passing registration’s active
  //    worker and "activating" as the arguments.
  UpdateWorkerState(registration->active_worker(),
                    kServiceWorkerStateActivating);
  // 6. Let matchedClients be a list of service worker clients whose creation
  //    URL matches registration’s storage key and registration’s scope url.
  std::list<web::Context*> matched_clients;
  for (auto& context : web_context_registrations_) {
    url::Origin context_storage_key =
        url::Origin::Create(context->environment_settings()->creation_url());
    scoped_refptr<ServiceWorkerRegistrationObject> matched_registration =
        scope_to_registration_map_.MatchServiceWorkerRegistration(
            context_storage_key, registration->scope_url());
    if (matched_registration == registration) {
      matched_clients.push_back(context);
    }
  }
  // 7. For each client of matchedClients, queue a task on client’s  responsible
  //    event loop, using the DOM manipulation task source, to run the following
  //    substeps:
  for (auto& context : matched_clients) {
    // 7.1. Let readyPromise be client’s global object's
    //      ServiceWorkerContainer object’s ready
    //      promise.
    // 7.2. If readyPromise is null, then continue.
    // 7.3. If readyPromise is pending, resolve
    //      readyPromise with the the result of getting
    //      the service worker registration object that
    //      represents registration in readyPromise’s
    //      relevant settings object.
    context->message_loop()->task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(&ServiceWorkerContainer::MaybeResolveReadyPromise,
                       base::Unretained(context->GetWindowOrWorkerGlobalScope()
                                            ->navigator_base()
                                            ->service_worker()
                                            .get()),
                       registration));
  }
  // 8. For each client of matchedClients:
  // 8.1. If client is a window client, unassociate client’s responsible
  //      document from its application cache, if it has one.
  // 8.2. Else if client is a shared worker client, unassociate client’s
  //      global object from its application cache, if it has one.
  // Cobalt doesn't implement 'application cache':
  //   https://www.w3.org/TR/2011/WD-html5-20110525/offline.html#applicationcache
  // 9. For each service worker client client who is using registration:
  for (auto& context : web_context_registrations_) {
    web::EnvironmentSettings* client = context->environment_settings();
    // When a service worker client is controlled by a service worker, it is
    // said that the service worker client is using the service worker’s
    // containing service worker registration.
    //   https://w3c.github.io/ServiceWorker/#dfn-control
    if (context->is_controlled_by(registration->active_worker())) {
      // 9.1. Set client’s active worker to registration’s active worker.
      client->context()->set_active_service_worker(
          registration->active_worker());
      // 9.2. Invoke Notify Controller Change algorithm with client as the
      //      argument.
      NotifyControllerChange(client);
    }
  }
  // 10. Let activeWorker be registration’s active worker.
  ServiceWorkerObject* active_worker = registration->active_worker();
  // 11. If the result of running the Should Skip Event algorithm with
  //     activeWorker and "activate" is false, then:
  if (!ShouldSkipEvent(base::Tokens::activate(), active_worker)) {
    // 11.1. If the result of running the Run Service Worker algorithm with
    //       activeWorker is not failure, then:
    auto* run_result = RunServiceWorker(active_worker);
    if (run_result) {
      // 11.1.1. Queue a task task on activeWorker’s event loop using the DOM
      //         manipulation task source to run the following steps:
      DCHECK_EQ(active_worker->web_agent()->context(),
                active_worker->worker_global_scope()
                    ->environment_settings()
                    ->context());
      active_worker->web_agent()
          ->context()
          ->message_loop()
          ->task_runner()
          ->PostBlockingTask(
              FROM_HERE,
              base::Bind(
                  [](ServiceWorkerObject* active_worker) {
                    // 11.1.1.1. Let e be the result of creating an event with
                    //           ExtendableEvent.
                    // TODO(b/228976500): implement this as ExtendableEvent.
                    // 11.1.1.2. Initialize e’s type attribute to activate.
                    // 11.1.1.3. Dispatch e at activeWorker’s global object.
                    active_worker->worker_global_scope()->DispatchEvent(
                        new web::Event(base::Tokens::activate()));
                    // 11.1.1.4. WaitForAsynchronousExtensions: Wait, in
                    //           parallel, until e is not active.
                  },
                  base::Unretained(active_worker)));
      // 11.1.2. Wait for task to have executed or been discarded.
      // Waiting is done inside PostBlockingTask above.
      // 11.1.3. Wait for the step labeled WaitForAsynchronousExtensions to
      //         complete.
      NOTIMPLEMENTED();
    }
  }
  // 12. Run the Update Worker State algorithm passing registration’s active
  //     worker and "activated" as the arguments.
  UpdateWorkerState(registration->active_worker(),
                    kServiceWorkerStateActivated);
}

void ServiceWorkerJobs::NotifyControllerChange(
    web::EnvironmentSettings* client) {
  // Algorithm for Notify Controller Change:
  // https://w3c.github.io/ServiceWorker/#notify-controller-change-algorithm
  // 1. Assert: client is not null.
  DCHECK(client);

  // 2. If client is an environment settings object, queue a task to fire an
  //    event named controllerchange at the ServiceWorkerContainer object that
  //    client is associated with.
  client->context()->message_loop()->task_runner()->PostTask(
      FROM_HERE, base::Bind(
                     [](web::EnvironmentSettings* client) {
                       client->context()
                           ->GetWindowOrWorkerGlobalScope()
                           ->navigator_base()
                           ->service_worker()
                           ->DispatchEvent(new web::Event(
                               base::Tokens::controllerchange()));
                     },
                     client));
}

bool ServiceWorkerJobs::ServiceWorkerHasNoPendingEvents(
    ServiceWorkerObject* worker) {
  // Algorithm for Service Worker Has No Pending Events
  //   https://w3c.github.io/ServiceWorker/#service-worker-has-no-pending-events
  // TODO(b/228976500): implement this from ExtendableEvent support.
  NOTIMPLEMENTED();

  // 1. For each event of worker’s set of extended events:
  // 1.1. If event is active, return false.
  // 2. Return true.
  return true;
}

void ServiceWorkerJobs::ClearRegistration(
    scoped_refptr<ServiceWorkerRegistrationObject> registration) {
  TRACE_EVENT0("cobalt::worker", "ServiceWorkerJobs::ClearRegistration()");
  // Algorithm for Clear Registration:
  //   https://w3c.github.io/ServiceWorker/#clear-registration-algorithm
  // 1. Run the following steps atomically.
  DCHECK_EQ(message_loop(), base::MessageLoop::current());

  // 2. If registration’s installing worker is not null, then:
  ServiceWorkerObject* installing_worker = registration->installing_worker();
  if (installing_worker) {
    // 2.1. Terminate registration’s installing worker.
    TerminateServiceWorker(installing_worker);
    // 2.2. Run the Update Worker State algorithm passing registration’s
    //      installing worker and "redundant" as the arguments.
    UpdateWorkerState(installing_worker, kServiceWorkerStateRedundant);
    // 2.3. Run the Update Registration State algorithm passing registration,
    //      "installing" and null as the arguments.
    UpdateRegistrationState(registration, kInstalling, nullptr);
  }

  // 3. If registration’s waiting worker is not null, then:
  ServiceWorkerObject* waiting_worker = registration->waiting_worker();
  if (waiting_worker) {
    // 3.1. Terminate registration’s waiting worker.
    TerminateServiceWorker(waiting_worker);
    // 3.2. Run the Update Worker State algorithm passing registration’s
    //      waiting worker and "redundant" as the arguments.
    UpdateWorkerState(waiting_worker, kServiceWorkerStateRedundant);
    // 3.3. Run the Update Registration State algorithm passing registration,
    //      "waiting" and null as the arguments.
    UpdateRegistrationState(registration, kWaiting, nullptr);
  }

  // 3. If registration’s active worker is not null, then:
  ServiceWorkerObject* active_worker = registration->active_worker();
  if (active_worker) {
    // 3.1. Terminate registration’s active worker.
    TerminateServiceWorker(active_worker);
    // 3.2. Run the Update Worker State algorithm passing registration’s
    //      active worker and "redundant" as the arguments.
    UpdateWorkerState(active_worker, kServiceWorkerStateRedundant);
    // 3.3. Run the Update Registration State algorithm passing registration,
    //      "active" and null as the arguments.
    UpdateRegistrationState(registration, kActive, nullptr);
  }
}

void ServiceWorkerJobs::TryClearRegistration(
    scoped_refptr<ServiceWorkerRegistrationObject> registration) {
  TRACE_EVENT0("cobalt::worker", "ServiceWorkerJobs::TryClearRegistration()");
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  // Algorithm for Try Clear Registration:
  //   https://w3c.github.io/ServiceWorker/#try-clear-registration-algorithm

  // 1. Invoke Clear Registration with registration if no service worker client
  // is using registration and all of the following conditions are true:
  if (IsAnyClientUsingRegistration(registration)) return;

  //    . registration’s installing worker is null or the result of running
  //      Service Worker Has No Pending Events with registration’s installing
  //      worker is true.
  if (registration->installing_worker() &&
      !ServiceWorkerHasNoPendingEvents(registration->installing_worker()))
    return;

  //    . registration’s waiting worker is null or the result of running
  //      Service Worker Has No Pending Events with registration’s waiting
  //      worker is true.
  if (registration->waiting_worker() &&
      !ServiceWorkerHasNoPendingEvents(registration->waiting_worker()))
    return;

  //    . registration’s active worker is null or the result of running
  //      ServiceWorker Has No Pending Events with registration’s active worker
  //      is true.
  if (registration->active_worker() &&
      !ServiceWorkerHasNoPendingEvents(registration->active_worker()))
    return;

  ClearRegistration(registration);
}

void ServiceWorkerJobs::UpdateRegistrationState(
    scoped_refptr<ServiceWorkerRegistrationObject> registration,
    RegistrationState target, scoped_refptr<ServiceWorkerObject> source) {
  TRACE_EVENT2("cobalt::worker", "ServiceWorkerJobs::UpdateRegistrationState()",
               "target", target, "source", source);
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  DCHECK(registration);
  // Algorithm for Update Registration State:
  //   https://w3c.github.io/ServiceWorker/#update-registration-state-algorithm

  // 1. Let registrationObjects be an array containing all the
  //    ServiceWorkerRegistration objects associated with registration.
  // This is implemented with a call to LookupServiceWorkerRegistration for each
  // registered web context.

  switch (target) {
    // 2. If target is "installing", then:
    case kInstalling: {
      // 2.1. Set registration’s installing worker to source.
      registration->set_installing_worker(source);
      // 2.2. For each registrationObject in registrationObjects:
      for (auto& context : web_context_registrations_) {
        // 2.2.1. Queue a task to...
        context->message_loop()->task_runner()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](web::Context* context,
                   scoped_refptr<ServiceWorkerRegistrationObject>
                       registration) {
                  // 2.2.1. ... set the installing attribute of
                  //        registrationObject to null if registration’s
                  //        installing worker is null, or the result of getting
                  //        the service worker object that represents
                  //        registration’s installing worker in
                  //        registrationObject’s relevant settings object.
                  auto registration_object =
                      context->LookupServiceWorkerRegistration(registration);
                  if (registration_object) {
                    registration_object->set_installing(
                        context->GetServiceWorker(
                            registration->installing_worker()));
                  }
                },
                context, registration));
      }
      break;
    }
    // 3. Else if target is "waiting", then:
    case kWaiting: {
      // 3.1. Set registration’s waiting worker to source.
      registration->set_waiting_worker(source);
      // 3.2. For each registrationObject in registrationObjects:
      for (auto& context : web_context_registrations_) {
        // 3.2.1. Queue a task to...
        context->message_loop()->task_runner()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](web::Context* context,
                   scoped_refptr<ServiceWorkerRegistrationObject>
                       registration) {
                  // 3.2.1. ... set the waiting attribute of registrationObject
                  //        to null if registration’s waiting worker is null, or
                  //        the result of getting the service worker object that
                  //        represents registration’s waiting worker in
                  //        registrationObject’s relevant settings object.
                  auto registration_object =
                      context->LookupServiceWorkerRegistration(registration);
                  if (registration_object) {
                    registration_object->set_waiting(context->GetServiceWorker(
                        registration->waiting_worker()));
                  }
                },
                context, registration));
      }
      break;
    }
    // 4. Else if target is "active", then:
    case kActive: {
      // 4.1. Set registration’s active worker to source.
      registration->set_active_worker(source);
      // 4.2. For each registrationObject in registrationObjects:
      for (auto& context : web_context_registrations_) {
        // 4.2.1. Queue a task to...
        context->message_loop()->task_runner()->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](web::Context* context,
                   scoped_refptr<ServiceWorkerRegistrationObject>
                       registration) {
                  // 4.2.1. ... set the active attribute of registrationObject
                  //        to null if registration’s active worker is null, or
                  //        the result of getting the service worker object that
                  //        represents registration’s active worker in
                  //        registrationObject’s relevant settings object.
                  auto registration_object =
                      context->LookupServiceWorkerRegistration(registration);
                  if (registration_object) {
                    registration_object->set_active(context->GetServiceWorker(
                        registration->active_worker()));
                  }
                },
                context, registration));
      }
      break;
    }
    default:
      NOTREACHED();
  }
}

void ServiceWorkerJobs::UpdateWorkerState(ServiceWorkerObject* worker,
                                          ServiceWorkerState state) {
  TRACE_EVENT1("cobalt::worker", "ServiceWorkerJobs::UpdateWorkerState()",
               "state", state);
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  DCHECK(worker);
  if (!worker) {
    return;
  }
  // Algorithm for Update Worker State:
  //   https://w3c.github.io/ServiceWorker/#update-state-algorithm
  // 1. Assert: state is not "parsed".
  DCHECK_NE(kServiceWorkerStateParsed, state);
  // 2. Set worker's state to state.
  worker->set_state(state);
  auto worker_origin = worker->script_url().GetOrigin();
  // 3. Let settingsObjects be all environment settings objects whose origin is
  //    worker's script url's origin.
  // 4. For each settingsObject of settingsObjects...
  for (auto& context : web_context_registrations_) {
    if (context->environment_settings()->GetOrigin() == worker_origin) {
      // 4. ... queue a task on
      //    settingsObject's responsible event loop in the DOM manipulation task
      //    source to run the following steps:
      context->message_loop()->task_runner()->PostTask(
          FROM_HERE,
          base::BindOnce(
              [](web::Context* context, ServiceWorkerObject* worker,
                 ServiceWorkerState state) {
                DCHECK_EQ(context->message_loop(),
                          base::MessageLoop::current());
                // 4.1. Let objectMap be settingsObject's service worker object
                //      map.
                // 4.2. If objectMap[worker] does not exist, then abort these
                //      steps.
                // 4.3. Let  workerObj be objectMap[worker].
                auto worker_obj = context->LookupServiceWorker(worker);
                if (worker_obj) {
                  // 4.4. Set workerObj's state to state.
                  worker_obj->set_state(state);
                  // 4.5. Fire an event named statechange at workerObj.
                  context->message_loop()->task_runner()->PostTask(
                      FROM_HERE,
                      base::BindOnce(
                          [](scoped_refptr<ServiceWorker> worker_obj) {
                            worker_obj->DispatchEvent(
                                new web::Event(base::Tokens::statechange()));
                          },
                          worker_obj));
                }
              },
              context, base::Unretained(worker), state));
    }
  }
}

bool ServiceWorkerJobs::ShouldSkipEvent(base::Token event_name,
                                        ServiceWorkerObject* worker) {
  // Algorithm for Should Skip Event:
  //   https://w3c.github.io/ServiceWorker/#should-skip-event-algorithm
  // TODO(b/229622132): Implementing this algorithm will improve performance.
  NOTIMPLEMENTED();
  return false;
}

void ServiceWorkerJobs::HandleServiceWorkerClientUnload(
    web::EnvironmentSettings* client) {
  TRACE_EVENT0("cobalt::worker",
               "ServiceWorkerJobs::HandleServiceWorkerClientUnload()");
  // Algorithm for Handle Servicer Worker Client Unload:
  //   https://w3c.github.io/ServiceWorker/#on-user-agent-shutdown-algorithm
  DCHECK(client);
  // 1. Run the following steps atomically.
  DCHECK_EQ(message_loop(), base::MessageLoop::current());

  // 2. Let registration be the service worker registration used by client.
  // 3. If registration is null, abort these steps.
  ServiceWorkerObject* active_service_worker =
      client->context()->active_service_worker();
  if (!active_service_worker) return;
  ServiceWorkerRegistrationObject* registration =
      active_service_worker->containing_service_worker_registration();
  if (!registration) return;

  // 4. If any other service worker client is using registration, abort these
  //    steps.
  // Ensure the client is already removed from the registrations when this runs.
  DCHECK(web_context_registrations_.end() ==
         web_context_registrations_.find(client->context()));
  if (IsAnyClientUsingRegistration(registration)) return;

  // 5. If registration is unregistered, invoke Try Clear Registration with
  //    registration.
  if (scope_to_registration_map_.IsUnregistered(registration)) {
    TryClearRegistration(registration);
  }

  // 6. Invoke Try Activate with registration.
  TryActivate(registration);
}

void ServiceWorkerJobs::TerminateServiceWorker(ServiceWorkerObject* worker) {
  TRACE_EVENT0("cobalt::worker", "ServiceWorkerJobs::TerminateServiceWorker()");
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  // Algorithm for Terminate Service Worker:
  //   https://w3c.github.io/ServiceWorker/#terminate-service-worker
  // 1. Run the following steps in parallel with serviceWorker’s main loop:
  // This runs in the ServiceWorkerRegistry thread.
  DCHECK_EQ(message_loop(), base::MessageLoop::current());

  // 1.1. Let serviceWorkerGlobalScope be serviceWorker’s global object.
  WorkerGlobalScope* service_worker_global_scope =
      worker->worker_global_scope();

  // 1.2. Set serviceWorkerGlobalScope’s closing flag to true.
  service_worker_global_scope->set_closing_flag(true);

  // 1.3. Remove all the items from serviceWorker’s set of extended events.
  // TODO(b/228976500): implement this with ExtendableEvent support.

  // 1.4. If there are any tasks, whose task source is either the handle fetch
  //      task source or the handle functional event task source, queued in
  //      serviceWorkerGlobalScope’s event loop’s task queues, queue them to
  //      serviceWorker’s containing service worker registration’s corresponding
  //      task queues in the same order using their original task sources, and
  //      discard all the tasks (including tasks whose task source is neither
  //      the handle fetch task source nor the handle functional event task
  //      source) from serviceWorkerGlobalScope’s event loop’s task queues
  //      without processing them.
  // TODO(b/234787641): Queue tasks to the registration.

  // Note: This step is not in the spec, but without this step the service
  // worker object map will always keep an entry with a service worker instance
  // for the terminated service worker, which besides leaking memory can lead to
  // unexpected behavior when new service worker objects are created with the
  // same key for the service worker object map (which in Cobalt's case
  // happens when a new service worker object is constructed at the same
  // memory address).
  for (auto& context : web_context_registrations_) {
    context->message_loop()->task_runner()->PostBlockingTask(
        FROM_HERE, base::Bind(
                       [](web::Context* context, ServiceWorkerObject* worker) {
                         context->RemoveServiceWorker(worker);
                       },
                       context, base::Unretained(worker)));
  }

  // 1.5. Abort the script currently running in serviceWorker.
  DCHECK(worker->is_running());
  worker->Abort();

  // 1.6. Set serviceWorker’s start status to null.
  worker->set_start_status(nullptr);
}

void ServiceWorkerJobs::Unregister(Job* job) {
  TRACE_EVENT0("cobalt::worker", "ServiceWorkerJobs::Unregister()");
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  // Algorithm for Unregister:
  //   https://w3c.github.io/ServiceWorker/#unregister-algorithm
  // 1. If the origin of job’s scope url is not job’s client's origin, then:
  if (!url::Origin::Create(job->client->GetOrigin())
           .IsSameOriginWith(url::Origin::Create(job->scope_url))) {
    // 1.1. Invoke Reject Job Promise with job and "SecurityError" DOMException.
    RejectJobPromise(
        job,
        PromiseErrorData(
            web::DOMException::kSecurityErr,
            "Service Worker Unregister failed: Scope origin does not match."));

    // 1.2. Invoke Finish Job with job and abort these steps.
    FinishJob(job);
    return;
  }

  // 2. Let registration be the result of running Get Registration given job’s
  //    storage key and job’s scope url.
  scoped_refptr<ServiceWorkerRegistrationObject> registration =
      scope_to_registration_map_.GetRegistration(job->storage_key,
                                                 job->scope_url);

  // 3. If registration is null, then:
  if (!registration) {
    // 3.1. Invoke Resolve Job Promise with job and false.
    ResolveJobPromise(job, false);

    // 3.2. Invoke Finish Job with job and abort these steps.
    FinishJob(job);
    return;
  }

  // 4. Remove registration map[(registration’s storage key, job’s scope url)].
  // Keep the registration until this algorithm finishes.
  scope_to_registration_map_.RemoveRegistration(registration->storage_key(),
                                                job->scope_url);

  // 5. Invoke Resolve Job Promise with job and true.
  ResolveJobPromise(job, true);

  // 6. Invoke Try Clear Registration with registration.
  TryClearRegistration(registration);

  // 7. Invoke Finish Job with job.
  FinishJob(job);
}

void ServiceWorkerJobs::RejectJobPromise(Job* job,
                                         const PromiseErrorData& error_data) {
  TRACE_EVENT0("cobalt::worker", "ServiceWorkerJobs::RejectJobPromise()");
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  // Algorithm for Reject Job Promise:
  //   https://w3c.github.io/ServiceWorker/#reject-job-promise
  // 1. If job’s client is not null, queue a task, on job’s client's responsible
  //    event loop using the DOM manipulation task source, to reject job’s job
  //    promise with a new exception with errorData and a user agent-defined
  //    message, in job’s client's Realm.

  auto reject_task = [](std::unique_ptr<JobPromiseType> promise,
                        const PromiseErrorData& error_data) {
    error_data.Reject(std::move(promise));
  };

  if (job->client) {
    base::AutoLock lock(job->equivalent_jobs_promise_mutex);
    job->client->context()->message_loop()->task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(reject_task, std::move(job->promise), error_data));
    // Ensure that the promise is cleared, so that equivalent jobs won't get
    // added from this point on.
    CHECK(!job->promise);
  }
  // 2. For each equivalentJob in job’s list of equivalent jobs:
  for (auto& equivalent_job : job->equivalent_jobs) {
    // Equivalent jobs should never have equivalent jobs of their own.
    DCHECK(equivalent_job->equivalent_jobs.empty());

    // 2.1. If equivalentJob’s client is null, continue.
    if (equivalent_job->client) {
      // 2.2. Queue a task, on equivalentJob’s client's responsible event loop
      //      using the DOM manipulation task source, to reject equivalentJob’s
      //      job promise with a new exception with errorData and a user
      //      agent-defined message, in equivalentJob’s client's Realm.
      equivalent_job->client->context()
          ->message_loop()
          ->task_runner()
          ->PostTask(
              FROM_HERE,
              base::BindOnce(reject_task, std::move(equivalent_job->promise),
                             error_data));
      // Check that the promise is cleared.
      CHECK(!equivalent_job->promise);
    }
  }
  job->equivalent_jobs.clear();
}

void ServiceWorkerJobs::ResolveJobPromise(
    Job* job, bool value,
    scoped_refptr<ServiceWorkerRegistrationObject> registration) {
  TRACE_EVENT0("cobalt::worker", "ServiceWorkerJobs::ResolveJobPromise()");
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  DCHECK(job);
  // Algorithm for Resolve Job Promise:
  //   https://w3c.github.io/ServiceWorker/#resolve-job-promise-algorithm

  // 1. If job’s client is not null, queue a task, on job’s client's responsible
  //    event loop using the DOM manipulation task source, to run the following
  //    substeps:
  auto resolve_task =
      [](JobType type, web::EnvironmentSettings* client,
         std::unique_ptr<JobPromiseType> promise, bool value,
         scoped_refptr<ServiceWorkerRegistrationObject> registration) {
        TRACE_EVENT0("cobalt::worker",
                     "ServiceWorkerJobs::ResolveJobPromise() ResolveTask");
        // 1.1./2.2.1. Let convertedValue be null.
        // 1.2./2.2.2. If job’s job type is either register or update, set
        //             convertedValue to the result of getting the service
        //             worker registration object that represents value in job’s
        //             client.
        if (type == kRegister || type == kUpdate) {
          scoped_refptr<cobalt::script::Wrappable> converted_value =
              client->context()->GetServiceWorkerRegistration(registration);
          // 1.4./2.2.4. Resolve job’s job promise with convertedValue.
          promise->Resolve(converted_value);
        } else {
          DCHECK_EQ(kUnregister, type);
          // 1.3./2.2.3. Else, set convertedValue to value, in job’s client's
          //             Realm.
          bool converted_value = value;
          // 1.4./2.2.4. Resolve job’s job promise with convertedValue.
          promise->Resolve(converted_value);
        }
      };

  if (job->client) {
    base::AutoLock lock(job->equivalent_jobs_promise_mutex);
    job->client->context()->message_loop()->task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(resolve_task, job->type, job->client,
                       std::move(job->promise), value, registration));
    // Ensure that the promise is cleared, so that equivalent jobs won't get
    // added from this point on.
    CHECK(!job->promise);
  }

  // 2. For each equivalentJob in job’s list of equivalent jobs:
  for (auto& equivalent_job : job->equivalent_jobs) {
    // Equivalent jobs should never have equivalent jobs of their own.
    DCHECK(equivalent_job->equivalent_jobs.empty());

    // 2.1. If equivalentJob’s client is null, continue to the next iteration of
    //      the loop.
    if (equivalent_job->client) {
      // 2.2. Queue a task, on equivalentJob’s client's responsible event loop
      //      using the DOM manipulation task source, to run the following
      //      substeps:
      equivalent_job->client->context()
          ->message_loop()
          ->task_runner()
          ->PostTask(FROM_HERE,
                     base::BindOnce(resolve_task, equivalent_job->type,
                                    equivalent_job->client,
                                    std::move(equivalent_job->promise), value,
                                    registration));
      // Check that the promise is cleared.
      CHECK(!equivalent_job->promise);
    }
  }
  job->equivalent_jobs.clear();
}

// https://w3c.github.io/ServiceWorker/#finish-job-algorithm
void ServiceWorkerJobs::FinishJob(Job* job) {
  TRACE_EVENT0("cobalt::worker", "ServiceWorkerJobs::FinishJob()");
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  // 1. Let jobQueue be job’s containing job queue.
  JobQueue* job_queue = job->containing_job_queue;

  // 2. Assert: the first item in jobQueue is job.
  DCHECK_EQ(job, job_queue->FirstItem());

  // 3. Dequeue from jobQueue.
  job_queue->Dequeue();

  // 4. If jobQueue is not empty, invoke Run Job with jobQueue.
  if (!job_queue->empty()) {
    RunJob(job_queue);
  }
}

void ServiceWorkerJobs::MaybeResolveReadyPromiseSubSteps(
    web::EnvironmentSettings* client) {
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  // Algorithm for Sub steps of ServiceWorkerContainer.ready():
  //   https://w3c.github.io/ServiceWorker/#navigator-service-worker-ready

  //    3.1. Let client by this's service worker client.
  //    3.2. Let storage key be the result of running obtain a storage
  //         key given client.
  url::Origin storage_key = client->ObtainStorageKey();
  //    3.3. Let registration be the result of running Match Service
  //         Worker Registration given storage key and client’s
  //         creation URL.
  // TODO(b/234659851): Investigate whether this should use the creation URL
  // directly instead.
  const GURL& base_url = client->creation_url();
  GURL client_url = base_url.Resolve("");
  scoped_refptr<ServiceWorkerRegistrationObject> registration =
      scope_to_registration_map_.MatchServiceWorkerRegistration(storage_key,
                                                                client_url);
  //    3.3. If registration is not null, and registration’s active
  //         worker is not null, queue a task on readyPromise’s
  //         relevant settings object's responsible event loop, using
  //         the DOM manipulation task source, to resolve readyPromise
  //         with the result of getting the service worker
  //         registration object that represents registration in
  //         readyPromise’s relevant settings object.
  if (registration && registration->active_worker()) {
    client->context()->message_loop()->task_runner()->PostTask(
        FROM_HERE,
        base::BindOnce(&ServiceWorkerContainer::MaybeResolveReadyPromise,
                       base::Unretained(client->context()
                                            ->GetWindowOrWorkerGlobalScope()
                                            ->navigator_base()
                                            ->service_worker()
                                            .get()),
                       registration));
  }
}

void ServiceWorkerJobs::GetRegistrationSubSteps(
    const url::Origin& storage_key, const GURL& client_url,
    web::EnvironmentSettings* client,
    std::unique_ptr<script::ValuePromiseWrappable::Reference>
        promise_reference) {
  TRACE_EVENT0("cobalt::worker",
               "ServiceWorkerJobs::GetRegistrationSubSteps()");
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  // Algorithm for Sub steps of ServiceWorkerContainer.getRegistration():
  //   https://w3c.github.io/ServiceWorker/#navigator-service-worker-getRegistration

  // 8.1. Let registration be the result of running Match Service Worker
  //      Registration algorithm with clientURL as its argument.
  scoped_refptr<ServiceWorkerRegistrationObject> registration =
      scope_to_registration_map_.MatchServiceWorkerRegistration(storage_key,
                                                                client_url);
  // 8.2. If registration is null, resolve promise with undefined and abort
  //      these steps.
  // 8.3. Resolve promise with the result of getting the service worker
  //      registration object that represents registration in promise’s
  //      relevant settings object.
  client->context()->message_loop()->task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](web::EnvironmentSettings* settings,
             std::unique_ptr<script::ValuePromiseWrappable::Reference> promise,
             scoped_refptr<ServiceWorkerRegistrationObject> registration) {
            TRACE_EVENT0(
                "cobalt::worker",
                "ServiceWorkerJobs::GetRegistrationSubSteps() Resolve");
            promise->value().Resolve(
                settings->context()->GetServiceWorkerRegistration(
                    registration));
          },
          client, std::move(promise_reference), registration));
}

void ServiceWorkerJobs::SkipWaitingSubSteps(
    web::EnvironmentSettings* client,
    const base::WeakPtr<ServiceWorkerObject>& service_worker,
    std::unique_ptr<script::ValuePromiseVoid::Reference> promise_reference) {
  TRACE_EVENT0("cobalt::worker", "ServiceWorkerJobs::SkipWaitingSubSteps()");
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  // Algorithm for Sub steps of ServiceWorkerGlobalScope.skipWaiting():
  //   https://w3c.github.io/ServiceWorker/#dom-serviceworkerglobalscope-skipwaiting

  // 2.1. Set service worker's skip waiting flag.
  service_worker->set_skip_waiting();

  // 2.2. Invoke Try Activate with service worker's containing service worker
  // registration.
  TryActivate(service_worker->containing_service_worker_registration());

  // 2.3. Resolve promise with undefined.
  client->context()->message_loop()->task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](std::unique_ptr<script::ValuePromiseVoid::Reference> promise) {
            promise->value().Resolve();
          },
          std::move(promise_reference)));
}

void ServiceWorkerJobs::ClientsGetSubSteps(
    web::EnvironmentSettings* settings,
    ServiceWorkerObject* associated_service_worker,
    std::unique_ptr<script::ValuePromiseWrappable::Reference> promise_reference,
    const std::string& id) {
  TRACE_EVENT0("cobalt::worker", "ServiceWorkerJobs::ClientsGetSubSteps()");
  DCHECK_EQ(message_loop_, base::MessageLoop::current());
  // Parallel sub steps (2) for algorithm for Clients.get(id):
  //   https://w3c.github.io/ServiceWorker/#clients-get
  // 2.1. For each service worker client client where the result of running
  //      obtain a storage key given client equals the associated service
  //      worker's containing service worker registration's storage key:
  const url::Origin& storage_key =
      associated_service_worker->containing_service_worker_registration()
          ->storage_key();
  for (auto& context : web_context_registrations_) {
    web::EnvironmentSettings* client = context->environment_settings();
    url::Origin client_storage_key = client->ObtainStorageKey();
    if (client_storage_key.IsSameOriginWith(storage_key)) {
      // 2.1.1. If client’s id is not id, continue.
      if (client->id() != id) continue;

      // 2.1.2. Wait for either client’s execution ready flag to be set or for
      //        client’s discarded flag to be set.
      // Web Contexts exist only in the web_context_registrations_ set when they
      // are both execution ready and not discarded.

      // 2.1.3. If client’s execution ready flag is set, then invoke Resolve Get
      //        Client Promise with client and promise, and abort these steps.
      ResolveGetClientPromise(client, settings, std::move(promise_reference));
      return;
    }
  }
  // 2.2. Resolve promise with undefined.
  settings->context()->message_loop()->task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](std::unique_ptr<script::ValuePromiseWrappable::Reference>
                 promise_reference) {
            TRACE_EVENT0(
                "cobalt::worker",
                "ServiceWorkerJobs::ResolveGetClientPromise() Resolve");
            promise_reference->value().Resolve(scoped_refptr<Client>());
          },
          std::move(promise_reference)));
}

void ServiceWorkerJobs::ResolveGetClientPromise(
    web::EnvironmentSettings* client,
    web::EnvironmentSettings* promise_relevant_settings,
    std::unique_ptr<script::ValuePromiseWrappable::Reference>
        promise_reference) {
  TRACE_EVENT0("cobalt::worker",
               "ServiceWorkerJobs::ResolveGetClientPromise()");
  // Algorithm for Resolve Get Client Promise:
  //   https://w3c.github.io/ServiceWorker/#resolve-get-client-promise

  // 1. If client is an environment settings object, then:
  // 1.1. If client is not a secure context, queue a task to reject promise with
  //      a "SecurityError" DOMException, on promise’s relevant settings
  //      object's responsible event loop using the DOM manipulation task
  //      source, and abort these steps.
  // 2. Else:
  // 2.1. If client’s creation URL is not a potentially trustworthy URL, queue
  //      a task to reject promise with a "SecurityError" DOMException, on
  //      promise’s relevant settings object's responsible event loop using the
  //      DOM manipulation task source, and abort these steps.
  // In production, Cobalt requires https, therefore all clients are secure
  // contexts.

  // 3. If client is an environment settings object and is not a window client,
  //    then:
  if (!client->context()->GetWindowOrWorkerGlobalScope()->IsWindow()) {
    // 3.1. Let clientObject be the result of running Create Client algorithm
    //      with client as the argument.
    scoped_refptr<Client> client_object = Client::Create(client);

    // 3.2. Queue a task to resolve promise with clientObject, on promise’s
    //      relevant settings object's responsible event loop using the DOM
    //      manipulation task source, and abort these steps.
    promise_relevant_settings->context()
        ->message_loop()
        ->task_runner()
        ->PostTask(
            FROM_HERE,
            base::BindOnce(
                [](std::unique_ptr<script::ValuePromiseWrappable::Reference>
                       promise_reference,
                   scoped_refptr<Client> client_object) {
                  TRACE_EVENT0(
                      "cobalt::worker",
                      "ServiceWorkerJobs::ResolveGetClientPromise() Resolve");
                  promise_reference->value().Resolve(client_object);
                },
                std::move(promise_reference), client_object));
    return;
  }
  // 4. Else:
  // 4.1. Let browsingContext be null.
  // 4.2. If client is an environment settings object, set browsingContext to
  //      client’s global object's browsing context.
  // 4.3. Else, set browsingContext to client’s target browsing context.
  // Note: Cobalt does not implement a distinction between environments and
  // environment settings objects.
  // 4.4. Queue a task to run the following steps on browsingContext’s event
  //      loop using the user interaction task source:
  // Note: The task below does not currently perform any actual
  // functionality in the client context. It is included however to help future
  // implementation for fetching values for WindowClient properties, with
  // similar logic existing in ClientsMatchAllSubSteps.
  client->context()->message_loop()->task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](web::EnvironmentSettings* client,
             web::EnvironmentSettings* promise_relevant_settings,
             std::unique_ptr<script::ValuePromiseWrappable::Reference>
                 promise_reference) {
            std::unique_ptr<WindowData> window_data(new WindowData);
            window_data->client = client;
            // 4.4.1. Let frameType be the result of running Get Frame Type with
            //        browsingContext.
            // Cobalt does not support nested or auxiliary
            // browsing contexts.
            window_data->frame_type = kFrameTypeTopLevel;

            // 4.4.2. Let visibilityState be browsingContext’s active document's
            //        visibilityState attribute value.
            // TODO(b/235838698): Implement WindowClient.visibilityState.

            // 4.4.3. Let focusState be the result of running the has focus
            //        steps with browsingContext’s active document as the
            //        argument.
            // TODO(b/235838698): Implement WindowClient.focused.

            // 4.4.4. Let ancestorOriginsList be the empty list.
            // 4.4.5. If client is a window client, set ancestorOriginsList to
            //        browsingContext’s active document's relevant global
            //        object's Location object’s ancestor origins list's
            //        associated list.
            // Cobalt does not implement Location.ancestorOrigins.

            // 4.4.6. Queue a task to run the following steps on promise’s
            //        relevant settings object's responsible event loop using
            //        the DOM manipulation task source:
            promise_relevant_settings->context()
                ->message_loop()
                ->task_runner()
                ->PostTask(
                    FROM_HERE,
                    base::BindOnce(
                        [](std::unique_ptr<
                               script::ValuePromiseWrappable::Reference>
                               promise_reference,
                           std::unique_ptr<WindowData> window_data) {
                          // 4.4.6.1. If client’s discarded flag is set, resolve
                          //          promise with undefined and abort these
                          //          steps.
                          // 4.4.6.2. Let windowClient be the result of running
                          //          Create Window Client with client,
                          //          frameType, visibilityState, focusState,
                          //          and ancestorOriginsList.
                          scoped_refptr<WindowClient> window_client =
                              WindowClient::Create(*window_data);
                          // 4.4.6.3. Resolve promise with windowClient.
                          promise_reference->value().Resolve(window_client);
                        },
                        std::move(promise_reference), std::move(window_data)));
          },
          client, promise_relevant_settings, std::move(promise_reference)));
  DCHECK_EQ(nullptr, promise_reference.get());
}

void ServiceWorkerJobs::ClientsMatchAllSubSteps(
    web::EnvironmentSettings* settings,
    ServiceWorkerObject* associated_service_worker,
    std::unique_ptr<script::ValuePromiseSequenceWrappable::Reference>
        promise_reference,
    bool include_uncontrolled, ClientType type) {
  TRACE_EVENT0("cobalt::worker",
               "ServiceWorkerJobs::ClientsMatchAllSubSteps()");
  DCHECK_EQ(message_loop_, base::MessageLoop::current());
  // Parallel sub steps (2) for algorithm for Clients.matchAll():
  //   https://w3c.github.io/ServiceWorker/#clients-matchall
  // 2.1. Let targetClients be a new list.
  std::list<web::EnvironmentSettings*> target_clients;

  // 2.2. For each service worker client client where the result of running
  //      obtain a storage key given client equals the associated service
  //      worker's containing service worker registration's storage key:
  const url::Origin& storage_key =
      associated_service_worker->containing_service_worker_registration()
          ->storage_key();
  for (auto& context : web_context_registrations_) {
    web::EnvironmentSettings* client = context->environment_settings();
    url::Origin client_storage_key = client->ObtainStorageKey();
    if (client_storage_key.IsSameOriginWith(storage_key)) {
      // 2.2.1. If client’s execution ready flag is unset or client’s discarded
      //        flag is set, continue.
      // Web Contexts exist only in the web_context_registrations_ set when they
      // are both execution ready and not discarded.

      // 2.2.2. If client is not a secure context, continue.
      // In production, Cobalt requires https, therefore all workers and their
      // owners are secure contexts.

      // 2.2.3. If options["includeUncontrolled"] is false, and if client’s
      //        active service worker is not the associated service worker,
      //        continue.
      if (!include_uncontrolled &&
          (client->context()->active_service_worker() !=
           associated_service_worker)) {
        continue;
      }

      // 2.2.4. Add client to targetClients.
      target_clients.push_back(client);
    }
  }

  // 2.3. Let matchedWindowData be a new list.
  std::unique_ptr<std::vector<WindowData>> matched_window_data(
      new std::vector<WindowData>);

  // 2.4. Let matchedClients be a new list.
  std::unique_ptr<std::vector<web::EnvironmentSettings*>> matched_clients(
      new std::vector<web::EnvironmentSettings*>);

  // 2.5. For each service worker client client in targetClients:
  for (auto* client : target_clients) {
    auto* global_scope = client->context()->GetWindowOrWorkerGlobalScope();

    if ((type == kClientTypeWindow || type == kClientTypeAll) &&
        (global_scope->IsWindow())) {
      // 2.5.1. If options["type"] is "window" or "all", and client is not an
      //        environment settings object or is a window client, then:

      // 2.5.1.1. Let windowData be [ "client" -> client, "ancestorOriginsList"
      //          -> a new list ].
      WindowData window_data;
      window_data.client = client;

      // 2.5.1.2. Let browsingContext be null.

      // 2.5.1.3. Let isClientEnumerable be true.
      // For Cobalt, isClientEnumerable is always true because the clauses that
      // would set it to false in 2.5.1.6. do not apply to Cobalt.

      // 2.5.1.4. If client is an environment settings object, set
      //          browsingContext to client’s global object's browsing context.

      // 2.5.1.5. Else, set browsingContext to client’s target browsing context.
      web::Context* browsing_context = settings->context();

      // 2.5.1.6. Queue a task task to run the following substeps on
      //          browsingContext’s event loop using the user interaction task
      //          source:
      // Note: The task below does not currently perform any actual
      // functionality. It is included however to help future implementation for
      // fetching values for WindowClient properties, with similar logic
      // existing in ResolveGetClientPromise.
      browsing_context->message_loop()->task_runner()->PostBlockingTask(
          FROM_HERE, base::Bind(
                         [](WindowData* window_data) {
                           // 2.5.1.6.1. If browsingContext has been discarded,
                           //            then set isClientEnumerable to false
                           //            and abort these steps.
                           // 2.5.1.6.2. If client is a window client and
                           //            client’s responsible document is not
                           //            browsingContext’s active document, then
                           //            set isClientEnumerable to false and
                           //            abort these steps.
                           // In Cobalt, the document of a window browsing
                           // context doesn't change: When a new document is
                           // created, a new browsing context is created with
                           // it.

                           // 2.5.1.6.3. Set windowData["frameType"] to the
                           //            result of running Get Frame Type with
                           //            browsingContext.
                           // Cobalt does not support nested or auxiliary
                           // browsing contexts.
                           window_data->frame_type = kFrameTypeTopLevel;

                           // 2.5.1.6.4. Set windowData["visibilityState"] to
                           //            browsingContext’s active document's
                           //            visibilityState attribute value.
                           // TODO(b/235838698): Implement
                           // WindowClient.visibilityState.

                           // 2.5.1.6.5. Set windowData["focusState"] to the
                           //            result of running the has focus steps
                           //            with browsingContext’s active document
                           //            as the argument.
                           // TODO(b/235838698): Implement WindowClient.focused.

                           // 2.5.1.6.6. If client is a window client, then set
                           //            windowData["ancestorOriginsList"] to
                           //            browsingContext’s active document's
                           //            relevant global object's Location
                           //            object’s ancestor origins list's
                           //            associated list.
                           // Cobalt does not implement
                           // Location.ancestorOrigins.
                         },
                         &window_data));

      // 2.5.1.7. Wait for task to have executed.
      // The task above is posted as a blocking task.

      // 2.5.1.8. If isClientEnumerable is true, then:

      // 2.5.1.8.1. Add windowData to matchedWindowData.
      matched_window_data->emplace_back(window_data);

      // 2.5.2. Else if options["type"] is "worker" or "all" and client is a
      //        dedicated worker client, or options["type"] is "sharedworker" or
      //        "all" and client is a shared worker client, then:
    } else if (((type == kClientTypeWorker || type == kClientTypeAll) &&
                global_scope->IsDedicatedWorker())) {
      // Note: Cobalt does not support shared workers.
      // 2.5.2.1. Add client to matchedClients.
      matched_clients->emplace_back(client);
    }
  }

  // 2.6. Queue a task to run the following steps on promise’s relevant
  // settings object's responsible event loop using the DOM manipulation
  // task source:
  settings->context()->message_loop()->task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](std::unique_ptr<script::ValuePromiseSequenceWrappable::Reference>
                 promise_reference,
             std::unique_ptr<std::vector<WindowData>> matched_window_data,
             std::unique_ptr<std::vector<web::EnvironmentSettings*>>
                 matched_clients) {
            TRACE_EVENT0(
                "cobalt::worker",
                "ServiceWorkerJobs::ClientsMatchAllSubSteps() Resolve Promise");
            // 2.6.1. Let clientObjects be a new list.
            script::Sequence<scoped_refptr<script::Wrappable>> client_objects;

            // 2.6.2. For each windowData in matchedWindowData:
            for (auto& window_data : *matched_window_data) {
              // 2.6.2.1. Let WindowClient be the result of running
              //          Create Window Client algorithm with
              //          windowData["client"],
              //          windowData["frameType"],
              //          windowData["visibilityState"],
              //          windowData["focusState"], and
              //          windowData["ancestorOriginsList"] as the
              //          arguments.
              // TODO(b/235838698): Implement WindowCLient properties
              // and methods.
              scoped_refptr<WindowClient> window_client =
                  WindowClient::Create(window_data);

              // 2.6.2.2. Append WindowClient to clientObjects.
              client_objects.push_back(window_client);
            }

            // 2.6.3. For each client in matchedClients:
            for (auto& client : *matched_clients) {
              // 2.6.3.1. Let clientObject be the result of running
              //          Create Client algorithm with client as the
              //          argument.
              scoped_refptr<Client> client_object = Client::Create(client);

              // 2.6.3.2. Append clientObject to clientObjects.
              client_objects.push_back(client_object);
            }
            // 2.6.4. Sort clientObjects such that:
            //        . WindowClient objects whose browsing context has been
            //          focused are placed first, sorted in the most recently
            //          focused order.
            //        . WindowClient objects whose browsing context has never
            //          been focused are placed next, sorted in their service
            //          worker client's creation order.
            //        . Client objects whose associated service worker client is
            //          a worker client are placed next, sorted in their service
            //          worker client's creation order.
            // TODO(b/235876598): Implement sorting of clientObjects.

            // 2.6.5. Resolve promise with a new frozen array of clientObjects
            //        in promise’s relevant Realm.
            promise_reference->value().Resolve(client_objects);
          },
          std::move(promise_reference), std::move(matched_window_data),
          std::move(matched_clients)));
}

void ServiceWorkerJobs::ClaimSubSteps(
    web::EnvironmentSettings* settings,
    ServiceWorkerObject* associated_service_worker,
    std::unique_ptr<script::ValuePromiseVoid::Reference> promise_reference) {
  TRACE_EVENT0("cobalt::worker", "ServiceWorkerJobs::ClaimSubSteps()");
  DCHECK_EQ(message_loop_, base::MessageLoop::current());
  // Parallel sub steps (3) for algorithm for Clients.claim():
  //   https://w3c.github.io/ServiceWorker/#dom-clients-claim
  std::list<web::EnvironmentSettings*> target_clients;

  // 3.1. For each service worker client client where the result of running
  //      obtain a storage key given client equals the service worker's
  //      containing service worker registration's storage key:
  const url::Origin& storage_key =
      associated_service_worker->containing_service_worker_registration()
          ->storage_key();
  for (auto& context : web_context_registrations_) {
    web::EnvironmentSettings* client = context->environment_settings();
    // Don't claim to be our own service worker.
    if (client == settings) continue;
    url::Origin client_storage_key = client->ObtainStorageKey();
    if (client_storage_key.IsSameOriginWith(storage_key)) {
      // 3.1.1. If client’s execution ready flag is unset or client’s discarded
      //        flag is set, continue.
      // Web Contexts exist only in the web_context_registrations_ set when they
      // are both execution ready and not discarded.

      // 3.1.2. If client is not a secure context, continue.
      // In production, Cobalt requires https, therefore all clients are secure
      // contexts.

      // 3.1.3. Let storage key be the result of running obtain a storage key
      //        given client.
      // 3.1.4. Let registration be the result of running Match Service Worker
      //        Registration given storage key and client’s creation URL.
      // TODO(b/234659851): Investigate whether this should use the creation
      // URL directly instead.
      const GURL& base_url = client->creation_url();
      GURL client_url = base_url.Resolve("");
      scoped_refptr<ServiceWorkerRegistrationObject> registration =
          scope_to_registration_map_.MatchServiceWorkerRegistration(
              client_storage_key, client_url);

      // 3.1.5. If registration is not the service worker's containing service
      //        worker registration, continue.
      if (registration !=
          associated_service_worker->containing_service_worker_registration()) {
        continue;
      }

      // 3.1.6. If client’s active service worker is not the service worker,
      //        then:
      if (client->context()->active_service_worker() !=
          associated_service_worker) {
        // 3.1.6.1. Invoke Handle Service Worker Client Unload with client as
        //          the argument.
        HandleServiceWorkerClientUnload(client);

        // 3.1.6.2. Set client’s active service worker to service worker.
        client->context()->set_active_service_worker(associated_service_worker);

        // 3.1.6.3. Invoke Notify Controller Change algorithm with client as the
        //          argument.
        NotifyControllerChange(client);
      }
    }
  }
  // 3.2. Resolve promise with undefined.
  settings->context()->message_loop()->task_runner()->PostTask(
      FROM_HERE,
      base::BindOnce(
          [](std::unique_ptr<script::ValuePromiseVoid::Reference> promise) {
            promise->value().Resolve();
          },
          std::move(promise_reference)));
}

void ServiceWorkerJobs::RegisterWebContext(web::Context* context) {
  DCHECK_NE(nullptr, context);
  web_context_registrations_cleared_.Reset();
  if (base::MessageLoop::current() != message_loop()) {
    DCHECK(message_loop());
    message_loop()->task_runner()->PostTask(
        FROM_HERE, base::BindOnce(&ServiceWorkerJobs::RegisterWebContext,
                                  base::Unretained(this), context));
    return;
  }
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  DCHECK_EQ(0, web_context_registrations_.count(context));
  web_context_registrations_.insert(context);
}

void ServiceWorkerJobs::UnregisterWebContext(web::Context* context) {
  DCHECK_NE(nullptr, context);
  if (base::MessageLoop::current() != message_loop()) {
    // Block to ensure that the context is unregistered before it is destroyed.
    DCHECK(message_loop());
    message_loop()->task_runner()->PostBlockingTask(
        FROM_HERE, base::Bind(&ServiceWorkerJobs::UnregisterWebContext,
                              base::Unretained(this), context));
    return;
  }
  DCHECK_EQ(message_loop(), base::MessageLoop::current());
  DCHECK_EQ(1, web_context_registrations_.count(context));
  web_context_registrations_.erase(context);
  HandleServiceWorkerClientUnload(context->environment_settings());
  if (web_context_registrations_.empty()) {
    web_context_registrations_cleared_.Signal();
  }
}

ServiceWorkerJobs::JobPromiseType::JobPromiseType(
    std::unique_ptr<script::ValuePromiseBool::Reference> promise_reference)
    : promise_bool_reference_(std::move(promise_reference)) {}
ServiceWorkerJobs::JobPromiseType::JobPromiseType(
    std::unique_ptr<script::ValuePromiseWrappable::Reference> promise_reference)
    : promise_wrappable_reference_(std::move(promise_reference)) {}

void ServiceWorkerJobs::JobPromiseType::Resolve(const bool result) {
  DCHECK(promise_bool_reference_);
  promise_bool_reference_->value().Resolve(result);
}

void ServiceWorkerJobs::JobPromiseType::Resolve(
    const scoped_refptr<cobalt::script::Wrappable>& result) {
  DCHECK(promise_wrappable_reference_);
  promise_wrappable_reference_->value().Resolve(result);
}

void ServiceWorkerJobs::JobPromiseType::Reject(
    script::SimpleExceptionType exception) {
  if (promise_bool_reference_) {
    promise_bool_reference_->value().Reject(exception);
    return;
  }
  if (promise_wrappable_reference_) {
    promise_wrappable_reference_->value().Reject(exception);
    return;
  }
  NOTREACHED();
}

void ServiceWorkerJobs::JobPromiseType::Reject(
    const scoped_refptr<script::ScriptException>& result) {
  if (promise_bool_reference_) {
    promise_bool_reference_->value().Reject(result);
    return;
  }
  if (promise_wrappable_reference_) {
    promise_wrappable_reference_->value().Reject(result);
    return;
  }
  NOTREACHED();
}

script::PromiseState ServiceWorkerJobs::JobPromiseType::State() {
  if (promise_bool_reference_) {
    return promise_bool_reference_->value().State();
  }
  if (promise_wrappable_reference_) {
    return promise_wrappable_reference_->value().State();
  }
  NOTREACHED();
  return script::PromiseState::kPending;
}

}  // namespace worker
}  // namespace cobalt
