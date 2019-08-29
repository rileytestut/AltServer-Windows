//
//  AltSign_Windows.cpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/8/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include <iostream>
#include <bitset>

/* The classes below are exported */

extern "C" {
    #pragma GCC visibility push(default)
    #include <plist/plist.h>
    #pragma GCC visibility pop
}

#include "Account.hpp"
#include "Error.hpp"
#include "CertificateRequest.hpp"

#include "AppleAPI.hpp"

#include <cpprest/http_compression.h>

using namespace std;
using namespace utility;                    // Common utilities like string conversions
using namespace web;                        // Common features like URIs.
using namespace web::http;                  // Common HTTP functionality
using namespace web::http::client;          // HTTP client features
using namespace concurrency::streams;       // Asynchronous streams

std::string kAuthenticationProtocolVersion = "A1234";
std::string kProtocolVersion = "QH65B2";
std::string kAppIDKey = "ba2ec180e6ca6e6c6a542255453b24d6e6e5b2be0cc48bc1b0d8ad64cfe0228f";
std::string kClientID = "XABBG36SBA";

extern std::string make_uuid();

bool decompress(const uint8_t* input, size_t input_size, std::vector<uint8_t>& output)
{
    auto decompressor = web::http::compression::builtin::make_decompressor(web::http::compression::builtin::algorithm::GZIP);
    
    // Need to guard against attempting to decompress when we're already finished or encountered an error!
    if (input == nullptr || input_size == 0)
    {
        return false;
    }
    
    size_t processed;
    size_t got;
    size_t inbytes = 0;
    size_t outbytes = 0;
    bool done;
    
    try
    {
        output.resize(input_size * 3);
        do
        {
            if (inbytes)
            {
                output.resize(output.size() + (std::max)(input_size, static_cast<size_t>(1024)));
            }
            got = decompressor->decompress(input + inbytes,
                                             input_size - inbytes,
                                             output.data() + outbytes,
                                             output.size() - outbytes,
                                           web::http::compression::operation_hint::is_last,
                                             processed,
                                             done);
            inbytes += processed;
            outbytes += got;
        } while (got && !done);
        output.resize(outbytes);
    }
    catch (...)
    {
        return false;
    }
    
    return true;
}

AppleAPI* AppleAPI::instance_ = nullptr;

AppleAPI* AppleAPI::getInstance()
{
    if (instance_ == 0)
    {
        instance_ = new AppleAPI();
    }
    
    return instance_;
}

AppleAPI::AppleAPI() : _authClient(U("https://idmsa.apple.com")), _client(U("https://developerservices2.apple.com/services/QH65B2"))
{
//    volatile long response_counter = 0;
//    auto response_count_handler =
//    [&response_counter](http_request request, std::shared_ptr<http_pipeline_stage> next_stage) -> pplx::task<http_response>
//    {
//        return next_stage->propagate(request)
//        .then([&response_counter](http_response resp)
//              {
//                  return resp.content_ready();
//              })
//        .then([&response_counter](http_response resp) -> http_response
//                                                   {
//                                                       // Use synchronization primitives to access data from within the handlers.
//                                                       resp.headers().add("ResponseHeader", "App");
//                                                       return resp;
//                                                   });
//    };
//
//    _client.add_handler(response_count_handler);
}

pplx::task<std::shared_ptr<Account>> AppleAPI::Authenticate(std::string appleID, std::string password)
{
    std::map<utility::string_t, utility::string_t> headers = {
        {L"Content-Type", L"application/x-www-form-urlencoded"},
        {L"User-Agent", L"Xcode"},
        {L"Accept", L"text/x-xml-plist"},
        {L"Accept-Language", L"en-us"},
        {L"Accept-Encoding", L"*"},
        {L"Connection", L"keep-alive"}
    };
    
    uri_builder builder(U("/IDMSWebAuth/clientDAW.cgi"));
    
    http_request request(methods::POST);
    request.set_request_uri(builder.to_string());
    
    auto encodedAppleID = web::uri::encode_uri(std::wstring(appleID.begin(), appleID.end()), uri::components::fragment);
    auto encodedPassword = web::uri::encode_uri(std::wstring(password.begin(), password.end()), uri::components::fragment);
    
    std::stringstream ss;
    ss << "format=" << "plist";
    ss << "&appIdKey=" << kAppIDKey;
    ss << "&appleId=" << encodedAppleID.c_str();
    ss << "&password=" << encodedPassword.c_str();
    ss << "&userLocale=" << "en_US";
    ss << "&protocolVersion=" << kAuthenticationProtocolVersion;
    
    auto body = ss.str();
    request.set_body(body);
    
    for (auto &pair : headers)
    {
        if (request.headers().has(pair.first))
        {
            request.headers().remove(pair.first);
        }
        
        request.headers().add(pair.first, pair.second);
    }
    
    auto task = this->authClient().request(request)
    .then([=](http_response response)
          {
              return response.content_ready();
          })
    .then([=](http_response response)
          {
              response.headers().set_content_type(L"text/plain");
              
              printf("Received response status code:%u\n", response.status_code());
              
              return response.extract_string(true);
          })
    .then([=](string_t plistXML)
          {
              plist_t plist = nullptr;
              plist_from_xml((const char *)plistXML.c_str(), (int)plistXML.size(), &plist);
              
              if (plist == nullptr)
              {
                  throw APIError(APIErrorCode::InvalidResponse);
              }
              
              return plist;
          })
    .then([=](plist_t plist)
          {
              auto account = this->ProcessResponse<std::shared_ptr<Account>>(plist, [=](auto plist)
                                                                             {
                                                                                 auto account = std::make_shared<Account>(appleID, plist);
                                                                                 return account;
                                                                             }, [=](auto resultCode) -> std::optional<APIError>
                                                                             {
                                                                                 switch (resultCode)
                                                                                 {
                                                                                     case -22910:
                                                                                     case -22938:
                                                                                         return std::make_optional<APIError>(APIErrorCode::AppSpecificPasswordRequired);
                                                                                         
                                                                                     case -1:
                                                                                     case -20101:
                                                                                         return std::make_optional<APIError>(APIErrorCode::IncorrectCredentials);
                                                                                         
                                                                                     default:
                                                                                         return std::nullopt;
                                                                                 }
                                                                             });
              return account;
              
              return std::make_shared<Account>();
          });
    
    return task;
}

#pragma mark - Teams -

pplx::task<std::vector<std::shared_ptr<Team>>> AppleAPI::FetchTeams(std::shared_ptr<Account> account)
{
    auto task = this->SendRequest("listTeams.action", {}, account, nullptr)
    .then([=](plist_t plist)
          {
              auto teams = this->ProcessResponse<std::vector<std::shared_ptr<Team>>>(plist, [account](auto plist)
                                                                                     {
                                                                                         auto node = plist_dict_get_item(plist, "teams");
                                                                                         if (node == nullptr)
                                                                                         {
                                                                                             throw APIError(APIErrorCode::InvalidResponse);
                                                                                         }
                                                                                         
                                                                                         std::vector<std::shared_ptr<Team>> teams;
                                                                                         
                                                                                         int size = plist_array_get_size(node);
                                                                                         for (int i = 0; i < size; i++)
                                                                                         {
                                                                                             plist_t plist = plist_array_get_item(node, i);
                                                                                             
                                                                                             auto team = std::make_shared<Team>(account, plist);
                                                                                             teams.push_back(team);
                                                                                         }
                                                                                         
                                                                                         if (teams.size() == 0)
                                                                                         {
                                                                                             throw APIError(APIErrorCode::NoTeams);
                                                                                         }
                                                                                         
                                                                                         return teams;
                                                                                         
                                                                                     }, [=](auto resultCode) -> std::optional<APIError>
                                                                                     {
                                                                                         return std::nullopt;
                                                                                     });
              return teams;
          });
    
    return task;
}

#pragma mark - Devices -

pplx::task<vector<shared_ptr<Device>>> AppleAPI::FetchDevices(shared_ptr<Team> team)
{
    auto task = this->SendRequest("ios/listDevices.action", {}, team->account(), team)
    .then([=](plist_t plist)
          {
              auto devices = this->ProcessResponse<vector<shared_ptr<Device>>>(plist, [](auto plist)
                                                                                     {
                                                                                         auto node = plist_dict_get_item(plist, "devices");
                                                                                         if (node == nullptr)
                                                                                         {
                                                                                             throw APIError(APIErrorCode::InvalidResponse);
                                                                                         }
                                                                                         
                                                                                         vector<shared_ptr<Device>> devices;
                                                                                         
                                                                                         int size = plist_array_get_size(node);
                                                                                         for (int i = 0; i < size; i++)
                                                                                         {
                                                                                             plist_t plist = plist_array_get_item(node, i);
                                                                                             
                                                                                             auto device = make_shared<Device>(plist);
                                                                                             devices.push_back(device);
                                                                                         }
                                                                                         
                                                                                         return devices;
                                                                                         
                                                                                     }, [=](auto resultCode) -> optional<APIError>
                                                                                     {
                                                                                         return nullopt;
                                                                                     });
              return devices;
          });
    
    return task;
}

pplx::task<shared_ptr<Device>> AppleAPI::RegisterDevice(string name, string identifier, shared_ptr<Team> team)
{
    map<string, string> parameters = {
        {"name", name},
        {"deviceNumber", identifier}
    };
    
    auto task = this->SendRequest("ios/addDevice.action", parameters, team->account(), team)
    .then([=](plist_t plist)
          {
              auto devices = this->ProcessResponse<shared_ptr<Device>>(plist, [](auto plist)
                                                                       {
                                                                           auto node = plist_dict_get_item(plist, "device");
                                                                           if (node == nullptr)
                                                                           {
                                                                               throw APIError(APIErrorCode::InvalidResponse);
                                                                           }
                                                                           
                                                                           auto device = make_shared<Device>(node);
                                                                           return device;
                                                                           
                                                                       }, [=](auto resultCode) -> std::optional<APIError>
                                                                       {
                                                                           return std::nullopt;
                                                                       });
              return devices;
          });
    
    return task;
}

#pragma mark - Certificates -

pplx::task<std::vector<std::shared_ptr<Certificate>>> AppleAPI::FetchCertificates(std::shared_ptr<Team> team)
{
    auto task = this->SendRequest("ios/listAllDevelopmentCerts.action", {}, team->account(), team)
    .then([=](plist_t plist)
          {
              auto certificates = this->ProcessResponse<vector<shared_ptr<Certificate>>>(plist, [](auto plist)
                                                                                         {
                                                                                             auto node = plist_dict_get_item(plist, "certificates");
                                                                                             if (node == nullptr)
                                                                                             {
                                                                                                 throw APIError(APIErrorCode::InvalidResponse);
                                                                                             }
                                                                                             
                                                                                             vector<shared_ptr<Certificate>> certificates;
                                                                                             
                                                                                             int size = plist_array_get_size(node);
                                                                                             for (int i = 0; i < size; i++)
                                                                                             {
                                                                                                 plist_t plist = plist_array_get_item(node, i);
                                                                                                 
                                                                                                 auto certificate = make_shared<Certificate>(plist);
                                                                                                 certificates.push_back(certificate);
                                                                                             }
                                                                                             
                                                                                             return certificates;
                                                                                             
                                                                                         }, [=](auto resultCode) -> optional<APIError>
                                                                                         {
                                                                                             return nullopt;
                                                                                         });
              return certificates;
          });
    
    return task;
}

pplx::task<std::shared_ptr<Certificate>> AppleAPI::AddCertificate(std::string machineName, std::shared_ptr<Team> team)
{
    CertificateRequest request;
    
    string encodedCSR(request.data().begin(), request.data().end());
    
    map<string, string> parameters = {
        { "csrContent", encodedCSR },
        { "machineId", make_uuid() },
        { "machineName", machineName }
    };

    auto task = this->SendRequest("ios/submitDevelopmentCSR.action", parameters, team->account(), team)
    .then([=](plist_t plist)
          {
              auto certificate = this->ProcessResponse<shared_ptr<Certificate>>(plist, [&request](auto plist)
                                                                       {
                                                                           auto node = plist_dict_get_item(plist, "certRequest");
                                                                           if (node == nullptr)
                                                                           {
                                                                               throw APIError(APIErrorCode::InvalidResponse);
                                                                           }
                                                                           
                                                                           auto certificate = make_shared<Certificate>(node);
                                                                           certificate->setPrivateKey(request.privateKey());
                                                                           return certificate;

                                                                       }, [=](auto resultCode) -> optional<APIError>
                                                                       {
                                                                           return nullopt;
                                                                       });
              return certificate;
          });

    return task;
}

pplx::task<bool> AppleAPI::RevokeCertificate(std::shared_ptr<Certificate> certificate, std::shared_ptr<Team> team)
{
    map<string, string> parameters = {
        {"serialNumber", certificate->serialNumber()}
    };
    
    auto task = this->SendRequest("ios/revokeDevelopmentCert.action", parameters, team->account(), team)
    .then([=](plist_t plist)
          {
              auto success = this->ProcessResponse<bool>(plist, [](auto plist)
                                                         {
                                                             auto node = plist_dict_get_item(plist, "certRequests");
                                                             if (node == nullptr)
                                                             {
                                                                 throw APIError(APIErrorCode::InvalidResponse);
                                                             }
                                                             
                                                             return true;
                                                             
                                                         }, [=](auto resultCode) -> std::optional<APIError>
                                                         {
                                                             switch (resultCode)
                                                             {
                                                                 case 7252:
                                                                     return std::make_optional<APIError>(APIErrorCode::CertificateDoesNotExist);
                                                                     
                                                                 default:
                                                                     return std::nullopt;
                                                             }
                                                         });
              return success;
          });
    
    return task;
}

#pragma mark - App IDs -

pplx::task<std::vector<std::shared_ptr<AppID>>> AppleAPI::FetchAppIDs(std::shared_ptr<Team> team)
{
    auto task = this->SendRequest("ios/listAppIds.action", {}, team->account(), team)
    .then([=](plist_t plist)
          {
              auto appIDs = this->ProcessResponse<vector<shared_ptr<AppID>>>(plist, [](auto plist)
                                                                                         {
                                                                                             auto node = plist_dict_get_item(plist, "appIds");
                                                                                             if (node == nullptr)
                                                                                             {
                                                                                                 throw APIError(APIErrorCode::InvalidResponse);
                                                                                             }
                                                                                             
                                                                                             vector<shared_ptr<AppID>> appIDs;
                                                                                             
                                                                                             int size = plist_array_get_size(node);
                                                                                             for (int i = 0; i < size; i++)
                                                                                             {
                                                                                                 plist_t plist = plist_array_get_item(node, i);
                                                                                                 
                                                                                                 auto appID = make_shared<AppID>(plist);
                                                                                                 appIDs.push_back(appID);
                                                                                             }
                                                                                             
                                                                                             return appIDs;
                                                                                             
                                                                                         }, [=](auto resultCode) -> optional<APIError>
                                                                                         {
                                                                                             return nullopt;
                                                                                         });
              return appIDs;
          });
    
    return task;
}

pplx::task<std::shared_ptr<AppID>> AppleAPI::AddAppID(std::string name, std::string bundleIdentifier, std::shared_ptr<Team> team)
{
    map<string, string> parameters = {
        { "name", name },
        { "identifier", bundleIdentifier },
    };
    
    auto task = this->SendRequest("ios/addAppId.action", parameters, team->account(), team)
    .then([=](plist_t plist)
          {
              auto appID = this->ProcessResponse<shared_ptr<AppID>>(plist, [](auto plist)
                                                                    {
                                                                        auto node = plist_dict_get_item(plist, "appId");
                                                                        if (node == nullptr)
                                                                        {
                                                                            throw APIError(APIErrorCode::InvalidResponse);
                                                                        }
                                                                        
                                                                        auto appID = make_shared<AppID>(node);
                                                                        return appID;
                                                                        
                                                                    }, [=](auto resultCode) -> optional<APIError>
                                                                    {
                                                                        switch (resultCode)
                                                                        {
                                                                            case 35:
                                                                                return std::make_optional<APIError>(APIErrorCode::InvalidAppIDName);
                                                                                
                                                                            case 9401:
                                                                                return std::make_optional<APIError>(APIErrorCode::BundleIdentifierUnavailable);
                                                                                
                                                                            case 9412:
                                                                                return std::make_optional<APIError>(APIErrorCode::InvalidBundleIdentifier);
                                                                                
                                                                            default:
                                                                                return std::nullopt;
                                                                        }
                                                                    });
              return appID;
          });
    
    return task;
}

#pragma mark - Provisioning Profiles -

pplx::task<std::shared_ptr<ProvisioningProfile>> AppleAPI::FetchProvisioningProfile(std::shared_ptr<AppID> appID, std::shared_ptr<Team> team)
{
    map<string, string> parameters = {
        { "appIdId", appID->identifier() },
    };
    
    auto task = this->SendRequest("ios/downloadTeamProvisioningProfile.action", parameters, team->account(), team)
    .then([=](plist_t plist)
          {
              auto profile = this->ProcessResponse<shared_ptr<ProvisioningProfile>>(plist, [](auto plist)
                                                                    {
                                                                        auto node = plist_dict_get_item(plist, "provisioningProfile");
                                                                        if (node == nullptr)
                                                                        {
                                                                            throw APIError(APIErrorCode::InvalidResponse);
                                                                        }
                                                                        
                                                                        auto profile = make_shared<ProvisioningProfile>(node);
                                                                        return profile;
                                                                        
                                                                    }, [=](auto resultCode) -> optional<APIError>
                                                                    {
                                                                        switch (resultCode)
                                                                        {
                                                                            case 8201:
                                                                                return std::make_optional<APIError>(APIErrorCode::AppIDDoesNotExist);
                                                                                
                                                                            default:
                                                                                return std::nullopt;
                                                                        }
                                                                    });
              return profile;
          });
    
    return task;
}

pplx::task<bool> AppleAPI::DeleteProvisioningProfile(std::shared_ptr<ProvisioningProfile> profile, std::shared_ptr<Team> team)
{
    if (!profile->identifier().has_value())
    {
        throw APIError(APIErrorCode::InvalidProvisioningProfileIdentifier);
    }
    
    map<string, string> parameters = {
        { "provisioningProfileId", *(profile->identifier()) },
        { "teamId", team->identifier() }
    };
    
    auto task = this->SendRequest("ios/deleteProvisioningProfile.action", parameters, team->account(), team)
    .then([=](plist_t plist)
          {
              auto success = this->ProcessResponse<bool>(plist, [](auto plist)
                                                         {
                                                             auto node = plist_dict_get_item(plist, "resultCode");
                                                             if (node == nullptr)
                                                             {
                                                                 throw APIError(APIErrorCode::InvalidResponse);
                                                             }
                                                             
                                                             return true;
                                                             
                                                         }, [=](auto resultCode) -> std::optional<APIError>
                                                         {
                                                             switch (resultCode)
                                                             {
                                                                 case 35:
                                                                     return std::make_optional<APIError>(APIErrorCode::InvalidProvisioningProfileIdentifier);
                                                                     
                                                                 case 8101:
                                                                     return std::make_optional<APIError>(APIErrorCode::ProvisioningProfileDoesNotExist);
                                                                     
                                                                 default:
                                                                     return std::nullopt;
                                                             }
                                                         });
              return success;
          });
    
    return task;
}

#pragma mark - Requests -

pplx::task<plist_t> AppleAPI::SendRequest(std::string uri,
                                              std::map<std::string, std::string> additionalParameters,
                                              std::shared_ptr<Account> account,
                                              std::shared_ptr<Team> team)
{
    //TODO: Make this non-constant
    std::string requestID("1841CE46-6A6C-4059-ABB3-677871CADC7D");
    
    auto locales = plist_new_array();
    plist_array_append_item(locales, plist_new_string("en_US"));
    
    std::map<std::string, plist_t> parameters = {
        { "DTDK_Platform", plist_new_string("ios") },
        { "clientId", plist_new_string(kClientID.c_str()) },
        { "protocolVersion", plist_new_string(kProtocolVersion.c_str()) },
        { "myacinfo", plist_new_string(account->cookie().c_str()) },
        { "requestId", plist_new_string(requestID.c_str()) },
        { "userLocale", locales}
    };
    
    auto plist = plist_new_dict();
    for (auto &parameter : parameters)
    {
        plist_dict_set_item(plist, parameter.first.c_str(), parameter.second);
    }
    
    for (auto &parameter : additionalParameters)
    {
        plist_dict_set_item(plist, parameter.first.c_str(), plist_new_string(parameter.second.c_str()));
    }

    if (team != nullptr)
    {
        plist_dict_set_item(plist, "teamId", plist_new_string(team->identifier().c_str()));
    }
    
    char *plistXML = nullptr;
    uint32_t length = 0;
    plist_to_xml(plist, &plistXML, &length);

	auto wideURI = utility::string_t(uri.begin(), uri.end());
	auto wideClientID = utility::string_t(kClientID.begin(), kClientID.end());

	auto encodedURI = web::uri::encode_uri(wideURI);
    uri_builder builder(encodedURI);
    builder.append_query(L"clientId", wideClientID);
    
    http_request request(methods::POST);
    request.set_request_uri(builder.to_string());
    request.set_body(plistXML);
    
    utility::string_t cookie = L"myacinfo=" + utility::string_t(account->cookie().begin(), account->cookie().end());
    
    std::map<utility::string_t, utility::string_t> headers = {
        {L"Content-Type", L"text/x-xml-plist"},
        {L"User-Agent", L"Xcode"},
        {L"Accept", L"text/x-xml-plist"},
        {L"Accept-Language", L"en-us"},
        {L"X-Xcode-Version", L"7.0 (7A120f)"},
        {L"Cookie", cookie},
        {L"Connection", L"keep-alive"},
        {L"Accept-Encoding", L"*"}
    };
    
    for (auto &pair : headers)
    {
        if (request.headers().has(pair.first))
        {
            request.headers().remove(pair.first);
        }
        
        request.headers().add(pair.first, pair.second);
    }
    
    auto task = this->client().request(request)
    .then([=](http_response response)
          {
              return response.content_ready();
          })
    .then([=](http_response response)
          {
              printf("Received response status code:%u\n", response.status_code());
              return response.extract_string(true);
          })
    .then([=](string_t plistXML)
          {
              std::vector<uint8_t> decompressedData;
              decompress((const uint8_t*)plistXML.c_str(), (size_t)plistXML.size(), decompressedData);
              
              std::string decompressedXML = std::string(decompressedData.begin(), decompressedData.end());
              
              plist_t plist = nullptr;
              plist_from_xml(decompressedXML.c_str(), (int)decompressedXML.size(), &plist);

              if (plist == nullptr)
              {
                  throw APIError(APIErrorCode::InvalidResponse);
              }

              return plist;
          });
    
//    free(plistXML);
//    plist_free(plist);
    
    return task;
}

template<typename T>
T AppleAPI::ProcessResponse(plist_t plist, std::function<T(plist_t)> parseHandler, std::function<std::optional<APIError>(int64_t)> resultCodeHandler)
{
    try
    {
        auto value = parseHandler(plist);
        return value;
    }
    catch (std::exception &exception)
    {
        std::cout << "Parse exception: " << exception.what() << std::endl;
        
        int64_t resultCode = 0;
        
        auto node = plist_dict_get_item(plist, "resultCode");
        if (node == nullptr)
        {
            throw APIError(APIErrorCode::InvalidResponse);
        }
        
        auto type = plist_get_node_type(node);
        switch (type)
        {
            case PLIST_STRING:
            {
                char *value = nullptr;
                plist_get_string_val(node, &value);
                
                resultCode = atoi(value);
            }
                
            case PLIST_UINT:
            {
                uint64_t value = 0;
                plist_get_uint_val(node, &value);
                
                resultCode = (int64_t)value;
                break;
            }
                
            case PLIST_REAL:
            {
                double value = 0;
                plist_get_real_val(node, &value);
                
                resultCode = (int64_t)value;
            }
                
            default:
                break;
        }

        auto error = resultCodeHandler(resultCode);
        if (error.has_value())
        {
            throw error.value();
        }
        
        auto descriptionNode = plist_dict_get_item(plist, "userString") ?: plist_dict_get_item(plist, "resultString");
        
        char *errorDescription = nullptr;
        plist_get_string_val(descriptionNode, &errorDescription);
        
        std::stringstream ss;
        ss << errorDescription << " (" << resultCode << ")";
        
        throw LocalizedError((int)resultCode, ss.str());
    }
}

web::http::client::http_client AppleAPI::authClient()
{
    return this->_authClient;
}

web::http::client::http_client AppleAPI::client()
{
    return this->_client;
}
