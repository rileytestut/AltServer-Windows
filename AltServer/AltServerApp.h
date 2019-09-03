//
//  AltServerApp.hpp
//  AltServer-Windows
//
//  Created by Riley Testut on 8/30/19.
//  Copyright (c) 2019 Riley Testut. All rights reserved.
//

#pragma once

#include <string>

#include "Account.hpp"
#include "AppID.hpp"
#include "Application.hpp"
#include "Certificate.hpp"
#include "Device.hpp"
#include "ProvisioningProfile.hpp"
#include "Team.hpp"

#include <pplx/pplxtasks.h>

#ifdef _WIN32
#include <filesystem>
#undef _WINSOCKAPI_
#define _WINSOCKAPI_  /* prevents <winsock.h> inclusion by <windows.h> */
#include <windows.h>
namespace fs = std::filesystem;
#else
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#endif

class AltServerApp
{
public:
	static AltServerApp *instance();

	void Start(HWND windowHandle, HINSTANCE instanceHandle);
    
    pplx::task<void> InstallAltStore(std::shared_ptr<Device> device, std::string appleID, std::string password);

	void ShowNotification(std::string title, std::string message);

	HWND windowHandle() const;
	HINSTANCE instanceHandle() const;

	bool automaticallyLaunchAtLogin() const;
	void setAutomaticallyLaunchAtLogin(bool launch);
private:
	AltServerApp();
	~AltServerApp();

	static AltServerApp *_instance;

	bool _presentedNotification;

	HWND _windowHandle;
	HINSTANCE _instanceHandle;
    
    pplx::task<fs::path> DownloadApp();
    
    pplx::task<std::shared_ptr<Account>> Authenticate(std::string appleID, std::string password);
    pplx::task<std::shared_ptr<Team>> FetchTeam(std::shared_ptr<Account> account);
    pplx::task<std::shared_ptr<Certificate>> FetchCertificate(std::shared_ptr<Team> team);
    pplx::task<std::shared_ptr<AppID>> RegisterAppID(std::string appName, std::string identifier, std::shared_ptr<Team> team);
    pplx::task<std::shared_ptr<Device>> RegisterDevice(std::shared_ptr<Device> device, std::shared_ptr<Team> team);
    pplx::task<std::shared_ptr<ProvisioningProfile>> FetchProvisioningProfile(std::shared_ptr<AppID> appID, std::shared_ptr<Team> team);
    
    pplx::task<void> InstallApp(std::shared_ptr<Application> app,
                                std::shared_ptr<Device> device,
                                std::shared_ptr<Team> team,
                                std::shared_ptr<AppID> appID,
                                std::shared_ptr<Certificate> certificate,
                                std::shared_ptr<ProvisioningProfile> profile);
};
