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

enum class ServerErrorCode
{
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
};

class ServerError: public Error
{
public:
    ServerError(ServerErrorCode code) : Error((int)code)
    {
    }
    
    virtual std::string domain() const
    {
        return "com.rileytestut.AltServer";
    }
    
    virtual std::string localizedDescription() const
    {
		switch ((ServerErrorCode)this->code())
		{
		case ServerErrorCode::Unknown:
			return "An unknown error occured.";

		case ServerErrorCode::ConnectionFailed:
			return "Could not connect to AltServer.";

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
			return "You have reached the limit of 3 apps per device.";

		case ServerErrorCode::UnsupportediOSVersion:
			return "Your device must be running iOS 12.2 or later to install AltStore.";

		case ServerErrorCode::UnknownRequest:
			return "AltServer does not support this request.";

		case ServerErrorCode::UnknownResponse:
			return "Received an unknown response from AltServer.";

		case ServerErrorCode::InvalidAnisetteData:
			return "Invalid anisette data. Please download the latest versions of iTunes and iCloud directly from Apple, and not from the Microsoft Store.";

		case ServerErrorCode::PluginNotFound:
			return "Could not connect to Mail plug-in. Please make sure the plug-in is installed and Mail is running, then try again.";
		}
    }
};

#endif /* ServerError_hpp */
