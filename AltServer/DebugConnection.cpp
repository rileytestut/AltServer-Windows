#include "DebugConnection.h"
#include "ServerError.hpp"
#include "ConnectionError.hpp"

#include <WS2tcpip.h>

#define odslog(msg) { std::stringstream ss; ss << msg << std::endl; OutputDebugStringA(ss.str().c_str()); }

char* bin2hex(const unsigned char* bin, size_t length)
{
	if (bin == NULL || length == 0)
	{
		return NULL;
	}

	char* hex = (char*)malloc(length * 2 + 1);
	for (size_t i = 0; i < length; i++)
	{
		hex[i * 2] = "0123456789ABCDEF"[bin[i] >> 4];
		hex[i * 2 + 1] = "0123456789ABCDEF"[bin[i] & 0x0F];
	}
	hex[length * 2] = '\0';

	return hex;
}

DebugConnection::DebugConnection(std::shared_ptr<Device> device) : _device(device), _client(nullptr)
{
}

DebugConnection::~DebugConnection()
{
	this->Disconnect();
}

pplx::task<void> DebugConnection::Connect()
{
    return pplx::create_task([this]() {
        /* Find Device */

        idevice_t device = NULL;

        auto cleanUp = [&]() {
            if (device != NULL)
            {
                idevice_free(device);
            }
        };

        try
        {
            if (idevice_new_with_options(&device, this->device()->identifier().c_str(), (enum idevice_options)((int)IDEVICE_LOOKUP_NETWORK | (int)IDEVICE_LOOKUP_USBMUX))
                != IDEVICE_E_SUCCESS)
            {
                throw ServerError(ServerErrorCode::DeviceNotFound);
            }

            /* Connect to debugserver */
            debugserver_error_t error = debugserver_client_start_service(device, &_client, "AltServer");
            if (error != DEBUGSERVER_E_SUCCESS)
            {
                auto err = ConnectionError::errorForDebugServerError(error, this->device());
                if (err.has_value())
                {
                    throw* err;
                }
            }

            cleanUp();
        }
        catch (std::exception& e)
        {
            cleanUp();
            throw;
        }
    });
}

void DebugConnection::Disconnect()
{
	if (_client == nullptr)
	{
		return;
	}

	debugserver_client_free(_client);
	_client = nullptr;
}

pplx::task<void> DebugConnection::EnableUnsignedCodeExecution(std::string processName)
{
    return this->EnableUnsignedCodeExecution(processName, 0);
}

pplx::task<void> DebugConnection::EnableUnsignedCodeExecution(int pid)
{
    return this->EnableUnsignedCodeExecution(std::nullopt, pid);
}

pplx::task<void> DebugConnection::EnableUnsignedCodeExecution(std::optional<std::string> processName, int pid)
{
	return pplx::create_task([=] {
        std::string name = processName.has_value() ? *processName : "this app";
        std::string localizedFailure = "JIT could not be enabled for " + name + ".";

		try
		{
            std::string attachCommand;

            if (processName.has_value())
            {
                std::string encodedName(bin2hex((const unsigned char*)processName->c_str(), processName->length()));
                attachCommand = "vAttachOrWait;" + encodedName;
            }
            else
            {
                // Convert to Big-endian
                int rawPID = htonl((int32_t)pid);

                std::string encodedName(bin2hex((const unsigned char*)&rawPID, 4));
                attachCommand = "vAttach;" + encodedName;
            }

			this->SendCommand(attachCommand, {});

			std::string detachCommand = "D";
			this->SendCommand(detachCommand, {});
		}
		catch (ConnectionError& connectionError)
		{
			auto userInfo = connectionError.userInfo();
			userInfo[AppNameErrorKey] = name;
			userInfo[DeviceNameErrorKey] = this->device()->name();
			userInfo[NSLocalizedFailureErrorKey] = localizedFailure;

			auto error = ConnectionError((ConnectionErrorCode)connectionError.code(), userInfo);
			throw error;
		}
		catch (ServerError& serverError)
		{
			auto userInfo = serverError.userInfo();
			userInfo[AppNameErrorKey] = name;
			userInfo[DeviceNameErrorKey] = this->device()->name();
			userInfo[NSLocalizedFailureErrorKey] = localizedFailure;

			auto error = ServerError((ServerErrorCode)serverError.code(), userInfo);
			throw error;
		}
	});
}

void DebugConnection::SendCommand(std::string command, std::vector<std::string> arguments)
{
	int argc = (int)arguments.size();
	char** argv = new char* [argc + 1];

	for (int i = 0; i < argc; i++)
	{
		std::string argument = arguments[i];
		argv[i] = (char*)argument.c_str();
	}

	argv[argc] = NULL;

	debugserver_command_t debugCommand = NULL;
	debugserver_command_new(command.c_str(), argc, argv, &debugCommand);

	delete[] argv;

	char* response = NULL;
	size_t responseSize = 0;
	debugserver_error_t debugServerError = debugserver_client_send_command(this->client(), debugCommand, &response, &responseSize);
	debugserver_command_free(debugCommand);

	if (debugServerError != DEBUGSERVER_E_SUCCESS)
	{
		auto error = ConnectionError::errorForDebugServerError(debugServerError, this->device());
		if (error.has_value())
		{
			throw* error;
		}
	}

	std::optional<std::string> rawResponse = (response != NULL) ? std::make_optional(response) : std::nullopt;
	this->ProcessResponse(rawResponse);
}

void DebugConnection::ProcessResponse(std::optional<std::string> rawResponse)
{
	if (!rawResponse.has_value())
	{
		throw ServerError(ServerErrorCode::RequestedAppNotRunning);
	}

	if (rawResponse->length() == 0 || rawResponse->compare("OK") == 0)
	{
		return;
	}

	char type = (*rawResponse)[0];
	std::string response(rawResponse->begin() + 1, rawResponse->end());

	switch (type)
	{
	case 'O':
	{
		// stdout/stderr

		char* decodedResponse = NULL;
		debugserver_decode_string(response.c_str(), response.size(), &decodedResponse);
		
		odslog("Response: " << decodedResponse);

		if (decodedResponse)
		{
			free(decodedResponse);
		}

		break;
	}

	case 'T':
	{
		// Thread Information

		odslog("Thread stopped. Details: " << response);

		std::istringstream stream(response.c_str());

		std::string mainThread;
		for (int i = 0; i < 2; i++)
		{
			if (!std::getline(stream, mainThread, ';'))
			{
				throw ConnectionError(ConnectionErrorCode::Unknown, { {"NSLocalizedFailureReason", response} });
			}
		}
        
        // Parse thread state to determine if app is running.
		std::istringstream threadStream(mainThread.c_str());

		std::string threadState;
		if (!std::getline(threadStream, threadState, ':'))
		{
			throw ConnectionError(ConnectionErrorCode::Unknown, { {"NSLocalizedFailureReason", response} });
		}

		if (!std::getline(threadStream, threadState, ';'))
		{
			throw ConnectionError(ConnectionErrorCode::Unknown, { {"NSLocalizedFailureReason", response} });
		}

        // If main thread state == 0, app is not running.
		// Must be unsigned long long to avoid potential overflow.
		if (std::stoull(threadState, NULL, 16) == 0)
		{
			throw ServerError(ServerErrorCode::RequestedAppNotRunning);
		}

		break;
	}

	case 'E':
	{
		// Error

		std::istringstream stream(response.c_str());

		std::string errorCode;
		if (!std::getline(stream, errorCode, ';'))
		{
			throw ConnectionError(ConnectionErrorCode::Unknown, { {"NSLocalizedFailureReason", response} });
		}

		switch (std::stoi(errorCode))
		{
		case 96: throw ServerError(ServerErrorCode::RequestedAppNotRunning);
		default: throw ConnectionError(ConnectionErrorCode::Unknown, { {"NSLocalizedFailureReason", response} });
		};

		break;
	}

    case 'W':
	{
		// Warning

		odslog("WARNING: " << response);
		break;
	}
	}
}

std::shared_ptr<Device> DebugConnection::device() const
{
	return _device;
}

debugserver_client_t DebugConnection::client() const
{
	return _client;
}