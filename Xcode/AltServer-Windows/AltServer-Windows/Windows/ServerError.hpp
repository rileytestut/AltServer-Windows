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
        return "";
        //        switch ((SignErrorCode)this->code())
        //        {
        //            case SignErrorCode::Unknown:
        //                return "An unknown error occured.";
        //
        //            case SignErrorCode::InvalidApp:
        //                return "The app is invalid.";
        //
        //            case SignErrorCode::MissingAppBundle:
        //                return "The provided .ipa does not contain an app bundle.";
        //
        //            case SignErrorCode::MissingInfoPlist:
        //                return "The provided app is missing its Info.plist.";
        //
        //            case SignErrorCode::MissingProvisioningProfile:
        //                return "Could not find matching provisioning profile.";
        //        }
    }
};

#endif /* ServerError_hpp */
