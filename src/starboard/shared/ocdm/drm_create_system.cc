#include "starboard/shared/ocdm/drm_system_ocdm.h"

SbDrmSystem SbDrmCreateSystem(const char *key_system, void *context,
        SbDrmSessionUpdateRequestFunc update_request_callback,
        SbDrmSessionUpdatedFunc session_updated_callback,
        SbDrmSessionKeyStatusesChangedFunc key_statuses_changed_callback,
        SbDrmServerCertificateUpdatedFunc server_certificate_updated_callback,
        SbDrmSessionClosedFunc session_closed_callback) {
    using starboard::shared::ocdm::DrmSystemOcdm;
    if (!update_request_callback || !session_updated_callback) {
        return kSbDrmSystemInvalid;
    }
    if (!key_statuses_changed_callback) {
        return kSbDrmSystemInvalid;
    }
    if (!server_certificate_updated_callback || !session_closed_callback) {
        return kSbDrmSystemInvalid;
    }
    if (!DrmSystemOcdm::IsKeySystemSupported(key_system)) {
        SB_DLOG(WARNING) << "Invalid key system " << key_system;
        return kSbDrmSystemInvalid;
    }
    return new DrmSystemOcdm(context, update_request_callback,
            session_updated_callback, key_statuses_changed_callback,
            server_certificate_updated_callback, session_closed_callback,
            "Linux", "Linux");
}
