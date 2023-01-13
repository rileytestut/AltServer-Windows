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
#include <Guiddef.h>

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
#include <regex>
#include <numeric>

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
const char* APPLE_FOLDER_KEY = "AppleFolder";

const char* STARTUP_ITEMS_KEY = "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";

#if STAGING
std::wstring altstoreSourceURL = L"https://f000.backblazeb2.com/file/altstore-staging/apps-staging.json";
#else
std::wstring altstoreSourceURL = L"https://apps.altstore.io";
#endif

#if BETA
std::wstring altstoreBundleID = L"com.rileytestut.AltStore.Beta";
#else
std::wstring altstoreBundleID = L"com.rileytestut.AltStore";
#endif

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

// Observes all exceptions that occurred in all tasks in the given range.
template<class T, class InIt>
void observe_all_exceptions(InIt first, InIt last)
{
	std::for_each(first, last, [](concurrency::task<T> t)
		{
			t.then([](concurrency::task<T> previousTask)
				{
					try
					{
						previousTask.get();
					}
					catch (const std::exception&)
					{
						// Swallow the exception.
					}
				});
		});
}

BOOL CALLBACK InstallDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch (Message)
	{
	case WM_INITDIALOG:
	{
		std::map<std::string, std::wstring>* parameters = (std::map<std::string, std::wstring>*)lParam;

		std::wstring title = (*parameters)["title"];
		std::wstring message = (*parameters)["message"];

		SetWindowText(hwnd, title.c_str());

		HWND descriptionText = GetDlgItem(hwnd, IDC_DESCRIPTION);
		SetWindowText(descriptionText, message.c_str());

		HWND downloadButton = GetDlgItem(hwnd, IDOK);
		PostMessage(hwnd, WM_NEXTDLGCTL, (WPARAM)downloadButton, TRUE);

		return TRUE;
	}

	case WM_CTLCOLORSTATIC:
	{
		if (GetDlgCtrlID((HWND)lParam) == IDC_DESCRIPTION)
		{
			HBRUSH success = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
			SetBkMode((HDC)wParam, TRANSPARENT);
			return (BOOL)success;
		}

		return TRUE;
	}

	case WM_COMMAND:
	{
		switch (LOWORD(wParam))
		{
		case IDOK:
		case IDCANCEL:
		case ID_FOLDER:
			EndDialog(hwnd, LOWORD(wParam));
			return TRUE;
		}
	}

	default: break;
	}

	return FALSE;
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

VOID CALLBACK DetailedErrorMessageBoxCallback(LPHELPINFO lpHelpInfo)
{
	auto helpError = AltServerApp::instance()->helpError();
	if (helpError == NULL)
	{
		return;
	}

	std::string url("https://faq.altstore.io/getting-started/error-codes?q=");
	url += helpError->domain() + "+" + std::to_string(helpError->displayCode());

	ShellExecute(NULL, L"open", WideStringFromString(url).c_str(), NULL, NULL, SW_SHOWNORMAL);
}

VOID CALLBACK ErrorMessageBoxCallback(LPHELPINFO lpHelpInfo)
{
	auto helpError = AltServerApp::instance()->helpError();
	if (helpError == NULL)
	{
		return;
	}

	std::string localizedErrorCode = helpError->localizedErrorCode();
	
	auto wideTitle = WideStringFromString(localizedErrorCode);
	auto wideMessage = WideStringFromString(helpError->formattedDetailedDescription() + "\n\n" + "Press 'Help' to search the AltStore FAQ.");

	MSGBOXPARAMSW parameters = {};
	parameters.cbSize = sizeof(parameters);
	parameters.lpszText = wideMessage.c_str();
	parameters.lpszCaption = wideTitle.c_str();
	parameters.lpfnMsgBoxCallback = DetailedErrorMessageBoxCallback;
	parameters.dwStyle = MB_HELP | MB_ICONINFORMATION;
	MessageBoxIndirectW(&parameters);
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

AltServerApp::AltServerApp() : _appGroupSemaphore(1)
{
	HRESULT result = CoCreateGuid(&_notificationIconGUID);
	if (result != S_OK)
	{
		//TODO: Better error handling?
		assert(false);
	}
}

AltServerApp::~AltServerApp()
{
}

static int CALLBACK BrowseFolderCallback(HWND hwnd, UINT uMsg, LPARAM lParam, LPARAM lpData)
{
	if (uMsg == BFFM_INITIALIZED)
	{
		std::string tmp = (const char*)lpData;
		odslog("Browser Path:" << tmp);
		SendMessage(hwnd, BFFM_SETSELECTION, TRUE, lpData);
	}

	return 0;
}

std::string AltServerApp::BrowseForFolder(std::wstring title, std::string folderPath)
{
	BROWSEINFO browseInfo = { 0 };
	browseInfo.lpszTitle = title.c_str();
	browseInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_NONEWFOLDERBUTTON;
	browseInfo.lpfn = BrowseFolderCallback;
	browseInfo.lParam = (LPARAM)folderPath.c_str();

	LPITEMIDLIST pidList = SHBrowseForFolder(&browseInfo);
	if (pidList == 0)
	{
		return "";
	}

	TCHAR path[MAX_PATH];
	SHGetPathFromIDList(pidList, path);

	IMalloc* imalloc = NULL;
	if (SUCCEEDED(SHGetMalloc(&imalloc)))
	{
		imalloc->Free(pidList);
		imalloc->Release();
	}

	return StringFromWideString(path);
}

void AltServerApp::Start(HWND windowHandle, HINSTANCE instanceHandle)
{
	_windowHandle = windowHandle;
	_instanceHandle = instanceHandle;

#if STAGING
	win_sparkle_set_appcast_url("https://altstore.io/altserver/sparkle-windows-staging.xml");
#else
	win_sparkle_set_appcast_url("https://altstore.io/altserver/sparkle-windows.xml");
#endif

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

	try
	{
		this->CheckDependencies();
		AnisetteDataManager::instance()->LoadDependencies();

#if SPOOF_MAC
		if (!this->CheckiCloudDependencies())
		{
			this->ShowAlert("iCloud Not Installed", "iCloud must be installed from Apple's website (not the Microsoft Store) in order to use AltStore.");
		}
#endif
	}
	catch (AnisetteError &error)
	{
		this->HandleAnisetteError(error);
	}
	catch (Error& error)
	{
		this->ShowAlert("Failed to Start AltServer", error.localizedDescription());
	}
	catch (std::exception& exception)
	{
		this->ShowAlert("Failed to Start AltServer", exception.what());
	}

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

	DeviceManager::instance()->Start();
}

void AltServerApp::Stop()
{
	win_sparkle_cleanup();
}

void AltServerApp::CheckForUpdates()
{
	win_sparkle_check_update_with_ui();
}

pplx::task<std::shared_ptr<Application>> AltServerApp::InstallApplication(std::optional<std::string> filepath, std::shared_ptr<Device> installDevice, std::string appleID, std::string password)
{
    auto appName = filepath.has_value() ? fs::path(*filepath).filename().string() : "AltStore";
    auto localizedFailure = "Could not install " + appName + " to " + installDevice->name() + ".";

	return this->_InstallApplication(filepath, installDevice, appleID, password)
	.then([=](pplx::task<std::shared_ptr<Application>> task) -> pplx::task<std::shared_ptr<Application>> {
		try
		{
			auto application = task.get();
			return pplx::create_task([application]() { 
				return application;
			});
		}
		catch (APIError& error)
		{
			if ((APIErrorCode)error.code() == APIErrorCode::InvalidAnisetteData)
			{
				// Our attempt to re-provision the device as a Mac failed, so reset provisioning and try one more time.
				// This appears to happen when iCloud is running simultaneously, and just happens to provision device at same time as AltServer.
				AnisetteDataManager::instance()->ResetProvisioning();

				this->ShowNotification("Registering PC with Apple...", "This may take a few seconds.");

				// Provisioning device can fail if attempted too soon after previous attempt.
				// As a hack around this, we wait a bit before trying again.
				// 10-11 seconds appears to be too short, so wait for 12 seconds instead.
				Sleep(12000);

				return this->_InstallApplication(filepath, installDevice, appleID, password);
			}
			else
			{
				throw;
			}
		}
	})
	.then([=](pplx::task<std::shared_ptr<Application>> task) -> std::shared_ptr<Application> {
		try
		{
			auto application = task.get();

			std::stringstream ss;
			ss << application->name() << " was successfully installed on " << installDevice->name() << ".";

			this->ShowNotification("Installation Succeeded", ss.str());

			return application;
		}
		catch (InstallError& error)
		{
			if ((InstallErrorCode)error.code() == InstallErrorCode::Cancelled)
			{
				// Ignore
			}
			else
			{
                this->ShowErrorAlert(error, localizedFailure);
                throw;
			}
		}
		catch (APIError& error)
		{
			if ((APIErrorCode)error.code() == APIErrorCode::InvalidAnisetteData)
			{
				AnisetteDataManager::instance()->ResetProvisioning();
			}

            this->ShowErrorAlert(error, localizedFailure);
            throw;
		}
		catch (AnisetteError& error)
		{
			this->HandleAnisetteError(error);
            throw;
		}
		catch (std::exception& exception)
		{
            this->ShowErrorAlert(exception, localizedFailure);
            throw;
		}
	});
}

pplx::task<std::shared_ptr<Application>> AltServerApp::_InstallApplication(std::optional<std::string> filepath, std::shared_ptr<Device> installDevice, std::string appleID, std::string password)
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

	return pplx::create_task([=]() {
		auto anisetteData = AnisetteDataManager::instance()->FetchAnisetteData();
		return this->Authenticate(appleID, password, anisetteData);
	})
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

				tempDevice->setName(installDevice->name()); // Ensure we use real device name.
				tempDevice->setOSVersion(installDevice->osVersion());
				*device = *tempDevice;

				return this->FetchCertificate(team, session);
          })
    .then([=](std::shared_ptr<Certificate> tempCertificate)
          {
				*certificate = *tempCertificate;

				odslog("Preparing device...");				
                return this->PrepareDevice(device).then([=](pplx::task<void> task) {
                    try
                    {
                        // Don't rethrow error, and instead continue installing app even if we couldn't install Developer disk image.
                        task.get();
                    }
                    catch (Error& error)
                    {
                        odslog("Failed to install DeveloperDiskImage.dmg to " << *device << ". " << error.localizedDescription());
                    }
                    catch (std::exception& exception)
                    {
                        odslog("Failed to install DeveloperDiskImage.dmg to " << *device << ". " << exception.what());
                    }

                    if (filepath.has_value())
                    {
                        odslog("Importing app...");

                        return pplx::create_task([filepath] {
                            return fs::path(*filepath);
                        });
                    }
                    else
                    {
                        odslog("Downloading app...");

                        // Show alert before downloading AltStore.
                        this->ShowInstallationNotification("AltStore", device->name());
                        return this->DownloadApp(device);
                    }
                });
          })
    .then([=](fs::path downloadedAppPath)
          {
			odslog("Downloaded app!");

              fs::create_directory(destinationDirectoryPath);
              
              auto appBundlePath = UnzipAppBundle(downloadedAppPath.string(), destinationDirectoryPath.string());
			  auto app = std::make_shared<Application>(appBundlePath);

			  if (filepath.has_value())
			  {
				  // Show alert after "downloading" local .ipa.
				  this->ShowInstallationNotification(app->name(), device->name());
			  }
			  else
			  {
				  // Remove downloaded app.

				  try
				  {
					  fs::remove(downloadedAppPath);
				  }
				  catch (std::exception& e)
				  {
					  odslog("Failed to remove downloaded .ipa." << e.what());
				  }
			  }              
              
              return app;
          })
    .then([=](std::shared_ptr<Application> tempApp)
          {
              *app = *tempApp;
			  return this->PrepareAllProvisioningProfiles(app, device, team, session);
          })
    .then([=](std::map<std::string, std::shared_ptr<ProvisioningProfile>> profiles)
          {
              return this->InstallApp(app, device, team, certificate, profiles);
          })
    .then([=](pplx::task<std::shared_ptr<Application>> task)
          {
			if (fs::exists(destinationDirectoryPath))
			{
				fs::remove_all(destinationDirectoryPath);
			}     

			try
			{
				auto application = task.get();
				return application;
			}
			catch (LocalizedError& error)
			{
				if (error.code() == -22421)
				{
					// Don't know what API call returns this error code, so assume any LocalizedError with -22421 error code
					// means invalid anisette data, then throw the correct APIError.
					throw APIError(APIErrorCode::InvalidAnisetteData);
				}
				else if (error.code() == -29004)
				{
					// Same with -29004, "Environment Mismatch"
					throw APIError(APIErrorCode::InvalidAnisetteData);
				}
				else
				{
					throw;
				}
			}
         });
}

pplx::task<void> AltServerApp::PrepareDevice(std::shared_ptr<Device> device)
{
	return DeviceManager::instance()->IsDeveloperDiskImageMounted(device)
	.then([=](bool isMounted) {
		if (isMounted)
		{
			return pplx::create_task([] { return; });
		}
		else
		{
			return this->_developerDiskManager.DownloadDeveloperDisk(device)
			.then([=](std::pair<std::string, std::string> paths) {
				return DeviceManager::instance()->InstallDeveloperDiskImage(paths.first, paths.second, device);
			})
			.then([=](pplx::task<void> task) {
				try
				{
					task.get();

					// No error thrown, so assume disk is compatible.
					this->_developerDiskManager.SetDeveloperDiskCompatible(true, device);
				}
				catch (ServerError& serverError)
				{
					if (serverError.code() == (int)ServerErrorCode::IncompatibleDeveloperDisk)
					{
						// Developer disk is not compatible with this device, so mark it as incompatible.
						this->_developerDiskManager.SetDeveloperDiskCompatible(false, device);
					}
					else
					{
						// Don't mark developer disk as incompatible because it probably failed for a different reason.
					}

					throw;
				}
			});
		}		
	});
}

pplx::task<std::string> AltServerApp::FetchAltStoreDownloadURL(std::shared_ptr<Device> device)
{
	uri_builder builder(altstoreSourceURL);
	http_client client(builder.to_uri());

	return client.request(methods::GET).then([=](http_response response)
	{
		return response.content_ready();
	})
	.then([=](http_response response)
	{
		odslog("Received AltStore source response status code: " << response.status_code());
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
	})
	.then([=](json::value json) {
		try
		{
			auto apps = json[L"apps"].as_array();

			std::optional<json::value> altstore = std::nullopt;
			for (auto& app : apps)
			{
				auto bundleID = app[L"bundleIdentifier"].as_string();
				if (bundleID == altstoreBundleID)
				{
					altstore = app;
					break;
				}
			}

			auto bundleID = StringFromWideString(altstoreBundleID);

			if (!altstore.has_value())
			{
				auto debugDescription = "App with bundle ID '" + bundleID + "' does not exist in source JSON.";
				throw CocoaError(CocoaErrorCode::CoderValueNotFound, { {NSDebugDescriptionErrorKey, debugDescription} });
			}

			if (!altstore->has_array_field(L"versions"))
			{
				auto debugDescription = "There is no 'versions' key for " + bundleID + ".";
				throw CocoaError(CocoaErrorCode::CoderReadCorrupt, { {NSDebugDescriptionErrorKey, debugDescription} });
			}

			auto versions = (*altstore)[L"versions"].as_array();
			if (versions.size() == 0)
			{
				auto debugDescription = "The 'versions' array is empty for " + bundleID + ".";
				throw CocoaError(CocoaErrorCode::CoderValueNotFound, { {NSDebugDescriptionErrorKey, debugDescription} });
			}

			auto latestVersion = versions[0];
			std::optional<json::value> latestSupportedVersion = std::nullopt;

			for (auto& version : versions)
			{
				if (!version.has_string_field(L"minOSVersion"))
				{
					// No minOSVersion, so assume it's compatible.
					latestSupportedVersion = version;
					break;
				}

				auto minOSVersionString = version[L"minOSVersion"].as_string();
				auto minOSVersion = OperatingSystemVersion(StringFromWideString(minOSVersionString));

				if (device->osVersion() < minOSVersion)
				{
					// Device OS version is older than minOSVersion, so ignore.
					continue;
				}

				latestSupportedVersion = version;
				break;
			}

			auto deviceOSName = ALTOperatingSystemNameForDeviceType(device->type());
			std::string osName = deviceOSName.has_value() ? *deviceOSName : "iOS";

			auto minOSVersionString = latestVersion.has_string_field(L"minOSVersion") ? StringFromWideString(latestVersion[L"minOSVersion"].as_string()) : "12.2";

			if (!latestSupportedVersion.has_value())
			{
				throw ServerError(ServerErrorCode::UnsupportediOSVersion, { {AppNameErrorKey, "AltStore"}, {OperatingSystemNameErrorKey, osName}, {OperatingSystemVersionErrorKey, minOSVersionString} });
			}

			auto latestVersionNumber = latestVersion[L"version"].as_string();
			auto latestSupportedVersionNumber = (*latestSupportedVersion)[L"version"].as_string();

			if (latestVersionNumber == latestSupportedVersionNumber)
			{
				// Latest version is supported, so return downloadURL.
				auto downloadURL = latestVersion[L"downloadURL"].as_string();
				return StringFromWideString(downloadURL);
			}
			else
			{
				auto minOSVersion = StringFromWideString(latestVersion[L"minOSVersion"].as_string());

				std::ostringstream oss;
				oss << device->name() << " is running " << osName << " " << device->osVersion().stringValue() << ", but AltStore requires " << osName << " " << minOSVersion << " or later.";
				oss << "\n\n";
				oss << "Would you like to download the last version compatible with this device instead (AltStore " << StringFromWideString(latestSupportedVersionNumber) << ")?";

				std::string alertTitle = "Unsupported " + osName + " Version";
				auto alertResult = MessageBox(NULL, WideStringFromString(oss.str()).c_str(), WideStringFromString(alertTitle).c_str(), MB_OKCANCEL);
				if (alertResult == IDCANCEL)
				{
					throw InstallError(InstallErrorCode::Cancelled);
				}

				auto downloadURL = (*latestSupportedVersion)[L"downloadURL"].as_string();
				return StringFromWideString(downloadURL);
			}
		}
		catch (Error& error)
		{
			if (error.domain() == ServerError(ServerErrorCode::Unknown).domain() && error.code() == (int)ServerErrorCode::UnsupportediOSVersion)
			{
				// Don't add localized failure for unsupported iOS version errors.
			}
			else
			{
				error.setLocalizedFailure("The download URL could not be determined.");
			}
			
			throw;
		}
		catch (std::exception& exception)
		{
			std::shared_ptr<Error> underlyingError(new ExceptionError(exception));

			throw CocoaError(CocoaErrorCode::CoderReadCorrupt, {
				{NSLocalizedFailureErrorKey, "The download URL could not be determined."},
				{NSUnderlyingErrorKey, underlyingError}
			});
		}
	});
}

pplx::task<fs::path> AltServerApp::DownloadApp(std::shared_ptr<Device> device)
{
    fs::path temporaryPath(temporary_directory());
    temporaryPath.append(make_uuid());
    
    auto outputFile = std::make_shared<ostream>();
    
    // Open stream to output file.
    auto task = fstream::open_ostream(WideStringFromString(temporaryPath.string()))
		.then([this, device, outputFile](ostream file)
		{
			*outputFile = file;
			return this->FetchAltStoreDownloadURL(device);
		})
		.then([=](std::string downloadURL) {
			uri_builder builder(WideStringFromString(downloadURL));
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

	return pplx::create_task([=]() {
		if (anisetteData == NULL)
		{
			throw ServerError(ServerErrorCode::InvalidAnisetteData);
		}

		return AppleAPI::getInstance()->Authenticate(appleID, password, anisetteData, verificationHandler);
	});
}

pplx::task<std::shared_ptr<Team>> AltServerApp::FetchTeam(std::shared_ptr<Account> account, std::shared_ptr<AppleAPISession> session)
{
    auto task = AppleAPI::getInstance()->FetchTeams(account, session)
    .then([](std::vector<std::shared_ptr<Team>> teams) {

		for (auto& team : teams)
		{
			if (team->type() == Team::Type::Individual)
			{
				return team;
			}
		}

		for (auto& team : teams)
		{
			if (team->type() == Team::Type::Free)
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
			auto certificatesDirectoryPath = this->certificatesDirectoryPath();
			auto cachedCertificatePath = certificatesDirectoryPath.append(team->identifier() + ".p12");

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

				if (fs::exists(cachedCertificatePath) && certificate->machineIdentifier().has_value())
				{
					try
					{
						auto data = readFile(cachedCertificatePath.string().c_str());
						auto cachedCertificate = std::make_shared<Certificate>(data, *certificate->machineIdentifier());

						// Manually set machineIdentifier so we can encrypt + embed certificate if needed.
						cachedCertificate->setMachineIdentifier(*certificate->machineIdentifier());

						return pplx::create_task([cachedCertificate] {
							return cachedCertificate;
						});
					}
					catch(std::exception &e)
					{
						// Ignore cached certificate errors.
						odslog("Failed to load cached certificate:" << cachedCertificatePath << ". " << e.what())
					}
				}

				preferredCertificate = certificate;

				// Machine name starts with AltStore.

				auto alertResult = MessageBox(NULL,
					L"Please use the same AltServer you previously used with this Apple ID, or else apps installed with other AltServers will stop working.\n\nAre you sure you want to continue?",
					L"Installing AltStore with Multiple AltServers Not Supported",
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
                  
                  return AppleAPI::getInstance()->AddCertificate(machineName, team, session)
					  .then([team, session, cachedCertificatePath](std::shared_ptr<Certificate> addedCertificate)
                        {
                            auto privateKey = addedCertificate->privateKey();
                            if (privateKey == std::nullopt)
                            {
                                throw InstallError(InstallErrorCode::MissingPrivateKey);
                            }
                                                                                             
                            return AppleAPI::getInstance()->FetchCertificates(team, session)
                            .then([privateKey, addedCertificate, cachedCertificatePath](std::vector<std::shared_ptr<Certificate>> certificates)
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

									try
									{
										if (certificate->machineIdentifier().has_value())
										{
											auto machineIdentifier = certificate->machineIdentifier();

											auto encryptedData = certificate->encryptedP12Data(*machineIdentifier);
											if (encryptedData.has_value())
											{
												std::ofstream fout(cachedCertificatePath.string(), std::ios::out | std::ios::binary);
												fout.write((const char*)encryptedData->data(), encryptedData->size());
												fout.close();
											}
										}
									}
									catch (std::exception& e)
									{
										// Ignore caching certificate errors.
										odslog("Failed to cache certificate:" << cachedCertificatePath << ". " << e.what())
									}

                                    return certificate;
                                });
                        });
              }
          });
    
    return task;
}

pplx::task<std::map<std::string, std::shared_ptr<ProvisioningProfile>>> AltServerApp::PrepareAllProvisioningProfiles(
	std::shared_ptr<Application> application,
	std::shared_ptr<Device> device,
	std::shared_ptr<Team> team,
	std::shared_ptr<AppleAPISession> session)
{
	return this->PrepareProvisioningProfile(application, std::nullopt, device, team, session)
	.then([=](std::shared_ptr<ProvisioningProfile> profile) {
		std::vector<pplx::task<std::pair<std::string, std::shared_ptr<ProvisioningProfile>>>> tasks;

		auto task = pplx::create_task([application, profile]() -> std::pair<std::string, std::shared_ptr<ProvisioningProfile>> { 
			return std::make_pair(application->bundleIdentifier(), profile); 
		});
		tasks.push_back(task);

		for (auto appExtension : application->appExtensions())
		{
			auto task = this->PrepareProvisioningProfile(appExtension, application, device, team, session)
			.then([appExtension](std::shared_ptr<ProvisioningProfile> profile) {
				return std::make_pair(appExtension->bundleIdentifier(), profile);
			});
			tasks.push_back(task);
		}

		return pplx::when_all(tasks.begin(), tasks.end())
			.then([tasks](pplx::task<std::vector<std::pair<std::string, std::shared_ptr<ProvisioningProfile>>>> task) {
				try
				{
					auto pairs = task.get();

					std::map<std::string, std::shared_ptr<ProvisioningProfile>> profiles;
					for (auto& pair : pairs)
					{
						profiles[pair.first] = pair.second;
					}

					observe_all_exceptions<std::pair<std::string, std::shared_ptr<ProvisioningProfile>>>(tasks.begin(), tasks.end());
					return profiles;
				}
				catch (std::exception& e)
				{
					observe_all_exceptions<std::pair<std::string, std::shared_ptr<ProvisioningProfile>>>(tasks.begin(), tasks.end());
					throw;
				}
		});
	});
}

pplx::task<std::shared_ptr<ProvisioningProfile>> AltServerApp::PrepareProvisioningProfile(
	std::shared_ptr<Application> app,
	std::optional<std::shared_ptr<Application>> parentApp,
	std::shared_ptr<Device> device,
	std::shared_ptr<Team> team,
	std::shared_ptr<AppleAPISession> session)
{
	std::string preferredName;
	std::string parentBundleID;

	if (parentApp.has_value())
	{
		parentBundleID = (*parentApp)->bundleIdentifier();
		preferredName = (*parentApp)->name() + " " + app->name();
	}
	else
	{
		parentBundleID = app->bundleIdentifier();
		preferredName = app->name();
	}

	std::string updatedParentBundleID;

	if (app->isAltStoreApp())
	{
		std::stringstream ss;
		ss << "com." << team->identifier() << "." << parentBundleID;

		updatedParentBundleID = ss.str();
	}
	else
	{
		updatedParentBundleID = parentBundleID + "." + team->identifier();
	}

	std::string bundleID = std::regex_replace(app->bundleIdentifier(), std::regex(parentBundleID), updatedParentBundleID);

	return this->RegisterAppID(preferredName, bundleID, team, session)
	.then([=](std::shared_ptr<AppID> appID)
	{
		return this->UpdateAppIDFeatures(appID, app, team, session);
	})
	.then([=](std::shared_ptr<AppID> appID)
	{
		return this->UpdateAppIDAppGroups(appID, app, team, session);
	})
	.then([=](std::shared_ptr<AppID> appID)
	{
		return this->FetchProvisioningProfile(appID, device, team, session);
	})
	.then([=](std::shared_ptr<ProvisioningProfile> profile)
	{
		return profile;
	});
}

pplx::task<std::shared_ptr<AppID>> AltServerApp::RegisterAppID(std::string appName, std::string bundleID, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
    auto task = AppleAPI::getInstance()->FetchAppIDs(team, session)
    .then([bundleID, appName, team, session](std::vector<std::shared_ptr<AppID>> appIDs)
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

pplx::task<std::shared_ptr<AppID>> AltServerApp::UpdateAppIDFeatures(std::shared_ptr<AppID> appID, std::shared_ptr<Application> app, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
	auto entitlements = app->entitlements();

	std::map<std::string, plist_t> features;
	for (auto& pair : entitlements)
	{
		auto feature = ALTFeatureForEntitlement(pair.first);
		if (feature.has_value())
		{
			features[*feature] = pair.second;
		}
	}

	auto appGroups = entitlements[ALTEntitlementAppGroups];
	plist_t appGroupNode = NULL;
	
	if (appGroups != NULL && plist_array_get_size(appGroups) > 0)
	{
		// App uses app groups, so assign `true` to enable the feature.
		appGroupNode = plist_new_bool(true);
	}
	else
	{
		// App has no app groups, so assign `false` to disable the feature.
		appGroupNode = plist_new_bool(false);
	}

	features[ALTFeatureAppGroups] = appGroupNode;

	bool updateFeatures = false;

	// Determine whether the required features are already enabled for the AppID.
	for (auto& pair : features)
	{
		plist_t currentValue = appID->features()[pair.first];
		plist_t newValue = pair.second;

		std::optional<bool> newBoolValue = std::nullopt;
		if (PLIST_IS_BOOLEAN(newValue))
		{
			uint8_t isEnabled = false;
			plist_get_bool_val(newValue, &isEnabled);
			newBoolValue = isEnabled;
		}

		if (currentValue != NULL && plist_compare_node_value(currentValue, newValue))
		{
			// AppID already has this feature enabled and the values are the same.
			continue;
		}
		else if (currentValue == NULL && newBoolValue == false)
		{
			// AppID doesn't already have this feature enabled, but we want it disabled anyway.
			continue;
		}
		else
		{
			// AppID either doesn't have this feature enabled or the value has changed,
			// so we need to update it to reflect new values.
			updateFeatures = true;
			break;
		}
	}

	if (updateFeatures)
	{
		std::shared_ptr<AppID> copiedAppID = std::make_shared<AppID>(*appID);
		copiedAppID->setFeatures(features);

		plist_free(appGroupNode);

		return AppleAPI::getInstance()->UpdateAppID(copiedAppID, team, session);
	}
	else
	{
		plist_free(appGroupNode);

		return pplx::create_task([appID]() {
			return appID;
		});
	}
}

pplx::task<std::shared_ptr<AppID>> AltServerApp::UpdateAppIDAppGroups(std::shared_ptr<AppID> appID, std::shared_ptr<Application> app, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
	return pplx::create_task([=]() -> pplx::task<std::shared_ptr<AppID>> {
		auto applicationGroupsNode = app->entitlements()[ALTEntitlementAppGroups];
		std::vector<std::string> applicationGroups;

		if (applicationGroupsNode != nullptr)
		{
			for (int i = 0; i < plist_array_get_size(applicationGroupsNode); i++)
			{
				auto groupNode = plist_array_get_item(applicationGroupsNode, i);

				char* groupName = nullptr;
				plist_get_string_val(groupNode, &groupName);

				applicationGroups.push_back(groupName);
			}
		}

		if (applicationGroups.size() == 0)
		{
			// Assigning an App ID to an empty app group array fails,
			// so just do nothing if there are no app groups.
			return pplx::create_task([appID]() {
				return appID;
			});
		}

		this->_appGroupSemaphore.wait();

		return AppleAPI::getInstance()->FetchAppGroups(team, session)
		.then([=](std::vector<std::shared_ptr<AppGroup>> fetchedGroups) {

			std::vector<pplx::task<std::shared_ptr<AppGroup>>> tasks;

			for (auto groupIdentifier : applicationGroups)
			{
				std::string adjustedGroupIdentifier = groupIdentifier + "." + team->identifier();
				std::optional<std::shared_ptr<AppGroup>> matchingGroup;

				for (auto group : fetchedGroups)
				{
					if (group->groupIdentifier() == adjustedGroupIdentifier)
					{
						matchingGroup = group;
						break;
					}
				}

				if (matchingGroup.has_value())
				{
					auto task = pplx::create_task([matchingGroup]() { return *matchingGroup; });
					tasks.push_back(task);
				}
				else
				{
					std::string name = std::regex_replace("AltStore " + groupIdentifier, std::regex("\\."), " ");

					auto task = AppleAPI::getInstance()->AddAppGroup(name, adjustedGroupIdentifier, team, session);
					tasks.push_back(task);
				}				
			}

			return pplx::when_all(tasks.begin(), tasks.end()).then([=](pplx::task<std::vector<std::shared_ptr<AppGroup>>> task) {
				try
				{
					auto groups = task.get();
					observe_all_exceptions<std::shared_ptr<AppGroup>>(tasks.begin(), tasks.end());
					return groups;
				}
				catch (std::exception& e)
				{
					observe_all_exceptions<std::shared_ptr<AppGroup>>(tasks.begin(), tasks.end());
					throw;
				}
			});
		})
		.then([=](std::vector<std::shared_ptr<AppGroup>> groups) {
			return AppleAPI::getInstance()->AssignAppIDToGroups(appID, groups, team, session);
		})
		.then([appID](bool result) {
			return appID;
		})
		.then([this](pplx::task<std::shared_ptr<AppID>> task) {
			this->_appGroupSemaphore.notify();

			auto appID = task.get();
			return appID;
		});
	});
}

pplx::task<std::shared_ptr<Device>> AltServerApp::RegisterDevice(std::shared_ptr<Device> device, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
    auto task = AppleAPI::getInstance()->FetchDevices(team, device->type(), session)
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
                  return AppleAPI::getInstance()->RegisterDevice(device->name(), device->identifier(), device->type(), team, session);
              }
          });
    
    return task;
}

pplx::task<std::shared_ptr<ProvisioningProfile>> AltServerApp::FetchProvisioningProfile(std::shared_ptr<AppID> appID, std::shared_ptr<Device> device, std::shared_ptr<Team> team, std::shared_ptr<AppleAPISession> session)
{
    return AppleAPI::getInstance()->FetchProvisioningProfile(appID, device->type(), team, session);
}

pplx::task<std::shared_ptr<Application>> AltServerApp::InstallApp(std::shared_ptr<Application> app,
                            std::shared_ptr<Device> device,
                            std::shared_ptr<Team> team,
                            std::shared_ptr<Certificate> certificate,
                            std::map<std::string, std::shared_ptr<ProvisioningProfile>> profilesByBundleID)
{
	auto prepareInfoPlist = [profilesByBundleID](std::shared_ptr<Application> app, plist_t additionalValues){
		auto profile = profilesByBundleID.at(app->bundleIdentifier());

		fs::path infoPlistPath(app->path());
		infoPlistPath.append("Info.plist");

		auto data = readFile(infoPlistPath.string().c_str());

		plist_t plist = nullptr;
		plist_from_memory((const char*)data.data(), (int)data.size(), &plist);
		if (plist == nullptr)
		{
			throw InstallError(InstallErrorCode::MissingInfoPlist);
		}

		plist_dict_set_item(plist, "CFBundleIdentifier", plist_new_string(profile->bundleIdentifier().c_str()));
		plist_dict_set_item(plist, "ALTBundleIdentifier", plist_new_string(app->bundleIdentifier().c_str()));

		if (additionalValues != NULL)
		{
			plist_dict_merge(&plist, additionalValues);
		}

		plist_t entitlements = profile->entitlements();
		if (entitlements != nullptr && plist_dict_get_item(entitlements, ALTEntitlementAppGroups) != nullptr)
		{
            plist_t appGroups = plist_copy(plist_dict_get_item(entitlements, ALTEntitlementAppGroups));
            plist_dict_set_item(plist, "ALTAppGroups", appGroups);
		}

		char* plistXML = nullptr;
		uint32_t length = 0;
		plist_to_xml(plist, &plistXML, &length);

		std::ofstream fout(infoPlistPath.string(), std::ios::out | std::ios::binary);
		fout.write(plistXML, length);
		fout.close();
	};

    return pplx::task<std::shared_ptr<Application>>([=]() {
        fs::path infoPlistPath(app->path());
        infoPlistPath.append("Info.plist");
        
        auto data = readFile(infoPlistPath.string().c_str());
        
        plist_t plist = nullptr;
        plist_from_memory((const char *)data.data(), (int)data.size(), &plist);
        if (plist == nullptr)
        {
            throw InstallError(InstallErrorCode::MissingInfoPlist);
        }
        
		plist_t additionalValues = plist_new_dict();

		std::string openAppURLScheme = "altstore-" + app->bundleIdentifier();

		plist_t allURLSchemes = plist_dict_get_item(plist, "CFBundleURLTypes");
		if (allURLSchemes == nullptr)
		{
			allURLSchemes = plist_new_array();
		}
		else
		{
			allURLSchemes = plist_copy(allURLSchemes);
		}

		plist_t altstoreURLScheme = plist_new_dict();
		plist_dict_set_item(altstoreURLScheme, "CFBundleTypeRole", plist_new_string("Editor"));
		plist_dict_set_item(altstoreURLScheme, "CFBundleURLName", plist_new_string(app->bundleIdentifier().c_str()));

		plist_t schemesNode = plist_new_array();
		plist_array_append_item(schemesNode, plist_new_string(openAppURLScheme.c_str()));
		plist_dict_set_item(altstoreURLScheme, "CFBundleURLSchemes", schemesNode);

		plist_array_append_item(allURLSchemes, altstoreURLScheme);
		plist_dict_set_item(additionalValues, "CFBundleURLTypes", allURLSchemes);

		if (app->isAltStoreApp())
		{
			plist_dict_set_item(additionalValues, "ALTDeviceID", plist_new_string(device->identifier().c_str()));

			auto serverID = this->serverID();
			plist_dict_set_item(additionalValues, "ALTServerID", plist_new_string(serverID.c_str()));

			auto machineIdentifier = certificate->machineIdentifier();
			if (machineIdentifier.has_value())
			{
				auto encryptedData = certificate->encryptedP12Data(*machineIdentifier);
				if (encryptedData.has_value())
				{
					plist_dict_set_item(additionalValues, "ALTCertificateID", plist_new_string(certificate->serialNumber().c_str()));

					// Embed encrypted certificate in app bundle.
					fs::path certificatePath(app->path());
					certificatePath.append("ALTCertificate.p12");

					std::ofstream fout(certificatePath.string(), std::ios::out | std::ios::binary);
					fout.write((const char*)encryptedData->data(), encryptedData->size());
					fout.close();
				}
			}
		}
        else if (plist_dict_get_item(plist, "ALTDeviceID") != NULL)
        {
            // There is an ALTDeviceID entry, so assume the app is using AltKit and replace it with the device's UDID.
            plist_dict_set_item(additionalValues, "ALTDeviceID", plist_new_string(device->identifier().c_str()));

			auto serverID = this->serverID();
			plist_dict_set_item(additionalValues, "ALTServerID", plist_new_string(serverID.c_str()));
        }

		prepareInfoPlist(app, additionalValues);

		for (auto appExtension : app->appExtensions())
		{
			prepareInfoPlist(appExtension, NULL);
		}

		std::vector<std::shared_ptr<ProvisioningProfile>> profiles;
		std::set<std::string> profileIdentifiers;
		for (auto pair : profilesByBundleID)
		{
			profiles.push_back(pair.second);
			profileIdentifiers.insert(pair.second->bundleIdentifier());
		}
        
        Signer signer(team, certificate);
        signer.SignApp(app->path(), profiles);

		std::optional<std::set<std::string>> activeProfiles = std::nullopt;
		if (team->type() == Team::Type::Free && app->isAltStoreApp())
		{
			activeProfiles = profileIdentifiers;
		}
        
		return DeviceManager::instance()->InstallApp(app->path(), device->identifier(), activeProfiles, [](double progress) {
			odslog("Installation Progress: " << progress);
		})
		.then([app] {
			return app;
		});
    });
}

pplx::task<void> AltServerApp::EnableJIT(InstalledApp app, std::shared_ptr<Device> device)
{
	return this->PrepareDevice(device)
	.then([=] {
		return DeviceManager::instance()->StartDebugConnection(device);
	})
	.then([=](std::shared_ptr<DebugConnection> debugConnection) {
		return debugConnection->EnableUnsignedCodeExecution(app.executableName())
		.then([debugConnection]() {
				debugConnection->Disconnect();
		});
	})
	.then([=](pplx::task<void> task) {
		try {
			task.get();

			this->ShowAlert(
				"Successfully enabled JIT for " + app.name() + "!",
				"JIT will remain enabled until you quit the app. You can now disconnect " + device->name() + " from your computer."
			);
		}
        catch (std::exception& exception)
        {
            auto localizedFailure = "JIT could not be enabled for " + app.name() + ".";
            this->ShowErrorAlert(exception, localizedFailure);
        }
	});
}

void AltServerApp::ShowNotification(std::string title, std::string message)
{
	HICON icon = (HICON)LoadImage(this->instanceHandle(), MAKEINTRESOURCE(IMG_MENUBAR), IMAGE_ICON, 0, 0, LR_MONOCHROME);

	NOTIFYICONDATA niData;
	ZeroMemory(&niData, sizeof(NOTIFYICONDATA));
	niData.uVersion = NOTIFYICON_VERSION_4;
	niData.cbSize = sizeof(NOTIFYICONDATA);
	niData.guidItem = _notificationIconGUID;
	niData.uFlags = NIF_ICON | NIF_GUID | NIF_MESSAGE; // NIF_MESSAGE required in order for main menu to appear.
	niData.hWnd = this->windowHandle();
	niData.hIcon = icon;
	niData.uCallbackMessage = WM_USER + 1;
	niData.uTimeout = 3000;
	niData.dwInfoFlags = NIIF_INFO;
	StringCchCopy(niData.szInfoTitle, ARRAYSIZE(niData.szInfoTitle), WideStringFromString(title).c_str());
	StringCchCopy(niData.szInfo, ARRAYSIZE(niData.szInfo), WideStringFromString(message).c_str());

	if (title.size() > 0 || message.size() > 0)
	{
		// Only add NIF_INFO flag if we're actually showing a notification.
		niData.uFlags |= NIF_INFO;
	}

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

void AltServerApp::ShowAlert(std::string title, std::string message)
{
	MessageBoxW(NULL, WideStringFromString(message).c_str(), WideStringFromString(title).c_str(), MB_OK);
}

void AltServerApp::ShowErrorAlert(std::exception& exception, std::string localizedTitle)
{
	std::string message;

	try
	{
		Error& error = dynamic_cast<Error&>(exception);
		this->_helpError = &error;

		std::vector<std::string> messageComponents = { error.localizedDescription() };

		if (error.localizedRecoverySuggestion().has_value())
		{
			auto localizedRecoverySuggestion = *error.localizedRecoverySuggestion();
			messageComponents.push_back(localizedRecoverySuggestion);
		}

		message = std::accumulate(messageComponents.begin(), messageComponents.end(), std::string(), [](auto& a, auto& b) {
			return a + (a.length() > 0 ? "\n\n" : "") + b;
		});
	}
	catch (std::bad_cast)
	{
		message = exception.what();
	}

	auto wideTitle = WideStringFromString(localizedTitle);
	auto wideMessage = WideStringFromString(message);

	MSGBOXPARAMSW parameters = {};
	parameters.cbSize = sizeof(parameters);
	parameters.lpszText = wideMessage.c_str();
	parameters.lpszCaption = wideTitle.c_str();
	parameters.lpfnMsgBoxCallback = ErrorMessageBoxCallback;

	if (this->_helpError != NULL)
	{
		// Only show "Help" button if exception is Error subclass.
		parameters.dwStyle = MB_HELP | MB_ICONWARNING;
	}
	else
	{
		parameters.dwStyle = MB_ICONWARNING;
	}

	MessageBoxIndirectW(&parameters);

	this->_helpError = NULL;
}

void AltServerApp::ShowInstallationNotification(std::string appName, std::string deviceName)
{
	std::stringstream ssTitle;
	ssTitle << "Installing " << appName << " to " << deviceName << "...";

	std::stringstream ssMessage;
	ssMessage << "This may take a few seconds.";

	this->ShowNotification(ssTitle.str(), ssMessage.str());
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

void AltServerApp::HandleAnisetteError(AnisetteError& error)
{
	switch ((AnisetteErrorCode)error.code())
	{
	case AnisetteErrorCode::iTunesNotInstalled:
	case AnisetteErrorCode::iCloudNotInstalled:
	{
		wchar_t* title = NULL;
		wchar_t *message = NULL;
		std::string downloadURL;

		switch ((AnisetteErrorCode)error.code())
		{
		case AnisetteErrorCode::iTunesNotInstalled: 
		{
			title = (wchar_t *)L"iTunes Not Found";
			message = (wchar_t*)LR"(Download the latest version of iTunes from apple.com (not the Microsoft Store) in order to continue using AltServer.

If you already have iTunes installed, please locate the "Apple" folder that was installed with iTunes. This can normally be found at:

)";

			BOOL is64Bit = false;

			if (GetProcAddress(GetModuleHandle(TEXT("kernel32")), "IsWow64Process2") != NULL)
			{
				USHORT pProcessMachine = 0;
				USHORT pNativeMachine = 0;

				if (IsWow64Process2(GetCurrentProcess(), &pProcessMachine, &pNativeMachine) != 0 && pProcessMachine != IMAGE_FILE_MACHINE_UNKNOWN)
				{
					is64Bit = true;
				}
				else
				{
					is64Bit = false;
				}
			}
			else if (GetProcAddress(GetModuleHandle(TEXT("kernel32")), "IsWow64Process") != NULL)
			{
				IsWow64Process(GetCurrentProcess(), &is64Bit);
			}
			else
			{
				is64Bit = false;
			}

			if (is64Bit)
			{
				// 64-bit
				downloadURL = "https://www.apple.com/itunes/download/win64";
			}
			else
			{
				// 32-bit
				downloadURL = "https://www.apple.com/itunes/download/win32";
			}

			break;
		}

		case AnisetteErrorCode::iCloudNotInstalled: 
			title = (wchar_t*)L"iCloud Not Found";
			message = (wchar_t*)LR"(Download the latest version of iCloud from apple.com (not the Microsoft Store) in order to continue using AltServer.

If you already have iCloud installed, please locate the "Apple" folder that was installed with iCloud. This can normally be found at:

)";
			downloadURL = "https://secure-appldnld.apple.com/windows/061-91601-20200323-974a39d0-41fc-4761-b571-318b7d9205ed/iCloudSetup.exe";
			break;
		}

		std::wstring completeMessage(message);
		completeMessage += WideStringFromString(this->defaultAppleFolderPath());

		std::map<std::string, std::wstring> parameters = { {"title", title}, {"message", completeMessage} };

		int result = DialogBoxParam(NULL, MAKEINTRESOURCE(ID_ICLOUD_MISSING_64), NULL, InstallDlgProc, (LPARAM)&parameters);
		if (result == IDOK)
		{
			ShellExecuteA(NULL, "open", downloadURL.c_str(), NULL, NULL, SW_SHOWNORMAL);
		}
		else if (result == ID_FOLDER)
		{
			std::string folderPath = this->BrowseForFolder(L"Choose the 'Apple' folder that contains both 'Apple Application Support' and 'Internet Services'. This can normally be found at: " + WideStringFromString(this->defaultAppleFolderPath()), this->appleFolderPath());
			if (folderPath.size() == 0)
			{
				return;
			}

			odslog("Chose Apple folder: " << folderPath);

			this->setAppleFolderPath(folderPath);
		}

		break;
	}

	case AnisetteErrorCode::MissingApplicationSupportFolder:
	case AnisetteErrorCode::MissingAOSKit:
	case AnisetteErrorCode::MissingFoundation:
	case AnisetteErrorCode::MissingObjc:
	{
		std::wstring message = L"Please locate the 'Apple' folder installed with iTunes to continue using AltServer.\n\nThis can normally be found at:\n";
		message += WideStringFromString(this->defaultAppleFolderPath());

		int result = MessageBoxW(NULL, message.c_str(), WideStringFromString(error.localizedDescription()).c_str(), MB_OKCANCEL);
		if (result != IDOK)
		{
			return;
		}

		std::string folderPath = this->BrowseForFolder(L"Choose the 'Apple' folder that contains both 'Apple Application Support' and 'Internet Services'. This can normally be found at: " + WideStringFromString(this->defaultAppleFolderPath()), this->appleFolderPath());
		if (folderPath.size() == 0)
		{
			return;
		}

		odslog("Chose Apple folder: " << folderPath);

		this->setAppleFolderPath(folderPath);

		break;
	}

	case AnisetteErrorCode::InvalidiTunesInstallation:
	{
		this->ShowAlert("Invalid iTunes Installation", error.localizedDescription());
		break;
	}
	}
}

HWND AltServerApp::windowHandle() const
{
	return _windowHandle;
}

HINSTANCE AltServerApp::instanceHandle() const
{
	return _instanceHandle;
}

Error* AltServerApp::helpError() const
{
	return _helpError;
}

bool AltServerApp::boolValueForRegistryKey(std::string key) const
{
	auto value = GetRegistryBoolValue(key.c_str());
	return value;
}

void AltServerApp::setBoolValueForRegistryKey(bool value, std::string key)
{
	SetRegistryBoolValue(key.c_str(), value);
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

std::string AltServerApp::defaultAppleFolderPath() const
{
	wchar_t* programFilesCommonDirectory;
	SHGetKnownFolderPath(FOLDERID_ProgramFilesCommon, 0, NULL, &programFilesCommonDirectory);

	fs::path appleDirectoryPath(programFilesCommonDirectory);
	appleDirectoryPath.append("Apple");

	return appleDirectoryPath.string();
}

std::string AltServerApp::appleFolderPath() const
{
	auto appleFolderPath = GetRegistryStringValue(APPLE_FOLDER_KEY);
	if (appleFolderPath.size() != 0)
	{
		return appleFolderPath;
	}

	return this->defaultAppleFolderPath();
}

void AltServerApp::setAppleFolderPath(std::string appleFolderPath)
{
	SetRegistryStringValue(APPLE_FOLDER_KEY, appleFolderPath);
}

std::string AltServerApp::internetServicesFolderPath() const
{
	fs::path internetServicesDirectoryPath(this->appleFolderPath());
	internetServicesDirectoryPath.append("Internet Services");
	return internetServicesDirectoryPath.string();
}

std::string AltServerApp::applicationSupportFolderPath() const
{
	fs::path applicationSupportDirectoryPath(this->appleFolderPath());
	applicationSupportDirectoryPath.append("Apple Application Support");
	return applicationSupportDirectoryPath.string();
}

fs::path AltServerApp::appDataDirectoryPath() const
{
	wchar_t* programDataDirectory;
	SHGetKnownFolderPath(FOLDERID_ProgramData, 0, NULL, &programDataDirectory);

	fs::path altserverDirectoryPath(programDataDirectory);
	altserverDirectoryPath.append("AltServer");

	if (!fs::exists(altserverDirectoryPath))
	{
		fs::create_directory(altserverDirectoryPath);
	}

	return altserverDirectoryPath;
}

fs::path AltServerApp::certificatesDirectoryPath() const
{
	auto appDataPath = this->appDataDirectoryPath();
	auto certificatesDirectoryPath = appDataPath.append("Certificates");

	if (!fs::exists(certificatesDirectoryPath))
	{
		fs::create_directory(certificatesDirectoryPath);
	}

	return certificatesDirectoryPath;
}

fs::path AltServerApp::developerDisksDirectoryPath() const
{
	auto appDataPath = this->appDataDirectoryPath();
	auto developerDisksDirectoryPath = appDataPath.append("DeveloperDiskImages");

	if (!fs::exists(developerDisksDirectoryPath))
	{
		fs::create_directory(developerDisksDirectoryPath);
	}

	return developerDisksDirectoryPath;
}