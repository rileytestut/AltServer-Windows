#pragma once
#include "AnisetteData.h"

namespace ExternalAnisette
{
    // Header-provider API: GET <base>/ and GET <base>/v3/client_info.
    // No Apple account, password, or verification code is sent to this provider.
    utility::string_t NormalizeServerURL(const utility::string_t& url);
    std::shared_ptr<AnisetteData> Parse(const web::json::value& headers,
        const web::json::value& clientInfo, int64_t now);
    std::shared_ptr<AnisetteData> Fetch(const utility::string_t& url);
}
