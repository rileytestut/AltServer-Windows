//
//  Connection.cpp
//  AltServer-Windows
//
//  Created by Riley Testut on 8/13/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include "Connection.hpp"

//#include <sys/time.h>
//#include <sys/socket.h>

#include <WinSock2.h>

#include <filesystem>

#include "DeviceManager.hpp"
#include "AnisetteDataManager.h"
#include "AnisetteData.h"

#include "ServerError.hpp"

#define odslog(msg) { std::stringstream ss; ss << msg << std::endl; OutputDebugStringA(ss.str().c_str()); }

extern std::string make_uuid();
extern std::string temporary_directory();

#include <limits.h>
#include <stddef.h>

#if SIZE_MAX == UINT_MAX
typedef int ssize_t;        /* common 32 bit case */
#elif SIZE_MAX == ULONG_MAX
typedef long ssize_t;       /* linux 64 bits */
#elif SIZE_MAX == ULLONG_MAX
typedef long long ssize_t;  /* windows 64 bits */
#elif SIZE_MAX == USHRT_MAX
typedef short ssize_t;      /* is this even possible? */
#else
#error platform has exotic SIZE_MAX
#endif

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

using namespace web;

namespace fs = std::filesystem;

Connection::Connection(int socket) : _socket(socket)
{
}

Connection::~Connection()
{
}

pplx::task<void> Connection::ProcessAppRequest()
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

pplx::task<void> Connection::ProcessPrepareAppRequest(web::json::value request)
{
	utility::string_t* filepath = new utility::string_t;
	std::string udid = StringFromWideString(request[L"udid"].as_string());

	auto task = this->ReceiveApp(request)
		.then([this, filepath](std::string path) {

		*filepath = WideStringFromString(path);

		//std::cout << L"Awaiting begin installation request for device " << *udid << "..." << std::endl;

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

			return task;
}

pplx::task<void> Connection::ProcessAnisetteDataRequest(web::json::value request)
{
	auto task = pplx::create_task([this, &request]() {

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

	return task;
}

pplx::task<void> Connection::InstallApp(std::string filepath, std::string udid)
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
        catch (Error &error)
        {
            std::cout << error << std::endl;
            
            throw error;
        }
        catch (std::exception &e)
        {
            std::cout << "Exception: " << e.what() << std::endl;
            
            throw e;
        }
        std::cout << "Installed app!" << std::endl;
    });
}

#pragma mark - Data Transfer -

pplx::task<std::string> Connection::ReceiveApp(web::json::value request)
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

pplx::task<web::json::value> Connection::ReceiveRequest()
{
    auto task = this->ReceiveData(std::nullopt)
    .then([](std::vector<unsigned char> data) {
        std::wstring jsonString(data.begin(), data.end());
        
        auto request = web::json::value::parse(jsonString);
        return request;
    });
    
    return task;
}

pplx::task<std::vector<unsigned char>> Connection::ReceiveData(std::optional<int> size)
{
    if (!size.has_value())
    {
        // Unknown size, so retrieve size first.
        int size = sizeof(uint32_t);
        
        std::cout << "Receiving request size..." << std::endl;
        
        auto task = this->ReceiveData(size)
        .then([this](std::vector<unsigned char> data) {
            int expectedBytes = *((int32_t *)data.data());
            std::cout << "Receiving " << expectedBytes << " bytes..." << std::endl;
            
            return this->ReceiveData(expectedBytes);
        });
        
        return task;
    }
    else
    {
        return pplx::create_task([this, size]() {
            std::vector<unsigned char> data;
            data.reserve(*size);
            
            char buffer[1024];
            
            fd_set          input_set;
            fd_set          copy_set;
            
            while (true)
            {
                struct timeval tv;
                tv.tv_sec = 1; /* 1 second timeout */
                tv.tv_usec = 0; /* no microseconds. */
                
                int socket = this->socket();
                std::cout << "Checking socket: " << socket << std::endl;
                
                /* Selection */
                FD_ZERO(&input_set );   /* Empty the FD Set */
                FD_SET(socket, &input_set);  /* Listen to the input descriptor */
                
                FD_ZERO(&copy_set );   /* Empty the FD Set */
                FD_SET(socket, &copy_set);  /* Listen to the input descriptor */
                
                int result = select(this->socket() + 1, &input_set, &copy_set, NULL, &tv);
                
                if (result == 0)
                {
                    continue;
                }
                else if (result == -1)
                {
                    std::cout << "Error!" << std::endl;
                }
                else
                {
                    ssize_t readBytes = recv(this->socket(), buffer, min((ssize_t)1024, (ssize_t)(*size - data.size())), 0);
                    for (int i = 0; i < readBytes; i++)
                    {
                        data.push_back(buffer[i]);
                    }
                    
                    std::cout << "Data Count: " << data.size() << std::endl;
                    
                    if (data.size() >= size)
                    {
                        break;
                    }
                }
            }
            
            return data;
        });
    }
}

pplx::task<void> Connection::SendResponse(web::json::value json)
{
    auto serializedJSON = json.serialize();
    std::vector<unsigned char> responseData(serializedJSON.begin(), serializedJSON.end());
    
    int32_t size = (int32_t)responseData.size();
    
    std::vector<unsigned char> responseSizeData;
    
    if (responseSizeData.size() < sizeof(size))
        responseSizeData.resize(sizeof(size));
    
    std::memcpy(responseSizeData.data(), &size, sizeof(size));
    
    std::cout << "Represented Value: " << *((int32_t *)responseSizeData.data()) << std::endl;
    
    auto task = this->SendData(responseSizeData)
    .then([this, responseData] {
        return this->SendData(responseData);
    });
    
    return task;
}

pplx::task<void> Connection::SendData(const std::vector<unsigned char>& data)
{
    return pplx::create_task([this, data]() {
        fd_set input_set;
        fd_set copy_set;
        
        int64_t totalSentBytes = 0;
        
        while (true)
        {
            struct timeval tv;
            tv.tv_sec = 1; /* 1 second timeout */
            tv.tv_usec = 0; /* no microseconds. */
            
            /* Selection */
            FD_ZERO(&input_set );   /* Empty the FD Set */
            FD_SET(this->socket(), &input_set);  /* Listen to the input descriptor */
            
            FD_ZERO(&copy_set );   /* Empty the FD Set */
            FD_SET(this->socket(), &copy_set);  /* Listen to the input descriptor */
            
            ssize_t sentBytes = send(this->socket(), (const char *)data.data(), (size_t)(data.size() - totalSentBytes), 0);
            totalSentBytes += sentBytes;
            
            std::cout << "Sent Bytes Count: " << sentBytes << " (" << totalSentBytes << ")" << std::endl;
            
            if (totalSentBytes >= sentBytes)
            {
                break;
            }
        }
        
        std::cout << "Sent Data: " << totalSentBytes << " Bytes" << std::endl;
    });
}

#pragma mark - Getters -

int Connection::socket() const
{
    return _socket;
}
