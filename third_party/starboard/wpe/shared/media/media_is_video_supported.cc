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

#include "base/logging.h"
#include "starboard/configuration.h"
#include "starboard/media.h"
#include "starboard/shared/starboard/media/media_support_internal.h"
#include "third_party/starboard/wpe/shared/media/gst_media_utils.h"
#include "starboard/configuration_constants.h"

using ::starboard::shared::starboard::media::MimeType;

SB_EXPORT bool SbMediaIsVideoSupported(SbMediaVideoCodec video_codec,
                                       const MimeType* mime_type,
                                       int /*profile*/,
                                       int /*level*/,
                                       int /*bit_depth*/,
                                       SbMediaPrimaryId /*primary_id*/,
                                       SbMediaTransferId /*transfer_id*/,
                                       SbMediaMatrixId /*matrix_id*/,
                                       int frame_width,
                                       int frame_height,
                                       int64_t bitrate,
                                       int fps,
                                       bool decode_to_texture_required) {
  if (decode_to_texture_required) {
    LOG(WARNING) << "Decoding to texture required with " << frame_width << "x"
                 << frame_height;
    return false;
  }

  return frame_width <= SB_MEDIA_MAX_VIDEO_FRAME_WIDTH &&
         frame_height <= SB_MEDIA_MAX_VIDEO_FRAME_HEIGHT &&
         bitrate <= kSbMediaMaxVideoBitrateInBitsPerSecond &&
         fps <= SB_MEDIA_MAX_VIDEO_FRAMERATE_IN_FRAMES_PER_SECOND &&
         third_party::starboard::wpe::shared::media::
             GstRegistryHasElementForMediaType(video_codec);
}
