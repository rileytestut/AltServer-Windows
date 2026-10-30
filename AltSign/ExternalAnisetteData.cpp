#include "ExternalAnisetteData.h"
#include "Error.hpp"
#include <cpprest/http_client.h>
#include <algorithm>
#include <chrono>
#include <cctype>
#include <cwctype>
#include <limits>
#include <stdexcept>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

namespace
{
    constexpr size_t MaximumResponseSize = 16 * 1024;

    std::string RequiredString(const web::json::value& object, const utility::string_t& key)
    {
        if (!object.is_object() || !object.has_string_field(key))
            throw std::invalid_argument("Missing external anisette field: " + utility::conversions::to_utf8string(key));
        auto value = utility::conversions::to_utf8string(object.at(key).as_string());
        if (value.empty() || value.size() > 2048 ||
            std::any_of(value.begin(), value.end(), [](unsigned char c) { return c < 32 || c == 127; }))
            throw std::invalid_argument("Invalid external anisette field: " + utility::conversions::to_utf8string(key));
        return value;
    }

    web::json::value RequestJSON(const utility::string_t& url)
    {
        web::http::client::http_client_config config;
        config.set_timeout(std::chrono::seconds(15));
        config.set_nativehandle_options([](web::http::client::native_handle handle) {
            DWORD disabled = WINHTTP_DISABLE_REDIRECTS;
            if (!WinHttpSetOption(handle, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled)))
                throw std::runtime_error("Could not disable external-provider redirects");
        });
        web::http::client::http_client client(url, config);
        web::http::http_request request(web::http::methods::GET);
        request.headers().add(U("Accept"), U("application/json"));
        auto response = client.request(request).get();
        if (response.status_code() != web::http::status_codes::OK)
            throw LocalizedAPIError((int)APIErrorCode::InvalidAnisetteData,
                "External anisette provider returned HTTP " + std::to_string(response.status_code()) + ".");
        if (response.headers().has(web::http::header_names::content_length) &&
            response.headers().content_length() > MaximumResponseSize)
            throw std::invalid_argument("External anisette response is too large");
        std::vector<uint8_t> bytes;
        auto stream = response.body().streambuf();
        uint8_t chunk[1024];
        for (;;)
        {
            auto count = stream.getn(chunk, sizeof(chunk)).get();
            if (count == 0) break;
            if (count > MaximumResponseSize - bytes.size())
                throw std::invalid_argument("External anisette response is too large");
            bytes.insert(bytes.end(), chunk, chunk + count);
        }
        try
        {
            auto json = web::json::value::parse(utility::conversions::to_string_t(std::string(bytes.begin(), bytes.end())));
            if (!json.is_object()) throw std::invalid_argument("Not an object");
            return json;
        }
        catch (...)
        {
            // Never echo malformed bodies: they can contain authentication data.
            throw std::invalid_argument("External anisette response must be a JSON object");
        }
    }
}

utility::string_t ExternalAnisette::NormalizeServerURL(const utility::string_t& url)
{
    web::uri parsed;
    try { parsed = web::uri(url); }
    catch (...) { throw std::invalid_argument("Invalid external anisette server URL"); }
    auto host = parsed.host();
    std::transform(host.begin(), host.end(), host.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    bool loopback = host == U("localhost") || host == U("127.0.0.1") || host == U("[::1]") || host == U("::1");
    if (host.empty() || (parsed.scheme() != U("https") && !(parsed.scheme() == U("http") && loopback)) ||
        !parsed.user_info().empty() || !parsed.query().empty() || !parsed.fragment().empty())
        throw std::invalid_argument("External anisette URL requires HTTPS (or HTTP on loopback), with no credentials, query, or fragment");
    auto normalized = parsed.to_string();
    if (normalized.back() != U('/')) normalized += U('/');
    return normalized;
}

std::shared_ptr<AnisetteData> ExternalAnisette::Parse(const web::json::value& headers,
    const web::json::value& clientInfo, int64_t now)
{
    auto description = RequiredString(headers, U("X-MMe-Client-Info"));
    auto providerDescription = RequiredString(clientInfo, U("client_info"));
    auto userAgent = RequiredString(clientInfo, U("user_agent"));
    if (description != providerDescription)
        throw std::invalid_argument("Anisette headers and provider metadata have different client identities");
    auto routingString = RequiredString(headers, U("X-Apple-I-MD-RINFO"));
    if (!std::all_of(routingString.begin(), routingString.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        throw std::invalid_argument("Anisette routing info must be an unsigned decimal integer");
    unsigned long long routing;
    try { routing = std::stoull(routingString); }
    catch (...) { throw std::invalid_argument("Anisette routing info is outside the supported range"); }
    auto timeString = RequiredString(headers, U("X-Apple-I-Client-Time"));
    int64_t seconds;
    try
    {
        auto date = utility::datetime::from_string(utility::conversions::to_string_t(timeString), utility::datetime::ISO_8601);
        seconds = static_cast<int64_t>(date.to_interval() / 10000000) - 11644473600LL;
    }
    catch (...) { throw std::invalid_argument("Invalid external anisette timestamp"); }
    if (seconds < now - 120 || seconds > now + 30 || seconds < 0 || seconds > (std::numeric_limits<long>::max)())
        throw std::invalid_argument("External anisette timestamp is stale or outside the supported range");
    timeval date = { static_cast<long>(seconds), 0 };
    auto data = std::make_shared<AnisetteData>(
        RequiredString(headers, U("X-Apple-I-MD-M")), RequiredString(headers, U("X-Apple-I-MD")),
        RequiredString(headers, U("X-Apple-I-MD-LU")), routing,
        RequiredString(headers, U("X-Mme-Device-Id")), RequiredString(headers, U("X-Apple-I-SRL-NO")),
        description, date, RequiredString(headers, U("X-Apple-Locale")), RequiredString(headers, U("X-Apple-I-TimeZone")));
    data->setUserAgent(userAgent);
    return data;
}

std::shared_ptr<AnisetteData> ExternalAnisette::Fetch(const utility::string_t& url)
{
    try
    {
        auto base = NormalizeServerURL(url);
        auto metadata = RequestJSON(base + U("v3/client_info"));
        auto headers = RequestJSON(base);
        auto data = Parse(headers, metadata, static_cast<int64_t>(std::time(nullptr)));
        data->setRefreshHandler([base] { return Fetch(base); });
        return data;
    }
    catch (const LocalizedAPIError&) { throw; }
    catch (const std::invalid_argument& error)
    {
        throw LocalizedAPIError((int)APIErrorCode::InvalidAnisetteData, error.what());
    }
    catch (...)
    {
        throw LocalizedAPIError((int)APIErrorCode::InvalidAnisetteData,
            "Could not fetch external anisette data. Check the configured provider and try again.");
    }
}
