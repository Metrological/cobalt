#include "third_party/starboard/wpe/shared/player/player_internal.h"

namespace third_party {
namespace starboard {
namespace wpe {
namespace shared {
namespace player {

bool Player::PlatformNonFushingSetRate(_GstElement* pipeline, double rate) {
  return false;
}

}  // namespace player
}  // namespace shared
}  // namespace wpe
}  // namespace starboard
}  // namespace third_party