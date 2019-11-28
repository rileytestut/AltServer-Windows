//
//  AltSign_Windows.hpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/8/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef AltSign_Windows_
#define AltSign_Windows_

/* The classes below are exported */
#pragma GCC visibility push(default)

#include <cpprest/http_client.h>

#include "Account.hpp"
#include "AppID.hpp"
#include "Certificate.hpp"
#include "Device.hpp"
#include "ProvisioningProfile.hpp"
#include "Team.hpp"
#include "Error.hpp"

class AppleAPI
{
public:
    static AppleAPI *getInstance();
    
    // Authentication
    pplx::task<std::shared_ptr<Account>> Authenticate(std::string appleID, std::string password);
    
    // Teams
    pplx::task<std::vector<std::shared_ptr<Team>>> FetchTeams(std::shared_ptr<Account> account);
    
    // Devices
    pplx::task<std::vector<std::shared_ptr<Device>>> FetchDevices(std::shared_ptr<Team> team);
    pplx::task<std::shared_ptr<Device>> RegisterDevice(std::string name, std::string identifier, std::shared_ptr<Team> team);
    
    // Certificates
    pplx::task<std::vector<std::shared_ptr<Certificate>>> FetchCertificates(std::shared_ptr<Team> team);
    pplx::task<std::shared_ptr<Certificate>> AddCertificate(std::string machineName, std::shared_ptr<Team> team);
    pplx::task<bool> RevokeCertificate(std::shared_ptr<Certificate> certificate, std::shared_ptr<Team> team);
    
    // App IDs
    pplx::task<std::vector<std::shared_ptr<AppID>>> FetchAppIDs(std::shared_ptr<Team> team);
    pplx::task<std::shared_ptr<AppID>> AddAppID(std::string name, std::string bundleIdentifier, std::shared_ptr<Team> team);
    
    // Provisioning Profiles
    pplx::task<std::shared_ptr<ProvisioningProfile>> FetchProvisioningProfile(std::shared_ptr<AppID> appID, std::shared_ptr<Team> team);
    pplx::task<bool> DeleteProvisioningProfile(std::shared_ptr<ProvisioningProfile> profile, std::shared_ptr<Team> team);
    
private:
    AppleAPI();
    
    static AppleAPI *instance_;
    
    web::http::client::http_client _authClient;
    web::http::client::http_client authClient();
    
    web::http::client::http_client _client;
    web::http::client::http_client client();
    
    pplx::task<plist_t> SendRequest(std::string uri,
                                    std::map<std::string, std::string> additionalParameters,
                                    std::shared_ptr<Account> account,
                                    std::shared_ptr<Team> team);
    
    template<typename T>
    T ProcessResponse(plist_t plist, std::function<T(plist_t)> parseHandler, std::function<std::optional<APIError>(int64_t)> resultCodeHandler);
};

#pragma GCC visibility pop
#endif
