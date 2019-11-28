//
//  Connection.cpp
//  AltServer-Windows
//
//  Created by Riley Testut on 8/13/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include "Connection.hpp"

#include <sys/time.h>
#include <sys/socket.h>

#include <boost/filesystem.hpp>

#include "DeviceManager.hpp"

#include "ServerError.hpp"

extern std::string make_uuid();
extern std::string temporary_directory();

using namespace web;

namespace fs = boost::filesystem;

Connection::Connection(int socket) : _socket(socket)
{
}

Connection::~Connection()
{
    std::cout << "wtf" << std::endl;
}

pplx::task<void> Connection::ProcessAppRequest()
{
    web::json::value *request = new web::json::value;
    std::string *filepath = new std::string;
    std::string *udid = new std::string;
    
    auto task = this->ReceiveApp()
    .then([this, request, filepath, udid](std::tuple<web::json::value, std::string> appRequest) {
        *request = std::get<0>(appRequest);
        *filepath = std::get<1>(appRequest);
        
        *udid = (*request)["udid"].as_string();
        std::cout << "Awaiting begin installation request for device " << *udid << "..." << std::endl;
        
        return this->ReceiveRequest();
    })
    .then([this, filepath, udid](web::json::value request) {
        return this->InstallApp(*filepath, *udid);
    })
    .then([this, filepath, request, udid](pplx::task<void> task) {
        
        delete request;
        delete filepath;
        delete udid;
        
        auto response = json::value::object();
        response["version"] = json::value::number(1);
        response["identifier"] = json::value::string("ServerResponse");
        response["progress"] = json::value::number(1.0);
        
        try
        {
            task.get();
        }
        catch (Error& error)
        {
            response["errorCode"] = json::value::number(error.code());
        }
        catch (std::exception& exception)
        {
            response["errorCode"] = json::value::number((int)ServerErrorCode::Unknown);
        }
        
        return this->SendResponse(response);
    });
    
    return task;
}

pplx::task<void> Connection::InstallApp(std::string filepath, std::string udid)
{
    return pplx::create_task([this, filepath, udid]() {
        try {
            DeviceManager::instance()->InstallApp(filepath, udid);
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

pplx::task<std::tuple<web::json::value, std::string>> Connection::ReceiveApp()
{
    web::json::value *appRequest = new web::json::value;
    
    auto task = this->ReceiveRequest()
    .then([this, appRequest](web::json::value request) {
        
        auto appSize = request["contentSize"].as_integer();
        std::cout << "Receiving app (" << appSize << " bytes)..." << std::endl;
        
        *appRequest = request;
        
        return this->ReceiveData(appSize);
    })
    .then([this, appRequest](std::vector<unsigned char> data) {
        fs::path filepath = fs::path(temporary_directory()).append(make_uuid() + ".ipa");
        
        std::ofstream file(filepath.string(), std::ios::out|std::ios::binary);
        copy(data.cbegin(), data.cend(), std::ostreambuf_iterator<char>(file));
        
        return std::make_tuple(*appRequest, filepath.string());
    })
    .then([appRequest](pplx::task<std::tuple<web::json::value, std::string>> task) {
        delete appRequest;
        return task;
    });
    
    return task;
}

pplx::task<web::json::value> Connection::ReceiveRequest()
{
    auto task = this->ReceiveData(std::nullopt)
    .then([](std::vector<unsigned char> data) {
        std::string jsonString(data.begin(), data.end());
        
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
                    ssize_t readBytes = recv(this->socket(), buffer, std::min((ssize_t)1024, (ssize_t)(*size - data.size())), 0);
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
            
            ssize_t sentBytes = send(this->socket(), (const void *)data.data(), (size_t)(data.size() - totalSentBytes), 0);
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
