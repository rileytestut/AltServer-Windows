//
//  AltServerApp.cpp
//  AltServer-Windows
//
//  Created by Riley Testut on 8/30/19.
//  Copyright (c) 2019 Riley Testut. All rights reserved.
//

#include "AltServerApp.h"

#include "AppleAPI.hpp"
#include "ConnectionManager.hpp"
#include "InstallError.hpp"
#include "Signer.hpp"
#include "DeviceManager.hpp"
#include "Archiver.hpp"

#include <cpprest/http_client.h>
#include <cpprest/filestream.h>

#include <plist/plist.h>

#include <WS2tcpip.h>

#pragma comment( lib, "gdiplus.lib" ) 
#include <gdiplus.h> 
#include <strsafe.h>

#include "resource.h"

#define odslog(msg) { std::stringstream ss; ss << msg << std::endl; OutputDebugStringA(ss.str().c_str()); }

using namespace utility;                    // Common utilities like string conversions
using namespace web;                        // Common features like URIs.
using namespace web::http;                  // Common HTTP functionality
using namespace web::http::client;          // HTTP client features
using namespace concurrency::streams;       // Asynchronous streams

extern std::string temporary_directory();
extern std::string make_uuid();
extern std::vector<unsigned char> readFile(const char* filename);

extern std::string StringFromWideString(std::wstring wideString);
extern std::wstring WideStringFromString(std::string string);

const char* REGISTRY_ROOT_KEY = "SOFTWARE\\RileyTestut\\AltServer";
const char* DID_LAUNCH_KEY = "Launched";
const char* LAUNCH_AT_STARTUP_KEY = "LaunchAtStartup";
const char* SERVER_ID_KEY = "ServerID";

const char* STARTUP_ITEMS_KEY = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";

HKEY OpenRegistryKey()
{
	HKEY hKey;
	LONG nError = RegOpenKeyExA(HKEY_CURRENT_USER, REGISTRY_ROOT_KEY, NULL, KEY_ALL_ACCESS, &hKey);

	if (nError == ERROR_FILE_NOT_FOUND)
	{
		nError = RegCreateKeyExA(HKEY_CURRENT_USER, REGISTRY_ROOT_KEY, NULL, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL);
	}

	if (nError)
	{
		odslog("Error finding/creating registry value. " << nError);
	}

	return hKey;
}

void SetRegistryBoolValue(const char *lpValue, bool data)
{
	int32_t value = data ? 1 : 0;

	HKEY rootKey = OpenRegistryKey();
	LONG nError = RegSetValueExA(rootKey, lpValue, NULL, REG_DWORD, (BYTE *)&value, sizeof(int32_t));

	if (nError)
	{
		odslog("Error setting registry value. " << nError);
	}

	RegCloseKey(rootKey);
}

void SetRegistryStringValue(const char* lpValue, std::string string)
{
	HKEY rootKey = OpenRegistryKey();
	LONG nError = RegSetValueExA(rootKey, lpValue, NULL, REG_SZ, (const BYTE *)string.c_str(), string.size() + 1);

	if (nError)
	{
		odslog("Error setting registry value. " << nError);
	}

	RegCloseKey(rootKey);
}

bool GetRegistryBoolValue(const char *lpValue)
{
	HKEY rootKey = OpenRegistryKey();

	int32_t data;
	DWORD size = sizeof(int32_t);
	DWORD type = REG_DWORD;
	LONG nError = RegQueryValueExA(rootKey, lpValue, NULL, &type, (BYTE *)& data, &size);

	if (nError == ERROR_FILE_NOT_FOUND)
	{
		data = 0;
	}
	else if (nError)
	{
		odslog("Could not get registry value. " << nError);
	}

	RegCloseKey(rootKey);

	return (bool)data;
}

std::string GetRegistryStringValue(const char* lpValue)
{
	HKEY rootKey = OpenRegistryKey();

	char value[1024];
	DWORD length = sizeof(value);

	DWORD type = REG_SZ;
	LONG nError = RegQueryValueExA(rootKey, lpValue, NULL, &type, (LPBYTE)& value, &length);

	if (nError == ERROR_FILE_NOT_FOUND)
	{
		value[0] = 0;
	}
	else if (nError)
	{
		odslog("Could not get registry value. " << nError);
	}

	RegCloseKey(rootKey);

	std::string string(value);
	return string;
}

AltServerApp* AltServerApp::_instance = nullptr;

AltServerApp* AltServerApp::instance()
{
	if (_instance == 0)
	{
		_instance = new AltServerApp();
	}

	return _instance;
}

AltServerApp::AltServerApp()
{
}

AltServerApp::~AltServerApp()
{
}

void AltServerApp::Start(HWND windowHandle, HINSTANCE instanceHandle)
{
	_windowHandle = windowHandle;
	_instanceHandle = instanceHandle;

	bool didLaunch = GetRegistryBoolValue(DID_LAUNCH_KEY);
	if (!didLaunch)
	{
		// First launch.

		// Automatically launch at login.
		this->setAutomaticallyLaunchAtLogin(true);

		auto serverID = make_uuid();
		this->setServerID(serverID);

		SetRegistryBoolValue(DID_LAUNCH_KEY, true);
	}

	Gdiplus::GdiplusStartupInput gdiplusStartupInput;
	ULONG_PTR gdiplusToken;
	Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

	ConnectionManager::instance()->Start();

	this->ShowNotification("AltServer Running", "AltServer will continue to run in the background listening for AltStore");
}

pplx::task<void> AltServerApp::InstallAltStore(std::shared_ptr<Device> installDevice, std::string appleID, std::string password)
{
    fs::path destinationDirectoryPath(temporary_directory());
    destinationDirectoryPath.append(make_uuid());
    
	auto account = std::make_shared<Account>();
	auto app = std::make_shared<Application>();
	auto team = std::make_shared<Team>();
	auto device = std::make_shared<Device>();
	auto appID = std::make_shared<AppID>();
	auto certificate = std::make_shared<Certificate>();
	auto profile = std::make_shared<ProvisioningProfile>();

    return this->Authenticate(appleID, password)
    .then([=](std::shared_ptr<Account> tempAccount)
          {
              *account = *tempAccount;

			  std::stringstream ssTitle;
			  ssTitle << "Installing AltStore to " << installDevice->name() << "...";

			  std::stringstream ssMessage;
			  ssMessage << "This may take a few seconds.";

			  this->ShowNotification(ssTitle.str(), ssMessage.str());

              return this->FetchTeam(account);
          })
    .then([=](std::shared_ptr<Team> tempTeam)
          {
              *team = *tempTeam;
              return this->RegisterDevice(installDevice, team);
          })
    .then([=](std::shared_ptr<Device> tempDevice)
          {
              *device = *tempDevice;
              return this->FetchCertificate(team);
          })
    .then([=](std::shared_ptr<Certificate> tempCertificate)
          {
              *certificate = *tempCertificate;
              return this->DownloadApp();
          })
    .then([=](fs::path downloadedAppPath)
          {
              fs::create_directory(destinationDirectoryPath);
              
              auto appBundlePath = UnzipAppBundle(downloadedAppPath.string(), destinationDirectoryPath.string());
              
              auto app = std::make_shared<Application>(appBundlePath);
              return app;
          })
    .then([=](std::shared_ptr<Application> tempApp)
          {
              *app = *tempApp;
              return this->RegisterAppID(app->name(), app->bundleIdentifier(), team);
          })
    .then([=](std::shared_ptr<AppID> tempAppID)
          {
              *appID = *tempAppID;
              return this->FetchProvisioningProfile(appID, team);
          })
    .then([=](std::shared_ptr<ProvisioningProfile> tempProfile)
          {
              *profile = *tempProfile;
              return this->InstallApp(app, device, team, appID, certificate, profile);
          })
    .then([=](pplx::task<void> task)
          {
              fs::remove_all(destinationDirectoryPath);

			  try
			  {
				  task.get();

				  std::stringstream ss;
				  ss << "AltStore was successfully installed on " << installDevice->name() << ".";

				  this->ShowNotification("Installation Succeeded", ss.str());
			  }
			  catch (Error& error)
			  {
				  this->ShowNotification("Installation Failed", error.localizedDescription());
				  throw error;
			  }
			  catch (std::exception& exception)
			  {
				  this->ShowNotification("Installation Failed", exception.what());
				  throw exception;
			  }
          });
}

pplx::task<fs::path> AltServerApp::DownloadApp()
{
    fs::path temporaryPath(temporary_directory());
    temporaryPath.append(make_uuid());
    
    auto outputFile = std::make_shared<ostream>();
    
    // Open stream to output file.
    auto task = fstream::open_ostream(WideStringFromString(temporaryPath.string()))
    .then([=](ostream file)
          {
              *outputFile = file;
              
              uri_builder builder(L"https://www.dropbox.com/s/w1gn9iztlqvltyp/AltStore.ipa?dl=1");
              
              http_client client(builder.to_uri());
              return client.request(methods::GET);
          })
    .then([=](http_response response)
          {
              printf("Received response status code:%u\n", response.status_code());
              
              // Write response body into the file.
              return response.body().read_to_end(outputFile->streambuf());
          })
    .then([=](size_t)
          {
              outputFile->close();
              return temporaryPath;
          });
    
    return task;
}

pplx::task<std::shared_ptr<Account>> AltServerApp::Authenticate(std::string appleID, std::string password)
{
    return AppleAPI::getInstance()->Authenticate(appleID, password);
}

pplx::task<std::shared_ptr<Team>> AltServerApp::FetchTeam(std::shared_ptr<Account> account)
{
    auto task = AppleAPI::getInstance()->FetchTeams(account)
    .then([](std::vector<std::shared_ptr<Team>> teams) {
        if (teams.size() == 0)
        {
            throw InstallError(InstallErrorCode::NoTeam);
        }
        else
        {
            auto team = teams[0];
            return team;
        }
    });
    
    return task;
}

pplx::task<std::shared_ptr<Certificate>> AltServerApp::FetchCertificate(std::shared_ptr<Team> team)
{
    auto task = AppleAPI::getInstance()->FetchCertificates(team)
    .then([this, team](std::vector<std::shared_ptr<Certificate>> certificates)
          {
              if (certificates.size() != 0)
              {
                  auto certificate = certificates[0];
                  return AppleAPI::getInstance()->RevokeCertificate(certificate, team).then([this, team](bool success)
                                                                                            {
                                                                                                return this->FetchCertificate(team);
                                                                                            });
              }
              else
              {
                  std::string machineName = "AltServer";
                  
                  return AppleAPI::getInstance()->AddCertificate(machineName, team).then([team](std::shared_ptr<Certificate> addedCertificate)
                                                                                         {
                                                                                             auto privateKey = addedCertificate->privateKey();
                                                                                             if (privateKey == std::nullopt)
                                                                                             {
                                                                                                 throw InstallError(InstallErrorCode::MissingPrivateKey);
                                                                                             }
                                                                                             
                                                                                             return AppleAPI::getInstance()->FetchCertificates(team)
                                                                                             .then([privateKey, addedCertificate](std::vector<std::shared_ptr<Certificate>> certificates)
                                                                                                   {
                                                                                                       std::shared_ptr<Certificate> certificate = nullptr;
                                                                                                       
                                                                                                       for (auto tempCertificate : certificates)
                                                                                                       {
                                                                                                           if (tempCertificate->serialNumber() == addedCertificate->serialNumber())
                                                                                                           {
                                                                                                               certificate = tempCertificate;
                                                                                                               break;
                                                                                                           }
                                                                                                       }
                                                                                                       
                                                                                                       if (certificate == nullptr)
                                                                                                       {
                                                                                                           throw InstallError(InstallErrorCode::MissingCertificate);
                                                                                                       }
                                                                                                       
                                                                                                       certificate->setPrivateKey(privateKey);
                                                                                                       return certificate;
                                                                                                   });
                                                                                         });
              }
          });
    
    return task;
}

pplx::task<std::shared_ptr<AppID>> AltServerApp::RegisterAppID(std::string appName, std::string identifier, std::shared_ptr<Team> team)
{
    std::stringstream ss;
    ss << "com." << team->identifier() << "." << identifier;
    
    auto bundleID = ss.str();
    
    auto task = AppleAPI::getInstance()->FetchAppIDs(team)
    .then([bundleID, appName, identifier, team](std::vector<std::shared_ptr<AppID>> appIDs)
          {
              std::shared_ptr<AppID> appID = nullptr;
              
              for (auto tempAppID : appIDs)
              {
                  if (tempAppID->bundleIdentifier() == bundleID)
                  {
                      appID = tempAppID;
                      break;
                  }
              }
              
              if (appID != nullptr)
              {
                  return pplx::task<std::shared_ptr<AppID>>([appID]()
                                                            {
                                                                return appID;
                                                            });
              }
              else
              {
                  return AppleAPI::getInstance()->AddAppID(appName, bundleID, team);
              }
          });
    
    return task;
}

pplx::task<std::shared_ptr<Device>> AltServerApp::RegisterDevice(std::shared_ptr<Device> device, std::shared_ptr<Team> team)
{
    auto task = AppleAPI::getInstance()->FetchDevices(team)
    .then([device, team](std::vector<std::shared_ptr<Device>> devices)
          {
              std::shared_ptr<Device> matchingDevice = nullptr;
              
              for (auto tempDevice : devices)
              {
                  if (tempDevice->identifier() == device->identifier())
                  {
                      matchingDevice = tempDevice;
                      break;
                  }
              }
              
              if (matchingDevice != nullptr)
              {
                  return pplx::task<std::shared_ptr<Device>>([matchingDevice]()
                                                             {
                                                                 return matchingDevice;
                                                             });
              }
              else
              {
                  return AppleAPI::getInstance()->RegisterDevice(device->name(), device->identifier(), team);
              }
          });
    
    return task;
}

pplx::task<std::shared_ptr<ProvisioningProfile>> AltServerApp::FetchProvisioningProfile(std::shared_ptr<AppID> appID, std::shared_ptr<Team> team)
{
    return AppleAPI::getInstance()->FetchProvisioningProfile(appID, team);
}

pplx::task<void> AltServerApp::InstallApp(std::shared_ptr<Application> app,
                            std::shared_ptr<Device> device,
                            std::shared_ptr<Team> team,
                            std::shared_ptr<AppID> appID,
                            std::shared_ptr<Certificate> certificate,
                            std::shared_ptr<ProvisioningProfile> profile)
{
    return pplx::task<void>([=]() {
        fs::path infoPlistPath(app->path());
        infoPlistPath.append("Info.plist");
        
        auto data = readFile(infoPlistPath.string().c_str());
        
        plist_t plist = nullptr;
        plist_from_memory((const char *)data.data(), (int)data.size(), &plist);
        if (plist == nullptr)
        {
            throw InstallError(InstallErrorCode::MissingInfoPlist);
        }
        
        plist_dict_set_item(plist, "CFBundleIdentifier", plist_new_string(profile->bundleIdentifier().c_str()));
        plist_dict_set_item(plist, "ALTDeviceID", plist_new_string(device->identifier().c_str()));

		auto serverID = this->serverID();
		plist_dict_set_item(plist, "ALTServerID", plist_new_string(serverID.c_str()));
        
        char *plistXML = nullptr;
        uint32_t length = 0;
        plist_to_xml(plist, &plistXML, &length);
        
        std::ofstream fout(infoPlistPath.string(), std::ios::out | std::ios::binary);
        fout.write(plistXML, length);
        fout.close();
        
        Signer signer(team, certificate);
        signer.SignApp(app->path(), { profile });
        
		return DeviceManager::instance()->InstallApp(app->path(), device->identifier(), [](double progress) {
			odslog("AltStore Installation Progress: " << progress);
		});
    });
}

void AltServerApp::ShowNotification(std::string title, std::string message)
{
	static const wchar_t* filename = L"MenuBarIcon.png";

	Gdiplus::Bitmap* image = Gdiplus::Bitmap::FromFile(filename);
	HICON hicon;
	image->GetHICON(&hicon);

	NOTIFYICONDATA niData;
	ZeroMemory(&niData, sizeof(NOTIFYICONDATA));
	niData.uVersion = NOTIFYICON_VERSION_4;
	niData.cbSize = sizeof(NOTIFYICONDATA);
	niData.uID = 10456;
	niData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_INFO | NIF_TIP | NIF_GUID;
	niData.hWnd = this->windowHandle();
	niData.hIcon = hicon;
	niData.uCallbackMessage = WM_USER + 1;
	niData.uTimeout = 3000;
	niData.dwInfoFlags = NIIF_INFO;
	StringCchCopy(niData.szInfoTitle, ARRAYSIZE(niData.szInfoTitle), WideStringFromString(title).c_str());
	StringCchCopy(niData.szInfo, ARRAYSIZE(niData.szInfo), WideStringFromString(message).c_str());

	//TODO: Load correct variant
	HICON icon = (HICON)LoadImage(this->instanceHandle(), MAKEINTRESOURCE(IMG_MENUICON), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);

	if (!_presentedNotification)
	{
		Shell_NotifyIcon(NIM_ADD, &niData);
	}
	else
	{
		Shell_NotifyIcon(NIM_MODIFY, &niData);
	}

	_presentedNotification = true;
	
}

HWND AltServerApp::windowHandle() const
{
	return _windowHandle;
}

HINSTANCE AltServerApp::instanceHandle() const
{
	return _instanceHandle;
}

bool AltServerApp::automaticallyLaunchAtLogin() const
{
	auto value = GetRegistryBoolValue(LAUNCH_AT_STARTUP_KEY);
	return value;
}

void AltServerApp::setAutomaticallyLaunchAtLogin(bool launch)
{
	SetRegistryBoolValue(LAUNCH_AT_STARTUP_KEY, launch);

	HKEY hKey;
	long result = RegOpenKeyExA(HKEY_CURRENT_USER, STARTUP_ITEMS_KEY, 0, KEY_WRITE, &hKey);
	if (result != ERROR_SUCCESS)
	{
		return;
	}

	if (launch)
	{
		char executablePath[MAX_PATH + 1];
		GetModuleFileNameA(NULL, executablePath, MAX_PATH + 1);

		int length = strlen((const char*)executablePath);
		result = RegSetValueExA(hKey, "AltServer", 0, REG_SZ, (const BYTE*)executablePath, length + 1); // Must include NULL-character in size.
	}
	else
	{
		RegDeleteValueA(hKey, "AltServer");
	}

	RegCloseKey(hKey);
}

std::string AltServerApp::serverID() const
{
	auto serverID = GetRegistryStringValue(SERVER_ID_KEY);
	return serverID;
}

void AltServerApp::setServerID(std::string serverID)
{
	SetRegistryStringValue(SERVER_ID_KEY, serverID);
}