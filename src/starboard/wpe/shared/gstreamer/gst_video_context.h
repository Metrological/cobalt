#ifndef STARBOARD_WPE_SHARED_GSTREAMER_VIDEO_CONTEXT_H_
#define STARBOARD_WPE_SHARED_GSTREAMER_VIDEO_CONTEXT_H_

#include "starboard/thread.h"
#include "gst/gst.h"
#include "gst/app/gstappsrc.h"
#include "gst/app/gstappsink.h"

namespace starboard {
namespace wpe {
namespace shared {
namespace gstreamer {

class VideoContext {

public:
    VideoContext();
    ~VideoContext();

    void SetDecoder(void *video_decoder);
    void *GetDecoder();
    void SetPlay();

private:
    static gboolean BusCallback(GstBus *bus,
            GstMessage *message, gpointer *ptr);
    static void* MainThread(void* context);
    static void StartFeed(GstElement *pipeline, guint size, void *context);
    static void StopFeed(GstElement *pipeline, void *context);
    static void OnPadAdded(GstElement *element, GstPad *pad, void *context);
    static gboolean ReadData(void *context);
    static GstFlowReturn NewSample(GstElement *sink, void *context);

    SbThread main_thread_;
    GMainLoop *loop;

    int32_t width;
    int32_t height;
    int32_t stride;
    int32_t slice_height;
    GstPipeline *pipeline;
    GstAppSrc *src;
    GstElement *decoder;
    GstElement *appsink;
    GstPad *pad;
    guint sourceid;
    void *video_decoder;
};

}  // namespace gstreamer
}  // namespace shared
}  // namespace wpe
}  // namespace starboard

#endif
