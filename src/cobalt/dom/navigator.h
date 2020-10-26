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

#ifndef COBALT_DOM_NAVIGATOR_H_
#define COBALT_DOM_NAVIGATOR_H_

#include <string>

#include "base/memory/ref_counted.h"
#include "base/optional.h"
#include "cobalt/dom/captions/system_caption_settings.h"
#include "cobalt/dom/eme/media_key_system_configuration.h"
#include "cobalt/dom/mime_type_array.h"
#include "cobalt/dom/plugin_array.h"
#include "cobalt/media_capture/media_devices.h"
#include "cobalt/media_session/media_session.h"
#include "cobalt/script/promise.h"
#include "cobalt/script/script_value_factory.h"
#include "cobalt/script/sequence.h"
#include "cobalt/script/wrappable.h"

namespace cobalt {
namespace dom {

// The Navigator object represents the identity and state of the user agent (the
// client), and allows Web pages to register themselves as potential protocol
// and content handlers.
// https://www.w3.org/TR/html50/webappapis.html#navigator
class Navigator : public script::Wrappable {
 public:
  Navigator(
      script::EnvironmentSettings* settings, const std::string& user_agent,
      const std::string& language,
      scoped_refptr<cobalt::media_session::MediaSession> media_session,
      scoped_refptr<cobalt::dom::captions::SystemCaptionSettings> captions,
      script::ScriptValueFactory* script_value_factory);

  // Web API: NavigatorID
  const std::string& user_agent() const;

  // Web API: NavigatorLanguage
  const std::string& language() const;

  // Web API: NavigatorLicenses
  const std::string licenses() const;

  // Web API: NavigatorStorageUtils
  bool java_enabled() const;

  // Web API: NavigatorPlugins
  bool cookie_enabled() const;

  // Web API: MediaDevices
  scoped_refptr<media_capture::MediaDevices> media_devices();

  const scoped_refptr<MimeTypeArray>& mime_types() const;
  const scoped_refptr<PluginArray>& plugins() const;

  const scoped_refptr<cobalt::media_session::MediaSession>& media_session()
      const;

  // Web API: extension defined in Encrypted Media Extensions (16 March 2017).
  using InterfacePromise = script::Promise<scoped_refptr<script::Wrappable>>;
  script::Handle<InterfacePromise> RequestMediaKeySystemAccess(
      script::EnvironmentSettings* settings, const std::string& key_system,
      const script::Sequence<eme::MediaKeySystemConfiguration>&
          supported_configurations);

  const scoped_refptr<cobalt::dom::captions::SystemCaptionSettings>&
  system_caption_settings() const;

  DEFINE_WRAPPABLE_TYPE(Navigator);
  void TraceMembers(script::Tracer* tracer) override;

 private:
  ~Navigator() override {}

  base::Optional<script::Sequence<MediaKeySystemMediaCapability>>
  TryGetSupportedCapabilities(
      const media::CanPlayTypeHandler& can_play_type_handler,
      const std::string& key_system,
      const script::Sequence<MediaKeySystemMediaCapability>&
          requested_media_capabilities);

  base::Optional<eme::MediaKeySystemConfiguration> TryGetSupportedConfiguration(
      const media::CanPlayTypeHandler& can_play_type_handler,
      const std::string& key_system,
      const eme::MediaKeySystemConfiguration& candidate_configuration);

  bool CanPlayWithCapability(
      const media::CanPlayTypeHandler& can_play_type_handler,
      const std::string& key_system,
      const MediaKeySystemMediaCapability& media_capability);

  bool CanPlayWithoutAttributes(
      const media::CanPlayTypeHandler& can_play_type_handler,
      const std::string& content_type, const std::string& key_system,
      const std::string& encryption_scheme);

  bool CanPlayWithAttributes(
      const media::CanPlayTypeHandler& can_play_type_handler,
      const std::string& content_type, const std::string& key_system,
      const std::string& encryption_scheme);

  std::string user_agent_;
  std::string language_;
  scoped_refptr<MimeTypeArray> mime_types_;
  scoped_refptr<PluginArray> plugins_;
  scoped_refptr<cobalt::media_session::MediaSession> media_session_;
  scoped_refptr<cobalt::media_capture::MediaDevices> media_devices_;
  scoped_refptr<cobalt::dom::captions::SystemCaptionSettings>
      system_caption_settings_;
  script::ScriptValueFactory* script_value_factory_;
  base::Optional<bool> key_system_with_attributes_supported_;

  DISALLOW_COPY_AND_ASSIGN(Navigator);
};

}  // namespace dom
}  // namespace cobalt

#endif  // COBALT_DOM_NAVIGATOR_H_
