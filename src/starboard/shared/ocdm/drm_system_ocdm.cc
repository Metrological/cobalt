#include "starboard/shared/ocdm/drm_system_ocdm.h"

#define DEBUG_LOG(...) printf(__VA_ARGS__)

namespace starboard {
namespace shared {
namespace ocdm {

const char *kOcdmKeySystem[] = { "com.youtube.playready" };

DrmSystemOcdm::DrmSystemOcdm(void *context,
        SbDrmSessionUpdateRequestFunc session_update_request_callback,
        SbDrmSessionUpdatedFunc session_updated_callback,
        SbDrmSessionKeyStatusesChangedFunc key_statuses_changed_callback,
        SbDrmServerCertificateUpdatedFunc server_certificate_updated_callback,
        const std::string &company_name, const std::string &model_name) :
        context_(context), session_update_request_callback_(
                session_update_request_callback), session_updated_callback_(
                session_updated_callback), key_statuses_changed_callback_(
                key_statuses_changed_callback), server_certificate_updated_callback_(
                server_certificate_updated_callback) {

#ifdef NEW_OCDM_INTERFACE
    ocdm_system_ = opencdm_create_system("com.youtube.playready");
#else
    ocdm_system_ = opencdm_create_system();
#endif

}

DrmSystemOcdm::~DrmSystemOcdm() {

    opencdm_destruct_system(ocdm_system_);
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

    OpenCDMSession *session = nullptr;

    memset(&session_callbacks_, 0, sizeof(session_callbacks_));
    session_callbacks_.process_challenge_callback = OCDMProcessChallenge;
    session_callbacks_.key_update_callback = OCDMKeyUpdate;
    session_callbacks_.message_callback = OCDMMessage;

    pre_session_ticket_ = ticket;

#ifdef NEW_OCDM_INTERFACE
    opencdm_construct_session(ocdm_system_,
            (LicenseType) 0, "drmheader", (const uint8_t*) initialization_data,
            initialization_data_size,
            NULL, 0, &session_callbacks_, this, &session);
#else
    opencdm_construct_session(ocdm_system_, "com.youtube.playready",
            (LicenseType) 0, "drmheader", (const uint8_t*) initialization_data,
            initialization_data_size,
            NULL, 0, &session_callbacks_, this, &session);
#endif

}

void DrmSystemOcdm::CloseSession(const void *session_id, int session_id_size) {

    OpenCDMSession *session = SessionIdToSession(session_id, session_id_size);
    opencdm_session_close(session);
}

void DrmSystemOcdm::OCDMProcessChallenge(OpenCDMSession *session,
        void *user_data, const char url[], const uint8_t challenge[],
        const uint16_t challenge_length) {

    ((DrmSystemOcdm*) user_data)->OCDMProcessChallengeInternal(session, url,
            challenge, challenge_length);
}

void DrmSystemOcdm::OCDMProcessChallengeInternal(OpenCDMSession *session,
        const char url[], const uint8_t challenge[],
        const uint16_t challenge_length) {

    std::string session_id = SessionToSessionId(session);
    session_update_request_callback_(this, context_, pre_session_ticket_,
            kSbDrmStatusSuccess, kSbDrmSessionRequestTypeLicenseRequest, "",
            session_id.c_str(), static_cast<int>(session_id.size()), challenge,
            challenge_length, url);
}

void DrmSystemOcdm::UpdateSession(int ticket, const void *key, int key_size,
        const void *session_id, int session_id_size) {

    OpenCDMSession *session = SessionIdToSession(session_id, session_id_size);
    SetTicket(session, ticket);
    opencdm_session_update(session, (const uint8_t*) key, key_size);
}

void DrmSystemOcdm::OCDMKeyUpdate(struct OpenCDMSession *session,
        void *user_data, const uint8_t key_id[], const uint8_t length) {

    ((DrmSystemOcdm*) user_data)->OCDMKeyUpdateInternal(session, key_id,
            length);
}

void DrmSystemOcdm::OCDMKeyUpdateInternal(struct OpenCDMSession *session,
        const uint8_t key_id[], const uint8_t length) {

    std::string session_id = SessionToSessionId(session);
    int ticket = GetAndResetTicket(session);
    session_updated_callback_(this, context_, ticket, kSbDrmStatusSuccess, "",
            session_id.c_str(), static_cast<int>(session_id.size()));
}

void DrmSystemOcdm::OCDMMessage(struct OpenCDMSession *session, void *user_data,
        const char message[]) {

    ((DrmSystemOcdm*) user_data)->OCDMMessageInternal(session, message);
}

void DrmSystemOcdm::OCDMMessageInternal(struct OpenCDMSession *session,
        const char message[]) {
}

void DrmSystemOcdm::UpdateServerCertificate(int ticket, const void *certificate,
        int certificate_size) {
}

SbDrmSystemPrivate::DecryptStatus DrmSystemOcdm::Decrypt(InputBuffer *buffer) {

    const SbDrmSampleInfo *drm_info = buffer->drm_info();
    const SbDrmSubSampleMapping &subsample =
            buffer->drm_info()->subsample_mapping[0];

    DEBUG_LOG("BUFFER INFO %p %d %d %d %d %s\n", buffer->data(), buffer->size(),
            subsample.clear_byte_count, subsample.encrypted_byte_count,
            buffer->sample_type(),
            std::string(reinterpret_cast<const char*>(&drm_info->identifier[0]),
                    drm_info->identifier_size).c_str());
    return kSuccess;
}

std::string DrmSystemOcdm::SessionToSessionId(struct OpenCDMSession *session) {

    return (std::to_string((unsigned long) session));
}

struct OpenCDMSession* DrmSystemOcdm::SessionIdToSession(const void *session_id,
        int session_id_size) {

    const std::string session_id_str(static_cast<const char*>(session_id),
            session_id_size);
    return ((struct OpenCDMSession*) std::stol(session_id_str));
}

void DrmSystemOcdm::SetTicket(struct OpenCDMSession *session, int ticket) {

    session_to_ticket_.insert(std::make_pair(session, ticket));
}

int DrmSystemOcdm::GetAndResetTicket(struct OpenCDMSession *session) {

    auto iter = session_to_ticket_.find(session);
    if (iter == session_to_ticket_.end()) {
        return kSbDrmTicketInvalid;
    }
    auto ticket = iter->second;
    session_to_ticket_.erase(iter);
    return ticket;
}

}  // namespace ocdm
}  // namespace shared
}  // namespace starboard
