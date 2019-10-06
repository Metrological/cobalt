#include "starboard/shared/ocdm/drm_system_ocdm.h"

#define DEBUG_LOG(...) printf(__VA_ARGS__)

namespace starboard {
namespace shared {
namespace ocdm {

std::string DrmSystemOcdm::key_system_str = "";

DrmSystemOcdm::DrmSystemOcdm(void *context,
        SbDrmSessionUpdateRequestFunc session_update_request_callback,
        SbDrmSessionUpdatedFunc session_updated_callback,
        SbDrmSessionKeyStatusesChangedFunc key_statuses_changed_callback,
        SbDrmServerCertificateUpdatedFunc server_certificate_updated_callback,
        SbDrmSessionClosedFunc session_closed_callback,
        const std::string &company_name, const std::string &model_name) :
        context_(context), session_update_request_callback_(
                session_update_request_callback), session_updated_callback_(
                session_updated_callback), key_statuses_changed_callback_(
                key_statuses_changed_callback), server_certificate_updated_callback_(
                server_certificate_updated_callback), session_closed_callback_(
                session_closed_callback) {

    ocdm_system_ = opencdm_create_system(key_system_str.c_str());
}

DrmSystemOcdm::~DrmSystemOcdm() {

    auto it = session_list.begin();
    while (it != session_list.end()) {
        opencdm_destruct_session(*it);
        it = session_list.erase(it);
    }
    opencdm_destruct_system(ocdm_system_);
}

bool DrmSystemOcdm::IsKeySystemSupported(const char *key_system) {

    std::string empty_string;
    if (opencdm_is_type_supported(key_system, empty_string.c_str()) == 0) {
        if (key_system_str.empty()) {
            key_system_str = key_system;
        }
        return true;
    }
    return false;
}

void DrmSystemOcdm::GenerateSessionUpdateRequest(int ticket, const char *type,
        const void *initialization_data, int initialization_data_size) {

    OpenCDMSession *session = nullptr;

    memset(&session_callbacks_, 0, sizeof(session_callbacks_));
    session_callbacks_.process_challenge_callback = OCDMProcessChallenge;
    session_callbacks_.key_update_callback = OCDMKeyUpdate;
    session_callbacks_.keys_updated_callback = OCDMKeyUpdated;
    session_callbacks_.error_message_callback = OCDMMessage;

    pre_session_ticket_ = ticket;
    opencdm_construct_session(ocdm_system_,
            (LicenseType) 0, "drmheader", (const uint8_t*) initialization_data,
            initialization_data_size,
            NULL, 0, &session_callbacks_, this, &session);

    session_list.push_back(session);
}

void DrmSystemOcdm::CloseSession(const void *session_id, int session_id_size) {

    OpenCDMSession *session = SessionIdToSession(session_id, session_id_size);
    opencdm_destruct_session(session);
    session_closed_callback_(this, context_, session_id, session_id_size);

    auto it = std::find(session_list.begin(), session_list.end(), session);
    if (it != session_list.end())
        session_list.erase(it);
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

    std::string challenge_str(reinterpret_cast<char const*>(challenge),
            challenge_length);
    size_t type_position = challenge_str.find(":Type:");
    std::string request_type(challenge_str.c_str(),
            type_position != std::string::npos ? type_position : 0);

    unsigned offset = 0u;
    if (!request_type.empty()
            && request_type.length() != challenge_str.length())
        offset = type_position + 6;

    int message_type = 0;
    if (request_type.length() == 1)
        message_type = std::stoi(request_type);

    if (message_type == kSbDrmSessionRequestTypeLicenseRequest) {

        session_update_request_callback_(this, context_, pre_session_ticket_,
                kSbDrmStatusSuccess, (SbDrmSessionRequestType) message_type, "",
                session_id.c_str(), static_cast<int>(session_id.size()),
                challenge + offset, challenge_length - offset, url);
    }
    else if (message_type == kSbDrmSessionRequestTypeLicenseRenewal) {

        session_update_request_callback_(this, context_, kSbDrmTicketInvalid,
                kSbDrmStatusSuccess, (SbDrmSessionRequestType) message_type, "",
                session_id.c_str(), static_cast<int>(session_id.size()),
                challenge + offset, challenge_length - offset, url);
    }
    else {
        DEBUG_LOG("DrmSystemOcdm::OCDMProcessChallengeInternal %d\n",
                message_type);
    }
}

void DrmSystemOcdm::OCDMKeyUpdate(struct OpenCDMSession *session,
        void *user_data, const uint8_t key_id[], const uint8_t length) {

    ((DrmSystemOcdm*) user_data)->OCDMKeyUpdateInternal(session, key_id,
            length);
}

void DrmSystemOcdm::OCDMKeyUpdateInternal(struct OpenCDMSession *session,
        const uint8_t key_id[], const uint8_t length) {
}

void DrmSystemOcdm::UpdateSession(int ticket, const void *key, int key_size,
        const void *session_id, int session_id_size) {

    OpenCDMSession *session = SessionIdToSession(session_id, session_id_size);
    // SetTicket(session, ticket);
    opencdm_session_update(session, (const uint8_t*) key, key_size);

    // Note: Following needs to be done in OCDMKeyUpdated callback. But during
    // license renewal, the callback is not getting invoked by OCDM.
    session_updated_callback_(this, context_, ticket, kSbDrmStatusSuccess, "",
            session_id, session_id_size);
}

void DrmSystemOcdm::OCDMKeyUpdated(const struct OpenCDMSession *session,
        void *user_data) {

    ((DrmSystemOcdm*) user_data)->OCDMKeyUpdatedInternal(
            (struct OpenCDMSession*) session);
}

void DrmSystemOcdm::OCDMKeyUpdatedInternal(struct OpenCDMSession *session) {

    // std::string session_id = SessionToSessionId(session);
    //
    // int ticket = GetAndResetTicket(session);
    // if (ticket == kSbDrmTicketInvalid) return;
    //
    // session_updated_callback_(this, context_, ticket, kSbDrmStatusSuccess, "",
    //        session_id.c_str(), static_cast<int>(session_id.size()));
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

    opencdm_system_set_server_certificate(ocdm_system_,
            (const uint8_t*) certificate, certificate_size);

    server_certificate_updated_callback_(this, context_, ticket,
            kSbDrmStatusSuccess, "");
}

void IncrementIv(uint8_t *iv, size_t block_count) {
    if (0 == block_count)
        return;
    uint8_t carry = 0;
    uint8_t n = static_cast<uint8_t>(16 - 1);

    while (n >= 8) {
        uint32_t temp = block_count & 0xff;
        temp += iv[n];
        temp += carry;
        iv[n] = temp & 0xff;
        carry = (temp & 0x100) ? 1 : 0;
        block_count = block_count >> 8;
        n--;
        if (0 == block_count && !carry) {
            break;
        }
    }
}

#define SWAP(a,b) a^=b^=a^=b;

SbDrmSystemPrivate::DecryptStatus DrmSystemOcdm::Decrypt(InputBuffer *buffer) {

    const SbDrmSampleInfo *drm_info = buffer->drm_info();
    uint8_t *keyid = (uint8_t*) drm_info->identifier;
    uint32_t keyid_length = drm_info->identifier_size;
    std::string keyid_str(reinterpret_cast<char const*>(keyid), keyid_length);

    OpenCDMSession *session = opencdm_get_session(keyid, keyid_length, 0);
    if (!session) {
        return kRetry;
    }

    uint8_t *data = (uint8_t*) buffer->data();
    uint32_t data_length = buffer->size();

    std::vector<uint8_t> initialization_vector(drm_info->initialization_vector,
            drm_info->initialization_vector
                    + drm_info->initialization_vector_size);
    uint8_t *iv = initialization_vector.data();
    uint32_t iv_length = initialization_vector.size();

    // Note: This is a work-around for Widevine.It needs to be removed as
    // OCDM should be DRM agnostic
    if (key_system_str.compare("com.widevine.alpha") == 0) {
        SWAP(iv[0], iv[7]);
        SWAP(iv[1], iv[6]);
        SWAP(iv[2], iv[5]);
        SWAP(iv[3], iv[4]);
    }

    uint64_t block_offset = 0;
    uint64_t byte_offset = 0;
    size_t block_counter = 0;
    size_t encrypted_offset = 0;

    for (size_t i = 0; i < buffer->drm_info()->subsample_count; i++) {

        const SbDrmSubSampleMapping &subsample =
                buffer->drm_info()->subsample_mapping[i];

        if (subsample.clear_byte_count) {
            data = data + subsample.clear_byte_count;
            data_length -= subsample.clear_byte_count;
        }
        if (subsample.encrypted_byte_count) {

            OcdmCounterContext counter_context = { 0 };
            NETWORKBYTES_TO_QWORD(counter_context.initialization_vector_, iv,
                    0);
            NETWORKBYTES_TO_QWORD(block_offset, iv, 8);
            counter_context.block_offset_ = block_offset;
            counter_context.byte_offset_ = byte_offset;
            const uint8_t *piv = (const uint8_t*) &counter_context;

            opencdm_session_decrypt(session, data,
                    subsample.encrypted_byte_count, piv,
                    sizeof(OcdmCounterContext), drm_info->identifier,
                    drm_info->identifier_size, 1);

            data = data + subsample.encrypted_byte_count;
            data_length -= subsample.encrypted_byte_count;
        }
        byte_offset += subsample.encrypted_byte_count;
        byte_offset %= 16;

        encrypted_offset += subsample.encrypted_byte_count;
        size_t new_block_counter = encrypted_offset / 16;
        IncrementIv(iv, new_block_counter - block_counter);
        block_counter = new_block_counter;
    }
    opencdm_destruct_session(session);
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
