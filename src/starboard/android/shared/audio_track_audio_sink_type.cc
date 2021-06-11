// Copyright 2017 The Cobalt Authors. All Rights Reserved.
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

#include "starboard/android/shared/audio_track_audio_sink_type.h"

#include <algorithm>
#include <string>
#include <vector>

#include "starboard/common/string.h"
#include "starboard/shared/starboard/player/filter/common.h"

namespace {
starboard::android::shared::AudioTrackAudioSinkType*
    audio_track_audio_sink_type_;
}

namespace starboard {
namespace android {
namespace shared {
namespace {

// The same as AudioTrack.ERROR_DEAD_OBJECT.
const int kAudioTrackErrorDeadObject = -6;

// The maximum number of frames that can be written to android audio track per
// write request. If we don't set this cap for writing frames to audio track,
// we will repeatedly allocate a large byte array which cannot be consumed by
// audio track completely.
const int kMaxFramesPerRequest = 65536;

// Most Android audio HAL updates audio time for A/V synchronization on audio
// sync frames. For example, audio HAL may try to render when it gets an entire
// sync frame and then update audio time. Shorter duration of sync frame
// improves the accuracy of audio time, especially at the beginning of a
// playback, as otherwise the audio time during the initial update may be too
// large (non zero) and results in dropped video frames.
const SbTime kMaxDurationPerRequestInTunnelMode = 16 * kSbTimeMillisecond;

const jint kNoOffset = 0;
const size_t kSilenceFramesPerAppend = 1024;

const int kMaxRequiredFrames = 16 * 1024;
const int kRequiredFramesIncrement = 2 * 1024;
const int kMinStablePlayedFrames = 12 * 1024;

const int kSampleFrequency22050 = 22050;
const int kSampleFrequency48000 = 48000;

// Helper function to compute the size of the two valid starboard audio sample
// types.
size_t GetSampleSize(SbMediaAudioSampleType sample_type) {
  switch (sample_type) {
    case kSbMediaAudioSampleTypeFloat32:
      return sizeof(float);
    case kSbMediaAudioSampleTypeInt16Deprecated:
      return sizeof(int16_t);
  }
  SB_NOTREACHED();
  return 0u;
}

int GetAudioFormatSampleType(SbMediaAudioSampleType sample_type) {
  switch (sample_type) {
    case kSbMediaAudioSampleTypeFloat32:
      // Android AudioFormat.ENCODING_PCM_FLOAT.
      return 4;
    case kSbMediaAudioSampleTypeInt16Deprecated:
      // Android AudioFormat.ENCODING_PCM_16BIT.
      return 2;
  }
  SB_NOTREACHED();
  return 0u;
}

void* IncrementPointerByBytes(void* pointer, size_t offset) {
  return static_cast<uint8_t*>(pointer) + offset;
}

int GetMaxFramesPerRequestForTunnelMode(int sampling_frequency_hz) {
  auto max_frames = kMaxDurationPerRequestInTunnelMode * sampling_frequency_hz /
                    kSbTimeSecond;
  return (max_frames + 15) / 16 * 16;  // align to 16
}

}  // namespace

AudioTrackAudioSink::AudioTrackAudioSink(
    Type* type,
    int channels,
    int sampling_frequency_hz,
    SbMediaAudioSampleType sample_type,
    SbAudioSinkFrameBuffers frame_buffers,
    int frames_per_channel,
    int preferred_buffer_size_in_bytes,
    SbAudioSinkUpdateSourceStatusFunc update_source_status_func,
    ConsumeFramesFunc consume_frames_func,
    SbAudioSinkPrivate::ErrorFunc error_func,
    SbTime start_time,
    int tunnel_mode_audio_session_id,
    bool enable_audio_routing,
    void* context)
    : type_(type),
      channels_(channels),
      sampling_frequency_hz_(sampling_frequency_hz),
      sample_type_(sample_type),
      frame_buffer_(frame_buffers[0]),
      frames_per_channel_(frames_per_channel),
      update_source_status_func_(update_source_status_func),
      consume_frames_func_(consume_frames_func),
      error_func_(error_func),
      start_time_(start_time),
      tunnel_mode_audio_session_id_(tunnel_mode_audio_session_id),
      max_frames_per_request_(
          tunnel_mode_audio_session_id_ == -1
              ? kMaxFramesPerRequest
              : GetMaxFramesPerRequestForTunnelMode(sampling_frequency_hz_)),
      context_(context) {
  SB_DCHECK(update_source_status_func_);
  SB_DCHECK(consume_frames_func_);
  SB_DCHECK(frame_buffer_);
  SB_DCHECK(SbAudioSinkIsAudioSampleTypeSupported(sample_type));

  // TODO: Support query if platform supports float type for tunnel mode.
  if (tunnel_mode_audio_session_id_ != -1) {
    SB_DCHECK(sample_type_ == kSbMediaAudioSampleTypeInt16Deprecated);
  }

  SB_LOG(INFO) << "Creating audio sink starts at " << start_time_;

  JniEnvExt* env = JniEnvExt::Get();
  ScopedLocalJavaRef<jobject> j_audio_output_manager(
      env->CallStarboardObjectMethodOrAbort(
          "getAudioOutputManager", "()Ldev/cobalt/media/AudioOutputManager;"));
  jobject j_audio_track_bridge = env->CallObjectMethodOrAbort(
      j_audio_output_manager.Get(), "createAudioTrackBridge",
      "(IIIIZI)Ldev/cobalt/media/AudioTrackBridge;",
      GetAudioFormatSampleType(sample_type_), sampling_frequency_hz_, channels_,
      preferred_buffer_size_in_bytes, enable_audio_routing,
      tunnel_mode_audio_session_id_);
  if (!j_audio_track_bridge) {
    // One of the cases that this may hit is when output happened to be switched
    // to a device that doesn't support tunnel mode.
    // TODO: Find a way to exclude the device from tunnel mode playback, to
    //       avoid infinite loop in creating the audio sink on a device
    //       claims to support tunnel mode but fails to create the audio sink.
    // TODO: Currently this will be reported as a general decode error,
    //       investigate if this can be reported as a capability changed error.
    return;
  }
  j_audio_track_bridge_ = env->ConvertLocalRefToGlobalRef(j_audio_track_bridge);
  if (sample_type_ == kSbMediaAudioSampleTypeFloat32) {
    j_audio_data_ = env->NewFloatArray(channels_ * max_frames_per_request_);
  } else if (sample_type_ == kSbMediaAudioSampleTypeInt16Deprecated) {
    j_audio_data_ = env->NewByteArray(channels_ * GetSampleSize(sample_type_) *
                                      max_frames_per_request_);
  } else {
    SB_NOTREACHED();
  }
  SB_DCHECK(j_audio_data_) << "Failed to allocate |j_audio_data_|";

  j_audio_data_ = env->ConvertLocalRefToGlobalRef(j_audio_data_);

  audio_out_thread_ = SbThreadCreate(
      0, kSbThreadPriorityRealTime, kSbThreadNoAffinity, true,
      "audio_track_audio_out", &AudioTrackAudioSink::ThreadEntryPoint, this);
  SB_DCHECK(SbThreadIsValid(audio_out_thread_));
}

AudioTrackAudioSink::~AudioTrackAudioSink() {
  quit_ = true;

  if (SbThreadIsValid(audio_out_thread_)) {
    SbThreadJoin(audio_out_thread_, NULL);
  }

  JniEnvExt* env = JniEnvExt::Get();
  if (j_audio_track_bridge_) {
    ScopedLocalJavaRef<jobject> j_audio_output_manager(
        env->CallStarboardObjectMethodOrAbort(
            "getAudioOutputManager",
            "()Ldev/cobalt/media/AudioOutputManager;"));
    env->CallVoidMethodOrAbort(
        j_audio_output_manager.Get(), "destroyAudioTrackBridge",
        "(Ldev/cobalt/media/AudioTrackBridge;)V", j_audio_track_bridge_);
    env->DeleteGlobalRef(j_audio_track_bridge_);
    j_audio_track_bridge_ = NULL;
  }

  if (j_audio_data_) {
    env->DeleteGlobalRef(j_audio_data_);
    j_audio_data_ = NULL;
  }
}

void AudioTrackAudioSink::SetPlaybackRate(double playback_rate) {
  SB_DCHECK(playback_rate >= 0.0);
  if (playback_rate != 0.0 && playback_rate != 1.0) {
    SB_NOTIMPLEMENTED() << "TODO: Only playback rates of 0.0 and 1.0 are "
                           "currently supported.";
    playback_rate = (playback_rate > 0.0) ? 1.0 : 0.0;
  }
  ScopedLock lock(mutex_);
  playback_rate_ = playback_rate;
}

// static
void* AudioTrackAudioSink::ThreadEntryPoint(void* context) {
  SB_DCHECK(context);
  AudioTrackAudioSink* sink = reinterpret_cast<AudioTrackAudioSink*>(context);
  sink->AudioThreadFunc();

  return NULL;
}

// TODO: Break down the function into manageable pieces.
void AudioTrackAudioSink::AudioThreadFunc() {
  JniEnvExt* env = JniEnvExt::Get();
  bool was_playing = false;
  int frames_in_audio_track = 0;

  SB_LOG(INFO) << "AudioTrackAudioSink thread started.";

  int accumulated_written_frames = 0;
  // TODO: |last_playback_head_changed_at| is also reset when a warning is
  //       logged after the playback head position hasn't been updated for a
  //       while.  We should refine the name to make it better reflect its
  //       usage.
  SbTime last_playback_head_changed_at = -1;
  SbTime playback_head_not_changed_duration = 0;
  SbTime last_written_succeeded_at = -1;

  while (!quit_) {
    int playback_head_position = 0;
    SbTime frames_consumed_at = 0;
    bool new_audio_device_added = env->CallBooleanMethodOrAbort(
        j_audio_track_bridge_, "getAndResetHasNewAudioDeviceAdded", "()Z");
    if (new_audio_device_added) {
      SB_LOG(INFO) << "New audio device added.";
      error_func_(kSbPlayerErrorCapabilityChanged, "New audio device added.",
                  context_);
      break;
    }

    if (was_playing) {
      ScopedLocalJavaRef<jobject> j_audio_timestamp(
          env->CallObjectMethodOrAbort(j_audio_track_bridge_,
                                       "getAudioTimestamp",
                                       "()Landroid/media/AudioTimestamp;"));
      playback_head_position = env->GetLongFieldOrAbort(j_audio_timestamp.Get(),
                                                        "framePosition", "J");
      frames_consumed_at =
          env->GetLongFieldOrAbort(j_audio_timestamp.Get(), "nanoTime", "J") /
          1000;

      SB_DCHECK(playback_head_position >= last_playback_head_position_);

      playback_head_position =
          std::max(playback_head_position, last_playback_head_position_);
      int frames_consumed =
          playback_head_position - last_playback_head_position_;
      SbTime now = SbTimeGetMonotonicNow();

      if (last_playback_head_changed_at == -1) {
        last_playback_head_changed_at = now;
      }
      if (last_playback_head_position_ == playback_head_position) {
        SbTime elapsed = now - last_playback_head_changed_at;
        if (elapsed > 5 * kSbTimeSecond) {
          playback_head_not_changed_duration += elapsed;
          last_playback_head_changed_at = now;
          SB_LOG(INFO) << "last playback head position is "
                       << last_playback_head_position_
                       << " and it hasn't been updated for " << elapsed
                       << " microseconds.";
        }
      } else {
        last_playback_head_changed_at = now;
        playback_head_not_changed_duration = 0;
      }

      last_playback_head_position_ = playback_head_position;
      frames_consumed = std::min(frames_consumed, frames_in_audio_track);

      if (frames_consumed != 0) {
        SB_DCHECK(frames_consumed >= 0);
        consume_frames_func_(frames_consumed, frames_consumed_at, context_);
        frames_in_audio_track -= frames_consumed;
      }
    }

    int frames_in_buffer;
    int offset_in_frames;
    bool is_playing;
    bool is_eos_reached;
    update_source_status_func_(&frames_in_buffer, &offset_in_frames,
                               &is_playing, &is_eos_reached, context_);
    {
      ScopedLock lock(mutex_);
      if (playback_rate_ == 0.0) {
        is_playing = false;
      }
    }

    if (was_playing && !is_playing) {
      was_playing = false;
      env->CallVoidMethodOrAbort(j_audio_track_bridge_, "pause", "()V");
      SB_LOG(INFO) << "AudioTrackAudioSink paused.";
    } else if (!was_playing && is_playing) {
      was_playing = true;
      last_playback_head_changed_at = -1;
      playback_head_not_changed_duration = 0;
      last_written_succeeded_at = -1;
      env->CallVoidMethodOrAbort(j_audio_track_bridge_, "play", "()V");
      SB_LOG(INFO) << "AudioTrackAudioSink playing.";
    }

    if (!is_playing || frames_in_buffer == 0) {
      SbThreadSleep(10 * kSbTimeMillisecond);
      continue;
    }

    int start_position =
        (offset_in_frames + frames_in_audio_track) % frames_per_channel_;
    int expected_written_frames = 0;
    if (frames_per_channel_ > offset_in_frames + frames_in_audio_track) {
      expected_written_frames = std::min(
          frames_per_channel_ - (offset_in_frames + frames_in_audio_track),
          frames_in_buffer - frames_in_audio_track);
    } else {
      expected_written_frames = frames_in_buffer - frames_in_audio_track;
    }

    expected_written_frames =
        std::min(expected_written_frames, max_frames_per_request_);

    if (expected_written_frames == 0) {
      // It is possible that all the frames in buffer are written to the
      // soundcard, but those are not being consumed. If eos is reached,
      // write silence to make sure audio track start working and avoid
      // underflow. Android audio track would not start working before
      // its buffer is fully filled once.
      if (is_eos_reached) {
        // Currently AudioDevice and AudioRenderer will write tail silence.
        // It should be reached only in tests. It's not ideal to allocate
        // a new silence buffer every time.
        const int silence_frames_per_append =
            std::min<int>(kSilenceFramesPerAppend, max_frames_per_request_);
        std::vector<uint8_t> silence_buffer(channels_ *
                                            GetSampleSize(sample_type_) *
                                            silence_frames_per_append);
        auto sync_time = start_time_ + accumulated_written_frames *
                                           kSbTimeSecond /
                                           sampling_frequency_hz_;
        // Not necessary to handle error of WriteData(), even for
        // kAudioTrackErrorDeadObject, as the audio has reached the end of
        // stream.
        // TODO: Ensure that the audio stream can still reach the end when an
        //       error occurs.
        WriteData(env, silence_buffer.data(), silence_frames_per_append,
                  sync_time);
      }

      SbThreadSleep(10 * kSbTimeMillisecond);
      continue;
    }
    SB_DCHECK(expected_written_frames > 0);
    auto sync_time = start_time_ + accumulated_written_frames * kSbTimeSecond /
                                       sampling_frequency_hz_;

    SB_DCHECK(start_position + expected_written_frames <= frames_per_channel_)
        << "start_position: " << start_position
        << ", expected_written_frames: " << expected_written_frames
        << ", frames_per_channel_: " << frames_per_channel_
        << ", frames_in_buffer: " << frames_in_buffer
        << ", frames_in_audio_track: " << frames_in_audio_track
        << ", offset_in_frames: " << offset_in_frames;

    int written_frames = WriteData(
        env,
        IncrementPointerByBytes(frame_buffer_, start_position * channels_ *
                                                   GetSampleSize(sample_type_)),
        expected_written_frames, sync_time);
    SbTime now = SbTimeGetMonotonicNow();

    if (written_frames < 0) {
      // Take all |frames_in_audio_track| as consumed since audio track could be
      // dead.
      consume_frames_func_(frames_in_audio_track, now, context_);

      bool capabilities_changed = written_frames == kAudioTrackErrorDeadObject;
      error_func_(
          capabilities_changed,
          FormatString("Error while writing frames: %d", written_frames),
          context_);
      break;
    } else if (written_frames > 0) {
      last_written_succeeded_at = now;
    }
    frames_in_audio_track += written_frames;
    accumulated_written_frames += written_frames;

    bool written_fully = (written_frames == expected_written_frames);
    auto unplayed_frames_in_time =
        frames_in_audio_track * kSbTimeSecond / sampling_frequency_hz_ -
        (now - frames_consumed_at);
    // As long as there is enough data in the buffer, run the loop in lower
    // frequency to avoid taking too much CPU.  Note that the threshold should
    // be big enough to account for the unstable playback head reported at the
    // beginning of the playback and during underrun.
    if (playback_head_position > 0 &&
        unplayed_frames_in_time > 500 * kSbTimeMillisecond) {
      SbThreadSleep(40 * kSbTimeMillisecond);
    } else if (!written_fully) {
      // Only sleep if the buffer is nearly full and the last write is partial.
      SbThreadSleep(10 * kSbTimeMillisecond);
    }
  }

  // For an immediate stop, use pause(), followed by flush() to discard audio
  // data that hasn't been played back yet.
  env->CallVoidMethodOrAbort(j_audio_track_bridge_, "pause", "()V");
  // Flushes the audio data currently queued for playback. Any data that has
  // been written but not yet presented will be discarded.
  env->CallVoidMethodOrAbort(j_audio_track_bridge_, "flush", "()V");
}

int AudioTrackAudioSink::WriteData(JniEnvExt* env,
                                   void* buffer,
                                   int expected_written_frames,
                                   SbTime sync_time) {
  if (sample_type_ == kSbMediaAudioSampleTypeFloat32) {
    int expected_written_size = expected_written_frames * channels_;
    env->SetFloatArrayRegion(static_cast<jfloatArray>(j_audio_data_), kNoOffset,
                             expected_written_size,
                             static_cast<const float*>(buffer));
    int written =
        env->CallIntMethodOrAbort(j_audio_track_bridge_, "write", "([FI)I",
                                  j_audio_data_, expected_written_size);
    if (written < 0) {
      // Error code returned as negative value, like kAudioTrackErrorDeadObject.
      return written;
    }
    SB_DCHECK(written % channels_ == 0);
    return written / channels_;
  }
  if (sample_type_ == kSbMediaAudioSampleTypeInt16Deprecated) {
    int expected_written_size =
        expected_written_frames * channels_ * GetSampleSize(sample_type_);
    env->SetByteArrayRegion(static_cast<jbyteArray>(j_audio_data_), kNoOffset,
                            expected_written_size,
                            static_cast<const jbyte*>(buffer));

    int written = env->CallIntMethodOrAbort(j_audio_track_bridge_, "write",
                                            "([BIJ)I", j_audio_data_,
                                            expected_written_size, sync_time);
    if (written < 0) {
      // Error code returned as negative value, like kAudioTrackErrorDeadObject.
      return written;
    }
    SB_DCHECK(written % (channels_ * GetSampleSize(sample_type_)) == 0);
    return written / (channels_ * GetSampleSize(sample_type_));
  }
  SB_NOTREACHED();
  return 0;
}

void AudioTrackAudioSink::SetVolume(double volume) {
  auto* env = JniEnvExt::Get();
  jint status = env->CallIntMethodOrAbort(j_audio_track_bridge_, "setVolume",
                                          "(F)I", static_cast<float>(volume));
  if (status != 0) {
    SB_LOG(ERROR) << "Failed to set volume";
  }
}

int AudioTrackAudioSink::GetUnderrunCount() {
  auto* env = JniEnvExt::Get();
  jint underrun_count = env->CallIntMethodOrAbort(j_audio_track_bridge_,
                                                  "getUnderrunCount", "()I");
  return underrun_count;
}

// static
int AudioTrackAudioSinkType::GetMinBufferSizeInFrames(
    int channels,
    SbMediaAudioSampleType sample_type,
    int sampling_frequency_hz) {
  SB_DCHECK(audio_track_audio_sink_type_);

  JniEnvExt* env = JniEnvExt::Get();
  ScopedLocalJavaRef<jobject> j_audio_output_manager(
      env->CallStarboardObjectMethodOrAbort(
          "getAudioOutputManager", "()Ldev/cobalt/media/AudioOutputManager;"));
  int audio_track_min_buffer_size = static_cast<int>(env->CallIntMethodOrAbort(
      j_audio_output_manager.Get(), "getMinBufferSize", "(III)I",
      GetAudioFormatSampleType(sample_type), sampling_frequency_hz, channels));
  int audio_track_min_buffer_size_in_frames =
      audio_track_min_buffer_size / channels / GetSampleSize(sample_type);
  return std::max(
      audio_track_min_buffer_size_in_frames,
      audio_track_audio_sink_type_->GetMinBufferSizeInFramesInternal(
          channels, sample_type, sampling_frequency_hz));
}

AudioTrackAudioSinkType::AudioTrackAudioSinkType()
    : min_required_frames_tester_(kMaxRequiredFrames,
                                  kRequiredFramesIncrement,
                                  kMinStablePlayedFrames) {}

SbAudioSink AudioTrackAudioSinkType::Create(
    int channels,
    int sampling_frequency_hz,
    SbMediaAudioSampleType audio_sample_type,
    SbMediaAudioFrameStorageType audio_frame_storage_type,
    SbAudioSinkFrameBuffers frame_buffers,
    int frames_per_channel,
    SbAudioSinkUpdateSourceStatusFunc update_source_status_func,
    SbAudioSinkPrivate::ConsumeFramesFunc consume_frames_func,
    SbAudioSinkPrivate::ErrorFunc error_func,
    void* context) {
  const SbTime kStartTime = 0;
  const int kTunnelModeAudioSessionId = -1;  // disable tunnel mode
  const bool kEnableAudioRouting = true;
  return Create(channels, sampling_frequency_hz, audio_sample_type,
                audio_frame_storage_type, frame_buffers, frames_per_channel,
                update_source_status_func, consume_frames_func, error_func,
                kStartTime, kTunnelModeAudioSessionId, kEnableAudioRouting,
                context);
}

SbAudioSink AudioTrackAudioSinkType::Create(
    int channels,
    int sampling_frequency_hz,
    SbMediaAudioSampleType audio_sample_type,
    SbMediaAudioFrameStorageType audio_frame_storage_type,
    SbAudioSinkFrameBuffers frame_buffers,
    int frames_per_channel,
    SbAudioSinkUpdateSourceStatusFunc update_source_status_func,
    SbAudioSinkPrivate::ConsumeFramesFunc consume_frames_func,
    SbAudioSinkPrivate::ErrorFunc error_func,
    SbTime start_media_time,
    int tunnel_mode_audio_session_id,
    bool enable_audio_routing,
    void* context) {
  int min_required_frames = SbAudioSinkGetMinBufferSizeInFrames(
      channels, audio_sample_type, sampling_frequency_hz);
  SB_DCHECK(frames_per_channel >= min_required_frames);
  int preferred_buffer_size_in_bytes =
      min_required_frames * channels * GetSampleSize(audio_sample_type);
  AudioTrackAudioSink* audio_sink = new AudioTrackAudioSink(
      this, channels, sampling_frequency_hz, audio_sample_type, frame_buffers,
      frames_per_channel, preferred_buffer_size_in_bytes,
      update_source_status_func, consume_frames_func, error_func,
      start_media_time, tunnel_mode_audio_session_id, enable_audio_routing,
      context);
  if (!audio_sink->IsAudioTrackValid()) {
    SB_DLOG(ERROR)
        << "AudioTrackAudioSinkType::Create failed to create audio track";
    Destroy(audio_sink);
    return kSbAudioSinkInvalid;
  }
  return audio_sink;
}

void AudioTrackAudioSinkType::TestMinRequiredFrames() {
  auto onMinRequiredFramesForWebAudioReceived =
      [&](int number_of_channels, SbMediaAudioSampleType sample_type,
          int sample_rate, int min_required_frames) {
        SB_LOG(INFO) << "Received min required frames " << min_required_frames
                     << " for " << number_of_channels << " channels, "
                     << sample_rate << "hz.";
        ScopedLock lock(min_required_frames_map_mutex_);
        min_required_frames_map_[sample_rate] = min_required_frames;
      };

  SbMediaAudioSampleType sample_type = kSbMediaAudioSampleTypeFloat32;
  if (!SbAudioSinkIsAudioSampleTypeSupported(sample_type)) {
    sample_type = kSbMediaAudioSampleTypeInt16Deprecated;
    SB_DCHECK(SbAudioSinkIsAudioSampleTypeSupported(sample_type));
  }
  min_required_frames_tester_.AddTest(2, sample_type, kSampleFrequency48000,
                                      onMinRequiredFramesForWebAudioReceived,
                                      8 * 1024);
  min_required_frames_tester_.AddTest(2, sample_type, kSampleFrequency22050,
                                      onMinRequiredFramesForWebAudioReceived,
                                      4 * 1024);
  min_required_frames_tester_.Start();
}

int AudioTrackAudioSinkType::GetMinBufferSizeInFramesInternal(
    int channels,
    SbMediaAudioSampleType sample_type,
    int sampling_frequency_hz) {
  if (sampling_frequency_hz <= kSampleFrequency22050) {
    ScopedLock lock(min_required_frames_map_mutex_);
    if (min_required_frames_map_.find(kSampleFrequency22050) !=
        min_required_frames_map_.end()) {
      return min_required_frames_map_[kSampleFrequency22050];
    }
  } else if (sampling_frequency_hz <= kSampleFrequency48000) {
    ScopedLock lock(min_required_frames_map_mutex_);
    if (min_required_frames_map_.find(kSampleFrequency48000) !=
        min_required_frames_map_.end()) {
      return min_required_frames_map_[kSampleFrequency48000];
    }
  }
  return kMaxRequiredFrames;
}

}  // namespace shared
}  // namespace android
}  // namespace starboard

// static
void SbAudioSinkPrivate::PlatformInitialize() {
  SB_DCHECK(!audio_track_audio_sink_type_);
  audio_track_audio_sink_type_ =
      new starboard::android::shared::AudioTrackAudioSinkType;
  SetPrimaryType(audio_track_audio_sink_type_);
  EnableFallbackToStub();
  audio_track_audio_sink_type_->TestMinRequiredFrames();
}

// static
void SbAudioSinkPrivate::PlatformTearDown() {
  SB_DCHECK(audio_track_audio_sink_type_ == GetPrimaryType());
  SetPrimaryType(NULL);
  delete audio_track_audio_sink_type_;
  audio_track_audio_sink_type_ = NULL;
}
