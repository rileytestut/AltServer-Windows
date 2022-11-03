#include "Error.hpp"

std::string NSLocalizedDescriptionKey = "NSLocalizedDescription";
std::string NSLocalizedFailureErrorKey = "NSLocalizedFailure";
std::string NSLocalizedFailureReasonErrorKey = "NSLocalizedFailureReason";
std::string NSLocalizedRecoverySuggestionErrorKey = "NSLocalizedRecoverySuggestion";
std::string NSUnderlyingErrorKey = "NSUnderlyingError";
std::string NSDebugDescriptionErrorKey = "NSDebugDescription";

std::string ALTLocalizedDescriptionKey = "ALTLocalizedDescription";
std::string ALTLocalizedFailureReasonErrorKey = "ALTLocalizedFailureReason";
std::string ALTLocalizedRecoverySuggestionErrorKey = "ALTLocalizedRecoverySuggestion";

extern std::wstring WideStringFromString(std::string string);

std::string AnyStringValue(std::any& any)
{
    try
    {
        std::string string = std::any_cast<std::string>(any);
        return string;
    }
    catch (std::bad_any_cast) {}

    try
    {
        std::optional<std::string> string = std::any_cast<std::optional<std::string>>(any);
        if (string.has_value())
        {
            return *string;
        }
        else
        {
            return "";
        }
    }
    catch (std::bad_any_cast) {}

    try
    {
        char* string = std::any_cast<char*>(any);
        return string;
    }
    catch (std::bad_any_cast) {}

    try
    {
        const char* string = std::any_cast<const char*>(any);
        return string;
    }
    catch (std::bad_any_cast) {}

    try
    {
        int number = std::any_cast<int>(any);
        return std::to_string(number);
    }
    catch (std::bad_any_cast) {}

    std::shared_ptr<Error> error = std::any_cast<std::shared_ptr<Error>>(any);

    std::ostringstream oss;
    oss << *error;
    return oss.str();
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
            auto error = std::any_cast<std::shared_ptr<Error>>(pair.second);
            userInfo[WideStringFromString(pair.first)] = error->serialized();
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

std::string Error::formattedDetailedDescription() const
{
    std::vector<std::string> preferredKeyOrder = {
        NSDebugDescriptionErrorKey,
        NSLocalizedDescriptionKey,
        NSLocalizedFailureErrorKey,
        NSLocalizedFailureReasonErrorKey,
        NSLocalizedRecoverySuggestionErrorKey,
        NSUnderlyingErrorKey,
    };

    auto userInfo = this->userInfo();
    userInfo[NSLocalizedDescriptionKey] = this->localizedDescription();

    auto localizedFailure = this->localizedFailure();
    if (localizedFailure.has_value())
    {
        userInfo[NSLocalizedFailureErrorKey] = *localizedFailure;
    }

    auto localizedFailureReason = this->localizedFailureReason();
    if (localizedFailureReason.has_value())
    {
        userInfo[NSLocalizedFailureReasonErrorKey] = *localizedFailureReason;
    }

    auto localizedRecoverySuggestion = this->localizedRecoverySuggestion();
    if (localizedRecoverySuggestion.has_value())
    {
        userInfo[NSLocalizedRecoverySuggestionErrorKey] = *localizedRecoverySuggestion;
    }

    std::vector<std::pair<std::string, std::any>> sortedUserInfo;
    for (auto& pair : userInfo)
    {
        sortedUserInfo.push_back(pair);
    }

    sort(sortedUserInfo.begin(), sortedUserInfo.end(), [preferredKeyOrder](auto& a, auto& b) {
        auto indexA = find(preferredKeyOrder.begin(), preferredKeyOrder.end(), a.first);
        auto indexB = find(preferredKeyOrder.begin(), preferredKeyOrder.end(), b.first);

        if (indexA != preferredKeyOrder.end() && indexB != preferredKeyOrder.end())
        {
            return indexA < indexB;
        }
        else if (indexA != preferredKeyOrder.end() && indexB == preferredKeyOrder.end())
        {
            // indexA exists, indexB does not, so A should come first.
            return true;
        }
        else if (indexA == preferredKeyOrder.end() && indexB != preferredKeyOrder.end())
        {
            // indexA does not exist, indexB does, so B should come first.
            return false;
        }
        else
        {
            // both indexes are nil, so sort alphabetically.
            return a.first < b.first;
        }
    });

    std::string detailedDescription;

    for (auto& pair : sortedUserInfo)
    {
        std::string value;
        try
        {
            value = AnyStringValue(pair.second);
        }
        catch (std::bad_any_cast)
        {
            continue;
        }

        std::string keyName;
        if (pair.first == NSDebugDescriptionErrorKey)
        {
            keyName = "Debug Description";
        }
        else if (pair.first == NSLocalizedDescriptionKey)
        {
            keyName = "Error Description";
        }
        else if (pair.first == NSLocalizedFailureErrorKey)
        {
            keyName = "Failure";
        }
        else if (pair.first == NSLocalizedFailureReasonErrorKey)
        {
            keyName = "Failure Reason";
        }
        else if (pair.first == NSLocalizedRecoverySuggestionErrorKey)
        {
            keyName = "Recovery Suggestion";
        }
        else if (pair.first == NSUnderlyingErrorKey)
        {
            keyName = "Underlying Error";
        }
        else
        {
            keyName = pair.first;
        }

        std::string string = keyName + "\n" + value;

        if (detailedDescription.length() > 0)
        {
            detailedDescription += "\n\n";
        }

        detailedDescription += string;
    }

    return detailedDescription;
}
