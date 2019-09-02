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
    Unknown,
    ConnectionFailed,
    LostConnection,
    
    DeviceNotFound,
    DeviceWriteFailed,
    
    InvalidRequest,
    InvalidResponse,
    
    InvalidApp,
    InstallationFailed,
    MaximumFreeAppLimitReached
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
		}
    }
};

#endif /* ServerError_hpp */
