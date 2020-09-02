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

#include "third_party/starboard/wpe/shared/player/player_video_sink.h"

static void ElementAdded(GstElement* pipeline, GstElement* element, void* arg) {

  bool pip_mode = (bool) arg;

  if ((g_strrstr(GST_ELEMENT_NAME(element), "uridecodebin"))
      || (g_strrstr(GST_ELEMENT_NAME(element), "decodebin"))) {
    g_signal_connect(element, "element-added",
        G_CALLBACK(&ElementAdded), arg);
  }
  if (g_strrstr(GST_ELEMENT_NAME(element), "brcmvideodecoder")) {
    if (pip_mode) {
      g_object_set(element, "pip", TRUE, NULL);
    }
  }
  if (g_strrstr(GST_ELEMENT_NAME(element), "brcmaudiodecoder")) {
    g_object_set(element, "use-primer", TRUE, NULL);
    if (pip_mode) { // Do not start audio decoder for pip window
      g_object_set(element, "prime", TRUE, NULL);
    }
  }
}

void AddVideoSink(GstElement* pipeline, bool pip_mode) {

  g_signal_connect(pipeline, "element-added", G_CALLBACK(&ElementAdded),
      (void*) pip_mode);

#if defined(WESTEROS_SINK)

  GstElement* video_sink = gst_element_factory_make("westerossink",
      "videosink");
  if (pip_mode) {
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(video_sink), "pip")) {
      g_object_set(G_OBJECT(video_sink), "pip", TRUE, NULL);
    }
  }
  g_object_set(pipeline, "video-sink", video_sink, NULL);

#else

  GstElement* video_sink = gst_element_factory_make("brcmvideosink",
      "videosink");
  g_object_set(pipeline, "video-sink", video_sink, NULL);

#endif

}
