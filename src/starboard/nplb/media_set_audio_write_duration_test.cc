// Copyright 2019 The Cobalt Authors. All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "starboard/media.h"

#include "starboard/common/optional.h"
#include "starboard/common/spin_lock.h"
#include "starboard/player.h"
#include "starboard/shared/starboard/media/media_support_internal.h"
#include "starboard/shared/starboard/player/video_dmp_reader.h"
#include "starboard/testing/fake_graphics_context_provider.h"
#include "testing/gtest/include/gtest/gtest.h"

#if SB_API_VERSION >= 11
namespace starboard {
namespace nplb {
namespace {

using ::starboard::testing::FakeGraphicsContextProvider;
using shared::starboard::player::video_dmp::VideoDmpReader;
using ::testing::ValuesIn;

const SbTime kDuration = kSbTimeSecond / 2;
const SbTime kSmallWaitInterval = 10 * kSbTimeMillisecond;

std::string GetTestInputDirectory() {
  const size_t kPathSize = SB_FILE_MAX_PATH + 1;

  char content_path[kPathSize];
  SB_CHECK(
      SbSystemGetPath(kSbSystemPathContentDirectory, content_path, kPathSize));
  std::string directory_path =
      std::string(content_path) + SB_FILE_SEP_CHAR + "test" + SB_FILE_SEP_CHAR +
      "starboard" + SB_FILE_SEP_CHAR + "shared" + SB_FILE_SEP_CHAR +
      "starboard" + SB_FILE_SEP_CHAR + "player" + SB_FILE_SEP_CHAR + "testdata";

  SB_CHECK(SbDirectoryCanOpen(directory_path.c_str()))
      << "Cannot open directory " << directory_path;
  return directory_path;
}

static void DeallocateSampleFunc(SbPlayer player,
                                 void* context,
                                 const void* sample_buffer) {
  SB_UNREFERENCED_PARAMETER(player);
  SB_UNREFERENCED_PARAMETER(context);
  SB_UNREFERENCED_PARAMETER(sample_buffer);
}

std::string ResolveTestFileName(const char* filename) {
  auto ret = GetTestInputDirectory() + SB_FILE_SEP_CHAR + filename;
  return ret;
}

class SbMediaSetAudioWriteDurationTest
    : public ::testing::TestWithParam<const char*> {
 public:
  SbMediaSetAudioWriteDurationTest()
      : dmp_reader_(ResolveTestFileName(GetParam()).c_str()) {}

  void TryToWritePendingSample() {
    {
      starboard::ScopedSpinLock lock(&pending_decoder_status_lock_);
      if (!pending_decoder_status_.has_engaged()) {
        return;
      }
    }

    // If we have played the full duration required, then stop.
    if ((last_input_timestamp_ - first_input_timestamp_) >= total_duration_) {
      return;
    }

    // Check if we're about to input too far beyond the current playback time.
    SbPlayerInfo2 info;
    SbPlayerGetInfo2(pending_decoder_status_->player, &info);
    if ((last_input_timestamp_ - info.current_media_timestamp) > kDuration) {
      // Postpone writing samples.
      return;
    }

    SbPlayerSampleInfo player_sample_info =
        dmp_reader_.GetPlayerSampleInfo(kSbMediaTypeAudio, ++index_);

    SbPlayerSampleInfo sample_info = {};
    sample_info.buffer = player_sample_info.buffer;
    sample_info.buffer_size = player_sample_info.buffer_size;
    sample_info.timestamp = player_sample_info.timestamp;
    sample_info.drm_info = NULL;
#if SB_API_VERSION >= 11
    sample_info.type = kSbMediaTypeAudio;
    sample_info.audio_sample_info = dmp_reader_.audio_sample_info();
#endif  // SB_API_VERSION >= 11

    SbPlayer player = pending_decoder_status_->player;
    SbMediaType type = pending_decoder_status_->type;
    int ticket = pending_decoder_status_->ticket;
    {
      starboard::ScopedSpinLock lock(&pending_decoder_status_lock_);
      pending_decoder_status_ = nullopt;
    }

    SbPlayerWriteSample2(player, kSbMediaTypeAudio, &sample_info, 1);
    last_input_timestamp_ = player_sample_info.timestamp;
  }

  void PlayerStatus(SbPlayer player, SbPlayerState state, int ticket) {
    player_state_ = state;
  }

  void StorePendingDecoderStatus(SbPlayer player,
                                 SbMediaType type,
                                 int ticket) {
    starboard::ScopedSpinLock lock(&pending_decoder_status_lock_);
    SB_DCHECK(!pending_decoder_status_.has_engaged());
    PendingDecoderStatus pending_decoder_status = {};
    pending_decoder_status.player = player;
    pending_decoder_status.type = type;
    pending_decoder_status.ticket = ticket;
    pending_decoder_status_ = pending_decoder_status;
  }

  SbPlayer CreatePlayer() {
    SbMediaAudioSampleInfo audio_sample_info = dmp_reader_.audio_sample_info();
    SbMediaAudioCodec kAudioCodec = dmp_reader_.audio_codec();
    SbDrmSystem kDrmSystem = kSbDrmSystemInvalid;

    SbPlayerOutputMode output_mode = kSbPlayerOutputModeDecodeToTexture;
    if (!SbPlayerOutputModeSupported(output_mode, kSbMediaVideoCodecNone,
                                     kSbDrmSystemInvalid)) {
      output_mode = kSbPlayerOutputModePunchOut;
    }

    last_input_timestamp_ =
        dmp_reader_.GetPlayerSampleInfo(kSbMediaTypeAudio, 0).timestamp;
    first_input_timestamp_ = last_input_timestamp_;

    SbPlayer player = SbPlayerCreate(
        fake_graphics_context_provider_.window(), kSbMediaVideoCodecNone,
        kAudioCodec, kSbDrmSystemInvalid, &audio_sample_info,
#if SB_API_VERSION >= 11
        NULL /* max_video_capabilities */,
#endif  // SB_API_VERSION >= 11
        DummyDeallocateSampleFunc, DecoderStatusFunc, PlayerStatusFunc,
        DummyErrorFunc, this /* context */, output_mode,
        fake_graphics_context_provider_.decoder_target_provider());
    EXPECT_TRUE(SbPlayerIsValid(player));
    return player;
  }

  void WaitForPlayerState(SbPlayerState desired_state) {
    SbTime start_of_wait = SbTimeGetMonotonicNow();
    const SbTime kMaxWaitTime = 3 * kSbTimeSecond;
    while (player_state_ != desired_state &&
           (SbTimeGetMonotonicNow() - start_of_wait) < kMaxWaitTime) {
      SbThreadSleep(kSmallWaitInterval);
      TryToWritePendingSample();
    }
    ASSERT_EQ(desired_state, player_state_);
  }

 protected:
  struct PendingDecoderStatus {
    SbPlayer player;
    SbMediaType type;
    int ticket;
  };

  FakeGraphicsContextProvider fake_graphics_context_provider_;
  VideoDmpReader dmp_reader_;
  SbPlayerState player_state_ = kSbPlayerStateInitialized;
  SbTime last_input_timestamp_ = 0;
  SbTime first_input_timestamp_ = 0;
  int index_ = 0;
  SbTime total_duration_ = kDuration;
  // Guard access to |pending_decoder_status_|.
  mutable SbAtomic32 pending_decoder_status_lock_ =
      starboard::kSpinLockStateReleased;
  optional<PendingDecoderStatus> pending_decoder_status_;

 private:
  static void DummyDeallocateSampleFunc(SbPlayer player,
                                        void* context,
                                        const void* sample_buffer) {}

  static void DummyErrorFunc(SbPlayer player,
                             void* context,
                             SbPlayerError error,
                             const char* message) {}

  static void DecoderStatusFunc(SbPlayer player,
                                void* context,
                                SbMediaType type,
                                SbPlayerDecoderState state,
                                int ticket) {
    if (state != kSbPlayerDecoderStateNeedsData) {
      return;
    }
    static_cast<SbMediaSetAudioWriteDurationTest*>(context)
        ->StorePendingDecoderStatus(player, type, ticket);
  }

  static void PlayerStatusFunc(SbPlayer player,
                               void* context,
                               SbPlayerState state,
                               int ticket) {
    static_cast<SbMediaSetAudioWriteDurationTest*>(context)->PlayerStatus(
        player, state, ticket);
  }
};

TEST_P(SbMediaSetAudioWriteDurationTest, WriteLimitedInput) {
  ASSERT_NE(dmp_reader_.audio_codec(), kSbMediaAudioCodecNone);
  ASSERT_GT(dmp_reader_.number_of_audio_buffers(), 0);

  SbMediaSetAudioWriteDuration(kDuration);

  SbPlayer player = CreatePlayer();
  WaitForPlayerState(kSbPlayerStateInitialized);

  // Seek to preroll.
  SbPlayerSeek2(player, first_input_timestamp_, /* ticket */ 1);

  WaitForPlayerState(kSbPlayerStatePresenting);

  // Check that the playback time is > 0.
  SbPlayerInfo2 info;
  SbPlayerGetInfo2(player, &info);
  ASSERT_GT(info.current_media_timestamp, 0);

  SbPlayerDestroy(player);
}

TEST_P(SbMediaSetAudioWriteDurationTest, WriteContinuedLimitedInput) {
  ASSERT_NE(dmp_reader_.audio_codec(), kSbMediaAudioCodecNone);
  ASSERT_GT(dmp_reader_.number_of_audio_buffers(), 0);

  SbMediaSetAudioWriteDuration(kDuration);

  // This directly impacts the runtime of the test.
  total_duration_ = 15 * kSbTimeSecond;

  SbPlayer player = CreatePlayer();
  WaitForPlayerState(kSbPlayerStateInitialized);

  // Seek to preroll.
  SbPlayerSeek2(player, first_input_timestamp_, /* ticket */ 1);

  WaitForPlayerState(kSbPlayerStatePresenting);

  // Wait for the player to play far enough. It may not play all the way to
  // the end, but it should leave off no more than |kDuration|.
  SbTime min_ending_playback_time = total_duration_ - kDuration;
  SbTime start_of_wait = SbTimeGetMonotonicNow();
  const SbTime kMaxWaitTime = total_duration_ + 5 * kSbTimeSecond;
  SbPlayerInfo2 info;
  SbPlayerGetInfo2(player, &info);
  while (info.current_media_timestamp < min_ending_playback_time &&
         (SbTimeGetMonotonicNow() - start_of_wait) < kMaxWaitTime) {
    SbPlayerGetInfo2(player, &info);
    SbThreadSleep(kSmallWaitInterval);
    TryToWritePendingSample();
  }
  EXPECT_GE(info.current_media_timestamp, min_ending_playback_time);
  SbPlayerDestroy(player);
}

std::vector<const char*> GetSupportedTests() {
  const char* kFilenames[] = {"beneath_the_canopy_140_aac.dmp",
                              "beneath_the_canopy_249_opus.dmp"};

  static std::vector<const char*> test_params;

  if (!test_params.empty()) {
    return test_params;
  }

  for (auto filename : kFilenames) {
    VideoDmpReader dmp_reader(ResolveTestFileName(filename).c_str());
    SB_DCHECK(dmp_reader.number_of_audio_buffers() > 0);
    if (SbMediaIsAudioSupported(dmp_reader.audio_codec(),
                                dmp_reader.audio_bitrate())) {
      test_params.push_back(filename);
    }
  }
  SB_DCHECK(!test_params.empty());
  return test_params;
}

INSTANTIATE_TEST_CASE_P(SbMediaSetAudioWriteDurationTests,
                        SbMediaSetAudioWriteDurationTest,
                        ValuesIn(GetSupportedTests()));
}  // namespace
}  // namespace nplb
}  // namespace starboard
#endif  // SB_API_VERSION >= 11