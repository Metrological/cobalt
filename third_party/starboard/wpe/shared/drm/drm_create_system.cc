#include <string>

#if defined(HAS_OCDM)
#include "third_party/starboard/wpe/shared/drm/drm_system_ocdm.h"
#else
#include "starboard/shared/starboard/drm/drm_system_internal.h"
#endif

SbDrmSystem SbDrmCreateSystem(
    const char* key_system,
    void* context,
    SbDrmSessionUpdateRequestFunc update_request_callback,
    SbDrmSessionUpdatedFunc session_updated_callback,
    SbDrmSessionKeyStatusesChangedFunc key_statuses_changed_callback,
    SbDrmServerCertificateUpdatedFunc server_certificate_updated_callback,
    SbDrmSessionClosedFunc session_closed_callback) {
#if defined(HAS_OCDM)
  using third_party::starboard::wpe::shared::drm::DrmSystemOcdm;
  std::string empty;
  if (!DrmSystemOcdm::IsKeySystemSupported(key_system, empty.c_str())) {
    SB_DLOG(WARNING) << "Invalid key system " << key_system;
    return kSbDrmSystemInvalid;
  }

  return new DrmSystemOcdm(
      key_system, context, update_request_callback, session_updated_callback,
      key_statuses_changed_callback, server_certificate_updated_callback,
      session_closed_callback);
#else
  return nullptr;
#endif
}
