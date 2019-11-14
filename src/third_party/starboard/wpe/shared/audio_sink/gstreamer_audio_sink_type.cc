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

#include "third_party/starboard/wpe/shared/audio_sink/gstreamer_audio_sink_type.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

#include <glib.h>
#include <gst/app/gstappsrc.h>
#include <gst/audio/gstaudiobasesink.h>
#include <gst/audio/streamvolume.h>
#include <gst/gst.h>

#include "base/logging.h"
#include "starboard/common/mutex.h"
#include "starboard/configuration.h"
#include "starboard/file.h"
#include "starboard/media.h"
#include "starboard/shared/starboard/media/media_util.h"
#include "starboard/thread.h"
#include "starboard/time.h"

namespace third_party {
namespace starboard {
namespace wpe {
namespace shared {
namespace audio_sink {
namespace {

GST_DEBUG_CATEGORY(cobalt_gst_audio_sink_debug);
#define GST_CAT_DEFAULT cobalt_gst_audio_sink_debug

constexpr int kFramesPerRequest = 1024;

using ::starboard::shared::starboard::media::GetBytesPerSample;

class GStreamerAudioSink : public SbAudioSinkPrivate {
 public:
  GStreamerAudioSink(
      Type* type,
      int channels,
      int sampling_frequency_hz,
      SbMediaAudioSampleType audio_sample_type,
      SbMediaAudioFrameStorageType audio_frame_storage_type,
      SbAudioSinkFrameBuffers frame_buffers,
      int frame_buffers_size_in_frames,
      SbAudioSinkUpdateSourceStatusFunc update_source_status_func,
      SbAudioSinkConsumeFramesFunc consume_frames_func,
      void* context);
  ~GStreamerAudioSink() override;

  bool IsType(Type* type) override { return type_ == type; }

  void SetPlaybackRate(double playback_rate) override {
    NOTIMPLEMENTED();
  }

  void SetVolume(double volume) override {
    GST_LOG_OBJECT(pipeline_, "volume %lf", volume);
    gst_stream_volume_set_volume(GST_STREAM_VOLUME(pipeline_),
                                 GST_STREAM_VOLUME_FORMAT_LINEAR, volume);
  }

 private:
  static void* AudioThreadEntryPoint(void* context);
  static gboolean BusMessageCallback(GstBus* bus,
                                     GstMessage* message,
                                     gpointer user_data);
  static void AppSrcNeedData(GstAppSrc* src, guint length, gpointer user_data);
  static void AppSrcEnoughData(GstAppSrc* src, gpointer user_data);
  static void AutoAudioSinkChildAddedCallback(GstChildProxy* proxy,
                                              GObject* object,
                                              gchar* name,
                                              gpointer user_data);

  size_t GetBytesPerFrame() const {
    return channels_ * GetBytesPerSample(audio_sample_type_);
  }

  Type* type_{nullptr};
  int channels_{0};
  int sampling_frequency_hz_{0};
  SbMediaAudioSampleType audio_sample_type_{kSbMediaAudioSampleTypeInt16};
  SbAudioSinkUpdateSourceStatusFunc update_source_status_func_{nullptr};
  SbAudioSinkConsumeFramesFunc consume_frame_func_{nullptr};
  SbAudioSinkFrameBuffers frame_buffers_{nullptr};
  int frame_buffers_size_in_frames_{0};
  SbThread audio_loop_thread_{kSbThreadInvalid};
  void* context_{nullptr};
  ::starboard::Mutex mutex_;
  GstElement* pipeline_{nullptr};
  GstElement* appsrc_{nullptr};
  GstElement* queue_{nullptr};
  GstElement* audiosink_{nullptr};
  GMainLoop* mainloop_{nullptr};
  guint source_id_{0};
  bool destroying_{false};
  bool enough_data_{false};
  std::string file_name_;
  int total_frames_{0};
};

GStreamerAudioSink::GStreamerAudioSink(
    Type* type,
    int channels,
    int sampling_frequency_hz,
    SbMediaAudioSampleType audio_sample_type,
    SbMediaAudioFrameStorageType audio_frame_storage_type,
    SbAudioSinkFrameBuffers frame_buffers,
    int frame_buffers_size_in_frames,
    SbAudioSinkUpdateSourceStatusFunc update_source_status_func,
    SbAudioSinkConsumeFramesFunc consume_frame_func,
    void* context)
    : type_(type),
      channels_(channels),
      sampling_frequency_hz_(sampling_frequency_hz),
      audio_sample_type_(audio_sample_type),
      update_source_status_func_(update_source_status_func),
      consume_frame_func_(consume_frame_func),
      frame_buffers_(frame_buffers),
      frame_buffers_size_in_frames_(frame_buffers_size_in_frames),
      context_(context) {
  GST_DEBUG_CATEGORY_INIT(cobalt_gst_audio_sink_debug, "gstaudsink", 0,
                          "Cobalt audio sink");

  GST_TRACE("TID: %d", SbThreadGetId());

  DCHECK(audio_frame_storage_type == kSbMediaAudioFrameStorageTypeInterleaved)
      << "It seems SbAudioSinkIsAudioFrameStorageTypeSupported() was changed "
      << "without adjustng here.";

  const char* format =
      audio_sample_type == kSbMediaAudioSampleTypeFloat32 ? "F32LE" : "S16LE";
  GstCaps* audio_caps = gst_caps_new_simple(
      "audio/x-raw", "format", G_TYPE_STRING, format, "rate", G_TYPE_INT,
      sampling_frequency_hz, "channels", G_TYPE_INT, channels, "layout",
      G_TYPE_STRING, "interleaved", nullptr);

  appsrc_ = gst_element_factory_make("appsrc", "source");
  GstAppSrcCallbacks callbacks = {&GStreamerAudioSink::AppSrcNeedData,
                                  &GStreamerAudioSink::AppSrcEnoughData,
                                  nullptr};
  gst_app_src_set_callbacks(GST_APP_SRC(appsrc_), &callbacks, this, nullptr);
  gst_app_src_set_max_bytes(GST_APP_SRC(appsrc_),
                            kFramesPerRequest * GetBytesPerFrame());
  g_object_set(appsrc_, "format", GST_FORMAT_TIME, nullptr);
  gst_app_src_set_caps(GST_APP_SRC(appsrc_), audio_caps);

  audiosink_ = gst_element_factory_make("autoaudiosink", "sink");
  g_signal_connect(
      audiosink_, "child-added",
      G_CALLBACK(&GStreamerAudioSink::AutoAudioSinkChildAddedCallback), this);

  pipeline_ = gst_pipeline_new("audio");
  GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline_));
  source_id_ =
      gst_bus_add_watch(bus, &GStreamerAudioSink::BusMessageCallback, this);
  gst_object_unref(bus);
  mainloop_ = g_main_loop_new(NULL, FALSE);

  GstElement* convert = gst_element_factory_make("audioconvert", nullptr);
  GstElement* resample = gst_element_factory_make("audioresample", nullptr);
  queue_ = gst_element_factory_make("queue", nullptr);
  g_object_set(queue_, "max-size-bytes", kFramesPerRequest * GetBytesPerFrame(),
               nullptr);
  gst_bin_add_many(GST_BIN(pipeline_), appsrc_, convert, resample, queue_,
                   audiosink_, nullptr);
  gst_element_link_many(appsrc_, convert, resample, queue_, audiosink_,
                        nullptr);
  gst_caps_unref(audio_caps);

  gst_element_set_state(pipeline_, GST_STATE_PLAYING);

  audio_loop_thread_ = SbThreadCreate(
      0, kSbThreadPriorityRealTime, kSbThreadNoAffinity, true, "audio_loop",
      &GStreamerAudioSink::AudioThreadEntryPoint, this);
  SB_DCHECK(SbThreadIsValid(audio_loop_thread_));
}

GStreamerAudioSink::~GStreamerAudioSink() {
  GST_TRACE_OBJECT(pipeline_, "TID: %d", SbThreadGetId());

  {
    ::starboard::ScopedLock lock(mutex_);
    destroying_ = true;
  }
  SbThreadJoin(audio_loop_thread_, nullptr);

  gst_element_set_state(pipeline_, GST_STATE_NULL);
  g_source_remove(source_id_);
  GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(pipeline_));
  gst_bus_set_sync_handler(bus, nullptr, nullptr, nullptr);
  gst_object_unref(bus);
  g_main_loop_unref(mainloop_);
  gst_object_unref(pipeline_);
}

// static
void* GStreamerAudioSink::AudioThreadEntryPoint(void* context) {
  SB_DCHECK(context);

  GStreamerAudioSink* sink = reinterpret_cast<GStreamerAudioSink*>(context);
  GST_TRACE_OBJECT(sink->pipeline_, "TID: %d", SbThreadGetId());
  g_main_loop_run(sink->mainloop_);

  return nullptr;
}

// static
gboolean GStreamerAudioSink::BusMessageCallback(GstBus* bus,
                                                GstMessage* message,
                                                gpointer user_data) {
  SB_UNREFERENCED_PARAMETER(bus);

  GStreamerAudioSink* sink = static_cast<GStreamerAudioSink*>(user_data);

  GST_TRACE_OBJECT(sink->pipeline_, "TID: %d", SbThreadGetId());

  switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_EOS:
      if (GST_MESSAGE_SRC(message) == GST_OBJECT(sink->pipeline_)) {
        GST_INFO_OBJECT(sink->pipeline_, "EOS");
        if (sink->destroying_)
          g_main_loop_quit(sink->mainloop_);
      }
      break;

    case GST_MESSAGE_ERROR: {
      GError* err = nullptr;
      gchar* debug = nullptr;
      gst_message_parse_error(message, &err, &debug);
      GST_ERROR("Error %d: %s (%s)", err->code, err->message, debug);
      g_free(debug);
      g_error_free(err);
      break;
    }

    case GST_MESSAGE_STATE_CHANGED:
      if (GST_MESSAGE_SRC(message) == GST_OBJECT(sink->pipeline_)) {
        GstState oldState, newState, pending;
        gst_message_parse_state_changed(message, &oldState, &newState,
                                        &pending);

        GST_INFO_OBJECT(sink->pipeline_,
                        "State changed (old: %s, new: %s, pending: %s)",
                        gst_element_state_get_name(oldState),
                        gst_element_state_get_name(newState),
                        gst_element_state_get_name(pending));

        std::string file_name = "cobalt_";
        file_name += (GST_OBJECT_NAME(sink->pipeline_));
        file_name += "_";
        file_name += gst_element_state_get_name(oldState);
        file_name += "_";
        file_name += gst_element_state_get_name(newState);
        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS(GST_BIN(sink->pipeline_),
                                          GST_DEBUG_GRAPH_SHOW_ALL,
                                          file_name.c_str());
      }
      break;

    default:
      GST_LOG("Got GST message %s from %s", GST_MESSAGE_TYPE_NAME(message),
              GST_MESSAGE_SRC_NAME(message));
      break;
  }

  return TRUE;
}

// static
void GStreamerAudioSink::AppSrcNeedData(GstAppSrc* src,
                                        guint length,
                                        gpointer user_data) {
  SB_UNREFERENCED_PARAMETER(src);

  GStreamerAudioSink* sink = reinterpret_cast<GStreamerAudioSink*>(user_data);

  GST_TRACE_OBJECT(sink->pipeline_, "TID: %d", SbThreadGetId());

  sink->enough_data_ = false;
  int frames_in_buffer = 0;
  int offset_in_frames = 0;
  bool is_playing = true;
  bool is_eos_reached = false;
  while (!is_eos_reached && !sink->enough_data_) {
    bool destroying = false;
    {
      ::starboard::ScopedLock lock(sink->mutex_);
      destroying = sink->destroying_;
    }

    if (destroying) {
      GST_DEBUG_OBJECT(sink->pipeline_,
                       "GStreamerAudioSink::AppSrcNeedData "
                       "bailing out");
      gst_app_src_end_of_stream(GST_APP_SRC(sink->appsrc_));
      return;
    } else {
      sink->update_source_status_func_(&frames_in_buffer, &offset_in_frames,
                                       &is_playing, &is_eos_reached,
                                       sink->context_);
      GST_DEBUG_OBJECT(sink->pipeline_,
                       "Updated: frames in buff: %d, offset: %d"
                       " is_playing: %d, eos %d",
                       frames_in_buffer, offset_in_frames, is_playing,
                       is_eos_reached);

      int frames_to_write =
          std::min(kFramesPerRequest, frames_in_buffer - offset_in_frames);

      if (is_playing && frames_to_write > 0) {
        if (is_eos_reached) {
          GST_LOG_OBJECT(sink->pipeline_, "EOS");
          gst_app_src_end_of_stream(GST_APP_SRC(sink->appsrc_));
        } else {
          GstBuffer* buffer = gst_buffer_new_allocate(
              nullptr, frames_to_write * sink->GetBytesPerFrame(), nullptr);
          uint8_t* beginning = static_cast<uint8_t*>(sink->frame_buffers_[0]) +
                               offset_in_frames * sink->GetBytesPerFrame();
          gst_buffer_fill(buffer, 0, beginning,
                          frames_to_write * sink->GetBytesPerFrame());
          GST_DEBUG_OBJECT(sink->pipeline_, "Pushing %d frames (%d bytes)",
                           frames_to_write,
                           frames_to_write * sink->GetBytesPerFrame());
          auto timestamp = gst_util_uint64_scale(
              sink->total_frames_, GST_SECOND, sink->sampling_frequency_hz_);
          GST_BUFFER_TIMESTAMP(buffer) = timestamp;
          sink->total_frames_ += frames_to_write;
          GST_BUFFER_DURATION(buffer) =
              gst_util_uint64_scale(sink->total_frames_, GST_SECOND,
                                    sink->sampling_frequency_hz_) -
              timestamp;
          GST_DEBUG_OBJECT(sink->pipeline_,
                           "Buffer to be pushed has %" GST_TIME_FORMAT
                           " ts and %" GST_TIME_FORMAT " dur",
                           GST_TIME_ARGS(GST_BUFFER_TIMESTAMP(buffer)),
                           GST_TIME_ARGS(GST_BUFFER_DURATION(buffer)));
          gst_app_src_push_buffer(GST_APP_SRC(sink->appsrc_), buffer);

          GST_DEBUG_OBJECT(sink->pipeline_,
                           "Update consumed by %d. Total %d"
                           "(%d b)",
                           frames_to_write, sink->total_frames_,
                           sink->total_frames_ * sink->GetBytesPerFrame());
          sink->consume_frame_func_(frames_to_write,
#if SB_HAS(ASYNC_AUDIO_FRAMES_REPORTING)
                                    SbTimeGetMonotonicNow(),
#endif  // SB_HAS(ASYNC_AUDIO_FRAMES_REPORTING)
                                    sink->context_);

#if defined(DUMP_PCM_TO_FILE)
          if (sink->file_name_.empty()) {
            sink->file_name_ = "/tmp/sound" +
                               std::to_string(SbTimeToPosix(SbTimeGetNow())) +
                               ".pcm";
          }
          SbFileError error;
          bool created;
          SbFile file = SbFileOpen(
              sink->file_name_.c_str(),
              SbFileFlags::kSbFileOpenAlways | SbFileFlags::kSbFileWrite,
              &created, &error);
          if (SbFileIsValid(file)) {
            SbFileSeek(file, SbFileWhence::kSbFileFromEnd, 0);
            SbFileWrite(file,
                        static_cast<const char*>(sink->frame_buffers_[0]) +
                            offset_in_frames * sink->GetBytesPerFrame(),
                        frames_to_write * sink->GetBytesPerFrame());
            SbFileClose(file);
          }
#endif
        }
      }
    }
  }
}

// static
void GStreamerAudioSink::AppSrcEnoughData(GstAppSrc* src, gpointer user_data) {
  SB_UNREFERENCED_PARAMETER(src);
  GStreamerAudioSink* sink = static_cast<GStreamerAudioSink*>(user_data);

  sink->enough_data_ = true;
  GST_TRACE_OBJECT(sink->pipeline_, "TID: %d", SbThreadGetId());
}

// static
void GStreamerAudioSink::AutoAudioSinkChildAddedCallback(GstChildProxy* obj,
                                                         GObject* object,
                                                         gchar* name,
                                                         gpointer user_data) {
  SB_UNREFERENCED_PARAMETER(obj);
  SB_UNREFERENCED_PARAMETER(name);
  GStreamerAudioSink* sink = static_cast<GStreamerAudioSink*>(user_data);
  if (GST_IS_AUDIO_BASE_SINK(object)) {
    static constexpr int kLatencyTimeValue = 50;
    g_object_set(GST_AUDIO_BASE_SINK(object), "buffer-time",
                 static_cast<gint64>(kLatencyTimeValue * kSbTimeMillisecond),
                 nullptr);
    g_object_set(sink->queue_, "min-threshold-time",
                 kLatencyTimeValue * kSbTimeMillisecond *
                     kSbTimeNanosecondsPerMicrosecond,
                 nullptr);
  }
}

}  // namespace

SbAudioSink GStreamerAudioSinkType::Create(
    int channels,
    int sampling_frequency_hz,
    SbMediaAudioSampleType audio_sample_type,
    SbMediaAudioFrameStorageType audio_frame_storage_type,
    SbAudioSinkFrameBuffers frame_buffers,
    int frame_buffers_size_in_frames,
    SbAudioSinkUpdateSourceStatusFunc update_source_status_func,
    SbAudioSinkConsumeFramesFunc consume_frames_func,
    void* context) {
  return new GStreamerAudioSink(
      this, channels, sampling_frequency_hz, audio_sample_type,
      audio_frame_storage_type, frame_buffers, frame_buffers_size_in_frames,
      update_source_status_func, consume_frames_func, context);
}

}  // namespace audio_sink
}  // namespace shared
}  // namespace wpe
}  // namespace starboard
}  // namespace third_party

using third_party::starboard::wpe::shared::audio_sink::GStreamerAudioSinkType;

// static
void SbAudioSinkPrivate::PlatformInitialize() {
  auto* sink_type = GStreamerAudioSinkType::CreateInstance();
  SetPrimaryType(sink_type);
  EnableFallbackToStub();
}

// static
void SbAudioSinkPrivate::PlatformTearDown() {
  auto* sink_type = GetPrimaryType();
  SetPrimaryType(NULL);
  GStreamerAudioSinkType::DestroyInstance(
      static_cast<GStreamerAudioSinkType*>(sink_type));
}
