//
//  InstallError.h
//  AltServer-Windows
//
//  Created by Riley Testut on 8/31/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef InstallError_h
#define InstallError_h

#include "Error.hpp"

enum class InstallErrorCode
{
    InvalidCredentials,
    NoTeam,
    MissingPrivateKey,
    MissingCertificate,
    MissingInfoPlist,
};

class InstallError: public Error
{
public:
    InstallError(InstallErrorCode code) : Error((int)code)
    {
    }
    
    virtual std::string domain() const
    {
        return "com.rileytestut.AltServer.InstallError";
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


#endif /* InstallError_h */
