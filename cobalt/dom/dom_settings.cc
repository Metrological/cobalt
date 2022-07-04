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

#include "cobalt/dom/dom_settings.h"

#include "cobalt/dom/document.h"
#include "cobalt/dom/window.h"
#include "cobalt/web/url_utils.h"

namespace cobalt {
namespace dom {

DOMSettings::DOMSettings(
    const base::DebuggerHooks& debugger_hooks, const int max_dom_element_depth,
    MediaSourceRegistry* media_source_registry,
    media::CanPlayTypeHandler* can_play_type_handler,
    const media::DecoderBufferMemoryInfo* decoder_buffer_memory_info,
    MutationObserverTaskManager* mutation_observer_task_manager,
    const Options& options)
    : web::EnvironmentSettings(debugger_hooks),
      max_dom_element_depth_(max_dom_element_depth),
      microphone_options_(options.microphone_options),
      media_source_registry_(media_source_registry),
      can_play_type_handler_(can_play_type_handler),
      decoder_buffer_memory_info_(decoder_buffer_memory_info),
      mutation_observer_task_manager_(mutation_observer_task_manager) {}

DOMSettings::~DOMSettings() {}

void DOMSettings::set_window(const scoped_refptr<Window>& window) {
  window_ = window;
  set_base_url(window->document()->url_as_gurl());
}
scoped_refptr<Window> DOMSettings::window() const { return window_; }

loader::Origin DOMSettings::document_origin() const {
  return window()->document()->location()->GetOriginAsObject();
}

}  // namespace dom
}  // namespace cobalt
