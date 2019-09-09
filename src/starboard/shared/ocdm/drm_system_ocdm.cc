#include "starboard/shared/ocdm/drm_system_ocdm.h"
#include "starboard/common/string.h"

namespace starboard {
namespace shared {
namespace ocdm {

const char* kOcdmKeySystem[] = {"com.microsoft.playready", "com.youtube.playready"};

DrmSystemOcdm::DrmSystemOcdm(void *context,
        SbDrmSessionUpdateRequestFunc session_update_request_callback,
        SbDrmSessionUpdatedFunc session_updated_callback,
        SbDrmSessionKeyStatusesChangedFunc key_statuses_changed_callback,
        SbDrmServerCertificateUpdatedFunc server_certificate_updated_callback,
        const std::string &company_name, const std::string &model_name) {
}

DrmSystemOcdm::~DrmSystemOcdm() {
}

bool DrmSystemOcdm::IsKeySystemSupported(const char *key_system) {
    for (auto ocdm_key_system : kOcdmKeySystem) {
        if (SbStringCompareAll(key_system, ocdm_key_system) == 0) {
            return true;
        }
    }
    return false;
}

void DrmSystemOcdm::GenerateSessionUpdateRequest(int ticket, const char *type,
        const void *initialization_data, int initialization_data_size) {
}

void DrmSystemOcdm::UpdateSession(int ticket, const void *key, int key_size,
        const void *sb_drm_session_id, int sb_drm_session_id_size) {
}

void DrmSystemOcdm::CloseSession(const void *sb_drm_session_id,
        int sb_drm_session_id_size) {
}

void DrmSystemOcdm::UpdateServerCertificate(int ticket, const void *certificate,
        int certificate_size) {
}

SbDrmSystemPrivate::DecryptStatus DrmSystemOcdm::Decrypt(InputBuffer *buffer) {
    return kSuccess;
}

}  // namespace ocdm
}  // namespace shared
}  // namespace starboard
