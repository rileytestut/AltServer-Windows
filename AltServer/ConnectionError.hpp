//
//  ConnectionError.hpp
//  AltServer-Windows
//
//  Created by Riley Testut on 6/19/21.
//  Copyright © 2021 Riley Testut. All rights reserved.
//

#ifndef ConnectionError_h
#define ConnectionError_h

#include "ServerError.hpp"
#include "Device.hpp"

#include <libimobiledevice/mobile_image_mounter.h>
#include <libimobiledevice/debugserver.h>
#include <libimobiledevice/installation_proxy.h>

#include <sstream>
#include <optional>

extern std::string ConnectionErrorDomain;

enum class ConnectionErrorCode
{
	Unknown,
	DeviceLocked,
	InvalidRequest,
	InvalidResponse,
	Usbmuxd,
	SSL,
	TimedOut
};

class ConnectionError : public Error
{
public:
	ConnectionError(ConnectionErrorCode code, std::map<std::string, std::string> userInfo = {}) : Error((int)code, userInfo)
	{
	}

	virtual std::string domain() const
	{
		return ConnectionErrorDomain;
	}

	virtual std::string localizedDescription() const
	{
		std::string deviceName = "The device";
		if (this->userInfo().count(DeviceNameErrorKey) > 0)
		{
			deviceName = this->userInfo()[DeviceNameErrorKey];
		}

		std::ostringstream ss;

		switch ((ConnectionErrorCode)this->code())
		{
		case ConnectionErrorCode::Unknown:
		{
			auto underlyingErrorDomain = this->userInfo()[UnderlyingErrorDomainErrorKey];
			auto underlyingErrorCode = this->userInfo()[UnderlyingErrorCodeErrorKey];

			if (underlyingErrorDomain.length() != 0 && underlyingErrorCode.length() != 0)
			{
				ss << underlyingErrorDomain << " error " << underlyingErrorCode << ".";
			}
			else if (underlyingErrorCode.length() != 0)
			{
				ss << "Connection error code: " << underlyingErrorCode << ".";
			}
			else
			{
				ss << "Unknown connection error.";
			}

			break;
		}

		case ConnectionErrorCode::DeviceLocked:
			ss << deviceName << " is currently locked.";
			break;

		case ConnectionErrorCode::InvalidRequest:
			ss << deviceName << " received an invalid request from AltServer.";
			break;

		case ConnectionErrorCode::InvalidResponse:
			ss << "AltServer received an invalid response from " << deviceName << ".";
			break;

		case ConnectionErrorCode::Usbmuxd:
			ss << "There was an issue communicating with the usbmuxd daemon.";
			break;

		case ConnectionErrorCode::SSL:
			ss << "AltServer could not establish a secure connection to " << deviceName << ".";
			break;

		case ConnectionErrorCode::TimedOut:
			ss << "AltServer's connection to " << deviceName << " timed out.";
			break;

		}

		return ss.str();
	}

    virtual std::optional<std::string> localizedRecoverySuggestion() const
    {
        switch ((ConnectionErrorCode)this->code())
        {
        case ConnectionErrorCode::DeviceLocked:
            return "Please unlock the device with your passcode and try again.";

        default: return Error::localizedRecoverySuggestion();
        }
    }

	static std::optional<ConnectionError> errorForMobileImageMounterError(mobile_image_mounter_error_t error, std::shared_ptr<Device> device)
	{
		std::map<std::string, std::string> userInfo = {
			{ UnderlyingErrorDomainErrorKey, "Mobile Image Mounter" },
			{ UnderlyingErrorCodeErrorKey, std::to_string((int)error) },
		};

		if (device != nullptr)
		{
			userInfo[DeviceNameErrorKey] = device->name();
		}

		switch (error)
		{
		case MOBILE_IMAGE_MOUNTER_E_SUCCESS: return std::nullopt;
		case MOBILE_IMAGE_MOUNTER_E_INVALID_ARG: return ConnectionError(ConnectionErrorCode::InvalidRequest, userInfo);
		case MOBILE_IMAGE_MOUNTER_E_PLIST_ERROR: return ConnectionError(ConnectionErrorCode::InvalidResponse, userInfo);
		case MOBILE_IMAGE_MOUNTER_E_CONN_FAILED: return ConnectionError(ConnectionErrorCode::Usbmuxd, userInfo);
		case MOBILE_IMAGE_MOUNTER_E_COMMAND_FAILED: return ConnectionError(ConnectionErrorCode::InvalidRequest, userInfo);
		case MOBILE_IMAGE_MOUNTER_E_DEVICE_LOCKED: return ConnectionError(ConnectionErrorCode::DeviceLocked, userInfo);
		case MOBILE_IMAGE_MOUNTER_E_UNKNOWN_ERROR: return ConnectionError(ConnectionErrorCode::Unknown, userInfo);
		}
	}

	static std::optional<ConnectionError> errorForDebugServerError(debugserver_error_t error, std::shared_ptr<Device> device)
	{
		std::map<std::string, std::string> userInfo = {
			{ UnderlyingErrorDomainErrorKey, "Debug Server" },
			{ UnderlyingErrorCodeErrorKey, std::to_string((int)error) },
		};

		if (device != nullptr)
		{
			userInfo[DeviceNameErrorKey] = device->name();
		}

		switch (error)
		{
		case DEBUGSERVER_E_SUCCESS: return std::nullopt;
		case DEBUGSERVER_E_INVALID_ARG: return ConnectionError(ConnectionErrorCode::InvalidRequest, userInfo);
		case DEBUGSERVER_E_MUX_ERROR: return ConnectionError(ConnectionErrorCode::Usbmuxd, userInfo);
		case DEBUGSERVER_E_SSL_ERROR: return ConnectionError(ConnectionErrorCode::SSL, userInfo);
		case DEBUGSERVER_E_RESPONSE_ERROR: return ConnectionError(ConnectionErrorCode::InvalidResponse, userInfo);
		case DEBUGSERVER_E_TIMEOUT: return ConnectionError(ConnectionErrorCode::TimedOut, userInfo);
		case DEBUGSERVER_E_UNKNOWN_ERROR: return ConnectionError(ConnectionErrorCode::Unknown, userInfo);
		}
	}

	static std::optional<ConnectionError> errorForInstallationProxyError(instproxy_error_t error, std::shared_ptr<Device> device)
	{
		std::map<std::string, std::string> userInfo = {
			{ UnderlyingErrorDomainErrorKey, "Installation Proxy" },
			{ UnderlyingErrorCodeErrorKey, std::to_string((int)error) },
		};

		if (device != nullptr)
		{
			userInfo[DeviceNameErrorKey] = device->name();
		}

		switch (error)
		{
		case INSTPROXY_E_SUCCESS: return std::nullopt;
		case INSTPROXY_E_INVALID_ARG: return ConnectionError(ConnectionErrorCode::InvalidRequest, userInfo);
		case INSTPROXY_E_PLIST_ERROR: return ConnectionError(ConnectionErrorCode::InvalidResponse, userInfo);
		case INSTPROXY_E_CONN_FAILED: return ConnectionError(ConnectionErrorCode::Usbmuxd, userInfo);
		case INSTPROXY_E_RECEIVE_TIMEOUT: return ConnectionError(ConnectionErrorCode::TimedOut, userInfo);
//      case INSTPROXY_E_DEVICE_OS_VERSION_TOO_LOW: return ConnectionError(ConnectionErrorCode::Unknown, userInfo); // Error message assumes we're installing AltStore
		default: return ConnectionError(ConnectionErrorCode::Unknown, userInfo);
		}
	}
};


#endif /* ConnectionError_h */
