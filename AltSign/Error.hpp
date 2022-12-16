//
//  Error.hpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/8/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef Error_hpp
#define Error_hpp

#include <cpprest/json.h>

#include <iostream>
#include <exception>
#include <map>
#include <optional>
#include <string>
#include <any>

#include <assert.h>

#define __FILENAME__ (strrchr(__FILE__, '\\') ? strrchr(__FILE__, '\\') + 1 : __FILE__)
#define SOURCE_USERINFO { {"Source File", __FILENAME__}, {"Source Line", __LINE__} }

extern std::string NSLocalizedDescriptionKey;
extern std::string NSLocalizedFailureErrorKey;
extern std::string NSLocalizedFailureReasonErrorKey;
extern std::string NSLocalizedRecoverySuggestionErrorKey;
extern std::string NSUnderlyingErrorKey;
extern std::string NSDebugDescriptionErrorKey;

extern std::string ALTLocalizedDescriptionKey;
extern std::string ALTLocalizedFailureReasonErrorKey;
extern std::string ALTLocalizedRecoverySuggestionErrorKey;
extern std::string ALTDebugDescriptionErrorKey;

extern std::string AnyStringValue(std::any& any);

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
    Unknown = 3000,
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

enum class CocoaErrorCode
{
    FileNoSuchFile = 4,

    FileReadUnknown = 256,
    FileReadCorruptFile = 259,

    FileWriteUnknown = 512,

    CoderReadCorrupt = 4864,
    CoderValueNotFound = 4865,
};

class Error: public std::exception
{
public:
    Error(int code, std::map<std::string, std::any> userInfo = {}) : _code(code), _userInfo(userInfo)
    {
        if (_userInfo.count(NSUnderlyingErrorKey) == 0)
        {
            return;
        }
            
        try
        {
            auto value = _userInfo[NSUnderlyingErrorKey];
            std::shared_ptr<Error> underlyingError = std::any_cast<std::shared_ptr<Error>>(value);
        }
        catch (std::bad_any_cast)
        {
            // All underlying errors MUST be std::shared_ptrs in order to retrieve them from user info.
            assert(false);
        }
    }

    virtual std::string domain() const = 0;

    int code() const
    {
        return _code;
    }

    std::map<std::string, std::any> userInfo() const
    {
        return _userInfo;
    }

    std::string localizedDescription() const
    {
        std::string localizedDescription;

        if (this->_userInfo.count(NSLocalizedDescriptionKey) > 0)
        {
            localizedDescription = AnyStringValue(this->userInfo().at(NSLocalizedDescriptionKey));
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
            localizedDescription = this->localizedErrorCode();
        }

        return localizedDescription;
    }

    std::optional<std::string> localizedFailure() const
    {
        if (this->_userInfo.count(NSLocalizedFailureErrorKey) > 0)
        {
            auto localizedFailure = AnyStringValue(this->userInfo().at(NSLocalizedFailureErrorKey));
            return localizedFailure;
        }

        return std::nullopt;
    }

    void setLocalizedFailure(std::optional<std::string> localizedFailure)
    {
        this->_userInfo[NSLocalizedFailureErrorKey] = localizedFailure;
    }

    virtual std::optional<std::string> localizedFailureReason() const
    {
        if (this->_userInfo.count(NSLocalizedFailureReasonErrorKey) > 0)
        {
            auto localizedFailureReason = AnyStringValue(this->userInfo().at(NSLocalizedFailureReasonErrorKey));
            return localizedFailureReason;
        }

        return std::nullopt;
    }

    virtual std::optional<std::string> localizedRecoverySuggestion() const
    {
        if (this->_userInfo.count(NSLocalizedRecoverySuggestionErrorKey) > 0)
        {
            auto localizedRecoverySuggestion = AnyStringValue(this->userInfo().at(NSLocalizedRecoverySuggestionErrorKey));
            return localizedRecoverySuggestion;
        }

        return std::nullopt;
    }

    virtual std::optional<std::string> localizedDebugDescription() const
    {
        if (this->_userInfo.count(NSDebugDescriptionErrorKey) > 0)
        {
            auto debugDescription = AnyStringValue(this->userInfo().at(NSDebugDescriptionErrorKey));
            return debugDescription;
        }

        return std::nullopt;
    }

    std::string localizedErrorCode() const
    {
        auto localizedErrorCode = this->domain() + " " + std::to_string(this->displayCode());
        return localizedErrorCode;
    }

    virtual int displayCode() const
    {
        return this->code();
    }

    friend std::ostream& operator<<(std::ostream& os, const Error& error)
    {
        os << error.localizedErrorCode() << ". \"" << error.localizedDescription() << "\"";
        return os;
    }

    web::json::value serialized() const;
    std::string formattedDetailedDescription() const;
    
protected:
    int _code;
	std::map<std::string, std::any> _userInfo;
};

class APIError : public Error
{
public:
    APIError(APIErrorCode code, std::map<std::string, std::any> userInfo = {}) : Error((int)code, userInfo)
    {
    }
    
    virtual std::string domain() const
    {
        return "AltStore.AppleDeveloperError";
    }
    
    virtual std::optional<std::string> localizedFailureReason() const
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
        return "AltSign.Error";
    }
    
    virtual std::optional<std::string> localizedFailureReason() const
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

class CocoaError: public Error
{
public:
    CocoaError(CocoaErrorCode code, std::map<std::string, std::any> userInfo = {}) : Error((int)code, userInfo)
    {
    }
    
    virtual std::string domain() const
    {
        return "NSCocoaErrorDomain";
    }
    
    virtual std::optional<std::string> localizedFailureReason() const
    {
        switch ((CocoaErrorCode)this->code())
        {
            case CocoaErrorCode::FileReadUnknown:
                return "An unknown error occured while reading the file.";

            case CocoaErrorCode::FileWriteUnknown:
                return "An unknown error occured while writing to disk.";

			case CocoaErrorCode::FileNoSuchFile:
                return "The app could not be found.";

            case CocoaErrorCode::FileReadCorruptFile:
                return "The app isn't in the correct format.";

            case CocoaErrorCode::CoderReadCorrupt:
                return "The data isn't in the correct format.";

            case CocoaErrorCode::CoderValueNotFound:
                return "The data is missing.";
        }

		return "Unknown error.";
    }
};

class LocalizedError : public Error
{
public:
    LocalizedError(int code, std::string localizedDescription) : Error(code, { {NSLocalizedDescriptionKey, localizedDescription} })
    {
    }

    using Error::Error;

    virtual std::optional<std::string> localizedFailureReason() const
    {
        return std::nullopt;
    }
};

class LocalizedAPIError : public LocalizedError
{
public:
    virtual std::string domain() const { return "Apple.APIError"; }

    using LocalizedError::LocalizedError; // Inherit constructor
};

class LocalizedInstallationError : public LocalizedError
{
public:
    virtual std::string domain() const { return "Apple.InstallationError"; }

    using LocalizedError::LocalizedError; // Inherit constructor
};

class ExceptionError : public LocalizedError
{
public:
    virtual std::string domain() const { return "AltServer.ExceptionError"; }

    ExceptionError(std::exception& exception) : LocalizedError(0, exception.what())
    {
    }
};

#endif /* Error_hpp */
