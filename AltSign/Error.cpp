#include "Error.hpp"

std::string NSLocalizedDescriptionKey = "NSLocalizedDescription";
std::string NSLocalizedFailureErrorKey = "NSLocalizedFailure";
std::string NSLocalizedFailureReasonErrorKey = "NSLocalizedFailureReason";
std::string NSLocalizedRecoverySuggestionErrorKey = "NSLocalizedRecoverySuggestion";
std::string NSUnderlyingErrorKey = "NSUnderlyingError";

std::string ALTLocalizedDescriptionKey = "ALTLocalizedDescription";
std::string ALTLocalizedFailureReasonErrorKey = "ALTLocalizedFailureReason";
std::string ALTLocalizedRecoverySuggestionErrorKey = "ALTLocalizedRecoverySuggestion";

extern std::wstring WideStringFromString(std::string string);

std::string AnyStringValue(std::any& any)
{
    try
    {
        try
        {
            std::string string = std::any_cast<std::string>(any);
            return string;
        }
        catch (std::bad_any_cast)
        {
            const char* string = std::any_cast<const char*>(any);
            return string;
        }
    }
    catch (std::bad_any_cast)
    {
        int number = std::any_cast<int>(any);
        return std::to_string(number);
    }
}

web::json::value Error::serialized() const
{
    web::json::value serializedError = web::json::value();
    serializedError[L"errorDomain"] = web::json::value::string(WideStringFromString(this->domain()));
    serializedError[L"errorCode"] = web::json::value::number(this->code());

    auto rawUserInfo = this->userInfo();

    web::json::value userInfo;
    for (auto pair : rawUserInfo)
    {
        try
        {
            std::string string = AnyStringValue(pair.second);
            userInfo[WideStringFromString(pair.first)] = web::json::value::string(WideStringFromString(string));
            continue;
        }
        catch (std::bad_any_cast) {}

        try
        {
            int integer = std::any_cast<int>(pair.second);
            userInfo[WideStringFromString(pair.first)] = web::json::value::number(integer);
            continue;
        }
        catch (std::bad_any_cast) {}

        try
        {
            Error &error = std::any_cast<Error&>(pair.second);
            userInfo[WideStringFromString(pair.first)] = error.serialized();
            continue;
        }
        catch (std::bad_any_cast) {}

        // TODO: Support std::vector<std::any> and std::map<std::string, std::any>
        // We can get away with this for now because we don't store either in error user info (yet).
    }

    userInfo[WideStringFromString(ALTLocalizedDescriptionKey)] = web::json::value::string(WideStringFromString(this->localizedDescription()));

    auto localizedFailureReason = this->localizedFailureReason();
    if (localizedFailureReason.has_value())
    {
        userInfo[WideStringFromString(ALTLocalizedFailureReasonErrorKey)] = web::json::value::string(WideStringFromString(*localizedFailureReason));
    }

    auto localizedRecoverySuggestion = this->localizedRecoverySuggestion();
    if (localizedRecoverySuggestion.has_value())
    {
        userInfo[WideStringFromString(ALTLocalizedRecoverySuggestionErrorKey)] = web::json::value::string(WideStringFromString(*localizedRecoverySuggestion));
    }

    // TODO: Support NSDebugDescriptionKey.
    // We don't have any errors with debug descriptions for now, so no need to implement yet.

    web::json::value legacyUserInfo;
    for (auto pair : rawUserInfo)
    {
        try
        {
            auto value = AnyStringValue(pair.second);
            legacyUserInfo[WideStringFromString(pair.first)] = web::json::value::string(WideStringFromString(value));
        }
        catch (std::bad_any_cast)
        {
            // legacyUserInfo only supports string values, so ignore all non-string values.
        }
    }

    serializedError[L"errorUserInfo"] = userInfo;
    serializedError[L"userInfo"] = legacyUserInfo;

    return serializedError;
}
