#include <gst/gst.h>

#include "third_party/starboard/wpe/shared/player/player_internal.h"

namespace third_party {
namespace starboard {
namespace wpe {
namespace shared {
namespace player {

bool Player::PlatformNonFushingSetRate(_GstElement* pipeline, double rate) {
  GstElement* sink = nullptr;
  GstSegment *segment;

  for (int32_t i = 0; i < 2 ; i++) {
    if (i == 0)
      g_object_get(pipeline, "audio-sink", &sink, nullptr);
    else
      g_object_get(pipeline, "video-sink", &sink, nullptr);

    if (sink) {
      GstIterator *iter = gst_element_iterate_sink_pads (GST_ELEMENT_CAST (sink));
      GstIteratorResult ires;
      GValue item = { 0, };

      ires = gst_iterator_next (iter, &item);
      if (ires == GST_ITERATOR_OK) {
        GstPad *pad = (GstPad *)g_value_get_object (&item);

        segment = gst_segment_new();
        gst_segment_init(segment, GST_FORMAT_TIME);
        segment->rate = rate;
        segment->start = GST_CLOCK_TIME_NONE;
        segment->position = GST_CLOCK_TIME_NONE;
        segment->stop = GST_SEEK_TYPE_NONE;
        segment->flags = GST_SEGMENT_FLAG_NONE;
        segment->format = GST_FORMAT_TIME;

        if (!gst_pad_send_event (pad, gst_event_new_segment(segment)))
          GST_ERROR("Error when sending rate segment!!!\n");
        else
          GST_WARNING ("sent segment rate: %f", rate);

        gst_segment_free(segment);
        g_value_reset (&item);
      } else {
        GST_ERROR("no sink pad");
      }
      gst_iterator_free (iter);
      g_object_unref(sink);
    } else {
      GST_INFO ("cant not get %c sink", i==0?'a':'v');
    }
  }
  return true;
}

}  // namespace player
}  // namespace shared
}  // namespace wpe
}  // namespace starboard
}  // namespace third_party