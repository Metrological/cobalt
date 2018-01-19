#include "starboard/wpe/shared/gstreamer/gst_audio_context.h"
#include "starboard/wpe/shared/gstreamer/gst_audio_decoder.h"
#include "starboard/audio_sink.h"

namespace starboard {
namespace shared {
namespace gstreamer {

namespace {

SbMediaAudioSampleType GetSupportedSampleType()
{
    if (SbAudioSinkIsAudioSampleTypeSupported(
            kSbMediaAudioSampleTypeFloat32)) {
        return kSbMediaAudioSampleTypeFloat32;
    }
    return kSbMediaAudioSampleTypeInt16;
}
}  // namespace

AudioDecoder::AudioDecoder(
        SbMediaAudioCodec audio_codec,
        const SbMediaAudioHeader& audio_header)
:sample_type_(GetSupportedSampleType()),
 audio_header_(audio_header),
 stream_ended_(false) {
    AudioContext *con = new AudioContext(audio_header_.number_of_channels);
    audio_context = reinterpret_cast<void*>(con);
    con->SetDecoder(this);
    con->SetPlay();
}

AudioDecoder::~AudioDecoder() {
    AudioContext *con = reinterpret_cast<AudioContext*>(audio_context);
    delete con;

    while (!decoded_audios_.empty()) {
        decoded_audios_.pop();
    }
    CancelPendingJobs();

    while (!input_buffers_.empty()) {
        input_buffers_.pop();
    }
}

void AudioDecoder::Initialize(
        const Closure& output_cb, const Closure& error_cb)
{
    SB_DCHECK(BelongsToCurrentThread());
    SB_DCHECK(output_cb.is_valid());
    SB_DCHECK(!output_cb_.is_valid());

    output_cb_ = output_cb;
}

void AudioDecoder::Decode(
        const scoped_refptr<InputBuffer>& input_buffer,
        const Closure& consumed_cb)
{
    AudioContext *con = reinterpret_cast<AudioContext*>(audio_context);

    SB_DCHECK(BelongsToCurrentThread());
    SB_DCHECK(input_buffer);
    SB_DCHECK(output_cb_.is_valid());

    consumed_cb_ = consumed_cb;
    input_buffers_.push(input_buffer);
}

scoped_refptr<AudioDecoder::InputBuffer> AudioDecoder::GetInputBuffer()
{
    scoped_refptr<InputBuffer> input_buffer;
    if (!input_buffers_.empty()) {
        input_buffer = input_buffers_.front();
        input_buffers_.pop();
    }
    return input_buffer;
}

void AudioDecoder::PushedInputBuffer()
{
    Schedule(consumed_cb_);
}

void AudioDecoder::PushOutputBuffer(
        uint8_t *buffer, int64_t size, int64_t pts)
{
    scoped_refptr<DecodedAudio> decoded_audio = new DecodedAudio(
            audio_header_.number_of_channels,
            GetSampleType(),
            kSbMediaAudioFrameStorageTypeInterleaved,
            pts,
            size);
    SbMemoryCopy(decoded_audio->buffer(), buffer, size);
    decoded_audios_.push(decoded_audio);
    Schedule(output_cb_);
}

void AudioDecoder::WriteEndOfStream()
{
    SB_DCHECK(BelongsToCurrentThread());
    SB_DCHECK(output_cb_.is_valid());

    stream_ended_ = true;
}

bool AudioDecoder::IfEndOfStream()
{
    return stream_ended_;
}

scoped_refptr<AudioDecoder::DecodedAudio> AudioDecoder::Read()
{
    SB_DCHECK(BelongsToCurrentThread());
    SB_DCHECK(output_cb_.is_valid());
    SB_DCHECK(!decoded_audios_.empty());

    scoped_refptr<DecodedAudio> result;
    if (!decoded_audios_.empty()) {
        result = decoded_audios_.front();
        decoded_audios_.pop();
    }
    return result;
}

void AudioDecoder::Reset()
{
    SB_DCHECK(BelongsToCurrentThread());

    AudioContext *con = reinterpret_cast<AudioContext*>(audio_context);
    con->SetReady();

    while (!decoded_audios_.empty()) {
        decoded_audios_.pop();
    }
    CancelPendingJobs();
    while (!input_buffers_.empty()) {
        input_buffers_.pop();
    }
    con->SetPlay();
}

SbMediaAudioSampleType AudioDecoder::GetSampleType() const
{
    SB_DCHECK(BelongsToCurrentThread());

    return sample_type_;
}

SbMediaAudioFrameStorageType AudioDecoder::GetStorageType() const
{
    SB_DCHECK(BelongsToCurrentThread());

    return kSbMediaAudioFrameStorageTypeInterleaved;
}

int AudioDecoder::GetSamplesPerSecond() const
{
    return audio_header_.samples_per_second;
}

}  // namespace gstreamer
}  // namespace shared
}  // namespace starboard
