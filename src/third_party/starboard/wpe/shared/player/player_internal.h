#ifndef THIRD_PARTY_STARBOARD_WPE_SHARED_PLAYER_PLAYER_INTERNAL_H_
#define THIRD_PARTY_STARBOARD_WPE_SHARED_PLAYER_PLAYER_INTERNAL_H_

#include <memory>

#include "starboard/player.h"

namespace third_party {
namespace starboard {
namespace wpe {
namespace shared {
namespace player {

struct SB_EXPORT Player {
  virtual ~Player() {}
  static int MaxNumberOfSamplesPerWrite();
  virtual void MarkEOS(SbMediaType stream_type) = 0;
  virtual void WriteSample(SbMediaType sample_type,
                           const SbPlayerSampleInfo* sample_infos,
                           int number_of_sample_infos) = 0;
  virtual void SetVolume(double volume) = 0;
  virtual void Seek(SbTime seek_to_timestamp, int ticket) = 0;
  virtual bool SetRate(double rate) = 0;
  virtual void GetInfo(SbPlayerInfo2* info) = 0;
  virtual void SetBounds(int zindex, int x, int y, int w, int h) = 0;
};

}  // namespace player
}  // namespace shared
}  // namespace wpe
}  // namespace starboard
}  // namespace third_party

struct SbPlayerPrivate {
  SbPlayerPrivate(SbWindow window,
                  SbMediaVideoCodec video_codec,
                  SbMediaAudioCodec audio_codec,
                  SbDrmSystem drm_system,
                  const SbMediaAudioSampleInfo* audio_sample_info,
#if SB_API_VERSION >= 11
                  const char* max_video_capabilities,
#endif  // SB_API_VERSION >= 11
                  SbPlayerDeallocateSampleFunc sample_deallocate_func,
                  SbPlayerDecoderStatusFunc decoder_status_func,
                  SbPlayerStatusFunc player_status_func,
                  SbPlayerErrorFunc player_error_func,
                  void* context,
                  SbPlayerOutputMode output_mode,
                  SbDecodeTargetGraphicsContextProvider* provider);
  ~SbPlayerPrivate() {}

  int MaxNumberOfSamplesPerWrite() const {
    using third_party::starboard::wpe::shared::player::Player;
    return Player::MaxNumberOfSamplesPerWrite();
  }
  std::unique_ptr<third_party::starboard::wpe::shared::player::Player> player_;
};

#endif  // THIRD_PARTY_STARBOARD_WPE_SHARED_PLAYER_PLAYER_INTERNAL_H_
