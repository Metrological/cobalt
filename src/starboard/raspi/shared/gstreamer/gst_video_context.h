#ifndef STARBOARD_RASPI_SHARED_GSTREAMER_VIDEO_CONTEXT_H_
#define STARBOARD_RASPI_SHARED_GSTREAMER_VIDEO_CONTEXT_H_

#include "starboard/common/ref_counted.h"
#include "starboard/common/scoped_ptr.h"
#include <queue>

#include "starboard/thread.h"
#include "gst/gst.h"
#include "gst/app/gstappsrc.h"
#include "gst/app/gstappsink.h"

namespace starboard {
namespace raspi {
namespace shared {
namespace gstreamer {

class VideoSample : public RefCountedThreadSafe<VideoSample> {

public:
    VideoSample(GstSample *sample) {
        sample_ = sample;
    }
    ~VideoSample() {
        gst_sample_unref(sample_);
    }
    GstSample *sample() {
        return sample_;
    }

private:
    GstSample *sample_;
};

class VideoContext {

public:
    VideoContext();
    ~VideoContext();

    void SetDecoder(void *video_decoder);
    void *GetDecoder();
    void SetPlay();
    void SetReady();
    gboolean fetchOutputBuffer();
    void updateState();

private:
    static gboolean BusCallback(GstBus *bus,
            GstMessage *message, gpointer *ptr);
    static void* MainThread(void* context);
    static void StartFeed(GstElement *pipeline, guint size, void *context);
    static void StopFeed(GstElement *pipeline, void *context);
    static gboolean ReadData(void *context);
    static GstFlowReturn NewSample(GstElement *sink, void *context);

    SbThread main_thread_;
    GMainLoop *loop;

    GstPipeline *pipeline;
    GstAppSrc *src;
    GstElement *h264parse;
    GstElement *omxh264dec;
    GstElement *queue;
    GstElement *appsink;
    GstPad *pad;
    guint sourceid;
    void *video_decoder;

    std::queue<scoped_refptr<VideoSample> > video_samples_;
};

}  // namespace gstreamer
}  // namespace shared
}  // namespace raspi
}  // namespace starboard

#endif
