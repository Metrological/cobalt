#include "starboard/wpe/shared/gstreamer/gst_audio_context.h"
#include "starboard/wpe/shared/gstreamer/gst_audio_decoder.h"

#include <string.h>

namespace starboard {
namespace shared {
namespace gstreamer {

AudioContext::AudioContext()
:sourceid(0) {
    if (!gst_is_initialized())
        gst_init(NULL, NULL);

    pipeline = (GstPipeline*)gst_pipeline_new("audiopipeline");

    GstBus *bus;
    bus = gst_pipeline_get_bus(pipeline);
    gst_bus_add_watch(bus, (GstBusFunc)BusCallback, this);
    gst_object_unref(bus);

    src = (GstAppSrc*)gst_element_factory_make("appsrc", "audsrc");
    decoder = gst_element_factory_make("decodebin", "auddecoder");
    appsink = gst_element_factory_make ("appsink", "audappsink");

    g_signal_connect(src, "need-data", G_CALLBACK(StartFeed), this);
    g_signal_connect(src, "enough-data", G_CALLBACK(StopFeed), this);
    g_signal_connect(decoder, "pad-added", G_CALLBACK(OnPadAdded), this);

    gst_bin_add_many(GST_BIN(pipeline),
            (GstElement*)src, decoder, appsink, NULL);
    gst_element_link((GstElement*)src, decoder);

    g_object_set(appsink, "emit-signals", TRUE, "sync", FALSE, NULL);
    g_signal_connect(appsink, "new-sample", G_CALLBACK (NewSample), this);

    loop = g_main_loop_new(NULL, FALSE);
    main_thread_ =
            SbThreadCreate(0, kSbThreadPriorityHigh, kSbThreadNoAffinity, true,
                    "", &AudioContext::MainThread, this);
}

AudioContext::~AudioContext() {
    gst_element_set_state((GstElement*)pipeline, GST_STATE_NULL);
    g_main_loop_quit(loop);
    SbThreadJoin(main_thread_, NULL);
    if (sourceid != 0) {
        g_source_remove (sourceid);
        sourceid = 0;
    }
    gst_object_unref(GST_OBJECT(pipeline));
}

void AudioContext::SetDecoder(void *audio_decoder) {
    this->audio_decoder = audio_decoder;
}

void *AudioContext::GetDecoder() {
    return (this->audio_decoder);
}

void AudioContext::SetPlay() {
    gst_element_set_state((GstElement*)pipeline, GST_STATE_PLAYING);
}

void AudioContext::SetReady() {
    gst_element_set_state((GstElement*)pipeline, GST_STATE_READY);
}

gboolean AudioContext::BusCallback(GstBus *bus, GstMessage *message, gpointer *ptr) {
    AudioContext *con = reinterpret_cast<AudioContext*>(ptr);
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

void* AudioContext::MainThread(void* context) {
    AudioContext *con = reinterpret_cast<AudioContext*>(context);
    g_main_loop_run(con->loop);
    return NULL;
}

void AudioContext::StartFeed (
        GstElement *pipeline, guint size, void *context) {
    AudioContext *con = reinterpret_cast<AudioContext*>(context);
    if (con->sourceid == 0) {
        GST_DEBUG ("start feeding");
        con->sourceid = g_idle_add ((GSourceFunc)ReadData, context);
    }
}

void AudioContext::StopFeed (GstElement *pipeline, void *context) {
    AudioContext *con = reinterpret_cast<AudioContext*>(context);
    if (con->sourceid != 0) {
        GST_DEBUG ("stop feeding");
        g_source_remove (con->sourceid);
        con->sourceid = 0;
    }
}

void AudioContext::OnPadAdded(
        GstElement *element, GstPad *pad, void *context) {
    AudioContext *con = reinterpret_cast<AudioContext*>(context);

    GstPad *appsinkpad;
    appsinkpad = gst_element_get_static_pad(con->appsink, "sink");
    gst_pad_link(pad, appsinkpad);
    g_object_unref(appsinkpad);
}

gboolean AudioContext::ReadData (void *context) {
    AudioContext *con = reinterpret_cast<AudioContext*>(context);
    AudioDecoder *audio_decoder =
            reinterpret_cast<AudioDecoder*>(con->GetDecoder());

    using InputBuffer = ::starboard::shared::starboard::player::InputBuffer;
    scoped_refptr<InputBuffer> input_buffer;
    input_buffer = audio_decoder->GetInputBuffer();
    if (!input_buffer) {
        if (audio_decoder->IfEndOfStream()) {
            gst_app_src_end_of_stream (con->src);
        }
    }
    else {
        GstBuffer *buffer =
                gst_buffer_new_and_alloc(input_buffer->size());
        GST_BUFFER_TIMESTAMP(buffer) = input_buffer->pts();
        GstMapInfo map;
        gst_buffer_map (buffer, &map, GST_MAP_WRITE);
        memcpy(map.data, input_buffer->data(), input_buffer->size());
        gst_buffer_unmap (buffer, &map);
        gst_app_src_push_buffer(con->src, buffer);
        audio_decoder->PushedInputBuffer();
    }
    return TRUE;
}

GstFlowReturn AudioContext::NewSample (
        GstElement *sink, void *context) {
    AudioContext *con = reinterpret_cast<AudioContext*>(context);
    AudioDecoder *audio_decoder =
            reinterpret_cast<AudioDecoder*>(con->GetDecoder());

    GstSample *sample;
    GstBuffer *buffer;

    sample = gst_app_sink_pull_sample (GST_APP_SINK (sink));
    buffer = gst_sample_get_buffer (sample);

    GstMapInfo map;
    gst_buffer_map (buffer, &map, GST_MAP_READ);
    audio_decoder->PushOutputBuffer(
            map.data, gst_buffer_get_size(buffer), GST_BUFFER_PTS(buffer));
    gst_buffer_unmap (buffer, &map);

    gst_sample_unref (sample);
    return GST_FLOW_OK;
}

}  // namespace gstreamer
}  // namespace shared
}  // namespace starboard
