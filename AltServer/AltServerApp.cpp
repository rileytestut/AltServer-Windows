//
//  AltServerApp.cpp
//  AltServer-Windows
//
//  Created by Riley Testut on 8/30/19.
//  Copyright (c) 2019 Riley Testut. All rights reserved.
//

#include "AltServerApp.h"
#include <windows.h>
#include <windowsx.h>
#include <strsafe.h>

#include "AppleAPI.hpp"
#include "ConnectionManager.hpp"
#include "InstallError.hpp"
#include "Signer.hpp"
#include "DeviceManager.hpp"
#include "Archiver.hpp"
#include "ServerError.hpp"

#include "AnisetteDataManager.h"

#include <cpprest/http_client.h>
#include <cpprest/filestream.h>

#include <filesystem>

#include <plist/plist.h>

#include <WS2tcpip.h>
#include <ShlObj_core.h>

#pragma comment( lib, "gdiplus.lib" ) 
#include <gdiplus.h> 
#include <strsafe.h>

#include "resource.h"

#include <winsparkle.h>

#define odslog(msg) { std::stringstream ss; ss << msg << std::endl; OutputDebugStringA(ss.str().c_str()); }

using namespace utility;                    // Common utilities like string conversions
using namespace web;                        // Common features like URIs.
using namespace web::http;                  // Common HTTP functionality
using namespace web::http::client;          // HTTP client features
using namespace concurrency::streams;       // Asynchronous streams

namespace fs = std::filesystem;

extern std::string temporary_directory();
extern std::string make_uuid();
extern std::vector<unsigned char> readFile(const char* filename);

extern std::string StringFromWideString(std::wstring wideString);
extern std::wstring WideStringFromString(std::string string);

const char* REGISTRY_ROOT_KEY = "SOFTWARE\\RileyTestut\\AltServer";
const char* DID_LAUNCH_KEY = "Launched";
const char* LAUNCH_AT_STARTUP_KEY = "LaunchAtStartup";
const char* PRESENTED_RUNNING_NOTIFICATION_KEY = "PresentedRunningNotification";
const char* SERVER_ID_KEY = "ServerID";
const char* REPROVISIONED_DEVICE_KEY = "ReprovisionedDevice";

const char* STARTUP_ITEMS_KEY = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";

std::string _verificationCode;

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

BOOL CALLBACK TwoFactorDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	HWND verificationCodeTextField = GetDlgItem(hwnd, IDC_EDIT1);
	HWND submitButton = GetDlgItem(hwnd, IDOK);

	switch (Message)
	{
	case WM_INITDIALOG:
	{
		Edit_SetCueBannerText(verificationCodeTextField, L"123456");
		Button_Enable(submitButton, false);

		break;
	}

	case WM_CTLCOLORSTATIC:
	{
		if (GetDlgCtrlID((HWND)lParam) == IDC_DESCRIPTION)
		{
			HBRUSH success = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
			SetBkMode((HDC)wParam, TRANSPARENT);
			return (BOOL)success;
		}

		break;
	}

	case WM_COMMAND:
		switch (HIWORD(wParam))
		{
		case EN_CHANGE:
		{
			/*PostMessage(hWnd, WM_CLOSE, 0, 0);
			break;*/

			int codeLength = Edit_GetTextLength(verificationCodeTextField);
			if (codeLength == 6)
			{
				Button_Enable(submitButton, true);
			}
			else
			{
				Button_Enable(submitButton, false);
			}

			break;
		}
		}

		switch (LOWORD(wParam))
		{
		case IDOK:
		{
			wchar_t verificationCode[512];
			Edit_GetText(verificationCodeTextField, verificationCode, 512);

			odslog("Verification Code:" << verificationCode);

			_verificationCode = StringFromWideString(verificationCode);

			EndDialog(hwnd, IDOK);

			break;
		}

		case IDCANCEL:
			EndDialog(hwnd, IDCANCEL);
			break;
		}

	default:
		return FALSE;
	}
	return TRUE;
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

	win_sparkle_set_appcast_url("https://altstore.io/altserver/sparkle-windows.xml");
	win_sparkle_init();

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

	if (!this->CheckDependencies())
	{
		this->ShowNotification("iTunes Not Installed", "iTunes must be installed from Apple's website (not the Microsoft Store) in order to use AltStore.");
	}
	else if (!this->CheckiCloudDependencies())
	{
		this->ShowNotification("iCloud Not Installed", "iCloud must be installed from Apple's website (not the Microsoft Store) in order to use AltStore.");
	}
	else if (!AnisetteDataManager::instance()->LoadDependencies())
	{
		this->ShowNotification("Missing Dependencies", "The latest versions of iCloud and iTunes must be installed from Apple's website (not the Microsoft Store) in order to use AltStore.");
	}
	else
	{
		if (!this->presentedRunningNotification())
		{
			this->ShowNotification("AltServer Running", "AltServer will continue to run in the background listening for AltStore.");
			this->setPresentedRunningNotification(true);
		}
		else
		{
			// Make AltServer appear in notification area.
			this->ShowNotification("", "");
		}
	}
}

void AltServerApp::Stop()
{
	win_sparkle_cleanup();
}

void AltServerApp::CheckForUpdates()
{
	win_sparkle_check_update_with_ui();
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

	auto session = std::make_shared<AppleAPISession>();
	auto anisetteData = AnisetteDataManager::instance()->FetchAnisetteData();

    return this->Authenticate(appleID, password, anisetteData)
    .then([=](std::pair<std::shared_ptr<Account>, std::shared_ptr<AppleAPISession>> pair)
          {
              *account = *(pair.first);
			  *session = *(pair.second);

			  odslog("Fetching team...");

              return this->FetchTeam(account, session);
          })
    .then([=](std::shared_ptr<Team> tempTeam)
          {
			odslog("Registering device...");

              *team = *tempTeam;
              return this->RegisterDevice(installDevice, team, session);
          })
    .then([=](std::shared_ptr<Device> tempDevice)
          {
			odslog("Fetching certificate...");

              *device = *tempDevice;
              return this->FetchCertificate(team, session);
          })
    .then([=](std::shared_ptr<Certificate> tempCertificate)
          {
              *certificate = *tempCertificate;

			  std::stringstream ssTitle;
			  ssTitle << "Installing AltStore to " << installDevice->name() << "...";

			  std::stringstream ssMessage;
			  ssMessage << "This may take a few seconds.";

			  this->ShowNotification(ssTitle.str(), ssMessage.str());

			  odslog("Downloading app...");

              return this->DownloadApp();
          })
    .then([=](fs::path downloadedAppPath)
          {
			odslog("Downloaded app!");

              fs::create_directory(destinationDirectoryPath);
              
              auto appBundlePath = UnzipAppBundle(downloadedAppPath.string(), destinationDirectoryPath.string());

			  try
			  {
				  fs::remove(downloadedAppPath);
			  }
			  catch (std::exception& e)
			  {
				  odslog("Failed to remove downloaded .ipa." << e.what());
			  }
              
              auto app = std::make_shared<Application>(appBundlePath);
              return app;
          })
    .then([=](std::shared_ptr<Application> tempApp)
          {
              *app = *tempApp;
              return this->RegisterAppID(app->name(), app->bundleIdentifier(), team, session);
          })
    .then([=](std::shared_ptr<AppID> tempAppID)
          {
              *appID = *tempAppID;
              return this->FetchProvisioningProfile(appID, team, session);
          })
    .then([=](std::shared_ptr<ProvisioningProfile> tempProfile)
          {
              *profile = *tempProfile;
              return this->InstallApp(app, device, team, appID, certificate, profile);
          })
    .then([=](pplx::task<void> task)
          {
			if (fs::exists(destinationDirectoryPath))
			{
				fs::remove_all(destinationDirectoryPath);
			}
              
			  try
			  {
				  task.get();

				  std::stringstream ss;
				  ss << "AltStore was successfully installed on " << installDevice->name() << ".";

				  this->ShowNotification("Installation Succeeded", ss.str());
			  }
			  catch (InstallError& error)
			  {
				  if ((InstallErrorCode)error.code() == InstallErrorCode::Cancelled)
				  {
					  // Ignore
				  }
				  else
				  {
					  MessageBox(NULL, WideStringFromString(error.localizedDescription()).c_str(), L"Installation Failed", MB_OK);
					  throw;
				  }
			  }
			  catch (Error& error)
			  {
				  MessageBox(NULL, WideStringFromString(error.localizedDescription()).c_str(), L"Installation Failed", MB_OK);
				  throw;
			  }
			  catch (std::exception& exception)
			  {
				  odslog("Execption:" << exception.what());

				  MessageBox(NULL, WideStringFromString(exception.what()).c_str(), L"Installation Failed", MB_OK);
				  throw;
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
              
              uri_builder builder(L"https://f000.backblazeb2.com/file/altstore/altstore.ipa");
              
              http_client client(builder.to_uri());
              return client.request(methods::GET);
          })
    .then([=](http_response response)
          {
              printf("Received download response status code:%u\n", response.status_code());
              
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

pplx::task<std::pair<std::shared_ptr<Account>, std::shared_ptr<AppleAPISession>>> AltServerApp::Authenticate(std::string appleID, std::string password, std::shared_ptr<AnisetteData> anisetteData)
{
	if (anisetteData == NULL)
	{
		throw ServerError(ServerErrorCode::InvalidAnisetteData);
	}

	auto verificationHandler = [=](void)->pplx::task<std::optional<std::string>> {
		return pplx::create_task([=]() -> std::optional<std::string> {

			int result = DialogBox(NULL, MAKEINTRESOURCE(ID_TWOFACTOR), NULL, TwoFactorDlgProc);
			if (result == IDCANCEL)
			{
				return std::nullopt;
			}

			auto verificationCode = std::make_optional<std::string>(_verificationCode);
			_verificationCode = "";

			return verificationCode;
		});
	};

	return AppleAPI::getInstance()->Authenticate(appleID, password, anisetteData, verificationHandler);
}

pplx::task<std::shared_ptr<Team>> AltServerApp::FetchTeam(std::shared_ptr<Account> account, std::shared_ptr<AppleAPISession> session)
{
    auto task = AppleAPI::getInstance()->FetchTeams(account, session)
    .then([](std::vector<std::shared_ptr<Team>> teams) {

		for (auto& team : teams)
		{
			if (team->type() == Team::Type::Free)
			{
				return team;
			}
		}

		for (auto& team : teams)
		{
			if (team->type() == Team::Type::Individual)
			{
				return team;
			}
		}

		for (auto& team : teams)
		{
			return team;
		}

		throw InstallError(InstallErrorCode::NoTeam);
    });
    
    return task;
}

pplx::task<std::shared_ptr<Certificate>> AltServerApp::FetchCertificate(std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
    auto task = AppleAPI::getInstance()->FetchCertificates(team, session)
    .then([this, team, session](std::vector<std::shared_ptr<Certificate>> certificates)
          {
			std::shared_ptr<Certificate> preferredCertificate = nullptr;

			for (auto& certificate : certificates)
			{
				if (!certificate->machineName().has_value())
				{
					continue;
				}

				std::string prefix("AltStore");

				if (certificate->machineName()->size() < prefix.size())
				{
					// Machine name doesn't begin with "AltStore", so ignore.
					continue;
				}
				else
				{
					auto result = std::mismatch(prefix.begin(), prefix.end(), certificate->machineName()->begin());
					if (result.first != prefix.end())
					{
						// Machine name doesn't begin with "AltStore", so ignore.
						continue;
					}
				}

				preferredCertificate = certificate;

				// Machine name starts with AltStore.

				auto alertResult = MessageBox(NULL,
					L"Apps installed with AltStore on your other devices will stop working. Are you sure you want to continue?",
					L"AltStore already installed on another device.",
					MB_OKCANCEL);

				if (alertResult == IDCANCEL)
				{
					throw InstallError(InstallErrorCode::Cancelled);
				}

				break;
			}

              if (certificates.size() != 0)
              {
                  auto certificate = (preferredCertificate != nullptr) ? preferredCertificate : certificates[0];
                  return AppleAPI::getInstance()->RevokeCertificate(certificate, team, session).then([this, team, session](bool success)
                                                                                            {
                                                                                                return this->FetchCertificate(team, session);
                                                                                            });
              }
              else
              {
                  std::string machineName = "AltStore";
                  
                  return AppleAPI::getInstance()->AddCertificate(machineName, team, session).then([team, session](std::shared_ptr<Certificate> addedCertificate)
                                                                                         {
                                                                                             auto privateKey = addedCertificate->privateKey();
                                                                                             if (privateKey == std::nullopt)
                                                                                             {
                                                                                                 throw InstallError(InstallErrorCode::MissingPrivateKey);
                                                                                             }
                                                                                             
                                                                                             return AppleAPI::getInstance()->FetchCertificates(team, session)
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

pplx::task<std::shared_ptr<AppID>> AltServerApp::RegisterAppID(std::string appName, std::string identifier, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
    std::stringstream ss;
    ss << "com." << team->identifier() << "." << identifier;
    
    auto bundleID = ss.str();
    
    auto task = AppleAPI::getInstance()->FetchAppIDs(team, session)
    .then([bundleID, appName, identifier, team, session](std::vector<std::shared_ptr<AppID>> appIDs)
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
                  return AppleAPI::getInstance()->AddAppID(appName, bundleID, team, session);
              }
          });
    
    return task;
}

pplx::task<std::shared_ptr<Device>> AltServerApp::RegisterDevice(std::shared_ptr<Device> device, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
    auto task = AppleAPI::getInstance()->FetchDevices(team, session)
    .then([device, team, session](std::vector<std::shared_ptr<Device>> devices)
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
                  return AppleAPI::getInstance()->RegisterDevice(device->name(), device->identifier(), team, session);
              }
          });
    
    return task;
}

pplx::task<std::shared_ptr<ProvisioningProfile>> AltServerApp::FetchProvisioningProfile(std::shared_ptr<AppID> appID, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
    return AppleAPI::getInstance()->FetchProvisioningProfile(appID, team, session);
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

		plist_dict_set_item(plist, "ALTCertificateID", plist_new_string(certificate->serialNumber().c_str()));
        
        char *plistXML = nullptr;
        uint32_t length = 0;
        plist_to_xml(plist, &plistXML, &length);
        
        std::ofstream fout(infoPlistPath.string(), std::ios::out | std::ios::binary);
        fout.write(plistXML, length);
        fout.close();

		auto machineIdentifier = certificate->machineIdentifier();
		if (machineIdentifier.has_value())
		{
			auto encryptedData = certificate->encryptedP12Data(*machineIdentifier);
			if (encryptedData.has_value())
			{
				// Embed encrypted certificate in app bundle.
				fs::path certificatePath(app->path());
				certificatePath.append("ALTCertificate.p12");

				std::ofstream fout(certificatePath.string(), std::ios::out | std::ios::binary);
				fout.write((const char *)encryptedData->data(), length);
				fout.close();
			}
		}
        
        Signer signer(team, certificate);
        signer.SignApp(app->path(), { profile });
        
		return DeviceManager::instance()->InstallApp(app->path(), device->identifier(), [](double progress) {
			odslog("AltStore Installation Progress: " << progress);
		});
    });
}

void AltServerApp::ShowNotification(std::string title, std::string message)
{
	HICON icon = (HICON)LoadImage(this->instanceHandle(), MAKEINTRESOURCE(IMG_MENUBAR), IMAGE_ICON, 0, 0, LR_MONOCHROME);

	NOTIFYICONDATA niData;
	ZeroMemory(&niData, sizeof(NOTIFYICONDATA));
	niData.uVersion = NOTIFYICON_VERSION_4;
	niData.cbSize = sizeof(NOTIFYICONDATA);
	niData.uID = 10456;
	niData.uFlags = NIF_ICON | NIF_MESSAGE | NIF_INFO | NIF_TIP | NIF_GUID;
	niData.hWnd = this->windowHandle();
	niData.hIcon = icon;
	niData.uCallbackMessage = WM_USER + 1;
	niData.uTimeout = 3000;
	niData.dwInfoFlags = NIIF_INFO;
	StringCchCopy(niData.szInfoTitle, ARRAYSIZE(niData.szInfoTitle), WideStringFromString(title).c_str());
	StringCchCopy(niData.szInfo, ARRAYSIZE(niData.szInfo), WideStringFromString(message).c_str());
	
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

bool AltServerApp::CheckDependencies()
{
	wchar_t* programFilesCommonDirectory;
	SHGetKnownFolderPath(FOLDERID_ProgramFilesCommon, 0, NULL, &programFilesCommonDirectory);

	fs::path deviceDriverDirectoryPath(programFilesCommonDirectory);
	deviceDriverDirectoryPath.append("Apple").append("Mobile Device Support");

	if (!fs::exists(deviceDriverDirectoryPath))
	{
		return false;
	}

	wchar_t* programFilesDirectory;
	SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, NULL, &programFilesDirectory);

	fs::path bonjourDirectoryPath(programFilesDirectory);
	bonjourDirectoryPath.append("Bonjour");

	if (!fs::exists(bonjourDirectoryPath))
	{
		return false;
	}

	return true;
}

bool AltServerApp::CheckiCloudDependencies()
{
	wchar_t* programFilesCommonDirectory;
	SHGetKnownFolderPath(FOLDERID_ProgramFilesCommon, 0, NULL, &programFilesCommonDirectory);

	fs::path deviceDriverDirectoryPath(programFilesCommonDirectory);
	deviceDriverDirectoryPath.append("Apple").append("Internet Services");

	fs::path aosKitPath(deviceDriverDirectoryPath);
	aosKitPath.append("AOSKit.dll");

	if (!fs::exists(aosKitPath))
	{
		return false;
	}

	return true;
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

bool AltServerApp::presentedRunningNotification() const
{
	auto presentedRunningNotification = GetRegistryBoolValue(PRESENTED_RUNNING_NOTIFICATION_KEY);
	return presentedRunningNotification;
}

void AltServerApp::setPresentedRunningNotification(bool presentedRunningNotification)
{
	SetRegistryBoolValue(PRESENTED_RUNNING_NOTIFICATION_KEY, presentedRunningNotification);
}

bool AltServerApp::reprovisionedDevice() const
{
	auto reprovisionedDevice = GetRegistryBoolValue(REPROVISIONED_DEVICE_KEY);
	return reprovisionedDevice;
}

void AltServerApp::setReprovisionedDevice(bool reprovisionedDevice)
{
	SetRegistryBoolValue(REPROVISIONED_DEVICE_KEY, reprovisionedDevice);
}