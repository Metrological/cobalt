#ifndef STARBOARD_SHARED_OCDM_DRM_SYSTEM_OCDM_H_
#define STARBOARD_SHARED_OCDM_DRM_SYSTEM_OCDM_H_

#include "starboard/shared/starboard/drm/drm_system_internal.h"

namespace starboard {
namespace shared {
namespace ocdm {

class DrmSystemOcdm: public SbDrmSystemPrivate {

public:
    DrmSystemOcdm(void *context,
            SbDrmSessionUpdateRequestFunc update_request_callback,
            SbDrmSessionUpdatedFunc session_updated_callback,
            SbDrmSessionKeyStatusesChangedFunc key_statuses_changed_callback,
            SbDrmServerCertificateUpdatedFunc server_certificate_updated_callback,
            const std::string &company_name, const std::string &model_name);

    ~DrmSystemOcdm() override;

    static bool IsKeySystemSupported(const char* key_system);

    void GenerateSessionUpdateRequest(int ticket, const char *type,
            const void *initialization_data, int initialization_data_size)
                    override;

    void UpdateSession(int ticket, const void *key, int key_size,
            const void *sb_drm_session_id, int sb_drm_session_id_size) override;

    void CloseSession(const void *sb_drm_session_id, int sb_drm_session_id_size)
            override;

    DecryptStatus Decrypt(InputBuffer *buffer) override;

    bool IsServerCertificateUpdatable() override {
        return true;
    }
    void UpdateServerCertificate(int ticket, const void *certificate,
            int certificate_size) override;
};

}  // namespace ocdm
}  // namespace shared
}  // namespace starboard

#endif  // STARBOARD_SHARED_OCDM_DRM_SYSTEM_OCDM_H_
