#ifndef STARBOARD_SHARED_OCDM_DRM_SYSTEM_OCDM_H_
#define STARBOARD_SHARED_OCDM_DRM_SYSTEM_OCDM_H_

#include <map>

#include "starboard/shared/starboard/drm/drm_system_internal.h"
#include "starboard/shared/starboard/thread_checker.h"
#include "starboard/common/string.h"
#include "starboard/memory.h"

#include "opencdm/open_cdm.h"

namespace starboard {
namespace shared {
namespace ocdm {

#define NEW_OCDM_INTERFACE 1
#ifdef NEW_OCDM_INTERFACE
typedef OpenCDMSystem OPEN_CDM_SYSTEM;
#else
typedef OpenCDMAccessor OPEN_CDM_SYSTEM;
#endif

struct OcdmCounterContext {
    uint64_t initialization_vector_;
    uint64_t block_offset_;
    unsigned char byte_offset_;
};

#define NETWORKBYTES_TO_QWORD(qword, byte, index)                              \
        do { (qword)  = ((unsigned char*)(byte))[(index)+0]; (qword) <<= 8;    \
             (qword) |= ((unsigned char*)(byte))[(index)+1]; (qword) <<= 8;    \
             (qword) |= ((unsigned char*)(byte))[(index)+2]; (qword) <<= 8;    \
             (qword) |= ((unsigned char*)(byte))[(index)+3]; (qword) <<= 8;    \
             (qword) |= ((unsigned char*)(byte))[(index)+4]; (qword) <<= 8;    \
             (qword) |= ((unsigned char*)(byte))[(index)+5]; (qword) <<= 8;    \
             (qword) |= ((unsigned char*)(byte))[(index)+6]; (qword) <<= 8;    \
             (qword) |= ((unsigned char*)(byte))[(index)+7]; } while( false )

class DrmSystemOcdm: public SbDrmSystemPrivate {

public:
    DrmSystemOcdm(void *context,
            SbDrmSessionUpdateRequestFunc update_request_callback,
            SbDrmSessionUpdatedFunc session_updated_callback,
            SbDrmSessionKeyStatusesChangedFunc key_statuses_changed_callback,
            SbDrmServerCertificateUpdatedFunc server_certificate_updated_callback,
            const std::string &company_name, const std::string &model_name);

    ~DrmSystemOcdm() override;

    static bool IsKeySystemSupported(const char *key_system);

    void GenerateSessionUpdateRequest(int ticket, const char *type,
            const void *initialization_data, int initialization_data_size)
                    override;

    void CloseSession(const void *session_id, int session_id_size) override;

    void UpdateSession(int ticket, const void *key, int key_size,
            const void *session_id, int session_id_size) override;

    DecryptStatus Decrypt(InputBuffer *buffer) override;

    bool IsServerCertificateUpdatable() override {
        return true;
    }
    void UpdateServerCertificate(int ticket, const void *certificate,
            int certificate_size) override;

    void OCDMProcessChallengeInternal(OpenCDMSession *session, const char url[],
            const uint8_t challenge[], const uint16_t challenge_length);
    void OCDMKeyUpdateInternal(struct OpenCDMSession *session,
            const uint8_t key_id[], const uint8_t length);
    void OCDMMessageInternal(struct OpenCDMSession *session,
            const char message[]);

private:
    static void OCDMProcessChallenge(OpenCDMSession *session, void *user_data,
            const char url[], const uint8_t challenge[],
            const uint16_t challenge_length);
    static void OCDMKeyUpdate(struct OpenCDMSession *session, void *user_data,
            const uint8_t key_id[], const uint8_t length);
    static void OCDMMessage(struct OpenCDMSession *session, void *user_data,
            const char message[]);

    void SetTicket(struct OpenCDMSession *session, int ticket);
    int GetAndResetTicket(struct OpenCDMSession *session);

    std::string SessionToSessionId(struct OpenCDMSession *session);
    struct OpenCDMSession* SessionIdToSession(const void *session_id,
            int session_id_size);

    void *const context_;
    int pre_session_ticket_;
    std::map<OpenCDMSession*, int> session_to_ticket_;
    std::map<std::string, OpenCDMSession*> keyid_to_session_;

    const SbDrmSessionUpdateRequestFunc session_update_request_callback_;
    const SbDrmSessionUpdatedFunc session_updated_callback_;
    const SbDrmSessionKeyStatusesChangedFunc key_statuses_changed_callback_;
    const SbDrmServerCertificateUpdatedFunc server_certificate_updated_callback_;

    OPEN_CDM_SYSTEM *ocdm_system_;
    OpenCDMSessionCallbacks session_callbacks_;
};

}  // namespace ocdm
}  // namespace shared
}  // namespace starboard

#endif  // STARBOARD_SHARED_OCDM_DRM_SYSTEM_OCDM_H_
