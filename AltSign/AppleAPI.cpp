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

#include <openssl/pem.h>
#include <openssl/pkcs12.h>

#include <WS2tcpip.h>

#include "AppleAPISession.h"
#include "AnisetteData.h"

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

extern std::string StringFromWideString(std::wstring wideString);
extern std::wstring WideStringFromString(std::string string);

#define odslog(msg) { std::stringstream ss; ss << msg << std::endl; OutputDebugStringA(ss.str().c_str()); }

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

AppleAPI::AppleAPI() : _servicesClient(U("https://developerservices2.apple.com/services/v1")), _client(U("https://developerservices2.apple.com/services/QH65B2")), _gsaClient(U("https://gsa.apple.com"))
{
	http_client_config config;
	config.set_validate_certificates(false);

	_gsaClient = web::http::client::http_client(U("https://gsa.apple.com"), config);

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

	OpenSSL_add_all_algorithms();
}

#pragma mark - Teams -

pplx::task<std::vector<std::shared_ptr<Team>>> AppleAPI::FetchTeams(std::shared_ptr<Account> account, std::shared_ptr<AppleAPISession> session)
{
	std::map<std::string, std::string> parameters = {};
    auto task = this->SendRequest("listTeams.action", parameters, session, nullptr)
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

pplx::task<vector<shared_ptr<Device>>> AppleAPI::FetchDevices(shared_ptr<Team> team, Device::Type types, std::shared_ptr<AppleAPISession> session)
{
	std::map<std::string, std::string> parameters = {};
    auto task = this->SendRequest("ios/listDevices.action", parameters, session, team)
    .then([=](plist_t plist)
          {
              auto devices = this->ProcessResponse<vector<shared_ptr<Device>>>(plist, [types](auto plist)
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

																							 if ((types & device->type()) != device->type())
																							 {
																								 // Device type doesn't match the ones we requested, so ignore it.
																								 continue;
																							 }

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

pplx::task<shared_ptr<Device>> AppleAPI::RegisterDevice(string name, string identifier, Device::Type type, shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
    map<string, string> parameters = {
        {"name", name},
        {"deviceNumber", identifier}
    };

	switch (type)
	{
	case Device::Type::iPhone:
	case Device::Type::iPad:
		parameters["DTDK_Platform"] = "ios";
		break;

	case Device::Type::AppleTV:
		parameters["DTDK_Platform"] = "tvos";
		parameters["subPlatform"] = "tvOS";
		break;

	default: break;
	}
    
    auto task = this->SendRequest("ios/addDevice.action", parameters, session, team)
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

pplx::task<std::vector<std::shared_ptr<Certificate>>> AppleAPI::FetchCertificates(std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
	auto task = this->SendServicesRequest("certificates", "GET", {std::make_pair("filter[certificateType]", "IOS_DEVELOPMENT")}, session, team)
    .then([=](web::json::value json)
          {
              auto certificates = this->ProcessServicesResponse<vector<shared_ptr<Certificate>>>(json, [](web::json::value json) -> vector<shared_ptr<Certificate>> 
				  {
					  if (!json.has_field(L"data"))
					  {
						  throw APIError(APIErrorCode::InvalidResponse);
					  }

					  vector<shared_ptr<Certificate>> certificates;

					  auto jsonArray = json[L"data"].as_array();

					  for (auto json : jsonArray)
					  {
						  auto certificate = make_shared<Certificate>(json);
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

pplx::task<std::shared_ptr<Certificate>> AppleAPI::AddCertificate(std::string machineName, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
    CertificateRequest request;

	string encodedCSR;

	for (int i = 0; i < request.data().size(); i++)
	{
		encodedCSR += request.data()[i];
	}
        
    map<string, string> parameters = {
        { "csrContent", encodedCSR },
        { "machineId", make_uuid() },
        { "machineName", machineName }
    };

    auto task = this->SendRequest("ios/submitDevelopmentCSR.action", parameters, session, team)
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

pplx::task<bool> AppleAPI::RevokeCertificate(std::shared_ptr<Certificate> certificate, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
	std::ostringstream ss;
	ss << "certificates/" << *(certificate->identifier());

	auto task = this->SendServicesRequest(ss.str(), "DELETE", {}, session, team)
    .then([=](web::json::value json)
          {
			auto success = this->ProcessServicesResponse<bool>(json, [](auto json)
				{
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

pplx::task<std::vector<std::shared_ptr<AppID>>> AppleAPI::FetchAppIDs(std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
	std::map<std::string, std::string> parameters = {};
    auto task = this->SendRequest("ios/listAppIds.action", parameters, session, team)
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

pplx::task<std::shared_ptr<AppID>> AppleAPI::AddAppID(std::string name, std::string bundleIdentifier, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
    map<string, string> parameters = {
        { "name", name },
        { "identifier", bundleIdentifier },
    };
    
    auto task = this->SendRequest("ios/addAppId.action", parameters, session, team)
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

pplx::task<std::shared_ptr<AppID>> AppleAPI::UpdateAppID(std::shared_ptr<AppID> appID, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
	map<string, plist_t> parameters = {
		{ "appIdId", plist_new_string(appID->identifier().c_str()) },
	};

	for (auto& feature : appID->features())
	{
		parameters[feature.first] = feature.second;
	}

	auto task = this->SendRequest("ios/updateAppId.action", parameters, session, team)
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

						case 9100:
							return std::make_optional<APIError>(APIErrorCode::AppIDDoesNotExist);

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

#pragma mark - App Groups -

pplx::task<std::vector<std::shared_ptr<AppGroup>>> AppleAPI::FetchAppGroups(std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
	map<string, string> additionalParameters = {};
	auto task = this->SendRequest("ios/listApplicationGroups.action", additionalParameters, session, team)
		.then([=](plist_t plist)
			{
				auto groups = this->ProcessResponse<vector<shared_ptr<AppGroup>>>(plist, [](auto plist)
					{
						auto node = plist_dict_get_item(plist, "applicationGroupList");
						if (node == nullptr)
						{
							throw APIError(APIErrorCode::InvalidResponse);
						}

						vector<shared_ptr<AppGroup>> groups;

						int size = plist_array_get_size(node);
						for (int i = 0; i < size; i++)
						{
							plist_t plist = plist_array_get_item(node, i);

							auto group = make_shared<AppGroup>(plist);
							groups.push_back(group);
						}

						return groups;

					}, [=](auto resultCode) -> optional<APIError>
					{
						return nullopt;
					});
				return groups;
			});

	return task;
}

pplx::task<std::shared_ptr<AppGroup>> AppleAPI::AddAppGroup(std::string name, std::string groupIdentifier, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
	map<string, string> parameters = {
		{ "name", name },
		{ "identifier", groupIdentifier },
	};

	auto task = this->SendRequest("ios/addApplicationGroup.action", parameters, session, team)
		.then([=](plist_t plist)
			{
				auto group = this->ProcessResponse<shared_ptr<AppGroup>>(plist, [](auto plist)
					{
						auto node = plist_dict_get_item(plist, "applicationGroup");
						if (node == nullptr)
						{
							throw APIError(APIErrorCode::InvalidResponse);
						}

						auto group = make_shared<AppGroup>(node);
						return group;

					}, [=](auto resultCode) -> optional<APIError>
					{
						switch (resultCode)
						{
						case 35:
							return std::make_optional<APIError>(APIErrorCode::InvalidAppGroup);

						default:
							return std::nullopt;
						}
					});
				return group;
			});

	return task;
}

pplx::task<bool> AppleAPI::AssignAppIDToGroups(std::shared_ptr<AppID> appID, std::vector<std::shared_ptr<AppGroup>> groups, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
	map<string, plist_t> parameters = {
		{ "appIdId", plist_new_string(appID->identifier().c_str()) }
	};

	plist_t groupIDs = plist_new_array();
	for (auto& group : groups)
	{
		auto node = plist_new_string(group->identifier().c_str());
		plist_array_append_item(groupIDs, node);
	}

	parameters["applicationGroups"] = groupIDs;

	auto task = this->SendRequest("ios/assignApplicationGroupToAppId.action", parameters, session, team)
		.then([=](plist_t plist)
			{
				auto success = this->ProcessResponse<bool>(plist, [](auto plist)
					{
						auto node = plist_dict_get_item(plist, "resultCode");
						if (node == nullptr)
						{
							throw APIError(APIErrorCode::InvalidResponse);
						}

						uint64_t resultCode = 0;
						plist_get_uint_val(node, &resultCode);

						if (resultCode != 0)
						{
							// Need to throw an exception for resultCodeHandler to be called.
							throw APIError(APIErrorCode::InvalidAppGroup);
						}

						return true;

					}, [=](auto resultCode) -> std::optional<APIError>
					{
						switch (resultCode)
						{
						case 9115:
							return std::make_optional<APIError>(APIErrorCode::AppIDDoesNotExist);

						case 35:
							return std::make_optional<APIError>(APIErrorCode::AppGroupDoesNotExist);

						default:
							return std::nullopt;
						}
					});
				return success;
			});

	return task;
}

#pragma mark - Provisioning Profiles -

pplx::task<std::shared_ptr<ProvisioningProfile>> AppleAPI::FetchProvisioningProfile(std::shared_ptr<AppID> appID, Device::Type deviceType, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
    map<string, string> parameters = {
        { "appIdId", appID->identifier() },
    };

	switch (deviceType)
	{
	case Device::Type::iPhone:
	case Device::Type::iPad:
		parameters["DTDK_Platform"] = "ios";
		break;

	case Device::Type::AppleTV:
		parameters["DTDK_Platform"] = "tvos";
		parameters["subPlatform"] = "tvOS";
		break;

	default: break;
	}
    
    auto task = this->SendRequest("ios/downloadTeamProvisioningProfile.action", parameters, session, team)
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

pplx::task<bool> AppleAPI::DeleteProvisioningProfile(std::shared_ptr<ProvisioningProfile> profile, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
    if (!profile->identifier().has_value())
    {
        throw APIError(APIErrorCode::InvalidProvisioningProfileIdentifier);
    }
    
    map<string, string> parameters = {
        { "provisioningProfileId", *(profile->identifier()) },
        { "teamId", team->identifier() }
    };
    
    auto task = this->SendRequest("ios/deleteProvisioningProfile.action", parameters, session, team)
    .then([=](plist_t plist)
          {
              auto success = this->ProcessResponse<bool>(plist, [](auto plist)
                                                         {
                                                             auto node = plist_dict_get_item(plist, "resultCode");
                                                             if (node == nullptr)
                                                             {
                                                                 throw APIError(APIErrorCode::InvalidResponse);
                                                             }

															 uint64_t resultCode = 0;
															 plist_get_uint_val(node, &resultCode);

															 if (resultCode != 0)
															 {
																 // Need to throw an exception for resultCodeHandler to be called.
																 throw APIError(APIErrorCode::InvalidProvisioningProfileIdentifier);
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
	std::shared_ptr<AppleAPISession> session,
	std::shared_ptr<Team> team)
{
	std::map<std::string, plist_t> parameters;

	for (auto& parameter : additionalParameters)
	{
		plist_t stringNode = plist_new_string(parameter.second.c_str());
		parameters[parameter.first] = stringNode;
	}

	return this->SendRequest(uri, parameters, session, team);
}

pplx::task<plist_t> AppleAPI::SendRequest(std::string uri,
	std::map<std::string, plist_t> additionalParameters,
	std::shared_ptr<AppleAPISession> session,
	std::shared_ptr<Team> team)
{
	std::string requestID(make_uuid());

	std::map<std::string, plist_t> parameters = {
		{ "clientId", plist_new_string(kClientID.c_str()) },
		{ "protocolVersion", plist_new_string(kProtocolVersion.c_str()) },
		{ "requestId", plist_new_string(requestID.c_str()) },
	};

	auto plist = plist_new_dict();
	for (auto& parameter : parameters)
	{
		plist_dict_set_item(plist, parameter.first.c_str(), parameter.second);
	}

	for (auto& parameter : additionalParameters)
	{
		plist_dict_set_item(plist, parameter.first.c_str(), plist_copy(parameter.second));
	}

	if (team != nullptr)
	{
		plist_dict_set_item(plist, "teamId", plist_new_string(team->identifier().c_str()));
	}

	char* plistXML = nullptr;
	uint32_t length = 0;
	plist_to_xml(plist, &plistXML, &length);

	auto wideURI = WideStringFromString(uri);
	auto wideClientID = WideStringFromString(kClientID);

	auto encodedURI = web::uri::encode_uri(wideURI);
	uri_builder builder(encodedURI);

	http_request request(methods::POST);
	request.set_request_uri(builder.to_string());
	request.set_body(plistXML);

	time_t time;
	struct tm* tm;
	char dateString[64];

	time = session->anisetteData()->date().tv_sec;
	tm = localtime(&time);

	strftime(dateString, sizeof dateString, "%FT%T%z", tm);

	std::map<utility::string_t, utility::string_t> headers = {
		{L"Content-Type", L"text/x-xml-plist"},
		{L"User-Agent", L"Xcode"},
		{L"Accept", L"text/x-xml-plist"},
		{L"Accept-Language", L"en-us"},
		{L"X-Apple-App-Info", L"com.apple.gs.xcode.auth"},
		{L"X-Xcode-Version", L"11.2 (11B41)"},

		{L"X-Apple-I-Identity-Id", WideStringFromString(session->dsid()) },
		{L"X-Apple-GS-Token", WideStringFromString(session->authToken()) },
		{L"X-Apple-I-MD-M", WideStringFromString(session->anisetteData()->machineID()) },
		{L"X-Apple-I-MD", WideStringFromString(session->anisetteData()->oneTimePassword()) },
		{L"X-Apple-I-MD-LU", WideStringFromString(session->anisetteData()->localUserID()) },
		{L"X-Apple-I-MD-RINFO", WideStringFromString(std::to_string(session->anisetteData()->routingInfo())) },
		{L"X-Mme-Device-Id", WideStringFromString(session->anisetteData()->deviceUniqueIdentifier()) },
		{L"X-Mme-Client-Info", WideStringFromString(session->anisetteData()->deviceDescription()) },
		{L"X-Apple-I-Client-Time", WideStringFromString(dateString) },
		{L"X-Apple-Locale", WideStringFromString(session->anisetteData()->locale()) },
		{L"X-Apple-I-TimeZone", WideStringFromString(session->anisetteData()->timeZone()) },
	};

	for (auto& pair : headers)
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
				odslog("Received response status code: " << response.status_code());
				return response.extract_vector();
			})
				.then([=](std::vector<unsigned char> compressedData)
					{
						std::vector<uint8_t> decompressedData;

						if (compressedData.size() > 2 && compressedData[0] == '<' && compressedData[1] == '?')
						{
							// Already decompressed
							decompressedData = compressedData;
						}
						else
						{
							decompress((const uint8_t*)compressedData.data(), (size_t)compressedData.size(), decompressedData);
						}

						std::string decompressedXML = std::string(decompressedData.begin(), decompressedData.end());

						plist_t plist = nullptr;
						plist_from_xml(decompressedXML.c_str(), (int)decompressedXML.size(), &plist);

						if (plist == nullptr)
						{
							throw APIError(APIErrorCode::InvalidResponse);
						}

						return plist;
					});

			free(plistXML);
			plist_free(plist);

			return task;
}

pplx::task<json::value> AppleAPI::SendServicesRequest(std::string uri,
	std::string method,
	std::map<std::string, std::string> requestParameters,
	std::shared_ptr<AppleAPISession> session,
	std::shared_ptr<Team> team)
{
	auto encodedParametersURI = web::uri::encode_uri(L"");
	uri_builder parametersBuilder(encodedParametersURI);
	parametersBuilder.append_query(L"teamId", WideStringFromString(team->identifier()), true);

	for (auto pair : requestParameters)
	{
		parametersBuilder.append_query(WideStringFromString(pair.first), WideStringFromString(pair.second), true);
	}

	auto query = parametersBuilder.query();

	auto json = web::json::value::object();
	json[L"urlEncodedQueryParams"] = web::json::value::string(query);

	utility::stringstream_t stream;
	json.serialize(stream);

	auto jsonString = StringFromWideString(stream.str());

	auto wideURI = WideStringFromString(uri);
	auto encodedURI = web::uri::encode_uri(wideURI);
	uri_builder builder(encodedURI);

	http_request request(methods::POST);
	request.set_request_uri(builder.to_string());
	request.set_body(jsonString);

	time_t time;
	struct tm* tm;
	char dateString[64];

	time = session->anisetteData()->date().tv_sec;
	tm = localtime(&time);

	strftime(dateString, sizeof dateString, "%FT%T%z", tm);

	std::map<utility::string_t, utility::string_t> headers = {
		{L"Content-Type", L"application/vnd.api+json"},
		{L"User-Agent", L"Xcode"},
		{L"Accept", L"application/vnd.api+json"},
		{L"Accept-Language", L"en-us"},
		{L"X-Apple-App-Info", L"com.apple.gs.xcode.auth"},
		{L"X-Xcode-Version", L"11.2 (11B41)"},
		{L"X-HTTP-Method-Override", WideStringFromString(method) },

		{L"X-Apple-I-Identity-Id", WideStringFromString(session->dsid()) },
		{L"X-Apple-GS-Token", WideStringFromString(session->authToken()) },
		{L"X-Apple-I-MD-M", WideStringFromString(session->anisetteData()->machineID()) },
		{L"X-Apple-I-MD", WideStringFromString(session->anisetteData()->oneTimePassword()) },
		{L"X-Apple-I-MD-LU", WideStringFromString(session->anisetteData()->localUserID()) },
		{L"X-Apple-I-MD-RINFO", WideStringFromString(std::to_string(session->anisetteData()->routingInfo())) },
		{L"X-Mme-Device-Id", WideStringFromString(session->anisetteData()->deviceUniqueIdentifier()) },
		{L"X-Mme-Client-Info", WideStringFromString(session->anisetteData()->deviceDescription()) },
		{L"X-Apple-I-Client-Time", WideStringFromString(dateString) },
		{L"X-Apple-Locale", WideStringFromString(session->anisetteData()->locale()) },
		{L"X-Apple-I-TimeZone", WideStringFromString(session->anisetteData()->timeZone()) },
	};

	for (auto& pair : headers)
	{
		if (request.headers().has(pair.first))
		{
			request.headers().remove(pair.first);
		}

		request.headers().add(pair.first, pair.second);
	}

	auto task = this->servicesClient().request(request)
		.then([=](http_response response)
			{
				return response.content_ready();
			})
		.then([=](http_response response)
			{
				odslog("Received response status code: " << response.status_code());
				return response.extract_vector();
			})
				.then([=](std::vector<unsigned char> decompressedData)
					{
						std::string decompressedJSON = std::string(decompressedData.begin(), decompressedData.end());

						if (decompressedJSON.size() == 0)
						{
							return json::value::object();
						}

						utility::stringstream_t s;
						s << WideStringFromString(decompressedJSON);

						auto json = json::value::parse(s);
						return json;
					});

			return task;
}

web::http::client::http_client AppleAPI::servicesClient()
{
    return this->_servicesClient;
}

web::http::client::http_client AppleAPI::client()
{
    return this->_client;
}

web::http::client::http_client AppleAPI::gsaClient()
{
	return this->_gsaClient;
}
