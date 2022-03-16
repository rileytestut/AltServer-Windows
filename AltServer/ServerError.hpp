//
//  ServerError.hpp
//  AltServer-Windows
//
//  Created by Riley Testut on 8/13/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef ServerError_hpp
#define ServerError_hpp

#include "Error.hpp"

#include <sstream>

extern std::string LocalizedFailureErrorKey;
extern std::string UnderlyingErrorDomainErrorKey;
extern std::string UnderlyingErrorCodeErrorKey;
extern std::string ProvisioningProfileBundleIDErrorKey;
extern std::string AppNameErrorKey;
extern std::string DeviceNameErrorKey;
extern std::string OperatingSystemNameErrorKey;
extern std::string OperatingSystemVersionErrorKey;

enum class ServerErrorCode
{
	UnderlyingError = -1,

    Unknown = 0,
    ConnectionFailed = 1,
    LostConnection = 2,
    
    DeviceNotFound = 3,
    DeviceWriteFailed = 4,
    
    InvalidRequest = 5,
    InvalidResponse = 6,
    
    InvalidApp = 7,
    InstallationFailed = 8,
    MaximumFreeAppLimitReached = 9,
	UnsupportediOSVersion = 10,

	UnknownRequest = 11,
	UnknownResponse = 12,

	InvalidAnisetteData = 13,
	PluginNotFound = 14,

	ProfileNotFound = 15,

	AppDeletionFailed = 16,

	RequestedAppNotRunning = 100,
	IncompatibleDeveloperDisk = 101,
};

class ServerError: public Error
{
public:
	ServerError(ServerErrorCode code, std::map<std::string, std::string> userInfo = {}) : Error((int)code, userInfo)
	{
	}
    
    virtual std::string domain() const
    {
        return "com.rileytestut.AltServer";
    }
    
    virtual std::optional<std::string> localizedFailureReason() const
    {
		switch ((ServerErrorCode)this->code())
		{
		case ServerErrorCode::UnderlyingError:
            if (this->userInfo().count(UnderlyingErrorCodeErrorKey) > 0)
            {
                auto errorCode = this->userInfo()[UnderlyingErrorCodeErrorKey];

                auto failureReason = "Error code: " + errorCode + ".";
                return failureReason;
            }
            else
            {
                return "An unknown error occured.";
            }

		case ServerErrorCode::Unknown:
			return "An unknown error occured.";

		case ServerErrorCode::ConnectionFailed:
			return "There was an error connecting to the device.";

		case ServerErrorCode::LostConnection:
			return "Lost connection to AltServer.";

		case ServerErrorCode::DeviceNotFound:
			return "AltServer could not find the device.";

		case ServerErrorCode::DeviceWriteFailed:
			return "Failed to write app data to device.";

		case ServerErrorCode::InvalidRequest:
			return "AltServer received an invalid request.";

		case ServerErrorCode::InvalidResponse:
			return "AltServer sent an invalid response.";

		case ServerErrorCode::InvalidApp:
			return "The app is invalid.";

		case ServerErrorCode::InstallationFailed:
			return "An error occured while installing the app.";

		case ServerErrorCode::MaximumFreeAppLimitReached:
            return "Non-developer Apple IDs are limited to 3 active sideloaded apps at a time.";

		case ServerErrorCode::UnsupportediOSVersion:
			return "Your device must be running iOS 12.2 or later to install AltStore.";

		case ServerErrorCode::UnknownRequest:
			return "AltServer does not support this request.";

		case ServerErrorCode::UnknownResponse:
			return "Received an unknown response from AltServer.";

		case ServerErrorCode::InvalidAnisetteData:
			return "The provided anisette data is invalid.";

		case ServerErrorCode::PluginNotFound:
			return "Could not connect to Mail plug-in. Please make sure the plug-in is installed and Mail is running, then try again.";

		case ServerErrorCode::ProfileNotFound:
			return "Could not find provisioning profile.";

		case ServerErrorCode::AppDeletionFailed:
			return "An error occured while removing the app.";

		case ServerErrorCode::RequestedAppNotRunning:
		{
			std::string appName = this->userInfo().count(AppNameErrorKey) > 0 ? this->userInfo()[AppNameErrorKey] : "The requested app";
			std::string deviceName = this->userInfo().count(DeviceNameErrorKey) > 0 ? this->userInfo()[DeviceNameErrorKey] : "the device";

			std::ostringstream oss;
			oss << appName << " is not currently running on " << deviceName << ".";

			return oss.str();
		}

		case ServerErrorCode::IncompatibleDeveloperDisk:
		{
			auto osVersion = this->osVersion();

			std::string failureReason = "The disk is incompatible with " + (osVersion.has_value() ? *osVersion : "this device's OS version") + ".";
			return failureReason;
		}
		}
    }

    virtual std::optional<std::string> localizedRecoverySuggestion() const
    {
        switch ((ServerErrorCode)this->code())
        {
        case ServerErrorCode::ConnectionFailed:
        case ServerErrorCode::DeviceNotFound:
            return "Make sure you have trusted this device with your computer and WiFi sync is enabled.";

        case ServerErrorCode::MaximumFreeAppLimitReached:
            return "Please deactivate a sideloaded app with AltStore in order to install another app. If you're running iOS 13.5 or later, make sure 'Offload Unused Apps' is disabled in Settings > iTunes & App Stores, then install or delete all offloaded apps to prevent them from erroneously counting towards this limit.";

        case ServerErrorCode::InvalidAnisetteData:
            return "Please download the latest versions of iTunes and iCloud directly from Apple, and not from the Microsoft Store.";

        case ServerErrorCode::RequestedAppNotRunning:
        {
            std::string deviceName = this->userInfo().count(DeviceNameErrorKey) > 0 ? this->userInfo()[DeviceNameErrorKey] : "your device";

            std::string localizedRecoverySuggestion = "Make sure the app is running in the foreground on " + deviceName + " then try again.";
            return localizedRecoverySuggestion;
        }

        default: return Error::localizedRecoverySuggestion();
        }
    }

private:
	std::optional<std::string> osVersion() const;
};

#endif /* ServerError_hpp */
