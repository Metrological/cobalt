#ifndef STARBOARD_SHARED_GSTREAMER_AUDIO_CONTEXT_H_
#define STARBOARD_SHARED_GSTREAMER_AUDIO_CONTEXT_H_

#include "starboard/thread.h"

#include "gst/gst.h"
#include "gst/app/gstappsrc.h"
#include "gst/app/gstappsink.h"

namespace starboard {
namespace shared {
namespace gstreamer {

class AudioContext {

public:
    AudioContext();
    ~AudioContext();

    void SetDecoder(void *audio_decoder);
    void *GetDecoder();
    void SetPlay();
    void SetReady();

private:
    static gboolean BusCallback (GstBus *bus,
            GstMessage *message, gpointer *ptr);
    static void* MainThread (void* context);
    static void StartFeed (GstElement *pipeline, guint size, void *context);
    static void StopFeed (GstElement *pipeline, void *context);
    static void OnPadAdded (GstElement *element, GstPad *pad, void *context);
    static gboolean ReadData (void *context);
    static GstFlowReturn NewSample (GstElement *sink, void *context);

    SbThread main_thread_;
    GMainLoop *loop;

    GstPipeline *pipeline;
    GstAppSrc *src;
    GstElement *decoder;
    GstElement *appsink;
    guint sourceid;
    void *audio_decoder;
};

}  // namespace gstreamer
}  // namespace shared
}  // namespace starboard

#endif
