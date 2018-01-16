#ifndef STARBOARD_SHARED_GSTREAMER_AUDIO_DECODER_H_
#define STARBOARD_SHARED_GSTREAMER_AUDIO_DECODER_H_

#include <queue>

#include "starboard/shared/starboard/player/filter/audio_decoder_internal.h"
#include "starboard/shared/starboard/player/job_queue.h"
#include "starboard/thread.h"

namespace starboard {
namespace shared {
namespace gstreamer {

class AudioDecoder : public starboard::player::filter::AudioDecoder,
private starboard::player::JobQueue::JobOwner {
public:
    AudioDecoder(SbMediaAudioCodec audio_codec,
            const SbMediaAudioHeader& audio_header);
    ~AudioDecoder() SB_OVERRIDE;

    void Initialize(const Closure& output_cb,
                  const Closure& error_cb) SB_OVERRIDE;
    void Decode(const scoped_refptr<InputBuffer>& input_buffer,
            const Closure& consumed_cb) SB_OVERRIDE;
    scoped_refptr<InputBuffer> GetInputBuffer();
    void PushedInputBuffer();
    void PushOutputBuffer(
            uint8_t *buffer, int64_t size, int64_t pts);
    void WriteEndOfStream() SB_OVERRIDE;
    bool IfEndOfStream();
    scoped_refptr<DecodedAudio> Read() SB_OVERRIDE;
    void Reset() SB_OVERRIDE;
    SbMediaAudioSampleType GetSampleType() const SB_OVERRIDE;
    SbMediaAudioFrameStorageType GetStorageType() const SB_OVERRIDE;
    int GetSamplesPerSecond() const SB_OVERRIDE;
    bool is_valid() const { return true;}

private:
    void *audio_context;
    Closure output_cb_;
    Closure consumed_cb_;
    SbMediaAudioSampleType sample_type_;
    SbMediaAudioHeader audio_header_;
    bool stream_ended_;
    std::queue<scoped_refptr<InputBuffer> > input_buffers_;
    std::queue<scoped_refptr<DecodedAudio> > decoded_audios_;
};

}  // namespace gstreamer
}  // namespace shared
}  // namespace starboard

#endif  // STARBOARD_SHARED_GSTREAMER_AUDIO_DECODER_H_
