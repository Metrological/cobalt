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

#include "third_party/starboard/wpe/shared/player/player_internal.h"

#include <stdlib.h>
#include <string.h>

bool SbPlayerSetPlaybackRate(SbPlayer player, double playback_rate) {
  double rate = playback_rate;

  // Check an environment variable for the supported playback rates
  const char* value = ::getenv("COBALT_SUPPORT_PLAYBACK_RATES");
  if ((value != nullptr) && (::strcmp(value, "false") == 0)) {
    if ( rate != .0 )
      rate = 1.;
  }
  return player->player_->SetRate(rate);
}
