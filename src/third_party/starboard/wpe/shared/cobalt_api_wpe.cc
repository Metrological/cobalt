#include "cobalt_api_wpe.h"
#include "application_wpe.h"
#include "system/system_reset.h"

namespace third_party {
namespace starboard {
namespace wpe {
namespace shared {

void SetURL(const char* link_data)
{
    ::third_party::starboard::wpe::shared::Application::WaitForInit(); // Make sure that Application Singleton exists

    auto* application_wpe = static_cast<Application*>(::starboard::shared::starboard::Application::Get());
    application_wpe->NavigateTo(link_data);
}

void DeepLink(const char* link_data)
{
    ::third_party::starboard::wpe::shared::Application::WaitForInit(); // Make sure that Application Singleton exists

    auto* application_wpe = static_cast<Application*>(::starboard::shared::starboard::Application::Get());
    application_wpe->DeepLink(link_data);
}

void Suspend() 
{
    Application::WaitForInit(); 
    Application::Get()->Suspend();
}

void Resume()
{
    Application::WaitForInit(); 
    Application::Get()->Resume();
}

bool Reset(ResetType resetType)
{
    bool status = false;

    switch (resetType) {
      case ResetType::kFactory:
        status = ::third_party::starboard::wpe::shared::system::ResetFactory();
        break;
      case ResetType::kCache:
        status = ::third_party::starboard::wpe::shared::system::ResetCache();
        break;
      case ResetType::kCredentials:
        status = ::third_party::starboard::wpe::shared::system::ResetCredentials();
        break;
      default:
        break;
    }
    return status;
}

void Stop()
{
    Application::WaitForInit();
    Application::Get()->Stop();
}

}  // namespace shared
}  // namespace wpe
}  // namespace starboard
}  // namespace third_party
