#include "cobalt_api_wpe.h"
#include "application_wpe.h"

namespace third_party {
namespace starboard {
namespace wpe {
namespace shared {

void SetURL(const char* link_data)
{
    ::third_party::starboard::wpe::shared::Application::WaitForInit(); // Make sure that Application Singleton exists

    auto* application_wpe = static_cast<Application*>(::starboard::shared::starboard::Application::Get());
    application_wpe->NavitgateTo(link_data);
}

}  // namespace shared
}  // namespace wpe
}  // namespace starboard
}  // namespace third_party1