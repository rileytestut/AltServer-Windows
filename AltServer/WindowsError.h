#pragma once

#include "Error.hpp"

enum class WindowsErrorCode
{
    WindowsDefenderBlockedCommunication = 1
};

class WindowsError : public Error
{
public:
    WindowsError(WindowsErrorCode code, std::map<std::string, std::any> userInfo = {}) : Error((int)code, userInfo)
    {
    }

    virtual std::string domain() const { return "AltServer.WindowsError"; }

    virtual std::optional<std::string> localizedFailureReason() const
    {
        switch ((WindowsErrorCode)this->code())
        {
        case WindowsErrorCode::WindowsDefenderBlockedCommunication:
            return "Windows Defender blocked AltStore from communicating with AltServer.";
        }

        return std::nullopt;
    }

    virtual std::optional<std::string> localizedRecoverySuggestion() const
    {
        switch ((WindowsErrorCode)this->code())
        {
        case WindowsErrorCode::WindowsDefenderBlockedCommunication:
            return "Disable Windows real-time protection on your computer then try again.";
        }

        return std::nullopt;
    }
};

