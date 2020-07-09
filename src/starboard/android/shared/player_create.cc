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

#include "starboard/player.h"

#include "starboard/android/shared/cobalt/android_media_session_client.h"
#include "starboard/common/log.h"
#include "starboard/configuration.h"
#include "starboard/decode_target.h"
#include "starboard/shared/starboard/player/filter/filter_based_player_worker_handler.h"
#include "starboard/shared/starboard/player/player_internal.h"
#include "starboard/shared/starboard/player/player_worker.h"

using starboard::shared::starboard::player::filter::
    FilterBasedPlayerWorkerHandler;
using starboard::shared::starboard::player::PlayerWorker;
using starboard::android::shared::cobalt::kPlaying;
using starboard::android::shared::cobalt::
    UpdateActiveSessionPlatformPlaybackState;

SbPlayer SbPlayerCreate(SbWindow window,
                        const SbPlayerCreationParam* creation_param,
                        SbPlayerDeallocateSampleFunc sample_deallocate_func,
                        SbPlayerDecoderStatusFunc decoder_status_func,
                        SbPlayerStatusFunc player_status_func,
                        SbPlayerErrorFunc player_error_func,
                        void* context,
                        SbDecodeTargetGraphicsContextProvider* provider) {
  SB_UNREFERENCED_PARAMETER(window);

  if (!creation_param) {
    SB_LOG(ERROR) << "CreationParam cannot be null.";
    return kSbPlayerInvalid;
  }

  if (!creation_param->audio_mime) {
    SB_LOG(ERROR) << "creation_param->audio_mime cannot be null.";
    return kSbPlayerInvalid;
  }
  if (!creation_param->video_mime) {
    SB_LOG(ERROR) << "creation_param->video_mime cannot be null.";
    return kSbPlayerInvalid;
  }
  if (!creation_param->max_video_capabilities) {
    SB_LOG(ERROR) << "creation_param->max_video_capabilities cannot be null.";
    return kSbPlayerInvalid;
  }

  SB_LOG(INFO) << "SbPlayerCreate() called with audio mime \""
               << creation_param->audio_mime << "\", video mime \""
               << creation_param->video_mime
               << "\", and max video capabilities \""
               << creation_param->max_video_capabilities << "\".";

  if (!sample_deallocate_func || !decoder_status_func || !player_status_func
#if SB_HAS(PLAYER_ERROR_MESSAGE)
      || !player_error_func
#endif  // SB_HAS(PLAYER_ERROR_MESSAGE)
      ) {
    return kSbPlayerInvalid;
  }

  auto audio_codec = creation_param->audio_sample_info.codec;
  auto video_codec = creation_param->video_sample_info.codec;

  if (audio_codec != kSbMediaAudioCodecNone &&
      audio_codec != kSbMediaAudioCodecAac &&
      audio_codec != kSbMediaAudioCodecOpus) {
    SB_LOG(ERROR) << "Unsupported audio codec " << audio_codec;
    return kSbPlayerInvalid;
  }

  if (video_codec != kSbMediaVideoCodecNone &&
      video_codec != kSbMediaVideoCodecH264 &&
      video_codec != kSbMediaVideoCodecVp9 &&
      video_codec != kSbMediaVideoCodecAv1) {
    SB_LOG(ERROR) << "Unsupported video codec " << video_codec;
    return kSbPlayerInvalid;
  }

  if (audio_codec == kSbMediaAudioCodecNone &&
      video_codec == kSbMediaVideoCodecNone) {
    SB_LOG(ERROR) << "SbPlayerCreate() requires at least one audio track or"
                  << " one video track.";
    return kSbPlayerInvalid;
  }

  auto output_mode = creation_param->output_mode;
  if (SbPlayerGetPreferredOutputMode(creation_param) != output_mode) {
    SB_LOG(ERROR) << "Unsupported player output mode " << output_mode;
    return kSbPlayerInvalid;
  }

  // TODO: increase this once we support multiple video windows.
  const int kMaxNumberOfPlayers = 1;
  if (SbPlayerPrivate::number_of_players() >= kMaxNumberOfPlayers) {
    return kSbPlayerInvalid;
  }

  UpdateActiveSessionPlatformPlaybackState(kPlaying);

  starboard::scoped_ptr<PlayerWorker::Handler> handler(
      new FilterBasedPlayerWorkerHandler(
          video_codec, audio_codec, creation_param->drm_system,
          &creation_param->audio_sample_info, output_mode, provider));
  SbPlayer player = SbPlayerPrivate::CreateInstance(
      audio_codec, video_codec, &creation_param->audio_sample_info,
      sample_deallocate_func, decoder_status_func, player_status_func,
      player_error_func, context, handler.Pass());

  // TODO: accomplish this through more direct means.
  // Set the bounds to initialize the VideoSurfaceView. The initial values don't
  // matter.
  SbPlayerSetBounds(player, 0, 0, 0, 0, 0);

  return player;
}
