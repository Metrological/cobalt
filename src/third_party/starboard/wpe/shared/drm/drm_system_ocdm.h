#ifndef THIRD_PARTY_STARBOARD_WPE_SHARED_DRM_DRM_SYSTEM_OCDM_H_
#define THIRD_PARTY_STARBOARD_WPE_SHARED_DRM_DRM_SYSTEM_OCDM_H_

#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "starboard/common/mutex.h"
#include "starboard/event.h"
#include "starboard/shared/starboard/drm/drm_system_internal.h"

struct _GstBuffer;
struct OpenCDMSystem;

namespace third_party {
namespace starboard {
namespace wpe {
namespace shared {
namespace drm {

namespace session {
class Session;
}

class DrmSystemOcdm : public SbDrmSystemPrivate {
 public:
  class Observer {
   public:
    virtual ~Observer() {}
    virtual void OnKeyReady(const uint8_t* key, size_t key_len) = 0;
  };

  struct KeyWithStatus {
    SbDrmKeyId key;
    SbDrmKeyStatus status;
  };

  using KeysWithStatus = std::vector<KeyWithStatus>;

  DrmSystemOcdm(
      const char* key_system,
      void* context,
      SbDrmSessionUpdateRequestFunc update_request_callback,
      SbDrmSessionUpdatedFunc session_updated_callback,
      SbDrmSessionKeyStatusesChangedFunc key_statuses_changed_callback,
      SbDrmServerCertificateUpdatedFunc server_certificate_updated_callback,
      SbDrmSessionClosedFunc session_closed_callback);

  ~DrmSystemOcdm() override;

  static bool IsKeySystemSupported(const char* key_system,
                                   const char* mime_type);

  // SbDrmSystemPrivate
  void GenerateSessionUpdateRequest(int ticket,
                                    const char* type,
                                    const void* initialization_data,
                                    int initialization_data_size) override;
  void CloseSession(const void* session_id, int session_id_size) override;
  void UpdateSession(int ticket,
                     const void* key,
                     int key_size,
                     const void* session_id,
                     int session_id_size) override;
  DecryptStatus Decrypt(InputBuffer* buffer) override;
  bool IsServerCertificateUpdatable() override { return false; }
  void UpdateServerCertificate(int ticket,
                               const void* certificate,
                               int certificate_size) override;

  void AddObserver(Observer* obs);
  void RemoveObserver(Observer* obs);
  void OnKeyUpdated(const std::string& session_id,
                    SbDrmKeyId&& key_id,
                    SbDrmKeyStatus status);
  void OnAllKeysUpdated();
  std::string SessionIdByKeyId(const uint8_t* key, uint8_t key_len);
  bool Decrypt(const std::string& id,
               _GstBuffer* buffer,
               _GstBuffer* sub_sample,
               uint32_t sub_sample_count,
               _GstBuffer* iv,
               _GstBuffer* key_id);
  std::set<std::string> GetReadyKeys() const;
  KeysWithStatus GetSessionKeys(const std::string& session_id) const;

 private:
  session::Session* GetSessionById(const std::string& id) const;
  void AnnounceKeys();

  std::set<std::string> GetReadyKeysUnlocked() const;

  std::string key_system_;
  void* context_;
  std::vector<std::unique_ptr<session::Session>> sessions_;

  const SbDrmSessionUpdateRequestFunc session_update_request_callback_;
  const SbDrmSessionUpdatedFunc session_updated_callback_;
  const SbDrmSessionKeyStatusesChangedFunc key_statuses_changed_callback_;
  const SbDrmServerCertificateUpdatedFunc server_certificate_updated_callback_;
  const SbDrmSessionClosedFunc session_closed_callback_;

  OpenCDMSystem* ocdm_system_;
  std::vector<Observer*> observers_;
  std::unordered_map<std::string, KeysWithStatus> session_keys_;
  mutable std::set<std::string> cached_ready_keys_;
  SbEventId event_id_;
  ::starboard::Mutex mutex_;
};

}  // namespace drm
}  // namespace shared
}  // namespace wpe
}  // namespace starboard
}  // namespace third_party

#endif  // THIRD_PARTY_STARBOARD_WPE_SHARED_DRM_DRM_SYSTEM_OCDM_H_
