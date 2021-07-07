#pragma once

#include "Device.hpp"

#include <pplx/pplxtasks.h>
#include <libimobiledevice/debugserver.h>

#include <memory>

class DebugConnection
{
public:
	DebugConnection(std::shared_ptr<Device> device);
	~DebugConnection();

	pplx::task<void> Connect();
	void Disconnect();

	pplx::task<void> EnableUnsignedCodeExecution(std::string processName);

	std::shared_ptr<Device> device() const;
	debugserver_client_t client() const;

private:
	std::shared_ptr<Device> _device;
	debugserver_client_t _client;

	void SendCommand(std::string command, std::vector<std::string> arguments);
	void ProcessResponse(std::optional<std::string> rawResponse);
};

