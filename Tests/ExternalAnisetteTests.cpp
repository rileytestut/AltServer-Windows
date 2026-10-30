#include "ExternalAnisetteData.h"
#include <iostream>
#include <stdexcept>
#include <functional>

std::string StringFromWideString(std::wstring value) { return utility::conversions::to_utf8string(value); }
std::wstring WideStringFromString(std::string value) { return utility::conversions::to_string_t(value); }

namespace
{
    using web::json::value;
    constexpr int64_t Now = 1700000000;
    int tests = 0;
    void Check(bool condition) { if (!condition) throw std::runtime_error("Test assertion failed"); }
    void Test(const char* name, std::function<void()> body)
    {
        body(); ++tests; std::cout << "PASS " << name << '\n';
    }
    void Reject(std::function<void()> body)
    {
        bool rejected = false;
        try { body(); } catch (...) { rejected = true; }
        Check(rejected);
    }
    value Headers()
    {
        return value::parse(U(R"({
            "X-Apple-I-MD-M":"fixture-machine", "X-Apple-I-MD":"old-otp",
            "X-Apple-I-MD-LU":"fixture-local-user", "X-Apple-I-MD-RINFO":"17106176",
            "X-Mme-Device-Id":"fixture-device", "X-Apple-I-SRL-NO":"0",
            "X-MMe-Client-Info":"fixture-client", "X-Apple-I-Client-Time":"2023-11-14T22:13:20Z",
            "X-Apple-Locale":"en_US", "X-Apple-I-TimeZone":"UTC"
        })"));
    }
    value Metadata() { return value::parse(U(R"({"client_info":"fixture-client","user_agent":"fixture-agent"})")); }
    std::shared_ptr<AnisetteData> Parse(value h = Headers(), value m = Metadata())
    {
        return ExternalAnisette::Parse(h, m, Now);
    }
}

int main(int argc, char** argv)
{
    try
    {
        if (argc >= 3)
        {
            auto request = [&] {
                auto data = ExternalAnisette::Fetch(utility::conversions::to_string_t(argv[2]));
                if (std::string(argv[1]) != "--probe-http")
                    Check(data->userAgent() == std::make_optional(std::string("fixture-agent")));
                data->refresh();
            };
            if (std::string(argv[1]) == "--reject-http") Reject(request);
            else request();
            std::cout << "Provider HTTP checks passed\n";
            return 0;
        }
        Test("parse matching header and client-info responses", [] {
            auto data = Parse();
            Check(data->date().tv_sec == Now && data->oneTimePassword() == "old-otp");
            Check(data->userAgent() == std::make_optional(std::string("fixture-agent")));
        });
        Test("routing info preserves all 64 bits", [] {
            auto h = Headers(); h[U("X-Apple-I-MD-RINFO")] = value::string(U("18446744073709551615"));
            Check(Parse(h)->routingInfo() == 18446744073709551615ULL);
        });
        Test("reject invalid routing info", [] {
            for (auto invalid : {U("-1"), U("+1"), U("1x"), U("18446744073709551616")}) {
                auto h = Headers(); h[U("X-Apple-I-MD-RINFO")] = value::string(invalid); Reject([&] { Parse(h); });
            }
        });
        Test("reject missing or empty OTP", [] {
            auto h = Headers(); h.erase(U("X-Apple-I-MD")); Reject([&] { Parse(h); });
            h[U("X-Apple-I-MD")] = value::string(U("")); Reject([&] { Parse(h); });
        });
        Test("reject invalid field types", [] {
            auto h = Headers(); h[U("X-Apple-I-MD")] = value::number(12); Reject([&] { Parse(h); });
        });
        Test("reject header injection", [] {
            auto m = Metadata(); m[U("user_agent")] = value::string(U("agent\r\nInjected: value")); Reject([&] { Parse(Headers(), m); });
        });
        Test("reject metadata identity mismatch", [] {
            auto m = Metadata(); m[U("client_info")] = value::string(U("another-client")); Reject([&] { Parse(Headers(), m); });
        });
        Test("reject stale timestamp", [] { Reject([] { ExternalAnisette::Parse(Headers(), Metadata(), Now + 121); }); });
        Test("reject timestamp too far in future", [] { Reject([] { ExternalAnisette::Parse(Headers(), Metadata(), Now - 31); }); });
        Test("reject malformed timestamp", [] {
            auto h = Headers(); h[U("X-Apple-I-Client-Time")] = value::string(U("invalid")); Reject([&] { Parse(h); });
        });
        Test("normalize HTTPS base URL", [] {
            Check(ExternalAnisette::NormalizeServerURL(U("https://example.test/provider")) == U("https://example.test/provider/"));
        });
        Test("allow loopback HTTP", [] {
            ExternalAnisette::NormalizeServerURL(U("http://127.0.0.1:9000"));
            ExternalAnisette::NormalizeServerURL(U("http://localhost:9000"));
            ExternalAnisette::NormalizeServerURL(U("http://[::1]:9000"));
        });
        Test("reject insecure remote and non-HTTP URLs", [] {
            for (auto url : {U("http://example.test"), U("file:///tmp/data"), U("https:///missing-host")})
                Reject([&] { ExternalAnisette::NormalizeServerURL(url); });
        });
        Test("reject URL credentials, query and fragment", [] {
            for (auto url : {U("https://user:password@example.test"), U("https://example.test/?secret=value"), U("https://example.test/#fragment")})
                Reject([&] { ExternalAnisette::NormalizeServerURL(url); });
        });
        Test("refresh updates OTP on the same identity", [] {
            auto data = Parse();
            data->setRefreshHandler([] { auto h = Headers(); h[U("X-Apple-I-MD")] = value::string(U("fresh-otp")); return Parse(h); });
            data->refresh(); Check(data->oneTimePassword() == "fresh-otp");
        });
        Test("identity rotation fails before changing OTP", [] {
            for (auto key : {U("X-Apple-I-MD-M"), U("X-Apple-I-MD-LU"), U("X-Mme-Device-Id"), U("X-Apple-I-SRL-NO")}) {
                auto data = Parse();
                data->setRefreshHandler([key] { auto h = Headers(); h[key] = value::string(U("rotated")); h[U("X-Apple-I-MD")] = value::string(U("fresh-otp")); return Parse(h); });
                Reject([&] { data->refresh(); }); Check(data->oneTimePassword() == "old-otp");
            }
        });
        Test("user-agent rotation fails closed", [] {
            auto data = Parse(); data->setRefreshHandler([] { auto m = Metadata(); m[U("user_agent")] = value::string(U("another-agent")); return Parse(Headers(), m); });
            Reject([&] { data->refresh(); });
        });
        Test("local data keeps legacy refresh behavior", [] {
            AnisetteData local; local.refresh(); Check(!local.userAgent().has_value());
        });
        std::cout << tests << " tests passed\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "Test failed: " << error.what() << '\n'; return 1; }
}
