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

#ifndef STARBOARD_SHARED_STARBOARD_PLAYER_FILTER_AUDIO_RENDERER_SINK_IMPL_H_
#define STARBOARD_SHARED_STARBOARD_PLAYER_FILTER_AUDIO_RENDERER_SINK_IMPL_H_

#include "starboard/shared/starboard/player/filter/audio_renderer_sink.h"

#include "starboard/shared/starboard/audio_sink/audio_sink_internal.h"
#include "starboard/shared/starboard/thread_checker.h"

namespace starboard {
namespace shared {
namespace starboard {
namespace player {
namespace filter {

class AudioRendererSinkImpl : public AudioRendererSink {
 public:
  AudioRendererSinkImpl();
  ~AudioRendererSinkImpl() SB_OVERRIDE;

 private:
  // AudioRendererSink methods
  bool IsAudioSampleTypeSupported(
      SbMediaAudioSampleType audio_sample_type) const SB_OVERRIDE;
  bool IsAudioFrameStorageTypeSupported(
      SbMediaAudioFrameStorageType audio_frame_storage_type) const SB_OVERRIDE;
  int GetNearestSupportedSampleFrequency(int sampling_frequency_hz) const
      SB_OVERRIDE;

  bool HasStarted() const SB_OVERRIDE;
  void Start(int channels,
             int sampling_frequency_hz,
             SbMediaAudioSampleType audio_sample_type,
             SbMediaAudioFrameStorageType audio_frame_storage_type,
             SbAudioSinkFrameBuffers frame_buffers,
             int frames_per_channel,
             RenderCallback* render_callback) SB_OVERRIDE;
  void Stop() SB_OVERRIDE;

  void SetVolume(double volume) SB_OVERRIDE;
  void SetPlaybackRate(double playback_rate) SB_OVERRIDE;

  // SbAudioSink callbacks
  static void UpdateSourceStatusFunc(int* frames_in_buffer,
                                     int* offset_in_frames,
                                     bool* is_playing,
                                     bool* is_eos_reached,
                                     void* context);
  static void ConsumeFramesFunc(int frames_consumed, void* context);

  ThreadChecker thread_checker_;
  SbAudioSinkPrivate* audio_sink_;
  RenderCallback* render_callback_;
  double playback_rate_;
  double volume_;
};

}  // namespace filter
}  // namespace player
}  // namespace starboard
}  // namespace shared
}  // namespace starboard

#endif  //  STARBOARD_SHARED_STARBOARD_PLAYER_FILTER_AUDIO_RENDERER_SINK_IMPL_H_
