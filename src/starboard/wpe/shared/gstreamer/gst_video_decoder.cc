#include "starboard/wpe/shared/gstreamer/gst_video_context.h"
#include "starboard/wpe/shared/gstreamer/gst_video_decoder.h"

namespace starboard {
namespace wpe {
namespace shared {
namespace gstreamer {

namespace {

const size_t kResourcePoolSize = 26;
const SbTimeMonotonic kUpdateInterval = 5 * kSbTimeMillisecond;

}  // namespace

VideoDecoder::VideoDecoder(SbMediaVideoCodec video_codec, JobQueue* job_queue)
: resource_pool_(new DispmanxResourcePool(kResourcePoolSize)),
  host_(NULL),
  eos_written_(false),
  job_queue_(job_queue) {
    SB_DCHECK(job_queue_ != NULL);
    update_closure_ =
            ::starboard::shared::starboard::player::Bind(&VideoDecoder::Update, this);
    job_queue_->Schedule(update_closure_, kUpdateInterval);

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
    while (!decoded_videos_.empty()) {
        decoded_videos_.pop();
    }
    job_queue_->Remove(update_closure_);
}

void VideoDecoder::SetHost(Host* host) {
    SB_DCHECK(host != NULL);
    SB_DCHECK(host_ == NULL);
    host_ = host;
}

void VideoDecoder::WriteInputBuffer(
        const scoped_refptr<InputBuffer>& input_buffer) {
    SB_DCHECK(input_buffer);
    SB_DCHECK(host_ != NULL);

    input_buffers_.push(input_buffer);

    for (int i = 0; i < 10; i++)
    {
        if (!TryToDeliverOneFrame()) {
            SbThreadSleep(kSbTimeMillisecond);
            host_->OnDecoderStatusUpdate(kNeedMoreInput, NULL);
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

void VideoDecoder::PushOutputBuffer(
        uint8_t *buffer, int64_t size, int64_t pts,
        int32_t width, int32_t height,
        int32_t stride, int32_t slice_height) {

    scoped_refptr<DecodedVideo> decoded_video =
            new DecodedVideo(size, pts,
                    width, height, stride, slice_height);
    SbMemoryCopy(decoded_video->data(), buffer, size);
    decoded_videos_.push(decoded_video);
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
    while (!decoded_videos_.empty()) {
        decoded_videos_.pop();
    }
    job_queue_->Schedule(update_closure_, kUpdateInterval);
    con->SetPlay();
}

void VideoDecoder::Update() {
    if (eos_written_) {
        TryToDeliverOneFrame();
    }
    job_queue_->Schedule(update_closure_, kUpdateInterval);
}

bool VideoDecoder::TryToDeliverOneFrame() {

    if (!decoded_videos_.empty()) {
        scoped_refptr<DecodedVideo> decoded_video =
                decoded_videos_.front();
        if (scoped_refptr<VideoFrame> frame
                = CreateFrame(
                        (uint8_t*)decoded_video->data(),
                        decoded_video->size(),
                        decoded_video->pts(),
                        decoded_video->width(),
                        decoded_video->height(),
                        decoded_video->stride(),
                        decoded_video->slice_height())) {
            host_->OnDecoderStatusUpdate(kNeedMoreInput, frame);
            decoded_videos_.pop();
            return true;
        }
    }
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
    frame = new VideoFrame(
            width, height, timestamp, resource, resource_pool_,
            &DispmanxResourcePool::DisposeDispmanxYUV420Resource);
    return frame;
}

}  // namespace gstreamer
}  // namespace shared
}  // namespace wpe

namespace shared {
namespace starboard {
namespace player {
namespace filter {

bool VideoDecoder::OutputModeSupported(SbPlayerOutputMode output_mode,
        SbMediaVideoCodec codec,
        SbDrmSystem drm_system) {
    return output_mode == kSbPlayerOutputModePunchOut;
}

}  // namespace filter
}  // namespace player
}  // namespace starboard
}  // namespace shared
}  // namespace starboard
