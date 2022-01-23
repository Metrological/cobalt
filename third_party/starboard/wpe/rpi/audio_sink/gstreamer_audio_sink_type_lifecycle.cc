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

#include <glib.h>
#include <gst/gst.h>

#include "starboard/thread.h"
#include "third_party/starboard/wpe/shared/audio_sink/gstreamer_audio_sink_type.h"

namespace third_party {
namespace starboard {
namespace wpe {
namespace shared {
namespace audio_sink {

// TODO(pstanek):
// Web audio works more reliably on RPI with this running in the background:
// gst-launch-1.0 filesrc location=/dev/zero ! audio/x-raw, format=F32LE,
// channels=2, layout=interleaved, rate=48000 ! audioconvert ! audioresample !
// autoaudiosink &
// Investigate.

// static
GStreamerAudioSinkType* GStreamerAudioSinkType::CreateInstance() {
  return new GStreamerAudioSinkType();
}

// static
void GStreamerAudioSinkType::DestroyInstance(GStreamerAudioSinkType* instance) {
  delete instance;
}

}  // namespace audio_sink
}  // namespace shared
}  // namespace wpe
}  // namespace starboard
}  // namespace third_party
