#include "ClientConnection.h"

#include <limits.h>
#include <stddef.h>

#include <WinSock2.h>
#include <filesystem>

#include "DeviceManager.hpp"
#include "AnisetteDataManager.h"
#include "AnisetteData.h"

#include "ServerError.hpp"

#define odslog(msg) { std::stringstream ss; ss << msg << std::endl; OutputDebugStringA(ss.str().c_str()); }

extern std::string make_uuid();
extern std::string temporary_directory();

using namespace web;

namespace fs = std::filesystem;

std::string StringFromWideString(std::wstring wideString)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;

	std::string string = converter.to_bytes(wideString);
	return string;
}

std::wstring WideStringFromString(std::string string)
{
	std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;

	std::wstring wideString = converter.from_bytes(string);
	return wideString;
}

ClientConnection::ClientConnection()
{
}

ClientConnection::~ClientConnection()
{
	this->Disconnect();
}

void ClientConnection::Disconnect()
{
}

pplx::task<void> ClientConnection::ProcessAppRequest()
{
	auto task = this->ReceiveRequest().then([this](web::json::value request) {
		auto identifier = StringFromWideString(request[L"identifier"].as_string());

		if (identifier == "PrepareAppRequest")
		{
			return this->ProcessPrepareAppRequest(request);
		}
		else
		{
			return this->ProcessAnisetteDataRequest(request);
		}
	});

	return task;
}

pplx::task<void> ClientConnection::ProcessPrepareAppRequest(web::json::value request)
{
	utility::string_t* filepath = new utility::string_t;
	std::string udid = StringFromWideString(request[L"udid"].as_string());

	return this->ReceiveApp(request).then([this, filepath](std::string path) {
		*filepath = WideStringFromString(path);
		return this->ReceiveRequest();
	})
	.then([this, filepath, udid](web::json::value request) {
		return this->InstallApp(StringFromWideString(*filepath), udid);
	})
	.then([this, filepath, udid](pplx::task<void> task) {

		if (filepath->size() > 0)
		{
			try
			{
				fs::remove(fs::path(*filepath));
			}
			catch (std::exception& e)
			{
				odslog("Failed to remove received .ipa." << e.what());
			}
		}

		delete filepath;

		auto response = json::value::object();
		response[L"version"] = json::value::number(1);

		try
		{
			task.get();

			response[L"identifier"] = json::value::string(L"InstallationProgressResponse");
			response[L"progress"] = json::value::number(1.0);
		}
		catch (ServerError& error)
		{
			response[L"identifier"] = json::value::string(L"ErrorResponse");
			response[L"errorCode"] = json::value::number(error.code());
		}
		catch (std::exception& exception)
		{
			response[L"identifier"] = json::value::string(L"ErrorResponse");
			response[L"errorCode"] = json::value::number((int)ServerErrorCode::Unknown);
		}

		return this->SendResponse(response);
	});
}

pplx::task<void> ClientConnection::ProcessAnisetteDataRequest(web::json::value request)
{
	return pplx::create_task([this, &request]() {

		auto anisetteData = AnisetteDataManager::instance()->FetchAnisetteData();

		auto response = json::value::object();
		response[L"version"] = json::value::number(1);

		if (anisetteData)
		{
			response[L"identifier"] = json::value::string(L"AnisetteDataResponse");
			response[L"anisetteData"] = anisetteData->json();
		}
		else
		{
			response[L"identifier"] = json::value::string(L"ErrorResponse");
			response[L"errorCode"] = json::value::number((uint64_t)ServerErrorCode::InvalidAnisetteData);
		}

		return this->SendResponse(response);
	});
}

pplx::task<std::string> ClientConnection::ReceiveApp(web::json::value request)
{
	auto appSize = request[L"contentSize"].as_integer();
	std::cout << "Receiving app (" << appSize << " bytes)..." << std::endl;

	return this->ReceiveData(appSize).then([this](std::vector<unsigned char> data) {
		fs::path filepath = fs::path(temporary_directory()).append(make_uuid() + ".ipa");

		std::ofstream file(filepath.string(), std::ios::out | std::ios::binary);
		copy(data.cbegin(), data.cend(), std::ostreambuf_iterator<char>(file));

		return filepath.string();
	});
}

pplx::task<void> ClientConnection::InstallApp(std::string filepath, std::string udid)
{
	return pplx::create_task([this, filepath, udid]() {
		try {
			auto isSending = std::make_shared<bool>();

			return DeviceManager::instance()->InstallApp(filepath, udid, [this, isSending](double progress) {
				if (*isSending)
				{
					return;
				}

				*isSending = true;

				auto response = json::value::object();
				response[L"version"] = json::value::number(1);
				response[L"identifier"] = json::value::string(L"InstallationProgressResponse");
				response[L"progress"] = json::value::number(progress);

				this->SendResponse(response).then([isSending](pplx::task<void> task) {
					*isSending = false;
					});
				});
		}
		catch (Error& error)
		{
			std::cout << error << std::endl;

			throw error;
		}
		catch (std::exception& e)
		{
			std::cout << "Exception: " << e.what() << std::endl;

			throw e;
		}
		std::cout << "Installed app!" << std::endl;
	});
}


pplx::task<void> ClientConnection::SendResponse(web::json::value json)
{
	auto serializedJSON = json.serialize();
	std::vector<unsigned char> responseData(serializedJSON.begin(), serializedJSON.end());

	int32_t size = (int32_t)responseData.size();

	std::vector<unsigned char> responseSizeData;

	if (responseSizeData.size() < sizeof(size))
	{
		responseSizeData.resize(sizeof(size));
	}

	std::memcpy(responseSizeData.data(), &size, sizeof(size));

	std::cout << "Represented Value: " << *((int32_t*)responseSizeData.data()) << std::endl;

	auto task = this->SendData(responseSizeData)
	.then([this, responseData]() mutable {
		return this->SendData(responseData);
	});

	return task;
}

pplx::task<web::json::value> ClientConnection::ReceiveRequest()
{
	int size = sizeof(uint32_t);

	std::cout << "Receiving request size..." << std::endl;

	auto task = this->ReceiveData(size)
	.then([this](std::vector<unsigned char> data) {
		int expectedBytes = *((int32_t*)data.data());
		std::cout << "Receiving " << expectedBytes << " bytes..." << std::endl;

		return this->ReceiveData(expectedBytes);
	})
	.then([](std::vector<unsigned char> data) {
		std::wstring jsonString(data.begin(), data.end());

		auto request = web::json::value::parse(jsonString);
		return request;
	});

	return task;
}
