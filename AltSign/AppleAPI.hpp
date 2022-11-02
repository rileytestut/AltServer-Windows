//
//  AltSign_Windows.hpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/8/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef AppleAPI_hpp
#define AppleAPI_hpp

/* The classes below are exported */
#pragma GCC visibility push(default)

#include <cpprest/http_client.h>
#include <cpprest/json.h>

#include "Account.hpp"
#include "AppID.hpp"
#include "AppGroup.hpp"
#include "Certificate.hpp"
#include "Device.hpp"
#include "ProvisioningProfile.hpp"
#include "Team.hpp"
#include "Error.hpp"

#include "AppleAPISession.h"

extern std::string StringFromWideString(std::wstring wideString);

class AppleAPI
{
public:
    static AppleAPI *getInstance();
	
	pplx::task<std::pair<std::shared_ptr<Account>, std::shared_ptr<AppleAPISession>>> Authenticate(
		std::string appleID,
		std::string password,
		std::shared_ptr<AnisetteData> anisetteData,
		std::optional<std::function <pplx::task<std::optional<std::string>>(void)>> verificationHandler);
    
    // Teams
	pplx::task<std::vector<std::shared_ptr<Team>>> FetchTeams(std::shared_ptr<Account> account, std::shared_ptr<AppleAPISession> session);
    
    // Devices
    pplx::task<std::vector<std::shared_ptr<Device>>> FetchDevices(std::shared_ptr<Team> team, Device::Type types, std::shared_ptr<AppleAPISession> session);
    pplx::task<std::shared_ptr<Device>> RegisterDevice(std::string name, std::string identifier, Device::Type type, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
    
    // Certificates
    pplx::task<std::vector<std::shared_ptr<Certificate>>> FetchCertificates(std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
    pplx::task<std::shared_ptr<Certificate>> AddCertificate(std::string machineName, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
    pplx::task<bool> RevokeCertificate(std::shared_ptr<Certificate> certificate, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
    
    // App IDs
    pplx::task<std::vector<std::shared_ptr<AppID>>> FetchAppIDs(std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
    pplx::task<std::shared_ptr<AppID>> AddAppID(std::string name, std::string bundleIdentifier, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
	pplx::task<std::shared_ptr<AppID>> UpdateAppID(std::shared_ptr<AppID> appID, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);

	// App Groups
	pplx::task<std::vector<std::shared_ptr<AppGroup>>> FetchAppGroups(std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
	pplx::task<std::shared_ptr<AppGroup>> AddAppGroup(std::string name, std::string groupIdentifier, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
	pplx::task<bool> AssignAppIDToGroups(std::shared_ptr<AppID> appID, std::vector<std::shared_ptr<AppGroup>> groups, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);

    // Provisioning Profiles
    pplx::task<std::shared_ptr<ProvisioningProfile>> FetchProvisioningProfile(std::shared_ptr<AppID> appID, Device::Type deviceType, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
    pplx::task<bool> DeleteProvisioningProfile(std::shared_ptr<ProvisioningProfile> profile, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
    
private:
    AppleAPI();
    
    static AppleAPI *instance_;
    
    web::http::client::http_client _servicesClient;
    web::http::client::http_client servicesClient();

	web::http::client::http_client _gsaClient;
	web::http::client::http_client gsaClient();
    
    web::http::client::http_client _client;
    web::http::client::http_client client();
    
	pplx::task<plist_t> SendRequest(std::string uri,
		std::map<std::string, std::string> additionalParameters,
		std::shared_ptr<AppleAPISession> session,
		std::shared_ptr<Team> team);

	pplx::task<plist_t> SendRequest(std::string uri,
		std::map<std::string, plist_t> additionalParameters,
		std::shared_ptr<AppleAPISession> session,
		std::shared_ptr<Team> team);

	pplx::task<plist_t> SendAuthenticationRequest(std::map<std::string, plist_t> requestParameters,
		std::shared_ptr<AnisetteData> anisetteData);

	pplx::task<web::json::value> SendServicesRequest(std::string uri,
		std::string method,
		std::map<std::string, std::string> requestParameters,
		std::shared_ptr<AppleAPISession> session,
		std::shared_ptr<Team> team);

	pplx::task<std::string> FetchAuthToken(std::map<std::string, plist_t> requestParameters, std::vector<unsigned char> sk, std::shared_ptr<AnisetteData> anisetteData);
	pplx::task<std::shared_ptr<Account>> FetchAccount(std::shared_ptr<AppleAPISession> session);

	pplx::task<bool> RequestTrustedDeviceTwoFactorCode(
		std::string dsid,
		std::string idmsToken,
		std::shared_ptr<AnisetteData> anisetteData,
		const std::function <pplx::task<std::optional<std::string>>(void)>& verificationHandler);

	pplx::task<bool> RequestSMSTwoFactorCode(
		std::string dsid,
		std::string idmsToken,
		std::shared_ptr<AnisetteData> anisetteData,
		const std::function <pplx::task<std::optional<std::string>>(void)>& verificationHandler);

	web::http::http_request MakeTwoFactorCodeRequest(
		std::string url,
		std::string dsid,
		std::string idmsToken,
		std::shared_ptr<AnisetteData> anisetteData);

	template<typename T>
	T ProcessAnyResponse(plist_t plist, std::string errorCodeKey, std::vector<std::string> errorMessageKeys, std::function<T(plist_t)> parseHandler, std::function<std::optional<APIError>(int64_t)> resultCodeHandler)
	{
		try
		{
			try
			{
				auto value = parseHandler(plist);
				plist_free(plist);
				return value;
			}
			catch (std::exception& exception)
			{
				//odslog("Parse exception: " << exception.what());

				int64_t resultCode = 0;

				auto node = plist_dict_get_item(plist, errorCodeKey.c_str());
				if (node == nullptr)
				{
					throw APIError(APIErrorCode::InvalidResponse);
				}

				auto type = plist_get_node_type(node);
				switch (type)
				{
				case PLIST_STRING:
				{
					char* value = nullptr;
					plist_get_string_val(node, &value);

					resultCode = atoi(value);
					break;
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
					break;
				}

				default:
					break;
				}

				auto error = resultCodeHandler(resultCode);
				if (error.has_value())
				{
					throw error.value();
				}

				plist_t descriptionNode = nullptr;
				for (auto& errorMessageKey : errorMessageKeys)
				{
					auto node = plist_dict_get_item(plist, errorMessageKey.c_str());
					if (node == NULL)
					{
						continue;
					}

					descriptionNode = node;
					break;
				}

				char* errorDescription = nullptr;
				plist_get_string_val(descriptionNode, &errorDescription);

				if (errorDescription == nullptr)
				{
					throw APIError(APIErrorCode::InvalidResponse);
				}

				std::stringstream ss;
				ss << errorDescription << " (" << resultCode << ")";

				throw LocalizedAPIError(resultCode, ss.str());
			}
		}
		catch (std::exception& exception)
		{
			plist_free(plist);
			throw;
		}
	}

	template<typename T>
	T ProcessResponse(plist_t plist, std::function<T(plist_t)> parseHandler, std::function<std::optional<APIError>(int64_t)> resultCodeHandler)
	{
		return this->ProcessAnyResponse(plist, "resultCode", { "userString", "resultString" }, parseHandler, resultCodeHandler);
	}

	template<typename T>
	T ProcessTwoFactorResponse(plist_t plist, std::function<T(plist_t)> parseHandler, std::function<std::optional<APIError>(int64_t)> resultCodeHandler)
	{
		return this->ProcessAnyResponse(plist, "ec", {"em"}, parseHandler, resultCodeHandler);
	}

	template<typename T>
	T ProcessServicesResponse(web::json::value json, std::function<T(web::json::value)> parseHandler, std::function<std::optional<APIError>(int64_t)> resultCodeHandler)
	{
		try
		{
			auto value = parseHandler(json);
			return value;
		}
		catch (std::exception& exception)
		{
			int64_t resultCode = 0;
			
			if (json.has_field(L"resultCode"))
			{
				resultCode = json[L"resultCode"].as_integer();
			}
			else if (json.has_field(L"errorCode"))
			{
				resultCode = json[L"errorCode"].as_integer();
			}
			else
			{
				resultCode = -1;
			}

			auto error = resultCodeHandler(resultCode);
			if (error.has_value())
			{
				throw error.value();
			}

			std::string errorDescription;

			if (json.has_field(L"userString"))
			{
				errorDescription = StringFromWideString(json[L"userString"].as_string());
			}
			else if (json.has_field(L"resultString"))
			{
				errorDescription = StringFromWideString(json[L"resultString"].as_string());
			}
			else if (json.has_field(L"errorMessage"))
			{
				errorDescription = StringFromWideString(json[L"errorMessage"].as_string());
			}
			else if (json.has_field(L"errorId"))
			{
				errorDescription = StringFromWideString(json[L"errorId"].as_string());
			}
			else
			{
				errorDescription = "Unknown services response error.";
			}

			std::stringstream ss;
			ss << errorDescription << " (" << resultCode << ")";

			throw LocalizedAPIError(resultCode, ss.str());
		}
	}

};

#pragma GCC visibility pop
#endif
