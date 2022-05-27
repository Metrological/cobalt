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

#include "cobalt/worker/dedicated_worker.h"

#include <memory>
#include <string>

#include "cobalt/browser/stack_size_constants.h"
#include "cobalt/dom/event_target.h"
#include "cobalt/script/environment_settings.h"
#include "cobalt/worker/message_port.h"
#include "cobalt/worker/worker.h"
#include "cobalt/worker/worker_options.h"
#include "url/gurl.h"

namespace cobalt {
namespace worker {

namespace {
const char kDedicatedWorkerName[] = "DedicatedWorker";
}  // namespace

DedicatedWorker::DedicatedWorker(script::EnvironmentSettings* settings,
                                 const std::string& scriptURL)
    : EventTarget(settings), settings_(settings), script_url_(scriptURL) {
  Initialize();
}

DedicatedWorker::DedicatedWorker(script::EnvironmentSettings* settings,
                                 const std::string& scriptURL,
                                 const WorkerOptions& worker_options)
    : EventTarget(settings),
      settings_(settings),
      script_url_(scriptURL),
      worker_options_(worker_options) {
  Initialize();
}

void DedicatedWorker::Initialize() {
  // Algorithm for the Worker constructor.
  //   https://html.spec.whatwg.org/commit-snapshots/465a6b672c703054de278b0f8133eb3ad33d93f4/#dom-worker

  // 1. The user agent may throw a "SecurityError" DOMException if the request
  //    violates a policy decision (e.g. if the user agent is configured to
  //    not
  //    allow the page to start dedicated workers).
  // 2. Let outside settings be the current settings object.
  // 3. Parse the scriptURL argument relative to outside settings.
  Worker::Options options(kDedicatedWorkerName);
  const GURL& base_url = environment_settings()->base_url();
  options.url = base_url.Resolve(script_url_);

  LOG_IF(WARNING, !options.url.is_valid())
      << script_url_ << " cannot be resolved based on " << base_url << ".";

  // 4. If this fails, throw a "SyntaxError" DOMException.
  // 5. Let worker URL be the resulting URL record.
  options.web_options.stack_size = cobalt::browser::kWorkerStackSize;
  options.web_options.network_module =
      base::polymorphic_downcast<web::EnvironmentSettings*>(settings_)
          ->context()
          ->network_module();
  // 6. Let worker be a new Worker object.
  worker_.reset(new Worker());
  // 7. Let outside port be a new MessagePort in outside settings's Realm.
  // 8. Associate the outside port with worker.
  outside_port_ = new MessagePort(this, settings_);
  // 9. Run this step in parallel:
  //    1. Run a worker given worker, worker URL, outside settings, outside
  //    port, and options.
  options.outside_settings = settings_;
  options.outside_port = outside_port_.get();
  options.options = worker_options_;

  worker_->Run(options);
  // 10. Return worker.
}

DedicatedWorker::~DedicatedWorker() { Terminate(); }

void DedicatedWorker::Terminate() {}

// void postMessage(any message, object transfer);
// -> void PostMessage(const script::ValueHandleHolder& message,
//                     script::Sequence<script::ValueHandle*> transfer) {}
void DedicatedWorker::PostMessage(const std::string& message) {
  DCHECK(worker_);
  worker_->PostMessage(message);
}


}  // namespace worker
}  // namespace cobalt
