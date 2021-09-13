//
//  Error.hpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/8/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef Error_hpp
#define Error_hpp

#include <iostream>
#include <exception>
#include <map>
#include <optional>
#include <string>

extern std::string NSLocalizedDescriptionKey;
extern std::string NSLocalizedFailureErrorKey;
extern std::string NSLocalizedFailureReasonErrorKey;
extern std::string NSLocalizedRecoverySuggestionErrorKey;

enum class SignErrorCode
{
    Unknown,
    InvalidApp,
    MissingAppBundle,
    MissingInfoPlist,
    MissingProvisioningProfile,
    MissingAppleRootCertificate,
    InvalidCertificate,
    InvalidProvisioningProfile,
};

enum class APIErrorCode
{
    Unknown,
    InvalidParameters,
    
    IncorrectCredentials,
    AppSpecificPasswordRequired,
    
    NoTeams,
    InvalidDeviceID,
    DeviceAlreadyRegistered,
    
    InvalidCertificateRequest,
    CertificateDoesNotExist,
    
    InvalidAppIDName,
    InvalidBundleIdentifier,
    BundleIdentifierUnavailable,
    AppIDDoesNotExist,
    
    InvalidAppGroup,
    AppGroupDoesNotExist,
    
    InvalidProvisioningProfileIdentifier,
    ProvisioningProfileDoesNotExist,
    
    InvalidResponse,

	RequiresTwoFactorAuthentication,
	IncorrectVerificationCode,
	AuthenticationHandshakeFailed,

	InvalidAnisetteData,
};

enum class ArchiveErrorCode
{
    Unknown,
    UnknownWrite,
    NoSuchFile,
    CorruptFile,
};

class Error: public std::exception
{
public:
    Error(int code) : _code(code), _userInfo(std::map<std::string, std::string>())
    {
    }

	Error(int code, std::map<std::string, std::string> userInfo) : _code(code), _userInfo(userInfo)
	{
	}
    
    virtual std::string localizedDescription() const
    {
        std::string localizedDescription;

        if (this->_userInfo.count(NSLocalizedDescriptionKey) > 0)
        {
            localizedDescription = this->_userInfo.at(NSLocalizedDescriptionKey);
        }
        else if (this->localizedFailure().has_value())
        {
            auto localizedFailure = *this->localizedFailure();

            if (this->localizedFailureReason().has_value())
            {
                auto localizedFailureReason = *this->localizedFailureReason();
                localizedDescription = localizedFailure + " " + localizedFailureReason;
            }
            else
            {
                localizedDescription = localizedFailure;
            }            
        }
        else if (this->localizedFailureReason().has_value())
        {
            localizedDescription = *this->localizedFailureReason();
        }
        else
        {
            localizedDescription = this->domain() + " error " + std::to_string(this->code()) + ".";
        }

        return localizedDescription;
    }
    
    int code() const
    {
        return _code;
    }

	std::map<std::string, std::string> userInfo() const
	{
        auto userInfo = this->_userInfo;

        if (_localizedFailure.has_value())
        {
            userInfo[NSLocalizedFailureErrorKey] = *_localizedFailure;
        }

        if (_localizedFailureReason.has_value())
        {
            userInfo[NSLocalizedFailureReasonErrorKey] = *_localizedFailureReason;
        }

        if (_localizedRecoverySuggestion.has_value())
        {
            userInfo[NSLocalizedRecoverySuggestionErrorKey] = *_localizedRecoverySuggestion;
        }

		return userInfo;
	}
    
    virtual std::string domain() const
    {
        return "com.rileytestut.AltServer.Error";
    }

    virtual std::optional<std::string> localizedFailure() const
    {
        if (this->_localizedFailure.has_value())
        {
            return this->_localizedFailure;
        }

        if (this->_userInfo.count(NSLocalizedFailureErrorKey) > 0)
        {
            auto localizedFailure = this->_userInfo.at(NSLocalizedFailureErrorKey);
            return localizedFailure;
        }

        return std::nullopt;
    }

    virtual void setLocalizedFailure(std::optional<std::string> localizedFailure)
    {
        this->_localizedFailure = localizedFailure;
    }

    virtual std::optional<std::string> localizedFailureReason() const
    {
        if (this->_localizedFailureReason.has_value())
        {
            return this->_localizedFailureReason;
        }

        if (this->_userInfo.count(NSLocalizedFailureReasonErrorKey) > 0)
        {
            auto localizedFailureReason = this->_userInfo.at(NSLocalizedFailureReasonErrorKey);
            return localizedFailureReason;
        }

        return std::nullopt;
    }

    virtual void setLocalizedFailureReason(std::optional<std::string> localizedFailureReason)
    {
        this->_localizedFailureReason = localizedFailureReason;
    }

    virtual std::optional<std::string> localizedRecoverySuggestion() const
    {
        if (this->_localizedRecoverySuggestion.has_value())
        {
            return this->_localizedRecoverySuggestion;
        }

        if (this->_userInfo.count(NSLocalizedRecoverySuggestionErrorKey) > 0)
        {
            auto localizedRecoverySuggestion = this->_userInfo.at(NSLocalizedRecoverySuggestionErrorKey);
            return localizedRecoverySuggestion;
        }

        return std::nullopt;
    }

    virtual void setLocalizedRecoverySuggestion(std::optional<std::string> localizedRecoverySuggestion)
    {
        this->_localizedRecoverySuggestion = localizedRecoverySuggestion;
    }
    
    friend std::ostream& operator<<(std::ostream& os, const Error& error)
    {
        os << "Error: (" << error.domain() << "): " << error.localizedDescription() << " (" << error.code() << ")";
        return os;
    }
    
protected:
    int _code;
	std::map<std::string, std::string> _userInfo;

    std::optional<std::string> _localizedFailure;
    std::optional<std::string> _localizedFailureReason;
    std::optional<std::string> _localizedRecoverySuggestion;
};

class LocalizedError: public Error
{
public:
    LocalizedError(int code, std::string localizedDescription) : Error(code), _localizedDescription(localizedDescription)
    {
    }
    
    virtual std::string localizedDescription() const
    {
        return _localizedDescription;
    }

    virtual std::string domain() const
    {
        return "com.rileytestut.AltServer.Localized";
    }
    
private:
    std::string _localizedDescription;
};

class APIError : public Error
{
public:
    APIError(APIErrorCode code) : Error((int)code)
    {
    }
    
    virtual std::string domain() const
    {
        return "com.rileytestut.ALTAppleAPI";
    }
    
    virtual std::string localizedDescription() const
    {
        switch ((APIErrorCode)this->code())
        {
            case APIErrorCode::Unknown:
                return "An unknown error occured.";
                
            case APIErrorCode::InvalidParameters:
                return "The provided parameters are invalid.";
                
            case APIErrorCode::IncorrectCredentials:
                return "Incorrect Apple ID or password.";
                
            case APIErrorCode::NoTeams:
                return "You are not a member of any development teams.";
                
            case APIErrorCode::AppSpecificPasswordRequired:
                return "An app-specific password is required. You can create one at appleid.apple.com.";
                
            case APIErrorCode::InvalidDeviceID:
                return "This device's UDID is invalid.";
                
            case APIErrorCode::DeviceAlreadyRegistered:
                return "This device is already registered with this team.";
                
            case APIErrorCode::InvalidCertificateRequest:
                return "The certificate request is invalid.";
                
            case APIErrorCode::CertificateDoesNotExist:
                return "There is no certificate with the requested serial number for this team.";
                
            case APIErrorCode::InvalidAppIDName:
                return "The name for this app is invalid.";
                
            case APIErrorCode::InvalidBundleIdentifier:
                return "The bundle identifier for this app is invalid.";
                
            case APIErrorCode::BundleIdentifierUnavailable:
                return "The bundle identifier for this app has already been registered.";
                
            case APIErrorCode::AppIDDoesNotExist:
                return "There is no App ID with the requested identifier on this team.";
                
            case APIErrorCode::InvalidAppGroup:
                return "The provided app group is invalid.";
                
            case APIErrorCode::AppGroupDoesNotExist:
                return "App group does not exist.";
                
            case APIErrorCode::InvalidProvisioningProfileIdentifier:
                return "The identifier for the requested provisioning profile is invalid.";
                
            case APIErrorCode::ProvisioningProfileDoesNotExist:
                return "There is no provisioning profile with the requested identifier on this team.";
                
            case APIErrorCode::InvalidResponse:
                return "Server returned invalid response.";

			case APIErrorCode::RequiresTwoFactorAuthentication:
				return "This account requires signing in with two-factor authentication.";

			case APIErrorCode::IncorrectVerificationCode:
				return "Incorrect verification code.";

			case APIErrorCode::AuthenticationHandshakeFailed:
				return "Failed to perform authentication handshake with server.";

			case APIErrorCode::InvalidAnisetteData:
				return "Invalid anisette data. Please close both iTunes and iCloud, then try again.";
        }

		return "Unknown error.";
    }
};

class SignError: public Error
{
public:
    SignError(SignErrorCode code) : Error((int)code)
    {
    }
    
    virtual std::string domain() const
    {
        return "com.rileytestut.AltSign";
    }
    
    virtual std::string localizedDescription() const
    {
        switch ((SignErrorCode)this->code())
        {
            case SignErrorCode::Unknown:
                return "An unknown error occured.";
                
            case SignErrorCode::InvalidApp:
                return "The app is invalid.";
                
            case SignErrorCode::MissingAppBundle:
                return "The provided .ipa does not contain an app bundle.";
                
            case SignErrorCode::MissingInfoPlist:
                return "The provided app is missing its Info.plist.";
                
            case SignErrorCode::MissingProvisioningProfile:
                return "Could not find matching provisioning profile.";
                
            case SignErrorCode::MissingAppleRootCertificate:
                return "Could not locate the root signing certificate.";
                
            case SignErrorCode::InvalidCertificate:
                return "The signing certificate is invalid.";
                
            case SignErrorCode::InvalidProvisioningProfile:
                return "The provisioning profile is invalid.";
        }

		return "Unknown error.";
    }
};

class ArchiveError: public Error
{
public:
    ArchiveError(ArchiveErrorCode code) : Error((int)code)
    {
    }
    
    virtual std::string domain() const
    {
        return "com.rileytestut.Archive";
    }
    
    virtual std::string localizedDescription() const
    {
        switch ((ArchiveErrorCode)this->code())
        {
            case ArchiveErrorCode::Unknown:
                return "An unknown error occured.";

            case ArchiveErrorCode::UnknownWrite:
                return "An unknown error occured while writing to disk.";

			case ArchiveErrorCode::NoSuchFile:
                return "The app could not be found.";

            case ArchiveErrorCode::CorruptFile:
                return "The app is corrupted.";
        }

		return "Unknown error.";
    }
};

#endif /* Error_hpp */
