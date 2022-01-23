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

#include <string>

#include "starboard/player.h"

#include "starboard/common/log.h"
#include "starboard/common/media.h"
#include "starboard/common/string.h"
#include "starboard/configuration.h"
#include "starboard/decode_target.h"
#include "starboard/shared/media_session/playback_state.h"
#include "starboard/shared/starboard/media/media_support_internal.h"
#include "starboard/shared/starboard/player/filter/filter_based_player_worker_handler.h"
#include "starboard/shared/starboard/player/player_internal.h"
#include "starboard/shared/starboard/player/player_worker.h"
#if SB_PLAYER_ENABLE_VIDEO_DUMPER
#include "starboard/shared/starboard/player/video_dmp_writer.h"
#endif  // SB_PLAYER_ENABLE_VIDEO_DUMPER

using starboard::shared::media_session::
    UpdateActiveSessionPlatformPlaybackState;
using starboard::shared::media_session::kPlaying;
using starboard::shared::starboard::player::filter::
    FilterBasedPlayerWorkerHandler;
using starboard::shared::starboard::player::PlayerWorker;

#if SB_HAS(PLAYER_CREATION_AND_OUTPUT_MODE_QUERY_IMPROVEMENT)

SbPlayer SbPlayerCreate(SbWindow window,
                        const SbPlayerCreationParam* creation_param,
                        SbPlayerDeallocateSampleFunc sample_deallocate_func,
                        SbPlayerDecoderStatusFunc decoder_status_func,
                        SbPlayerStatusFunc player_status_func,
                        SbPlayerErrorFunc player_error_func,
                        void* context,
                        SbDecodeTargetGraphicsContextProvider* provider) {
  if (!player_error_func) {
    SB_LOG(ERROR) << "|player_error_func| cannot be null.";
    return kSbPlayerInvalid;
  }

  if (!creation_param) {
    SB_LOG(ERROR) << "CreationParam cannot be null.";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      "CreationParam cannot be null");
    return kSbPlayerInvalid;
  }

  bool has_audio =
      creation_param->audio_sample_info.codec != kSbMediaAudioCodecNone;
  bool has_video =
      creation_param->video_sample_info.codec != kSbMediaVideoCodecNone;

  const char* audio_mime =
      has_audio ? creation_param->audio_sample_info.mime : "";
  const char* video_mime =
      has_video ? creation_param->video_sample_info.mime : "";
  const char* max_video_capabilities =
      has_video ? creation_param->video_sample_info.max_video_capabilities : "";

  if (!audio_mime) {
    SB_LOG(ERROR) << "creation_param->audio_sample_info.mime cannot be null.";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      "creation_param->video_sample_info.mime cannot be null");
    return kSbPlayerInvalid;
  }
  if (!video_mime) {
    SB_LOG(ERROR) << "creation_param->video_sample_info.mime cannot be null.";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      "creation_param->video_sample_info.mime cannot be null");
    return kSbPlayerInvalid;
  }
  if (!max_video_capabilities) {
    SB_LOG(ERROR) << "creation_param->video_sample_info.max_video_capabilities"
                  << " cannot be null.";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      "creation_param->video_sample_info.max_video_"
                      "capabilities cannot be null");
    return kSbPlayerInvalid;
  }

  SB_LOG(INFO) << "SbPlayerCreate() called with audio mime \"" << audio_mime
               << "\", video mime \"" << video_mime
               << "\", and max video capabilities \"" << max_video_capabilities
               << "\".";

  SbMediaAudioCodec audio_codec = creation_param->audio_sample_info.codec;
  SbMediaVideoCodec video_codec = creation_param->video_sample_info.codec;
#if SB_PLAYER_ENABLE_VIDEO_DUMPER
  SbDrmSystem drm_system = creation_param->drm_system;
#endif  // SB_PLAYER_ENABLE_VIDEO_DUMPER
  const SbMediaAudioSampleInfo* audio_sample_info =
      &creation_param->audio_sample_info;
  const auto output_mode = creation_param->output_mode;

#else   // SB_HAS(PLAYER_CREATION_AND_OUTPUT_MODE_QUERY_IMPROVEMENT)

SbPlayer SbPlayerCreate(SbWindow window,
                        SbMediaVideoCodec video_codec,
                        SbMediaAudioCodec audio_codec,
                        SbDrmSystem drm_system,
                        const SbMediaAudioSampleInfo* audio_sample_info,
                        const char* max_video_capabilities,
                        SbPlayerDeallocateSampleFunc sample_deallocate_func,
                        SbPlayerDecoderStatusFunc decoder_status_func,
                        SbPlayerStatusFunc player_status_func,
                        SbPlayerErrorFunc player_error_func,
                        void* context,
                        SbPlayerOutputMode output_mode,
                        SbDecodeTargetGraphicsContextProvider* provider) {
#endif  // SB_HAS(PLAYER_CREATION_AND_OUTPUT_MODE_QUERY_IMPROVEMENT)

  if (!player_error_func) {
    SB_LOG(ERROR) << "|player_error_func| cannot be null.";
    return kSbPlayerInvalid;
  }

  if (audio_sample_info) {
    SB_DCHECK(audio_sample_info->codec == audio_codec);
  }

  if (!sample_deallocate_func) {
    SB_LOG(ERROR) << "|sample_deallocate_func| cannot be null.";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      "|sample_deallocate_func| cannot be null");
    return kSbPlayerInvalid;
  }

  if (!decoder_status_func) {
    SB_LOG(ERROR) << "|decoder_status_func| cannot be null.";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      "|decoder_status_func| cannot be null");
    return kSbPlayerInvalid;
  }

  if (!player_status_func) {
    SB_LOG(ERROR) << "|player_status_func| cannot be null.";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      "|player_status_func| cannot be null");
    return kSbPlayerInvalid;
  }

  const int64_t kDefaultBitRate = 0;
  if (audio_codec != kSbMediaAudioCodecNone &&
      !SbMediaIsAudioSupported(audio_codec,
#if SB_API_VERSION >= 12
                               audio_mime,
#endif  // SB_API_VERSION >= 12
                               kDefaultBitRate)) {
    SB_LOG(ERROR) << "Unsupported audio codec "
                  << starboard::GetMediaAudioCodecName(audio_codec) << ".";
    player_error_func(
        kSbPlayerInvalid, context, kSbPlayerErrorDecode,
        starboard::FormatString("Unsupported audio codec: %s",
                                starboard::GetMediaAudioCodecName(audio_codec))
            .c_str());
    return kSbPlayerInvalid;
  }

  const int kDefaultProfile = -1;
  const int kDefaultLevel = -1;
  const int kDefaultColorDepth = 8;
  const int kDefaultFrameWidth = 0;
  const int kDefaultFrameHeight = 0;
  const int kDefaultFrameRate = 0;
  if (video_codec != kSbMediaVideoCodecNone &&
      !SbMediaIsVideoSupported(
          video_codec,
#if SB_API_VERSION >= 12
          video_mime,
#endif  // SB_API_VERSION >= 12
#if SB_HAS(MEDIA_IS_VIDEO_SUPPORTED_REFINEMENT)
          kDefaultProfile, kDefaultLevel, kDefaultColorDepth,
          kSbMediaPrimaryIdUnspecified, kSbMediaTransferIdUnspecified,
          kSbMediaMatrixIdUnspecified,
#endif  // SB_HAS(MEDIA_IS_VIDEO_SUPPORTED_REFINEMENT)
          kDefaultFrameWidth, kDefaultFrameHeight, kDefaultBitRate,
          kDefaultFrameRate,
          output_mode == kSbPlayerOutputModeDecodeToTexture)) {
    SB_LOG(ERROR) << "Unsupported video codec "
                  << starboard::GetMediaVideoCodecName(video_codec) << ".";
    player_error_func(
        kSbPlayerInvalid, context, kSbPlayerErrorDecode,
        starboard::FormatString("Unsupported video codec: %s",
                                starboard::GetMediaVideoCodecName(video_codec))
            .c_str());
    return kSbPlayerInvalid;
  }

  if (audio_codec != kSbMediaAudioCodecNone && !audio_sample_info) {
    SB_LOG(ERROR)
        << "SbPlayerCreate() requires a non-NULL SbMediaAudioSampleInfo "
        << "when |audio_codec| is not kSbMediaAudioCodecNone.";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      "SbPlayerCreate() requires a non-NULL "
                      "SbMediaAudioSampleInfo when |audio_codec| is not "
                      "kSbMediaAudioCodecNone");
    return kSbPlayerInvalid;
  }

  if (audio_codec == kSbMediaAudioCodecNone &&
      video_codec == kSbMediaVideoCodecNone) {
    SB_LOG(ERROR) << "SbPlayerCreate() requires at least one audio track or"
                  << " one video track.";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      "SbPlayerCreate() requires at least one audio track or "
                      "one video track");
    return kSbPlayerInvalid;
  }

  std::string error_message;
  if (audio_sample_info &&
      audio_sample_info->number_of_channels > SbAudioSinkGetMaxChannels()) {
    error_message = starboard::FormatString(
        "Number of audio channels (%d) exceeds the maximum number of audio "
        "channels supported by this platform (%d)",
        audio_sample_info->number_of_channels, SbAudioSinkGetMaxChannels());
    SB_LOG(ERROR) << error_message << ".";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      error_message.c_str());
    return kSbPlayerInvalid;
  }

#if SB_HAS(PLAYER_CREATION_AND_OUTPUT_MODE_QUERY_IMPROVEMENT)
  if (SbPlayerGetPreferredOutputMode(creation_param) != output_mode) {
    error_message = starboard::FormatString("Unsupported player output mode %d",
                                            output_mode);
    SB_LOG(ERROR) << error_message << ".";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      error_message.c_str());
    return kSbPlayerInvalid;
  }
#else   // SB_HAS(PLAYER_CREATION_AND_OUTPUT_MODE_QUERY_IMPROVEMENT)
  if (!SbPlayerOutputModeSupported(output_mode, video_codec, drm_system)) {
    error_message = starboard::FormatString("Unsupported player output mode %d",
                                            output_mode);
    SB_LOG(ERROR) << error_message << ".";
    player_error_func(kSbPlayerInvalid, context, kSbPlayerErrorDecode,
                      error_message.c_str());
    return kSbPlayerInvalid;
  }
#endif  // SB_HAS(PLAYER_CREATION_AND_OUTPUT_MODE_QUERY_IMPROVEMENT)

  UpdateActiveSessionPlatformPlaybackState(kPlaying);

#if SB_HAS(PLAYER_CREATION_AND_OUTPUT_MODE_QUERY_IMPROVEMENT)
  starboard::scoped_ptr<PlayerWorker::Handler> handler(
      new FilterBasedPlayerWorkerHandler(creation_param, provider));
#else   // SB_HAS(PLAYER_CREATION_AND_OUTPUT_MODE_QUERY_IMPROVEMENT)
  starboard::scoped_ptr<PlayerWorker::Handler> handler(
      new FilterBasedPlayerWorkerHandler(video_codec, audio_codec, drm_system,
                                         audio_sample_info, output_mode,
                                         provider));
#endif  // SB_HAS(PLAYER_CREATION_AND_OUTPUT_MODE_QUERY_IMPROVEMENT)

  SbPlayer player = SbPlayerPrivate::CreateInstance(
      audio_codec, video_codec, audio_sample_info, sample_deallocate_func,
      decoder_status_func, player_status_func, player_error_func, context,
      handler.Pass());

#if SB_PLAYER_ENABLE_VIDEO_DUMPER
  using ::starboard::shared::starboard::player::video_dmp::VideoDmpWriter;
  VideoDmpWriter::OnPlayerCreate(player, audio_codec, video_codec, drm_system,
                                 audio_sample_info);
#endif  // SB_PLAYER_ENABLE_VIDEO_DUMPER

  return player;
}
