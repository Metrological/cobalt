#include "starboard/wpe/shared/gstreamer/gst_video_context.h"
#include "starboard/wpe/shared/gstreamer/gst_video_decoder.h"

#include <string.h>
#include <gst/video/video.h>

#define GST_MAP_GL (GST_MAP_FLAG_LAST << 1)

namespace starboard {
namespace wpe {
namespace shared {
namespace gstreamer {

VideoContext::VideoContext()
:sourceid(0) {
    if (!gst_is_initialized())
        gst_init(NULL, NULL);

    pipeline = (GstPipeline*)gst_pipeline_new("videopipeline");

    GstBus *bus;
    bus = gst_pipeline_get_bus(pipeline);
    gst_bus_add_watch(bus, (GstBusFunc)BusCallback, this);
    gst_object_unref(bus);

    src = (GstAppSrc*)gst_element_factory_make("appsrc", "vidsrc");
    appsink = gst_element_factory_make("appsink", "vidappsink");
    g_signal_connect(src, "need-data", G_CALLBACK(StartFeed), this);
    g_signal_connect(src, "enough-data", G_CALLBACK(StopFeed), this);

    h264parse = gst_element_factory_make("h264parse", "vidh264parse");
    omxh264dec = gst_element_factory_make("omxh264dec", "vidomxh264dec");
    queue = gst_element_factory_make("queue", "vidqueue");

    gst_bin_add_many(GST_BIN(pipeline),
            (GstElement*)src, h264parse, omxh264dec, queue, appsink, NULL);
    gst_element_link_many(
            (GstElement*)src, h264parse, omxh264dec, queue, appsink, NULL);

    g_object_set(appsink, "emit-signals", TRUE, "sync", FALSE, NULL);
    g_signal_connect(appsink, "new-sample", G_CALLBACK(NewSample), this);

    loop = g_main_loop_new(NULL, FALSE);
    main_thread_ =
            SbThreadCreate(0, kSbThreadPriorityHigh,
                    kSbThreadNoAffinity, true,
                    "", &VideoContext::MainThread, this);
}

VideoContext::~VideoContext() {
    gst_element_set_state((GstElement*)pipeline, GST_STATE_NULL);
    while (!video_samples_.empty()) {
        video_samples_.pop();
    }
    g_main_loop_quit(loop);
    SbThreadJoin(main_thread_, NULL);
    if (sourceid != 0) {
        g_source_remove(sourceid);
        sourceid = 0;
    }
    gst_object_unref(GST_OBJECT(pipeline));
}

void VideoContext::SetDecoder(void *video_decoder) {
    this->video_decoder = video_decoder;
}

void *VideoContext::GetDecoder() {
    return (this->video_decoder);
}

void VideoContext::SetPlay() {
    gst_element_set_state((GstElement*)pipeline, GST_STATE_PLAYING);
}

void VideoContext::SetReady() {
    gst_element_set_state((GstElement*)pipeline, GST_STATE_READY);
    while (!video_samples_.empty()) {
        video_samples_.pop();
    }
}

gboolean VideoContext::BusCallback(GstBus *bus, GstMessage *message, gpointer *ptr) {
    VideoContext *con = reinterpret_cast<VideoContext*>(ptr);
    switch(GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_ERROR:{
        gchar *debug;
        GError *err;
        gst_message_parse_error(message, &err, &debug);
        g_print("Error %s\n", err->message);
        g_error_free(err);
        g_free(debug);
        g_main_loop_quit(con->loop);
    }
    break;

    case GST_MESSAGE_WARNING:{
        gchar *debug;
        GError *err;
        gchar *name;
        gst_message_parse_warning(message, &err, &debug);
        g_print("Warning %s\nDebug %s\n", err->message, debug);
        name = (gchar *)GST_MESSAGE_SRC_NAME(message);
        g_print("Name of src %s\n", name ? name : "nil");
        g_error_free(err);
        g_free(debug);
    }
    break;

    case GST_MESSAGE_EOS:
        g_print("End of stream\n");
        g_main_loop_quit(con->loop);
        break;

    case GST_MESSAGE_STATE_CHANGED:
        break;

    default:
        break;
    }
    return TRUE;
}

void* VideoContext::MainThread(void* context) {
    VideoContext *con = reinterpret_cast<VideoContext*>(context);
    g_main_loop_run(con->loop);
    return NULL;
}

void VideoContext::StartFeed(
        GstElement *pipeline, guint size, void *context) {
    VideoContext *con = reinterpret_cast<VideoContext*>(context);
    if (con->sourceid == 0) {
        GST_DEBUG("start feeding");
        con->sourceid = g_idle_add((GSourceFunc)ReadData, context);
    }
}

void VideoContext::StopFeed(GstElement *pipeline, void *context) {
    VideoContext *con = reinterpret_cast<VideoContext*>(context);
    if (con->sourceid != 0) {
        GST_DEBUG("stop feeding");
        g_source_remove(con->sourceid);
        con->sourceid = 0;
    }
}

gboolean VideoContext::ReadData(void *context) {
    VideoContext *con = reinterpret_cast<VideoContext*>(context);
    VideoDecoder *video_decoder =
            reinterpret_cast<VideoDecoder*>(con->GetDecoder());

    while (1) {
        using InputBuffer = ::starboard::shared::starboard::player::InputBuffer;
        scoped_refptr<InputBuffer> input_buffer;
        input_buffer = video_decoder->GetInputBuffer();
        if (!input_buffer) {
            if (video_decoder->IfEndOfStream()) {
                gst_app_src_end_of_stream(con->src);
            }
            break;
        }
        else {
            GstBuffer *buffer =
                    gst_buffer_new_and_alloc(input_buffer->size());
            GST_BUFFER_TIMESTAMP(buffer) = input_buffer->pts() * 10000.0 / 0.9;
            GstMapInfo map;
            gst_buffer_map(buffer, &map, GST_MAP_WRITE);
            memcpy(map.data, input_buffer->data(), input_buffer->size());
            gst_buffer_unmap(buffer, &map);
            gst_app_src_push_buffer(con->src, buffer);
        }
    }
    return TRUE;
}

GstFlowReturn VideoContext::NewSample(
        GstElement *sink, void *context) {
    VideoContext *con = reinterpret_cast<VideoContext*>(context);
    VideoDecoder *video_decoder =
            reinterpret_cast<VideoDecoder*>(con->GetDecoder());

    GstSample *sample;
    sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));

    scoped_refptr<VideoSample> video_sample = new VideoSample(sample);
    con->video_samples_.push(video_sample);
    return GST_FLOW_OK;
}

void VideoContext::updateState()
{
    guint size = video_samples_.size();

    GstState state;
    gst_element_get_state((GstElement*)pipeline, &state, nullptr, 0);

    if (size > 60) {
        if (state == GST_STATE_PLAYING)
            gst_element_set_state((GstElement*)pipeline, GST_STATE_PAUSED);
    }
    else if (size < 30) {
        if (state == GST_STATE_PAUSED)
            gst_element_set_state((GstElement*)pipeline, GST_STATE_PLAYING);
    }
}

gboolean VideoContext::fetchOutputBuffer() {
    VideoDecoder *video_decoder =
            reinterpret_cast<VideoDecoder*>(GetDecoder());

    if (!video_samples_.empty()) {
        scoped_refptr<VideoSample> video_sample = video_samples_.front();
        GstSample *sample = video_sample->sample();

        GstBuffer *buffer;
        buffer = gst_sample_get_buffer(sample);

        GstCaps* caps = gst_sample_get_caps(sample);

        GstVideoInfo videoInfo;
        gst_video_info_init(&videoInfo);
        gst_video_info_from_caps(&videoInfo, caps);

        GstVideoFrame videoFrame;
        gst_video_frame_map(&videoFrame, &videoInfo,
                buffer, static_cast<GstMapFlags>(GST_MAP_READ | GST_MAP_GL));

        gboolean res = video_decoder->PushOutputBuffer(
                (uint8_t*)videoFrame.data[0], gst_buffer_get_size(buffer),
                GST_BUFFER_PTS(buffer), videoInfo.width, videoInfo.height,
                videoInfo.stride[0], videoInfo.height);

        gst_video_frame_unmap(&videoFrame);

        if (res) {
            video_samples_.pop();
            return true;
        }
    }
    return false;
}

}  // namespace gstreamer
}  // namespace shared
}  // namespace wpe
}  // namespace starboard
