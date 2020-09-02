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

#if defined(STARBOARD)
#include "starboard/client_porting/poem/stdio_leaks_poem.h"
#endif

#include "cobalt/browser/application.h"

#include <memory>
#include <string>
#include <vector>

#include "base/command_line.h"
#include "base/lazy_instance.h"
#include "base/logging.h"
#include "base/metrics/statistics_recorder.h"
#include "base/optional.h"
#include "base/path_service.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/task/task_scheduler/task_scheduler.h"
#include "base/time/time.h"
#include "base/trace_event/trace_event.h"
#include "build/build_config.h"
#include "cobalt/base/accessibility_caption_settings_changed_event.h"
#include "cobalt/base/accessibility_settings_changed_event.h"
#include "cobalt/base/accessibility_text_to_speech_settings_changed_event.h"
#include "cobalt/base/cobalt_paths.h"
#include "cobalt/base/deep_link_event.h"
#include "cobalt/base/get_application_key.h"
#include "cobalt/base/init_cobalt.h"
#include "cobalt/base/language.h"
#include "cobalt/base/localized_strings.h"
#include "cobalt/base/on_screen_keyboard_blurred_event.h"
#include "cobalt/base/on_screen_keyboard_focused_event.h"
#include "cobalt/base/on_screen_keyboard_hidden_event.h"
#include "cobalt/base/on_screen_keyboard_shown_event.h"
#include "cobalt/base/on_screen_keyboard_suggestions_updated_event.h"
#include "cobalt/base/startup_timer.h"
#if defined(COBALT_ENABLE_VERSION_COMPATIBILITY_VALIDATIONS)
#include "cobalt/base/version_compatibility.h"
#endif  // defined(COBALT_ENABLE_VERSION_COMPATIBILITY_VALIDATIONS)
#include "cobalt/base/window_size_changed_event.h"
#include "cobalt/browser/device_authentication.h"
#include "cobalt/browser/memory_settings/auto_mem_settings.h"
#include "cobalt/browser/memory_tracker/tool.h"
#include "cobalt/browser/storage_upgrade_handler.h"
#include "cobalt/browser/switches.h"
#include "cobalt/browser/user_agent_string.h"
#include "cobalt/configuration/configuration.h"
#include "cobalt/extension/installation_manager.h"
#include "cobalt/loader/image/image_decoder.h"
#include "cobalt/math/size.h"
#include "cobalt/script/javascript_engine.h"
#if defined(ENABLE_DEBUG_COMMAND_LINE_SWITCHES)
#include "cobalt/storage/savegame_fake.h"
#endif  // defined(ENABLE_DEBUG_COMMAND_LINE_SWITCHES)
#include "cobalt/system_window/input_event.h"
#include "cobalt/trace_event/scoped_trace_to_file.h"
#include "starboard/configuration.h"
#include "starboard/system.h"
#include "url/gurl.h"

using cobalt::cssom::ViewportSize;

namespace cobalt {
namespace browser {

namespace {
const int kStatUpdatePeriodMs = 1000;

const char kDefaultURL[] = "https://www.youtube.com/tv";

#if defined(ENABLE_ABOUT_SCHEME)
const char kAboutBlankURL[] = "about:blank";
#endif  // defined(ENABLE_ABOUT_SCHEME)

bool IsStringNone(const std::string& str) {
  return !base::strcasecmp(str.c_str(), "none");
}

#if defined(ENABLE_WEBDRIVER) || defined(ENABLE_DEBUGGER)
std::string GetDevServersListenIp() {
  bool ip_v6;
#if SB_API_VERSION >= 12
  ip_v6 = SbSocketIsIpv6Supported();
#elif SB_HAS(IPV6)
  ip_v6 = true;
#else
  ip_v6 = false;
#endif
  // Default to INADDR_ANY
  std::string listen_ip(ip_v6 ? "::" : "0.0.0.0");

  // Desktop PCs default to loopback.
  if (SbSystemGetDeviceType() == kSbSystemDeviceTypeDesktopPC) {
    listen_ip = ip_v6 ? "::1" : "127.0.0.1";
  }

#if defined(ENABLE_DEBUG_COMMAND_LINE_SWITCHES)
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kDevServersListenIp)) {
    listen_ip =
        command_line->GetSwitchValueASCII(switches::kDevServersListenIp);
  }
#endif  // ENABLE_DEBUG_COMMAND_LINE_SWITCHES

  return listen_ip;
}
#endif  // defined(ENABLE_WEBDRIVER) || defined(ENABLE_DEBUGGER)

#if defined(ENABLE_DEBUGGER)
int GetRemoteDebuggingPort() {
#if defined(SB_OVERRIDE_DEFAULT_REMOTE_DEBUGGING_PORT)
  const unsigned int kDefaultRemoteDebuggingPort =
      SB_OVERRIDE_DEFAULT_REMOTE_DEBUGGING_PORT;
#else
  const unsigned int kDefaultRemoteDebuggingPort = 9222;
#endif  // defined(SB_OVERRIDE_DEFAULT_REMOTE_DEBUGGING_PORT)
  unsigned int remote_debugging_port = kDefaultRemoteDebuggingPort;
#if defined(ENABLE_DEBUG_COMMAND_LINE_SWITCHES)
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kRemoteDebuggingPort)) {
    std::string switch_value =
        command_line->GetSwitchValueASCII(switches::kRemoteDebuggingPort);
    if (!base::StringToUint(switch_value, &remote_debugging_port)) {
      DLOG(ERROR) << "Invalid port specified for remote debug server: "
                  << switch_value
                  << ". Using default port: " << kDefaultRemoteDebuggingPort;
      remote_debugging_port = kDefaultRemoteDebuggingPort;
    }
  }
  DCHECK(remote_debugging_port != 0 ||
         !command_line->HasSwitch(switches::kWaitForWebDebugger))
      << switches::kWaitForWebDebugger << " switch can't be used when "
      << switches::kRemoteDebuggingPort << " is 0 (disabled).";
#endif  // ENABLE_DEBUG_COMMAND_LINE_SWITCHES
  return uint16_t(remote_debugging_port);
}
#endif  // ENABLE_DEBUGGER

#if defined(ENABLE_WEBDRIVER)
int GetWebDriverPort() {
  // The default port on which the webdriver server should listen for incoming
  // connections.
#if defined(SB_OVERRIDE_DEFAULT_WEBDRIVER_PORT)
  const int kDefaultWebDriverPort = SB_OVERRIDE_DEFAULT_WEBDRIVER_PORT;
#else
  const int kDefaultWebDriverPort = 4444;
#endif  // defined(SB_OVERRIDE_DEFAULT_WEBDRIVER_PORT)
  int webdriver_port = kDefaultWebDriverPort;
#if defined(ENABLE_DEBUG_COMMAND_LINE_SWITCHES)
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kWebDriverPort)) {
    if (!base::StringToInt(
            command_line->GetSwitchValueASCII(switches::kWebDriverPort),
            &webdriver_port)) {
      DLOG(ERROR) << "Invalid port specified for WebDriver server: "
                  << command_line->GetSwitchValueASCII(switches::kWebDriverPort)
                  << ". Using default port: " << kDefaultWebDriverPort;
      webdriver_port = kDefaultWebDriverPort;
    }
  }
#endif  // ENABLE_DEBUG_COMMAND_LINE_SWITCHES
  return webdriver_port;
}

std::string GetWebDriverListenIp() {
  // The default IP on which the webdriver server should listen for incoming
  // connections.
  std::string webdriver_listen_ip = GetDevServersListenIp();
#if defined(ENABLE_DEBUG_COMMAND_LINE_SWITCHES)
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kWebDriverListenIp)) {
    DLOG(WARNING) << "The \"--" << switches::kWebDriverListenIp
                  << "\" switch is deprecated; please use \"--"
                  << switches::kDevServersListenIp << "\" instead.";
    webdriver_listen_ip =
        command_line->GetSwitchValueASCII(switches::kWebDriverListenIp);
  }
#endif  // ENABLE_DEBUG_COMMAND_LINE_SWITCHES
  return webdriver_listen_ip;
}
#endif  // ENABLE_WEBDRIVER

GURL GetInitialURL(bool should_preload) {
  GURL initial_url = GURL(kDefaultURL);
  // Allow the user to override the default URL via a command line parameter.
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kInitialURL)) {
    std::string url_switch =
        command_line->GetSwitchValueASCII(switches::kInitialURL);
#if defined(ENABLE_ABOUT_SCHEME)
    // Check the switch itself since some non-empty strings parse to empty URLs.
    if (url_switch.empty()) {
      LOG(ERROR) << "URL from parameter is empty, using " << kAboutBlankURL;
      return GURL(kAboutBlankURL);
    }
#endif  // defined(ENABLE_ABOUT_SCHEME)
    GURL url = GURL(url_switch);
    if (url.is_valid()) {
      initial_url = url;
    } else {
      LOG(ERROR) << "URL \"" << url_switch
                 << "\" from parameter is not valid, using default URL "
                 << initial_url;
    }
  }

  if (should_preload) {
    std::string query = initial_url.query();
    if (!query.empty()) {
      query += "&";
    }
    query += "launch=preload";
    GURL::Replacements replacements;
    replacements.SetQueryStr(query);
    initial_url = initial_url.ReplaceComponents(replacements);
  }

#if SB_API_VERSION >= 11
  if (!command_line->HasSwitch(
          switches::kOmitDeviceAuthenticationQueryParameters)) {
    // Append the device authentication query parameters based on the platform's
    // certification secret to the initial URL.
    std::string query = initial_url.query();
    std::string device_authentication_query_string =
        GetDeviceAuthenticationSignedURLQueryString();
    if (!query.empty() && !device_authentication_query_string.empty()) {
      query += "&";
    }
    query += device_authentication_query_string;

    if (!query.empty()) {
      GURL::Replacements replacements;
      replacements.SetQueryStr(query);
      initial_url = initial_url.ReplaceComponents(replacements);
    }
  }
#endif  // SB_API_VERSION >= 11

  return initial_url;
}

base::Optional<GURL> GetFallbackSplashScreenURL() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  std::string fallback_splash_screen_string;
  if (command_line->HasSwitch(switches::kFallbackSplashScreenURL)) {
    fallback_splash_screen_string =
        command_line->GetSwitchValueASCII(switches::kFallbackSplashScreenURL);
  } else {
    fallback_splash_screen_string = configuration::Configuration::GetInstance()
                                        ->CobaltFallbackSplashScreenUrl();
  }
  if (IsStringNone(fallback_splash_screen_string)) {
    return base::Optional<GURL>();
  }
  base::Optional<GURL> fallback_splash_screen_url =
      GURL(fallback_splash_screen_string);
  if (!fallback_splash_screen_url->is_valid() ||
      !(fallback_splash_screen_url->SchemeIsFile() ||
        fallback_splash_screen_url->SchemeIs("h5vcc-embedded"))) {
    LOG(FATAL) << "Ignoring invalid fallback splash screen: "
               << fallback_splash_screen_string;
  }
  return fallback_splash_screen_url;
}

base::TimeDelta GetTimedTraceDuration() {
#if defined(ENABLE_DEBUG_COMMAND_LINE_SWITCHES)
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  int duration_in_seconds = 0;
  if (command_line->HasSwitch(switches::kTimedTrace) &&
      base::StringToInt(
          command_line->GetSwitchValueASCII(switches::kTimedTrace),
          &duration_in_seconds)) {
    return base::TimeDelta::FromSeconds(duration_in_seconds);
  }
#endif  // ENABLE_DEBUG_COMMAND_LINE_SWITCHES

  return base::TimeDelta();
}

base::FilePath GetExtraWebFileDir() {
  // Default is empty, command line can override.
  base::FilePath result;

#if defined(ENABLE_DEBUG_COMMAND_LINE_SWITCHES)
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kExtraWebFileDir)) {
    result = base::FilePath(
        command_line->GetSwitchValueASCII(switches::kExtraWebFileDir));
    if (!result.IsAbsolute()) {
      // Non-absolute paths are relative to the executable directory.
      base::FilePath content_path;
      base::PathService::Get(base::DIR_EXE, &content_path);
      result = content_path.DirName().DirName().Append(result);
    }
    DLOG(INFO) << "Extra web file dir: " << result.value();
  }
#endif  // ENABLE_DEBUG_COMMAND_LINE_SWITCHES

  return result;
}

#if defined(ENABLE_DEBUG_COMMAND_LINE_SWITCHES)
void EnableUsingStubImageDecoderIfRequired() {
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kStubImageDecoder)) {
    loader::image::ImageDecoder::UseStubImageDecoder();
  }
}
#endif  // ENABLE_DEBUG_COMMAND_LINE_SWITCHES

base::Optional<cssom::ViewportSize> GetRequestedViewportSize(
    base::CommandLine* command_line) {
  DCHECK(command_line);
  if (!command_line->HasSwitch(browser::switches::kViewport)) {
    return base::nullopt;
  }

  std::string switch_value =
      command_line->GetSwitchValueASCII(browser::switches::kViewport);

  std::vector<std::string> lengths = base::SplitString(
      switch_value, "x", base::TRIM_WHITESPACE, base::SPLIT_WANT_ALL);

  if (lengths.empty()) {
    DLOG(ERROR) << "Viewport " << switch_value << " is invalid.";
    return base::nullopt;
  }

  int width = 0;
  if (!base::StringToInt(lengths[0], &width)) {
    DLOG(ERROR) << "Viewport " << switch_value << " has invalid width.";
    return base::nullopt;
  }

  if (lengths.size() < 2) {
    // Allow shorthand specification of the viewport by only giving the
    // width. This calculates the height at 4:3 aspect ratio for smaller
    // viewport widths, and 16:9 for viewports 1280 pixels wide or larger.
    if (width >= 1280) {
      return ViewportSize(width, 9 * width / 16);
    }
    return ViewportSize(width, 3 * width / 4);
  }

  int height = 0;
  if (!base::StringToInt(lengths[1], &height)) {
    DLOG(ERROR) << "Viewport " << switch_value << " has invalid height.";
    return base::nullopt;
  }

  if (lengths.size() < 3) {
    return ViewportSize(width, height);
  }

  double screen_diagonal_inches = 0.0f;
  if (lengths.size() >= 3) {
    if (!base::StringToDouble(lengths[2], &screen_diagonal_inches)) {
      DLOG(ERROR) << "Viewport " << switch_value
                  << " has invalid screen_diagonal_inches.";
      return base::nullopt;
    }
  }

  double video_pixel_ratio = 1.0f;
  if (lengths.size() >= 4) {
    if (!base::StringToDouble(lengths[3], &video_pixel_ratio)) {
      DLOG(ERROR) << "Viewport " << switch_value
                  << " has invalid video_pixel_ratio.";
      return base::nullopt;
    }
  }

  return ViewportSize(width, height, static_cast<float>(video_pixel_ratio),
                      static_cast<float>(screen_diagonal_inches));
}

std::string GetMinLogLevelString() {
#if defined(ENABLE_DEBUG_COMMAND_LINE_SWITCHES)
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(switches::kMinLogLevel)) {
    return command_line->GetSwitchValueASCII(switches::kMinLogLevel);
  }
#endif  // ENABLE_DEBUG_COMMAND_LINE_SWITCHES
  return "info";
}

int StringToLogLevel(const std::string& log_level) {
  if (log_level == "info") {
    return logging::LOG_INFO;
  } else if (log_level == "warning") {
    return logging::LOG_WARNING;
  } else if (log_level == "error") {
    return logging::LOG_ERROR;
  } else if (log_level == "fatal") {
    return logging::LOG_FATAL;
  } else {
    NOTREACHED() << "Unrecognized logging level: " << log_level;
    return logging::LOG_INFO;
  }
}

void SetIntegerIfSwitchIsSet(const char* switch_name, int* output) {
  if (base::CommandLine::ForCurrentProcess()->HasSwitch(switch_name)) {
    int32 out;
    if (base::StringToInt32(
            base::CommandLine::ForCurrentProcess()->GetSwitchValueNative(
                switch_name),
            &out)) {
      LOG(INFO) << "Command line switch '" << switch_name << "': Modifying "
                << *output << " -> " << out;
      *output = out;
    } else {
      LOG(ERROR) << "Invalid value for command line setting: " << switch_name;
    }
  }
}

void ApplyCommandLineSettingsToRendererOptions(
    renderer::RendererModule::Options* options) {
  SetIntegerIfSwitchIsSet(browser::switches::kScratchSurfaceCacheSizeInBytes,
                          &options->scratch_surface_cache_size_in_bytes);
#if defined(ENABLE_DEBUG_COMMAND_LINE_SWITCHES)
  auto command_line = base::CommandLine::ForCurrentProcess();
  if (command_line->HasSwitch(browser::switches::kDisableRasterizerCaching) ||
      command_line->HasSwitch(
          browser::switches::kForceDeterministicRendering)) {
    options->force_deterministic_rendering = true;
  }
#endif  // ENABLE_DEBUG_COMMAND_LINE_SWITCHES
}

struct NonTrivialStaticFields {
  NonTrivialStaticFields() : system_language(base::GetSystemLanguage()) {}

  const std::string system_language;
  std::string user_agent;

 private:
  DISALLOW_COPY_AND_ASSIGN(NonTrivialStaticFields);
};

struct SecurityFlags {
  csp::CSPHeaderPolicy csp_header_policy;
  network::HTTPSRequirement https_requirement;
};

// |non_trivial_static_fields| will be lazily created on the first time it's
// accessed.
base::LazyInstance<NonTrivialStaticFields>::DestructorAtExit
    non_trivial_static_fields = LAZY_INSTANCE_INITIALIZER;

#if defined(ENABLE_DEBUGGER) && defined(STARBOARD_ALLOWS_MEMORY_TRACKING)
const char kMemoryTrackerCommand[] = "memory_tracker";
const char kMemoryTrackerCommandShortHelp[] = "Create a memory tracker.";
const char kMemoryTrackerCommandLongHelp[] =
    "Create a memory tracker of the given type. Use an empty string to see the "
    "available trackers.";
#endif  // defined(ENABLE_DEBUGGER) && defined(STARBOARD_ALLOWS_MEMORY_TRACKING)

}  // namespace

// Helper stub to disable histogram tracking in StatisticsRecorder
struct RecordCheckerStub : public base::RecordHistogramChecker {
  bool ShouldRecord(uint64_t) const override { return false; }
};

// Static user logs
ssize_t Application::available_memory_ = 0;
int64 Application::lifetime_in_ms_ = 0;

Application::AppStatus Application::app_status_ =
    Application::kUninitializedAppStatus;
int Application::app_pause_count_ = 0;
int Application::app_unpause_count_ = 0;
int Application::app_suspend_count_ = 0;
int Application::app_resume_count_ = 0;

Application::NetworkStatus Application::network_status_ =
    Application::kDisconnectedNetworkStatus;
int Application::network_connect_count_ = 0;
int Application::network_disconnect_count_ = 0;

Application::Application(const base::Closure& quit_closure, bool should_preload)
    : message_loop_(base::MessageLoop::current()),
      quit_closure_(quit_closure)
#if defined(ENABLE_DEBUGGER) && defined(STARBOARD_ALLOWS_MEMORY_TRACKING)
      ,
      ALLOW_THIS_IN_INITIALIZER_LIST(memory_tracker_command_handler_(
          kMemoryTrackerCommand,
          base::Bind(&Application::OnMemoryTrackerCommand,
                     base::Unretained(this)),
          kMemoryTrackerCommandShortHelp, kMemoryTrackerCommandLongHelp))
#endif  // defined(ENABLE_DEBUGGER) && defined(STARBOARD_ALLOWS_MEMORY_TRACKING)
{
  DCHECK(!quit_closure_.is_null());
  // Check to see if a timed_trace has been set, indicating that we should
  // begin a timed trace upon startup.
  base::TimeDelta trace_duration = GetTimedTraceDuration();
  if (trace_duration != base::TimeDelta()) {
    trace_event::TraceToFileForDuration(
        base::FilePath(FILE_PATH_LITERAL("timed_trace.json")), trace_duration);
  }

  TRACE_EVENT0("cobalt::browser", "Application::Application()");

  DCHECK(base::MessageLoop::current());
  DCHECK_EQ(
      base::MessageLoop::TYPE_UI,
      static_cast<base::MessageLoop*>(base::MessageLoop::current())->type());

  DETACH_FROM_THREAD(network_event_thread_checker_);
  DETACH_FROM_THREAD(application_event_thread_checker_);

  // Set the minimum logging level, if specified on the command line.
  logging::SetMinLogLevel(StringToLogLevel(GetMinLogLevelString()));

  stats_update_timer_.Start(
      FROM_HERE, base::TimeDelta::FromMilliseconds(kStatUpdatePeriodMs),
      base::Bind(&Application::UpdatePeriodicStats, base::Unretained(this)));

  // Get the initial URL.
  GURL initial_url = GetInitialURL(should_preload);
  DLOG(INFO) << "Initial URL: " << initial_url;

  // Get the fallback splash screen URL.
  base::Optional<GURL> fallback_splash_screen_url =
      GetFallbackSplashScreenURL();
  DLOG(INFO) << "Fallback splash screen URL: "
             << (fallback_splash_screen_url ? fallback_splash_screen_url->spec()
                                            : "none");

  // Get the system language and initialize our localized strings.
  std::string language = base::GetSystemLanguage();
  base::LocalizedStrings::GetInstance()->Initialize(language);

  // Disable histogram tracking before TaskScheduler creates StatisticsRecorder
  // instances.
  auto record_checker = std::make_unique<RecordCheckerStub>();
  base::StatisticsRecorder::SetRecordChecker(std::move(record_checker));

  // A one-per-process task scheduler is needed for usage of APIs in
  // base/post_task.h which will be used by some net APIs like
  // URLRequestContext;
  base::TaskScheduler::CreateAndStartWithDefaultParams("Cobalt TaskScheduler");

  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  base::Optional<cssom::ViewportSize> requested_viewport_size =
      GetRequestedViewportSize(command_line);

  unconsumed_deep_link_ = GetInitialDeepLink();
  DLOG(INFO) << "Initial deep link: " << unconsumed_deep_link_;

  WebModule::Options web_options;
  storage::StorageManager::Options storage_manager_options;
  network::NetworkModule::Options network_module_options;
  // Create the main components of our browser.
  BrowserModule::Options options(web_options);
  options.web_module_options.name = "MainWebModule";
  network_module_options.preferred_language = language;
  options.command_line_auto_mem_settings =
      memory_settings::GetSettings(*command_line);
  options.build_auto_mem_settings = memory_settings::GetDefaultBuildSettings();
  options.fallback_splash_screen_url = fallback_splash_screen_url;

  if (command_line->HasSwitch(browser::switches::kFPSPrint)) {
    options.renderer_module_options.enable_fps_stdout = true;
  }
  if (command_line->HasSwitch(browser::switches::kFPSOverlay)) {
    options.renderer_module_options.enable_fps_overlay = true;
  }

  ApplyCommandLineSettingsToRendererOptions(&options.renderer_module_options);

  if (command_line->HasSwitch(browser::switches::kDisableJavaScriptJit)) {
    options.web_module_options.javascript_engine_options.disable_jit = true;
  }

  if (command_line->HasSwitch(
          browser::switches::kRetainRemoteTypefaceCacheDuringSuspend)) {
    options.web_module_options.should_retain_remote_typeface_cache_on_suspend =
        true;
  }

#if defined(ENABLE_DEBUG_COMMAND_LINE_SWITCHES)
  if (command_line->HasSwitch(browser::switches::kNullSavegame)) {
    storage_manager_options.savegame_options.factory =
        &storage::SavegameFake::Create;
  }
#if SB_API_VERSION >= 12 || SB_HAS(ON_SCREEN_KEYBOARD)
  if (command_line->HasSwitch(browser::switches::kDisableOnScreenKeyboard)) {
    options.enable_on_screen_keyboard = false;
  }
#endif  // SB_API_VERSION >= 12 ||
        // SB_HAS(ON_SCREEN_KEYBOARD)

#endif  // defined(ENABLE_DEBUG_COMMAND_LINE_SWITCHES)

#if defined(COBALT_ENABLE_VERSION_COMPATIBILITY_VALIDATIONS)
  constexpr int kDefaultMinCompatibilityVersion = 1;
  int minimum_version = kDefaultMinCompatibilityVersion;
#if defined(ENABLE_DEBUG_COMMAND_LINE_SWITCHES)
  if (command_line->HasSwitch(browser::switches::kMinCompatibilityVersion)) {
    std::string switch_value =
        command_line->GetSwitchValueASCII(switches::kMinCompatibilityVersion);
    if (!base::StringToInt(switch_value, &minimum_version)) {
      DLOG(ERROR) << "Invalid min_compatibility_version provided.";
    }
  }
#endif  // defined(ENABLE_DEBUG_COMMAND_LINE_SWITCHES)
  base::VersionCompatibility::GetInstance()->SetMinimumVersion(minimum_version);
#endif  // defined(COBALT_ENABLE_VERSION_COMPATIBILITY_VALIDATIONS)

  base::Optional<std::string> partition_key;
  if (command_line->HasSwitch(browser::switches::kLocalStoragePartitionUrl)) {
    std::string local_storage_partition_url = command_line->GetSwitchValueASCII(
        browser::switches::kLocalStoragePartitionUrl);
    partition_key = base::GetApplicationKey(GURL(local_storage_partition_url));
    CHECK(partition_key) << "local_storage_partition_url is not a valid URL.";
  } else {
    partition_key = base::GetApplicationKey(initial_url);
  }
  storage_manager_options.savegame_options.id = partition_key;

  base::Optional<std::string> default_key =
      base::GetApplicationKey(GURL(kDefaultURL));
  if (command_line->HasSwitch(
          browser::switches::kForceMigrationForStoragePartitioning) ||
      partition_key == default_key) {
    storage_manager_options.savegame_options.fallback_to_default_id =
        true;
  }

  // User can specify an extra search path entry for files loaded via file://.
  options.web_module_options.extra_web_file_dir = GetExtraWebFileDir();
  SecurityFlags security_flags{csp::kCSPRequired, network::kHTTPSRequired};
  // Set callback to be notified when a navigation occurs that destroys the
  // underlying WebModule.
  options.web_module_created_callback =
      base::Bind(&Application::WebModuleCreated, base::Unretained(this));

  // The main web module's stat tracker tracks event stats.
  options.web_module_options.track_event_stats = true;

  if (command_line->HasSwitch(browser::switches::kProxy)) {
    network_module_options.custom_proxy =
        command_line->GetSwitchValueASCII(browser::switches::kProxy);
  }

#if defined(ENABLE_DEBUG_COMMAND_LINE_SWITCHES)
#if defined(ENABLE_IGNORE_CERTIFICATE_ERRORS)
  if (command_line->HasSwitch(browser::switches::kIgnoreCertificateErrors)) {
    network_module_options.ignore_certificate_errors = true;
  }
#endif  // defined(ENABLE_IGNORE_CERTIFICATE_ERRORS)

  if (!command_line->HasSwitch(switches::kRequireHTTPSLocation)) {
    security_flags.https_requirement = network::kHTTPSOptional;
  }

  if (!command_line->HasSwitch(browser::switches::kRequireCSP)) {
    security_flags.csp_header_policy = csp::kCSPOptional;
  }

  if (command_line->HasSwitch(browser::switches::kProd)) {
    security_flags.https_requirement = network::kHTTPSRequired;
    security_flags.csp_header_policy = csp::kCSPRequired;
  }

  if (command_line->HasSwitch(switches::kVideoPlaybackRateMultiplier)) {
    double playback_rate = 1.0;
    base::StringToDouble(command_line->GetSwitchValueASCII(
                             switches::kVideoPlaybackRateMultiplier),
                         &playback_rate);
    options.web_module_options.video_playback_rate_multiplier =
        static_cast<float>(playback_rate);
    DLOG(INFO) << "Set video playback rate multiplier to "
               << options.web_module_options.video_playback_rate_multiplier;
  }

  EnableUsingStubImageDecoderIfRequired();

#if defined(ENABLE_DEBUGGER) && defined(STARBOARD_ALLOWS_MEMORY_TRACKING)
  if (command_line->HasSwitch(switches::kMemoryTracker)) {
    std::string command_arg =
        command_line->GetSwitchValueASCII(switches::kMemoryTracker);
    memory_tracker_tool_ = memory_tracker::CreateMemoryTrackerTool(command_arg);
  }
#endif  // defined(ENABLE_DEBUGGER) && defined(STARBOARD_ALLOWS_MEMORY_TRACKING)

  if (command_line->HasSwitch(switches::kDisableImageAnimations)) {
    options.web_module_options.enable_image_animations = false;
  }
  if (command_line->HasSwitch(switches::kDisableSplashScreenOnReloads)) {
    options.enable_splash_screen_on_reloads = false;
  }
#endif  // ENABLE_DEBUG_COMMAND_LINE_SWITCHES

// Production-builds override all switches to the most secure configuration.
#if defined(COBALT_FORCE_HTTPS)
  security_flags.https_requirement = network::kHTTPSRequired;
#endif  // defined(COBALT_FORCE_HTTPS)

#if defined(COBALT_FORCE_CSP)
  security_flags.csp_header_policy = csp::kCSPRequired;
#endif  // defined(COBALT_FORCE_CSP)

  network_module_options.https_requirement =
      security_flags.https_requirement;
  options.web_module_options.require_csp = security_flags.csp_header_policy;
  options.web_module_options.csp_enforcement_mode = dom::kCspEnforcementEnable;

  options.requested_viewport_size = requested_viewport_size;
  account_manager_.reset(new account::AccountManager());

  storage_manager_.reset(new storage::StorageManager(
      std::unique_ptr<storage::StorageManager::UpgradeHandler>(
          new StorageUpgradeHandler(initial_url)),
      storage_manager_options));

  network_module_.reset(new network::NetworkModule(
      CreateUserAgentString(GetUserAgentPlatformInfoFromSystem()),
      storage_manager_.get(), &event_dispatcher_,
      network_module_options));

#if SB_IS(EVERGREEN)
  if (SbSystemGetExtension(kCobaltExtensionInstallationManagerName)) {
    updater_module_.reset(new updater::UpdaterModule(network_module_.get()));
  }
#endif
  browser_module_.reset(new BrowserModule(
      initial_url,
      (should_preload ? base::kApplicationStatePreloading
                      : base::kApplicationStateStarted),
      &event_dispatcher_, account_manager_.get(), network_module_.get(),
#if SB_IS(EVERGREEN)
      updater_module_.get(),
#endif
      options));

  UpdateUserAgent();

  app_status_ = (should_preload ? kPreloadingAppStatus : kRunningAppStatus);

  // Register event callbacks.
#if SB_API_VERSION >= 8
  window_size_change_event_callback_ = base::Bind(
      &Application::OnWindowSizeChangedEvent, base::Unretained(this));
  event_dispatcher_.AddEventCallback(base::WindowSizeChangedEvent::TypeId(),
                                     window_size_change_event_callback_);
#endif  // SB_API_VERSION >= 8
#if SB_API_VERSION >= 12 || SB_HAS(ON_SCREEN_KEYBOARD)
  on_screen_keyboard_shown_event_callback_ = base::Bind(
      &Application::OnOnScreenKeyboardShownEvent, base::Unretained(this));
  event_dispatcher_.AddEventCallback(base::OnScreenKeyboardShownEvent::TypeId(),
                                     on_screen_keyboard_shown_event_callback_);
  on_screen_keyboard_hidden_event_callback_ = base::Bind(
      &Application::OnOnScreenKeyboardHiddenEvent, base::Unretained(this));
  event_dispatcher_.AddEventCallback(
      base::OnScreenKeyboardHiddenEvent::TypeId(),
      on_screen_keyboard_hidden_event_callback_);
  on_screen_keyboard_focused_event_callback_ = base::Bind(
      &Application::OnOnScreenKeyboardFocusedEvent, base::Unretained(this));
  event_dispatcher_.AddEventCallback(
      base::OnScreenKeyboardFocusedEvent::TypeId(),
      on_screen_keyboard_focused_event_callback_);
  on_screen_keyboard_blurred_event_callback_ = base::Bind(
      &Application::OnOnScreenKeyboardBlurredEvent, base::Unretained(this));
  event_dispatcher_.AddEventCallback(
      base::OnScreenKeyboardBlurredEvent::TypeId(),
      on_screen_keyboard_blurred_event_callback_);
#if SB_API_VERSION >= 11
  on_screen_keyboard_suggestions_updated_event_callback_ =
      base::Bind(&Application::OnOnScreenKeyboardSuggestionsUpdatedEvent,
                 base::Unretained(this));
  event_dispatcher_.AddEventCallback(
      base::OnScreenKeyboardSuggestionsUpdatedEvent::TypeId(),
      on_screen_keyboard_suggestions_updated_event_callback_);
#endif  // SB_API_VERSION >= 11
#endif  // SB_API_VERSION >= 12 ||
        // SB_HAS(ON_SCREEN_KEYBOARD)

#if SB_API_VERSION >= 12 || SB_HAS(CAPTIONS)
  on_caption_settings_changed_event_callback_ = base::Bind(
      &Application::OnCaptionSettingsChangedEvent, base::Unretained(this));
  event_dispatcher_.AddEventCallback(
      base::AccessibilityCaptionSettingsChangedEvent::TypeId(),
      on_caption_settings_changed_event_callback_);
#endif  // SB_API_VERSION >= 12 || SB_HAS(CAPTIONS)
#if defined(ENABLE_WEBDRIVER)
#if defined(ENABLE_DEBUG_COMMAND_LINE_SWITCHES)
  bool create_webdriver_module =
      !command_line->HasSwitch(switches::kDisableWebDriver);
#else
  bool create_webdriver_module = true;
#endif  // ENABLE_DEBUG_COMMAND_LINE_SWITCHES
  if (create_webdriver_module) {
    web_driver_module_.reset(new webdriver::WebDriverModule(
        GetWebDriverPort(), GetWebDriverListenIp(),
        base::Bind(&BrowserModule::CreateSessionDriver,
                   base::Unretained(browser_module_.get())),
        base::Bind(&BrowserModule::RequestScreenshotToMemory,
                   base::Unretained(browser_module_.get())),
        base::Bind(&BrowserModule::SetProxy,
                   base::Unretained(browser_module_.get())),
        base::Bind(&Application::Quit, base::Unretained(this))));
  }
#endif  // ENABLE_WEBDRIVER

#if defined(ENABLE_DEBUGGER)
  int remote_debugging_port = GetRemoteDebuggingPort();
  if (remote_debugging_port == 0) {
    DLOG(INFO) << "Remote web debugger disabled because "
               << switches::kRemoteDebuggingPort << " is 0.";
  } else {
    debug_web_server_.reset(new debug::remote::DebugWebServer(
        remote_debugging_port, GetDevServersListenIp(),
        base::Bind(&BrowserModule::CreateDebugClient,
                   base::Unretained(browser_module_.get()))));
  }
#endif  // ENABLE_DEBUGGER

#if defined(ENABLE_DEBUG_COMMAND_LINE_SWITCHES)
  int duration_in_seconds = 0;
  if (command_line->HasSwitch(switches::kShutdownAfter) &&
      base::StringToInt(
          command_line->GetSwitchValueASCII(switches::kShutdownAfter),
          &duration_in_seconds)) {
    // If the "shutdown_after" command line option is specified, setup a delayed
    // message to quit the application after the specified number of seconds
    // have passed.
    message_loop_->task_runner()->PostDelayedTask(
        FROM_HERE, quit_closure_,
        base::TimeDelta::FromSeconds(duration_in_seconds));
  }
#endif  // ENABLE_DEBUG_COMMAND_LINE_SWITCHES
}

Application::~Application() {
  // explicitly reset here because the destruction of the object is complex
  // and involves a thread join. If this were to hang the app then having
  // the destruction at this point gives a real file-line number and a place
  // for the debugger to land.
#if defined(ENABLE_DEBUGGER) && defined(STARBOARD_ALLOWS_MEMORY_TRACKING)
  memory_tracker_tool_.reset(NULL);
#endif  // defined(ENABLE_DEBUGGER) && defined(STARBOARD_ALLOWS_MEMORY_TRACKING)

  // Unregister event callbacks.
#if SB_API_VERSION >= 8
  event_dispatcher_.RemoveEventCallback(base::WindowSizeChangedEvent::TypeId(),
                                        window_size_change_event_callback_);
#endif  // SB_API_VERSION >= 8
#if SB_API_VERSION >= 12 || SB_HAS(ON_SCREEN_KEYBOARD)
  event_dispatcher_.RemoveEventCallback(
      base::OnScreenKeyboardShownEvent::TypeId(),
      on_screen_keyboard_shown_event_callback_);
  event_dispatcher_.RemoveEventCallback(
      base::OnScreenKeyboardHiddenEvent::TypeId(),
      on_screen_keyboard_hidden_event_callback_);
  event_dispatcher_.RemoveEventCallback(
      base::OnScreenKeyboardFocusedEvent::TypeId(),
      on_screen_keyboard_focused_event_callback_);
  event_dispatcher_.RemoveEventCallback(
      base::OnScreenKeyboardBlurredEvent::TypeId(),
      on_screen_keyboard_blurred_event_callback_);
#if SB_API_VERSION >= 11
  event_dispatcher_.RemoveEventCallback(
      base::OnScreenKeyboardSuggestionsUpdatedEvent::TypeId(),
      on_screen_keyboard_suggestions_updated_event_callback_);
#endif  // SB_API_VERSION >= 11
#endif  // SB_API_VERSION >= 12 ||
        // SB_HAS(ON_SCREEN_KEYBOARD)
#if SB_API_VERSION >= 12 || SB_HAS(CAPTIONS)
  event_dispatcher_.RemoveEventCallback(
      base::AccessibilityCaptionSettingsChangedEvent::TypeId(),
      on_caption_settings_changed_event_callback_);
#endif  // SB_API_VERSION >= 12 || SB_HAS(CAPTIONS)

  app_status_ = kShutDownAppStatus;
}

void Application::Start() {
  if (base::MessageLoop::current() != message_loop_) {
    message_loop_->task_runner()->PostTask(
        FROM_HERE, base::Bind(&Application::Start, base::Unretained(this)));
    return;
  }

  if (app_status_ != kPreloadingAppStatus) {
    NOTREACHED() << __FUNCTION__ << ": Redundant call.";
    return;
  }

  OnApplicationEvent(kSbEventTypeStart);
}

void Application::Quit() {
  if (base::MessageLoop::current() != message_loop_) {
    message_loop_->task_runner()->PostTask(
        FROM_HERE, base::Bind(&Application::Quit, base::Unretained(this)));
    return;
  }

  quit_closure_.Run();
  app_status_ = kQuitAppStatus;
}

void Application::HandleStarboardEvent(const SbEvent* starboard_event) {
  DCHECK(starboard_event);
  DCHECK_EQ(base::MessageLoop::current(), message_loop_);

  // Forward input events to |SystemWindow|.
  if (starboard_event->type == kSbEventTypeInput) {
    system_window::HandleInputEvent(starboard_event);
    return;
  }

  // Create a Cobalt event from the Starboard event, if recognized.
  switch (starboard_event->type) {
    case kSbEventTypePause:
    case kSbEventTypeUnpause:
    case kSbEventTypeSuspend:
    case kSbEventTypeResume:
    case kSbEventTypeLowMemory:
      OnApplicationEvent(starboard_event->type);
      break;
#if SB_API_VERSION >= 8
    case kSbEventTypeWindowSizeChanged:
      DispatchEventInternal(new base::WindowSizeChangedEvent(
          static_cast<SbEventWindowSizeChangedData*>(starboard_event->data)
              ->window,
          static_cast<SbEventWindowSizeChangedData*>(starboard_event->data)
              ->size));
      break;
#endif  // SB_API_VERSION >= 8
#if SB_API_VERSION >= 12 || SB_HAS(ON_SCREEN_KEYBOARD)
    case kSbEventTypeOnScreenKeyboardShown:
      DCHECK(starboard_event->data);
      DispatchEventInternal(new base::OnScreenKeyboardShownEvent(
          *static_cast<int*>(starboard_event->data)));
      break;
    case kSbEventTypeOnScreenKeyboardHidden:
      DispatchEventInternal(new base::OnScreenKeyboardHiddenEvent(
          *static_cast<int*>(starboard_event->data)));
      break;
    case kSbEventTypeOnScreenKeyboardFocused:
      DCHECK(starboard_event->data);
      DispatchEventInternal(new base::OnScreenKeyboardFocusedEvent(
          *static_cast<int*>(starboard_event->data)));
      break;
    case kSbEventTypeOnScreenKeyboardBlurred:
      DispatchEventInternal(new base::OnScreenKeyboardBlurredEvent(
          *static_cast<int*>(starboard_event->data)));
      break;
#if SB_API_VERSION >= 11
    case kSbEventTypeOnScreenKeyboardSuggestionsUpdated:
      DispatchEventInternal(new base::OnScreenKeyboardSuggestionsUpdatedEvent(
          *static_cast<int*>(starboard_event->data)));
      break;
#endif  // SB_API_VERSION >= 11
#endif  // SB_API_VERSION >= 12 ||
        // SB_HAS(ON_SCREEN_KEYBOARD)
    case kSbEventTypeLink: {
      DispatchDeepLink(static_cast<const char*>(starboard_event->data));
      break;
    }
    case kSbEventTypeAccessiblitySettingsChanged:
      DispatchEventInternal(new base::AccessibilitySettingsChangedEvent());
#if SB_API_VERSION < 12
      // Also dispatch the newer text-to-speech settings changed event since
      // the specific kSbEventTypeAccessiblityTextToSpeechSettingsChanged
      // event is not available in this starboard version.
      DispatchEventInternal(
          new base::AccessibilityTextToSpeechSettingsChangedEvent());
#endif
      break;
#if SB_API_VERSION >= 12 || SB_HAS(CAPTIONS)
    case kSbEventTypeAccessibilityCaptionSettingsChanged:
      DispatchEventInternal(
          new base::AccessibilityCaptionSettingsChangedEvent());
      break;
#endif  // SB_API_VERSION >= 12 || SB_HAS(CAPTIONS)
#if SB_API_VERSION >= 12
    case kSbEventTypeAccessiblityTextToSpeechSettingsChanged:
      DispatchEventInternal(
          new base::AccessibilityTextToSpeechSettingsChangedEvent());
      break;
#endif
    // Explicitly list unhandled cases here so that the compiler can give a
    // warning when a value is added, but not handled.
    case kSbEventTypeInput:
    case kSbEventTypePreload:
#if SB_API_VERSION < 11
    case kSbEventTypeNetworkConnect:
    case kSbEventTypeNetworkDisconnect:
#endif  // SB_API_VERSION < 11
    case kSbEventTypeScheduled:
    case kSbEventTypeStart:
    case kSbEventTypeStop:
    case kSbEventTypeUser:
    case kSbEventTypeVerticalSync:
      DLOG(WARNING) << "Unhandled Starboard event of type: "
                    << starboard_event->type;
  }
}

void Application::OnApplicationEvent(SbEventType event_type) {
  TRACE_EVENT0("cobalt::browser", "Application::OnApplicationEvent()");
  DCHECK_CALLED_ON_VALID_THREAD(application_event_thread_checker_);
  switch (event_type) {
    case kSbEventTypeStop:
      DLOG(INFO) << "Got quit event.";
      app_status_ = kWillQuitAppStatus;
      Quit();
      DLOG(INFO) << "Finished quitting.";
      break;
    case kSbEventTypeStart:
      DLOG(INFO) << "Got start event.";
      app_status_ = kRunningAppStatus;
      browser_module_->Start();
      DLOG(INFO) << "Finished starting.";
      break;
    case kSbEventTypePause:
      DLOG(INFO) << "Got pause event.";
      app_status_ = kPausedAppStatus;
      ++app_pause_count_;
      browser_module_->Pause();
      DLOG(INFO) << "Finished pausing.";
      break;
    case kSbEventTypeUnpause:
      DLOG(INFO) << "Got unpause event.";
      app_status_ = kRunningAppStatus;
      ++app_unpause_count_;
      browser_module_->Unpause();
      DLOG(INFO) << "Finished unpausing.";
      break;
    case kSbEventTypeSuspend:
      DLOG(INFO) << "Got suspend event.";
      app_status_ = kSuspendedAppStatus;
      ++app_suspend_count_;
      browser_module_->Suspend();
#if SB_IS(EVERGREEN)
      if (updater_module_) updater_module_->Suspend();
#endif
      DLOG(INFO) << "Finished suspending.";
      break;
    case kSbEventTypeResume:
      DCHECK(SbSystemSupportsResume());
      DLOG(INFO) << "Got resume event.";
      app_status_ = kPausedAppStatus;
      ++app_resume_count_;
      browser_module_->Resume();
#if SB_IS(EVERGREEN)
      if (updater_module_) updater_module_->Resume();
#endif
      DLOG(INFO) << "Finished resuming.";
      break;
    case kSbEventTypeLowMemory:
      DLOG(INFO) << "Got low memory event.";
      browser_module_->ReduceMemory();
      DLOG(INFO) << "Finished reducing memory usage.";
      break;
    // All of the remaining event types are unexpected:
    case kSbEventTypePreload:
#if SB_API_VERSION >= 8
    case kSbEventTypeWindowSizeChanged:
#endif
#if SB_API_VERSION >= 12 || SB_HAS(CAPTIONS)
    case kSbEventTypeAccessibilityCaptionSettingsChanged:
#endif  // SB_API_VERSION >= 12 || SB_HAS(CAPTIONS)
#if SB_API_VERSION >= 12
    case kSbEventTypeAccessiblityTextToSpeechSettingsChanged:
#endif  // SB_API_VERSION >= 12
#if SB_API_VERSION >= 12 || SB_HAS(ON_SCREEN_KEYBOARD)
    case kSbEventTypeOnScreenKeyboardBlurred:
    case kSbEventTypeOnScreenKeyboardFocused:
    case kSbEventTypeOnScreenKeyboardHidden:
    case kSbEventTypeOnScreenKeyboardShown:
#if SB_API_VERSION >= 11
    case kSbEventTypeOnScreenKeyboardSuggestionsUpdated:
#endif  // SB_API_VERSION >= 11
#endif  // SB_API_VERSION >= 12 ||
        // SB_HAS(ON_SCREEN_KEYBOARD)
    case kSbEventTypeAccessiblitySettingsChanged:
    case kSbEventTypeInput:
    case kSbEventTypeLink:
#if SB_API_VERSION < 11
    case kSbEventTypeNetworkConnect:
    case kSbEventTypeNetworkDisconnect:
#endif  // SB_API_VERSION < 11
    case kSbEventTypeScheduled:
    case kSbEventTypeUser:
    case kSbEventTypeVerticalSync:
      NOTREACHED() << "Unexpected event type: " << event_type;
      return;
  }
}

#if SB_API_VERSION >= 8
void Application::OnWindowSizeChangedEvent(const base::Event* event) {
  TRACE_EVENT0("cobalt::browser", "Application::OnWindowSizeChangedEvent()");
  const base::WindowSizeChangedEvent* window_size_change_event =
      base::polymorphic_downcast<const base::WindowSizeChangedEvent*>(event);
  const auto& size = window_size_change_event->size();
#if SB_API_VERSION >= 11
  float diagonal =
      SbWindowGetDiagonalSizeInInches(window_size_change_event->window());
#else
  float diagonal = 0.0f;  // Special value meaning diagonal size is not known.
#endif

  // A value of 0.0 for the video pixel ratio means that the ratio could not be
  // determined. In that case it should be assumed to be the same as the
  // graphics resolution, which corresponds to a device pixel ratio of 1.0.
  float device_pixel_ratio =
      (size.video_pixel_ratio == 0) ? 1.0f : size.video_pixel_ratio;
  cssom::ViewportSize viewport_size(size.width, size.height, diagonal,
                                    device_pixel_ratio);
  browser_module_->OnWindowSizeChanged(viewport_size);
}
#endif  // SB_API_VERSION >= 8

#if SB_API_VERSION >= 12 || SB_HAS(ON_SCREEN_KEYBOARD)
void Application::OnOnScreenKeyboardShownEvent(const base::Event* event) {
  TRACE_EVENT0("cobalt::browser",
               "Application::OnOnScreenKeyboardShownEvent()");
  browser_module_->OnOnScreenKeyboardShown(
      base::polymorphic_downcast<const base::OnScreenKeyboardShownEvent*>(
          event));
}

void Application::OnOnScreenKeyboardHiddenEvent(const base::Event* event) {
  TRACE_EVENT0("cobalt::browser",
               "Application::OnOnScreenKeyboardHiddenEvent()");
  browser_module_->OnOnScreenKeyboardHidden(
      base::polymorphic_downcast<const base::OnScreenKeyboardHiddenEvent*>(
          event));
}

void Application::OnOnScreenKeyboardFocusedEvent(const base::Event* event) {
  TRACE_EVENT0("cobalt::browser",
               "Application::OnOnScreenKeyboardFocusedEvent()");
  browser_module_->OnOnScreenKeyboardFocused(
      base::polymorphic_downcast<const base::OnScreenKeyboardFocusedEvent*>(
          event));
}

void Application::OnOnScreenKeyboardBlurredEvent(const base::Event* event) {
  TRACE_EVENT0("cobalt::browser",
               "Application::OnOnScreenKeyboardBlurredEvent()");
  browser_module_->OnOnScreenKeyboardBlurred(
      base::polymorphic_downcast<const base::OnScreenKeyboardBlurredEvent*>(
          event));
}

#if SB_API_VERSION >= 11
void Application::OnOnScreenKeyboardSuggestionsUpdatedEvent(
    const base::Event* event) {
  TRACE_EVENT0("cobalt::browser",
               "Application::OnOnScreenKeyboardSuggestionsUpdatedEvent()");
  browser_module_->OnOnScreenKeyboardSuggestionsUpdated(
      base::polymorphic_downcast<
          const base::OnScreenKeyboardSuggestionsUpdatedEvent*>(event));
}
#endif  // SB_API_VERSION >= 11
#endif  // SB_API_VERSION >= 12 ||
        // SB_HAS(ON_SCREEN_KEYBOARD)

#if SB_API_VERSION >= 12 || SB_HAS(CAPTIONS)
void Application::OnCaptionSettingsChangedEvent(const base::Event* event) {
  TRACE_EVENT0("cobalt::browser",
               "Application::OnCaptionSettingsChangedEvent()");
  browser_module_->OnCaptionSettingsChanged(
      base::polymorphic_downcast<
          const base::AccessibilityCaptionSettingsChangedEvent*>(event));
}
#endif  // SB_API_VERSION >= 12 || SB_HAS(CAPTIONS)

void Application::WebModuleCreated() {
  TRACE_EVENT0("cobalt::browser", "Application::WebModuleCreated()");
  DispatchDeepLinkIfNotConsumed();
#if defined(ENABLE_WEBDRIVER)
  if (web_driver_module_) {
    web_driver_module_->OnWindowRecreated();
  }
#endif
}

Application::CValStats::CValStats()
    : free_cpu_memory("Memory.CPU.Free", 0,
                      "Total free application CPU memory remaining."),
      used_cpu_memory("Memory.CPU.Used", 0,
                      "Total CPU memory allocated via the app's allocators."),
      app_start_time("Time.Cobalt.Start",
                     base::StartupTimer::StartTime().ToInternalValue(),
                     "Start time of the application in microseconds."),
      app_lifetime("Cobalt.Lifetime", base::TimeDelta(),
                   "Application lifetime in microseconds.") {
  if (SbSystemHasCapability(kSbSystemCapabilityCanQueryGPUMemoryStats)) {
    free_gpu_memory.emplace("Memory.GPU.Free", 0,
                            "Total free application GPU memory remaining.");
    used_gpu_memory.emplace("Memory.GPU.Used", 0,
                            "Total GPU memory allocated by the application.");
  }
}

// NOTE: UserAgent registration is handled separately, as the value is not
// available when the app is first being constructed. Registration must happen
// each time the user agent is modified, because the string may be pointing
// to a new location on the heap.
void Application::UpdateUserAgent() {
  non_trivial_static_fields.Get().user_agent = browser_module_->GetUserAgent();
  LOG(INFO) << "User Agent: " << non_trivial_static_fields.Get().user_agent;
}

void Application::UpdatePeriodicStats() {
  TRACE_EVENT0("cobalt::browser", "Application::UpdatePeriodicStats()");
  c_val_stats_.app_lifetime = base::StartupTimer::TimeElapsed();

  int64_t used_cpu_memory = SbSystemGetUsedCPUMemory();
  base::Optional<int64_t> used_gpu_memory;
  if (SbSystemHasCapability(kSbSystemCapabilityCanQueryGPUMemoryStats)) {
    used_gpu_memory = SbSystemGetUsedGPUMemory();
  }

  available_memory_ =
      static_cast<ssize_t>(SbSystemGetTotalCPUMemory() - used_cpu_memory);
  c_val_stats_.free_cpu_memory = available_memory_;
  c_val_stats_.used_cpu_memory = used_cpu_memory;

  if (used_gpu_memory) {
    *c_val_stats_.free_gpu_memory =
        SbSystemGetTotalGPUMemory() - *used_gpu_memory;
    *c_val_stats_.used_gpu_memory = *used_gpu_memory;
  }

  browser_module_->UpdateJavaScriptHeapStatistics();

  browser_module_->CheckMemory(used_cpu_memory, used_gpu_memory);
}

void Application::DispatchEventInternal(base::Event* event) {
  event_dispatcher_.DispatchEvent(std::unique_ptr<base::Event>(event));
}

#if defined(ENABLE_DEBUGGER) && defined(STARBOARD_ALLOWS_MEMORY_TRACKING)
void Application::OnMemoryTrackerCommand(const std::string& message) {
  if (base::MessageLoop::current() != message_loop_) {
    message_loop_->task_runner()->PostTask(
        FROM_HERE, base::Bind(&Application::OnMemoryTrackerCommand,
                              base::Unretained(this), message));
    return;
  }

  if (memory_tracker_tool_) {
    LOG(ERROR) << "Can not create a memory tracker when one is already active.";
    return;
  }
  LOG(WARNING) << "Creating \"" << message << "\" memory tracker.";
  memory_tracker_tool_ = memory_tracker::CreateMemoryTrackerTool(message);
}
#endif  // defined(ENABLE_DEBUGGER) && defined(STARBOARD_ALLOWS_MEMORY_TRACKING)

// Called to handle deep link consumed events.
void Application::OnDeepLinkConsumedCallback(const std::string& link) {
  LOG(INFO) << "Got deep link consumed callback: " << link;
  base::AutoLock auto_lock(unconsumed_deep_link_lock_);
  if (link == unconsumed_deep_link_) {
    unconsumed_deep_link_.clear();
  }
}

void Application::DispatchDeepLink(const char* link) {
  if (!link || *link == 0) {
    return;
  }

  std::string deep_link;
  // This block exists to ensure that the lock is held while accessing
  // unconsumed_deep_link_.
  {
    base::AutoLock auto_lock(unconsumed_deep_link_lock_);
    // Stash the deep link so that if it is not consumed, it can be dispatched
    // again after the next WebModule is created.
    unconsumed_deep_link_ = link;
    deep_link = unconsumed_deep_link_;
  }

  LOG(INFO) << "Dispatching deep link: " << deep_link;
  DispatchEventInternal(new base::DeepLinkEvent(
      deep_link, base::Bind(&Application::OnDeepLinkConsumedCallback,
                            base::Unretained(this), deep_link)));
}

void Application::DispatchDeepLinkIfNotConsumed() {
  std::string deep_link;
  // This block exists to ensure that the lock is held while accessing
  // unconsumed_deep_link_.
  {
    base::AutoLock auto_lock(unconsumed_deep_link_lock_);
    deep_link = unconsumed_deep_link_;
  }

  if (!deep_link.empty()) {
    LOG(INFO) << "Dispatching deep link: " << deep_link;
    DispatchEventInternal(new base::DeepLinkEvent(
        deep_link, base::Bind(&Application::OnDeepLinkConsumedCallback,
                              base::Unretained(this), deep_link)));
  }
}

}  // namespace browser
}  // namespace cobalt
