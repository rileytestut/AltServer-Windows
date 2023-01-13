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

#include "AppleAPISession.h"
#include "AnisetteDataManager.h"

#include "DeveloperDiskManager.h"

#include "Semaphore.h"

#include "InstalledApp.h"

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
	void Stop();
	void CheckForUpdates();
    
	pplx::task<std::shared_ptr<Application>> InstallApplication(std::optional<std::string> filepath, std::shared_ptr<Device> device, std::string appleID, std::string password);
	pplx::task<void> PrepareDevice(std::shared_ptr<Device> device);
	pplx::task<void> EnableJIT(InstalledApp app, std::shared_ptr<Device> device);

	void ShowNotification(std::string title, std::string message);
	void ShowAlert(std::string title, std::string message);
    void ShowErrorAlert(std::exception& exception, std::string localizedFailure);

	HWND windowHandle() const;
	HINSTANCE instanceHandle() const;

	bool automaticallyLaunchAtLogin() const;
	void setAutomaticallyLaunchAtLogin(bool launch);

	std::string serverID() const;
	void setServerID(std::string serverID);

	bool reprovisionedDevice() const;
	void setReprovisionedDevice(bool reprovisionedDevice);

	std::string appleFolderPath() const;
	std::string internetServicesFolderPath() const;
	std::string applicationSupportFolderPath() const;

	fs::path appDataDirectoryPath() const;
	fs::path certificatesDirectoryPath() const;
	fs::path developerDisksDirectoryPath() const;

	bool boolValueForRegistryKey(std::string key) const;
	void setBoolValueForRegistryKey(bool value, std::string key);

private:
	AltServerApp();
	~AltServerApp();

	static AltServerApp *_instance;

	pplx::task<std::shared_ptr<Application>> _InstallApplication(std::optional<std::string> filepath, std::shared_ptr<Device> installDevice, std::string appleID, std::string password);

	bool CheckDependencies();
	bool CheckiCloudDependencies();

	std::string BrowseForFolder(std::wstring title, std::string folderPath);

	bool _presentedNotification;
	GUID _notificationIconGUID;

	HWND _windowHandle;
	HINSTANCE _instanceHandle;

	Semaphore _appGroupSemaphore;

	DeveloperDiskManager _developerDiskManager;

	bool presentedRunningNotification() const;
	void setPresentedRunningNotification(bool presentedRunningNotification);

	void setAppleFolderPath(std::string appleFolderPath);
	std::string defaultAppleFolderPath() const;

	void HandleAnisetteError(AnisetteError& error);
    
    pplx::task<fs::path> DownloadApp();

	void ShowInstallationNotification(std::string appName, std::string deviceName);
    
	pplx::task<std::pair<std::shared_ptr<Account>, std::shared_ptr<AppleAPISession>>>  Authenticate(std::string appleID, std::string password, std::shared_ptr<AnisetteData> anisetteData);
    pplx::task<std::shared_ptr<Team>> FetchTeam(std::shared_ptr<Account> account, std::shared_ptr<AppleAPISession> session);
    pplx::task<std::shared_ptr<Certificate>> FetchCertificate(std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
	pplx::task<std::map<std::string, std::shared_ptr<ProvisioningProfile>>> PrepareAllProvisioningProfiles(
		std::shared_ptr<Application> application,
		std::shared_ptr<Device> device,
		std::shared_ptr<Team> team,
		std::shared_ptr<AppleAPISession> session);
	pplx::task<std::shared_ptr<ProvisioningProfile>> PrepareProvisioningProfile(
		std::shared_ptr<Application> application,
		std::optional<std::shared_ptr<Application>> parentApp,
		std::shared_ptr<Device> device,
		std::shared_ptr<Team> team,
		std::shared_ptr<AppleAPISession> session);
    pplx::task<std::shared_ptr<AppID>> RegisterAppID(std::string appName, std::string identifier, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
	pplx::task<std::shared_ptr<AppID>> UpdateAppIDFeatures(std::shared_ptr<AppID> appID, std::shared_ptr<Application> app, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
	pplx::task<std::shared_ptr<AppID>> UpdateAppIDAppGroups(std::shared_ptr<AppID> appID, std::shared_ptr<Application> app, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
    pplx::task<std::shared_ptr<Device>> RegisterDevice(std::shared_ptr<Device> device, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
    pplx::task<std::shared_ptr<ProvisioningProfile>> FetchProvisioningProfile(std::shared_ptr<AppID> appID, std::shared_ptr<Device> device, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session);
    
	pplx::task<std::shared_ptr<Application>> InstallApp(std::shared_ptr<Application> app,
		std::shared_ptr<Device> device,
		std::shared_ptr<Team> team,
		std::shared_ptr<Certificate> certificate,
		std::map<std::string, std::shared_ptr<ProvisioningProfile>> profiles);
};
