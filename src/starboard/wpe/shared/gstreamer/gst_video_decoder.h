#ifndef STARBOARD_WPE_SHARED_GSTREAMER_VIDEO_DECODER_H_
#define STARBOARD_WPE_SHARED_GSTREAMER_VIDEO_DECODER_H_

#include "starboard/wpe/shared/open_max/dispmanx_resource_pool.h"
#include "starboard/shared/starboard/player/filter/video_decoder_internal.h"
#include "starboard/shared/starboard/player/job_queue.h"

namespace starboard {
namespace wpe {
namespace shared {
namespace gstreamer {

typedef int64_t SbMediaTime;

class DecodedVideo : public RefCountedThreadSafe<DecodedVideo> {
public:
    DecodedVideo(int size, SbMediaTime pts,
            int width, int height, int stride, int slice_height) {
        data_=SbMemoryAllocate(size);
        size_= size;
        pts_=pts;
        width_=width;
        height_=height;
        stride_=stride;
        slice_height_=slice_height;
    }
    ~DecodedVideo() {
        SbMemoryFree(data_);
    }

    void* data() const { return data_; }
    int size() const { return size_; }
    SbMediaTime pts() const { return pts_; }
    int width() const { return width_; }
    int height() const { return height_; }
    int stride() const { return stride_; }
    int slice_height() const { return slice_height_; }

private:
    void* data_;
    int size_;
    SbMediaTime pts_;
    int width_;
    int height_;
    int stride_;
    int slice_height_;
};

class VideoDecoder:
        public starboard::shared::starboard::player::filter::VideoDecoder,
        private ::starboard::shared::starboard::player::JobQueue::JobOwner {
public:
    typedef starboard::shared::starboard::player::InputBuffer InputBuffer;
    typedef ::starboard::shared::starboard::player::JobQueue JobQueue;
    typedef ::starboard::wpe::shared::open_max::DispmanxResourcePool DispmanxResourcePool;

    VideoDecoder(SbMediaVideoCodec video_codec);
    ~VideoDecoder() SB_OVERRIDE;
    void Initialize(const DecoderStatusCB& decoder_status_cb,
            const ErrorCB& error_cb) override;
    void WriteInputBuffer(const scoped_refptr
            <InputBuffer>& input_buffer) SB_OVERRIDE;
    scoped_refptr<InputBuffer> GetInputBuffer();
    bool PushOutputBuffer(
            uint8_t *buffer, int64_t size, int64_t pts,
            int32_t width, int32_t height,
            int32_t stride, int32_t slice_height);
    bool IfEndOfStream();
    void WriteEndOfStream() SB_OVERRIDE;
    void Reset() SB_OVERRIDE;
    SbTime GetPrerollTimeout() const override { return kSbTimeMax; }
    size_t GetPrerollFrameCount() const override { return 1; }
    SbDecodeTarget GetCurrentDecodeTarget() override {
      return kSbDecodeTargetInvalid;
    }
    size_t GetMaxNumberOfCachedFrames() const override { return 12; }

private:
    bool TryToDeliverOneFrame();
    scoped_refptr<VideoFrame> CreateFrame(
            const uint8_t* buffer, int64_t size, SbMediaTime pts,
            int32_t width, int32_t height,
            int32_t stride, int32_t slice_height);
    void Update();

    scoped_refptr<DispmanxResourcePool> resource_pool_;
    DecoderStatusCB decoder_status_cb_;
    ErrorCB error_cb_;
    bool eos_written_;
    void *video_context;
    std::queue<scoped_refptr<InputBuffer> > input_buffers_;
    JobQueue::JobToken update_job_token_;
    std::function<void()> update_job_;
    JobQueue* job_queue_;
};

}  // namespace gstreamer
}  // namespace shared
}  // namespace wpe
}  // namespace starboard

#endif  // STARBOARD_WPE_SHARED_GSTREAMER_VIDEO_DECODER_H_
