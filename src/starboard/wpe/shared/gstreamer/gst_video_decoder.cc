#include "starboard/wpe/shared/gstreamer/gst_video_context.h"
#include "starboard/wpe/shared/gstreamer/gst_video_decoder.h"

namespace starboard {
namespace wpe {
namespace shared {
namespace gstreamer {

namespace {

using std::placeholders::_1;
const size_t kResourcePoolSize = 26;
const SbTimeMonotonic kUpdateInterval = 5 * kSbTimeMillisecond;

}  // namespace

VideoDecoder::VideoDecoder(SbMediaVideoCodec video_codec)
: resource_pool_(new DispmanxResourcePool(kResourcePoolSize)),
  eos_written_(false) {

    SB_DCHECK(job_queue_ != NULL);
    update_job_ = std::bind(&VideoDecoder::Update, this);
    update_job_token_ = Schedule(update_job_, kUpdateInterval);

    VideoContext *con = new VideoContext();
    video_context = reinterpret_cast<void*>(con);
    con->SetDecoder(this);
    con->SetPlay();
}

VideoDecoder::~VideoDecoder() {
    VideoContext *con = reinterpret_cast<VideoContext*>(video_context);
    delete con;

    while (!input_buffers_.empty()) {
        input_buffers_.pop();
    }
    RemoveJobByToken(update_job_token_);
}

void VideoDecoder::Initialize(const DecoderStatusCB& decoder_status_cb,
        const ErrorCB& error_cb) {
    SB_DCHECK(decoder_status_cb);
    SB_DCHECK(!decoder_status_cb_);
    SB_DCHECK(error_cb);
    SB_DCHECK(!error_cb_);

    decoder_status_cb_ = decoder_status_cb;
    error_cb_ = error_cb;
}

void VideoDecoder::WriteInputBuffer(
        const scoped_refptr<InputBuffer>& input_buffer) {
    SB_DCHECK(input_buffer);
    SB_DCHECK(host_ != NULL);

    input_buffers_.push(input_buffer);

    for (int i = 0; i < 4; i++)
    {
        if (!TryToDeliverOneFrame()) {
            SbThreadSleep(kSbTimeMillisecond);
            decoder_status_cb_(kNeedMoreInput, NULL);
        }
    }
}

scoped_refptr<VideoDecoder::InputBuffer> VideoDecoder::GetInputBuffer()
{
    scoped_refptr<InputBuffer> input_buffer;
    if (!input_buffers_.empty()) {
        input_buffer = input_buffers_.front();
        input_buffers_.pop();
    }
    return input_buffer;
}

bool VideoDecoder::PushOutputBuffer(
        uint8_t *buffer, int64_t size, int64_t pts,
        int32_t width, int32_t height,
        int32_t stride, int32_t slice_height) {

    if (scoped_refptr<VideoFrame> frame
            = CreateFrame(buffer, size, pts,
                    width, height, stride, slice_height)) {
        decoder_status_cb_(kNeedMoreInput, frame);
        return true;
    }
    return false;
}

bool VideoDecoder::IfEndOfStream() {
    return eos_written_;
}

void VideoDecoder::WriteEndOfStream() {

    eos_written_ = true;
}

void VideoDecoder::Reset() {

    VideoContext *con = reinterpret_cast<VideoContext*>(video_context);
    con->SetReady();

    while (!input_buffers_.empty()) {
        input_buffers_.pop();
    }
    con->SetPlay();
}

void VideoDecoder::Update() {

    VideoContext *con = reinterpret_cast<VideoContext*>(video_context);
    con->updateState();

    if (eos_written_) {
        TryToDeliverOneFrame();
    }
    update_job_token_ = Schedule(update_job_, kUpdateInterval);
}

bool VideoDecoder::TryToDeliverOneFrame() {

    VideoContext *con = reinterpret_cast<VideoContext*>(video_context);
    if (con->fetchOutputBuffer())
        return true;
    return false;
}

scoped_refptr<VideoDecoder::VideoFrame> VideoDecoder::CreateFrame(
        const uint8_t* buffer, int64_t size, SbMediaTime pts,
        int32_t width, int32_t height,
        int32_t stride, int32_t slice_height) {
    scoped_refptr<VideoFrame> frame;
    DispmanxYUV420Resource* resource =
            resource_pool_->Alloc(
                    stride, slice_height, width, height);
    if (!resource) {
        return NULL;
    }

    resource->WriteData(buffer);
    SbMediaTime timestamp = pts / 10000 * 0.9;
    resource_pool_->AddRef();
    frame = new DispmanxVideoFrame(
            timestamp, resource,
            std::bind(&DispmanxResourcePool::DisposeDispmanxYUV420Resource,
                      resource_pool_, _1));
    return frame;
}

}  // namespace gstreamer
}  // namespace shared
}  // namespace wpe

namespace shared {
namespace starboard {
namespace player {
namespace filter {

//bool VideoDecoder::OutputModeSupported(SbPlayerOutputMode output_mode,
//        SbMediaVideoCodec codec,
//        SbDrmSystem drm_system) {
//    return output_mode == kSbPlayerOutputModePunchOut;
//}

}  // namespace filter
}  // namespace player
}  // namespace starboard
}  // namespace shared
}  // namespace starboard
