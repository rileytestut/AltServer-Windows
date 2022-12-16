//
//  DeviceManager.cpp
//  AltServer-Windows
//
//  Created by Riley Testut on 8/13/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include "DeviceManager.hpp"

#include <libimobiledevice/libimobiledevice.h>
#include <libimobiledevice/lockdown.h>
#include <libimobiledevice/installation_proxy.h>
#include <libimobiledevice/notification_proxy.h>
#include <libimobiledevice/afc.h>
#include <libimobiledevice/misagent.h>
#include <libimobiledevice/src/idevice.h>

#include <filesystem>

#include <iostream>
#include <fstream>
#include <sstream>
#include <condition_variable>

#include "Archiver.hpp"
#include "ServerError.hpp"
#include "ProvisioningProfile.hpp"
#include "Application.hpp"

#include "ConnectionError.hpp"
#include "WindowsError.h"

#include <WinSock2.h>

#define DEVICE_LISTENING_SOCKET 28151

#define odslog(msg) { std::wstringstream ss; ss << msg << std::endl; OutputDebugStringW(ss.str().c_str()); }

extern std::string StringFromWideString(std::wstring wideString);
extern std::wstring WideStringFromString(std::string string);

void DeviceManagerUpdateStatus(plist_t command, plist_t status, void *udid);
void DeviceManagerUpdateAppDeletionStatus(plist_t command, plist_t status, void* udid);
void DeviceDidChangeConnectionStatus(const idevice_event_t* event, void* user_data);
ssize_t DeviceManagerUploadFile(void* buffer, size_t size, void* user_data);

namespace fs = std::filesystem;

extern std::string make_uuid();
extern std::string temporary_directory();
extern std::vector<unsigned char> readFile(const char* filename);

/// Returns a version of 'str' where every occurrence of
/// 'find' is substituted by 'replace'.
/// - Inspired by James Kanze.
/// - http://stackoverflow.com/questions/20406744/
std::string replace_all(
	const std::string& str,   // where to work
	const std::string& find,  // substitute 'find'
	const std::string& replace //      by 'replace'
) {
	using namespace std;
	string result;
	size_t find_len = find.size();
	size_t pos, from = 0;
	while (string::npos != (pos = str.find(find, from))) {
		result.append(str, from, pos - from);
		result.append(replace);
		from = pos + find_len;
	}
	result.append(str, from, string::npos);
	return result;
}

DeviceManager* DeviceManager::_instance = nullptr;

DeviceManager* DeviceManager::instance()
{
    if (_instance == 0)
    {
        _instance = new DeviceManager();
    }
    
    return _instance;
}

DeviceManager::DeviceManager()
{
}

void DeviceManager::Start()
{
	idevice_event_subscribe(DeviceDidChangeConnectionStatus, NULL);
}

pplx::task<void> DeviceManager::InstallApp(std::string appFilepath, std::string deviceUDID, std::optional<std::set<std::string>> activeProfiles, std::function<void(double)> progressCompletionHandler)
{
	return pplx::task<void>([=] {
		// Enforce only one installation at a time.
		this->_mutex.lock();

		auto UUID = make_uuid();

		char* uuidString = (char*)malloc(UUID.size() + 1);
		strncpy(uuidString, (const char*)UUID.c_str(), UUID.size());
		uuidString[UUID.size()] = '\0';

		idevice_t device = nullptr;
		lockdownd_client_t client = NULL;
		instproxy_client_t ipc = NULL;
		afc_client_t afc = NULL;
		misagent_client_t mis = NULL;
		lockdownd_service_descriptor_t service = NULL;

		fs::path temporaryDirectory(temporary_directory());
		temporaryDirectory.append(make_uuid());

		fs::create_directory(temporaryDirectory);

		auto installedProfiles = std::make_shared<std::vector<std::shared_ptr<ProvisioningProfile>>>();
		auto cachedProfiles = std::make_shared<std::map<std::string, std::shared_ptr<ProvisioningProfile>>>();

		auto finish = [this, installedProfiles, cachedProfiles, activeProfiles, temporaryDirectory, &uuidString]
		(idevice_t device, lockdownd_client_t client, instproxy_client_t ipc, afc_client_t afc, misagent_client_t mis, lockdownd_service_descriptor_t service)
		{
			auto cleanUp = [=]() {
				instproxy_client_free(ipc);
				afc_client_free(afc);
				lockdownd_client_free(client);
				misagent_client_free(mis);
				idevice_free(device);
				lockdownd_service_descriptor_free(service);

				free(uuidString);

				this->_mutex.unlock();
				fs::remove_all(temporaryDirectory);
			};

			try
			{
				if (activeProfiles.has_value())
				{
					// Remove installed provisioning profiles if they're not active.
					for (auto& installedProfile : *installedProfiles)
					{
						if (std::count(activeProfiles->begin(), activeProfiles->end(), installedProfile->bundleIdentifier()) == 0)
						{
							this->RemoveProvisioningProfile(installedProfile, mis);
						}
					}
				}

				for (auto& pair : *cachedProfiles)
				{
					BOOL reinstall = true;

					for (auto& installedProfile : *installedProfiles)
					{
						if (installedProfile->bundleIdentifier() == pair.second->bundleIdentifier())
						{
							// Don't reinstall cached profile because it was installed with app.
							reinstall = false;
							break;
						}
					}

					if (reinstall)
					{
						this->InstallProvisioningProfile(pair.second, mis);
					}					
				}				
			}
			catch (std::exception& exception)
			{
				cleanUp();
				throw;
			}

			// Clean up outside scope so if an exception is thrown, we don't
			// catch it ourselves again.
			cleanUp();
		};

		try
		{
			fs::path filepath(appFilepath);

			auto extension = filepath.extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
				return std::tolower(c);
				});

			fs::path appBundlePath;

			if (extension == ".app")
			{
				appBundlePath = filepath;
			}
			else if (extension == ".ipa")
			{
				std::cout << "Unzipping .ipa..." << std::endl;
				appBundlePath = UnzipAppBundle(filepath.string(), temporaryDirectory.string());
			}
			else
			{
				throw SignError(SignErrorCode::InvalidApp);
			}

			std::shared_ptr<Application> application = std::make_shared<Application>(appBundlePath.string());
			if (application == NULL)
			{
				throw SignError(SignErrorCode::InvalidApp);
			}

			if (application->provisioningProfile())
			{
				installedProfiles->push_back(application->provisioningProfile());
			}

			for (auto& appExtension : application->appExtensions())
			{
				if (appExtension->provisioningProfile())
				{
					installedProfiles->push_back(appExtension->provisioningProfile());
				}
			}

			/* Find Device */

			if (idevice_new_with_options(&device, deviceUDID.c_str(), (enum idevice_options)((int)IDEVICE_LOOKUP_NETWORK | (int)IDEVICE_LOOKUP_USBMUX)) != IDEVICE_E_SUCCESS)
			{
				throw ServerError(ServerErrorCode::DeviceNotFound);
			}

			/* Connect to Device */
			if (lockdownd_client_new_with_handshake(device, &client, "altserver") != LOCKDOWN_E_SUCCESS)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}

			/* Connect to Installation Proxy */
			if ((lockdownd_start_service(client, "com.apple.mobile.installation_proxy", &service) != LOCKDOWN_E_SUCCESS) || service == NULL)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}

			if (instproxy_client_new(device, service, &ipc) != INSTPROXY_E_SUCCESS)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}

			if (service)
			{
				lockdownd_service_descriptor_free(service);
				service = NULL;
			}


			/* Connect to Misagent */
			// Must connect now, since if we take too long writing files to device, connecting may fail later when managing profiles.
			if (lockdownd_start_service(client, "com.apple.misagent", &service) != LOCKDOWN_E_SUCCESS || service == NULL)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}

			if (misagent_client_new(device, service, &mis) != MISAGENT_E_SUCCESS)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}


			/* Connect to AFC service */
			if ((lockdownd_start_service(client, "com.apple.afc", &service) != LOCKDOWN_E_SUCCESS) || service == NULL)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}

			if (afc_client_new(device, service, &afc) != AFC_E_SUCCESS)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}

			fs::path stagingPath("PublicStaging");

			/* Prepare for installation */
			char** files = NULL;
			if (afc_get_file_info(afc, (const char*)stagingPath.c_str(), &files) != AFC_E_SUCCESS)
			{
				if (afc_make_directory(afc, (const char*)stagingPath.c_str()) != AFC_E_SUCCESS)
				{
					throw ServerError(ServerErrorCode::DeviceWriteFailed);
				}
			}

			if (files)
			{
				int i = 0;

				while (files[i])
				{
					free(files[i]);
					i++;
				}

				free(files);
			}

			std::cout << "Writing to device..." << std::endl;

			plist_t options = instproxy_client_options_new();
			instproxy_client_options_add(options, "PackageType", "Developer", NULL);

			fs::path destinationPath = stagingPath.append(appBundlePath.filename().string());

			int numberOfFiles = 0;
			for (auto& item : fs::recursive_directory_iterator(appBundlePath))
			{
				if (item.is_regular_file())
				{
					numberOfFiles++;
				}				
			}

			int writtenFiles = 0;

			try
			{
				this->WriteDirectory(afc, appBundlePath.string(), destinationPath.string(), [numberOfFiles, &writtenFiles, &progressCompletionHandler](std::string filepath) {
					writtenFiles++;

					double progress = (double)writtenFiles / (double)numberOfFiles;
					double weightedProgress = progress * 0.75;
					progressCompletionHandler(weightedProgress);
				});
			}
			catch (ServerError& e)
			{
				if (application->bundleIdentifier().find("science.xnu.undecimus") != std::string::npos)
				{
					auto userInfo = e.userInfo();
					userInfo["NSLocalizedRecoverySuggestion"] = "Make sure Windows real-time protection is disabled on your computer then try again.";

					throw ServerError((ServerErrorCode)e.code(), userInfo);
				}	
				else
				{
					throw;
				}				
			}
			catch (std::exception& exception)
			{
				if (application->bundleIdentifier().find("science.xnu.undecimus") != std::string::npos && 
					std::string(exception.what()) == std::string("vector<T> too long"))
				{
					std::shared_ptr<Error> underlyingError(new ExceptionError(exception));
					throw WindowsError(WindowsErrorCode::WindowsDefenderBlockedCommunication, { {NSUnderlyingErrorKey, underlyingError } });
				}
				else
				{
					throw;
				}
			}

			std::cout << "Finished writing to device." << std::endl;


			if (service)
			{
				lockdownd_service_descriptor_free(service);
				service = NULL;
			}

			/* Provisioning Profiles */			
			bool shouldManageProfiles = (activeProfiles.has_value() || (application->provisioningProfile() != NULL && application->provisioningProfile()->isFreeProvisioningProfile()));
			if (shouldManageProfiles)
			{				
				// Free developer account was used to sign this app, so we need to remove all
				// provisioning profiles in order to remain under sideloaded app limit.

				auto removedProfiles = this->RemoveAllFreeProvisioningProfilesExcludingBundleIdentifiers({}, mis);
				for (auto& pair : removedProfiles)
				{
					if (activeProfiles.has_value())
					{
						if (activeProfiles->count(pair.first) > 0)
						{
							// Only cache active profiles to reinstall afterwards.
							(*cachedProfiles)[pair.first] = pair.second;
						}
					}
					else
					{
						// Cache all profiles to reinstall afterwards if we didn't provide activeProfiles.
						(*cachedProfiles)[pair.first] = pair.second;
					}
				}				
			}

			lockdownd_client_free(client);
			client = NULL;

			std::mutex waitingMutex;
			std::condition_variable cv;

			std::optional<ServerError> serverError = std::nullopt;

			bool didBeginInstalling = false;
			bool didFinishInstalling = false;

			// Capture &finish by reference to avoid implicit copies of installedProfiles and cachedProfiles, resulting in memory leaks.
			this->_installationProgressHandlers[UUID] = [device, client, ipc, afc, mis, service, application, &finish, &progressCompletionHandler,
				&waitingMutex, &cv, &didBeginInstalling, &didFinishInstalling, &serverError](double progress, int resultCode, char *name, char *description) {
				double weightedProgress = progress * 0.25;
				double adjustedProgress = weightedProgress + 0.75;

				if (progress == 0 && didBeginInstalling)
				{
					if (resultCode != 0 || name != NULL)
					{
						if (resultCode == -402620383)
						{
							std::map<std::string, std::any> userInfo = {
                                { NSLocalizedDescriptionKey, description },
							};
							serverError = std::make_optional<ServerError>(ServerErrorCode::MaximumFreeAppLimitReached, userInfo);
						}
						else
						{
							std::string errorName(name);

							if (errorName == "DeviceOSVersionTooLow")
							{
								Device::Type deviceType = Device::Type::iPhone;
								if (application->supportedDeviceTypes() & Device::Type::AppleTV)
								{
									// App supports tvOS, so assume we're installing to Apple TV (because there are no "universal" tvOS binaries).
									deviceType = Device::Type::AppleTV;
								}

								auto osName = ALTOperatingSystemNameForDeviceType(deviceType);
								if (!osName.has_value())
								{
									osName = "iOS";
								}

								std::shared_ptr<Error> underlyingError(new LocalizedInstallationError(resultCode, description));

								std::map<std::string, std::any> userInfo = {
									{AppNameErrorKey, application->name()},
									{OperatingSystemNameErrorKey, *osName},
									{OperatingSystemVersionErrorKey, application->minimumOSVersion().stringValue()},
									{NSUnderlyingErrorKey, underlyingError}
								};
								serverError = std::make_optional<ServerError>(ServerErrorCode::UnsupportediOSVersion, userInfo);
							}
							else
							{
								std::shared_ptr<Error> localizedError(new LocalizedInstallationError(resultCode, description));

								std::map<std::string, std::any> userInfo = { {NSUnderlyingErrorKey, localizedError} };
								serverError = std::make_optional<ServerError>(ServerErrorCode::InstallationFailed, userInfo);
							}
						}
					}

					std::lock_guard<std::mutex> lock(waitingMutex);
					didFinishInstalling = true;
					cv.notify_all();
				}
				else
				{
					progressCompletionHandler(adjustedProgress);
				}

				didBeginInstalling = true;
			};

			auto narrowDestinationPath = StringFromWideString(destinationPath.c_str());
			std::replace(narrowDestinationPath.begin(), narrowDestinationPath.end(), '\\', '/');

			instproxy_install(ipc, narrowDestinationPath.c_str(), options, DeviceManagerUpdateStatus, uuidString);
			instproxy_client_options_free(options);

			// Wait until we're finished installing;
			std::unique_lock<std::mutex> lock(waitingMutex);
			cv.wait(lock, [&didFinishInstalling] { return didFinishInstalling; });

			lock.unlock();

			if (serverError.has_value())
			{
				throw serverError.value();
			}		
		}
		catch (std::exception& exception)
		{
			try
			{
				// MUST finish so we restore provisioning profiles.
				finish(device, client, ipc, afc, mis, service);
			}
			catch (std::exception& e)
			{
				// Ignore since we already caught an exception during installation.
			}

			throw;
		}

		// Call finish outside try-block so if an exception is thrown, we don't
		// catch it ourselves and "finish" again.
		finish(device, client, ipc, afc, mis, service);
	});
}

void DeviceManager::WriteDirectory(afc_client_t client, std::string directoryPath, std::string destinationPath, std::function<void(std::string)> wroteFileCallback)
{
	std::replace(destinationPath.begin(), destinationPath.end(), '\\', '/');

    afc_make_directory(client, destinationPath.c_str());
    
    for (auto& file : fs::directory_iterator(directoryPath))
    {
        auto filepath = file.path();
        
        if (fs::is_directory(filepath))
        {
            auto destinationDirectoryPath = fs::path(destinationPath).append(filepath.filename().string());
            this->WriteDirectory(client, filepath.string(), destinationDirectoryPath.string(), wroteFileCallback);
        }
        else
        {
            auto destinationFilepath = fs::path(destinationPath).append(filepath.filename().string());
            this->WriteFile(client, filepath.string(), destinationFilepath.string(), wroteFileCallback);
        }
    }
}

void DeviceManager::WriteFile(afc_client_t client, std::string filepath, std::string destinationPath, std::function<void(std::string)> wroteFileCallback)
{
	std::replace(destinationPath.begin(), destinationPath.end(), '\\', '/');
	destinationPath = replace_all(destinationPath, "__colon__", ":");

	odslog("Writing File: " << filepath.c_str() << " to: " << destinationPath.c_str());
    
    auto data = readFile(filepath.c_str());
    
    uint64_t af = 0;
    if ((afc_file_open(client, destinationPath.c_str(), AFC_FOPEN_WRONLY, &af) != AFC_E_SUCCESS) || af == 0)
    {
        throw ServerError(ServerErrorCode::DeviceWriteFailed);
    }
    
    uint32_t bytesWritten = 0;
    
    while (bytesWritten < data.size())
    {
        uint32_t count = 0;
        
        if (afc_file_write(client, af, (const char *)data.data() + bytesWritten, (uint32_t)data.size() - bytesWritten, &count) != AFC_E_SUCCESS)
        {
            throw ServerError(ServerErrorCode::DeviceWriteFailed);
        }
        
        bytesWritten += count;
    }
    
    if (bytesWritten != data.size())
    {
        throw ServerError(ServerErrorCode::DeviceWriteFailed);
    }
    
    afc_file_close(client, af);

	wroteFileCallback(filepath);
}

pplx::task<void> DeviceManager::RemoveApp(std::string bundleIdentifier, std::string deviceUDID)
{
	return pplx::task<void>([=] {
		idevice_t device = NULL;
		lockdownd_client_t client = NULL;
		instproxy_client_t ipc = NULL;
		lockdownd_service_descriptor_t service = NULL;

		auto cleanUp = [&]() {
			if (service) {
				lockdownd_service_descriptor_free(service);
			}

			if (ipc) {
				instproxy_client_free(ipc);
			}

			if (client) {
				lockdownd_client_free(client);
			}

			if (device) {
				idevice_free(device);
			}
		};

		try 
		{
			/* Find Device */
			if (idevice_new_with_options(&device, deviceUDID.c_str(), (enum idevice_options)((int)IDEVICE_LOOKUP_NETWORK | (int)IDEVICE_LOOKUP_USBMUX)) != IDEVICE_E_SUCCESS)
			{
				throw ServerError(ServerErrorCode::DeviceNotFound);
			}

			/* Connect to Device */
			if (lockdownd_client_new_with_handshake(device, &client, "altserver") != LOCKDOWN_E_SUCCESS)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}

			/* Connect to Installation Proxy */
			if ((lockdownd_start_service(client, "com.apple.mobile.installation_proxy", &service) != LOCKDOWN_E_SUCCESS) || service == NULL)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}

			if (instproxy_client_new(device, service, &ipc) != INSTPROXY_E_SUCCESS)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}

			if (service)
			{
				lockdownd_service_descriptor_free(service);
				service = NULL;
			}

			auto UUID = make_uuid();

			char* uuidString = (char*)malloc(UUID.size() + 1);
			strncpy(uuidString, (const char*)UUID.c_str(), UUID.size());
			uuidString[UUID.size()] = '\0';

			std::mutex waitingMutex;
			std::condition_variable cv;

			std::optional<ServerError> serverError = std::nullopt;

			bool didFinishInstalling = false;

			this->_deletionCompletionHandlers[UUID] = [this, &waitingMutex, &cv, &didFinishInstalling, &serverError, &uuidString]
			(bool success, int errorCode, char* errorName, char* errorDescription) {
				if (!success)
				{
					std::shared_ptr<Error> underlyingError(new LocalizedInstallationError(errorCode, errorDescription));
					std::map<std::string, std::any> userInfo = { {NSUnderlyingErrorKey, underlyingError} };
					serverError = std::make_optional<ServerError>(ServerErrorCode::AppDeletionFailed, userInfo);
				}

				std::lock_guard<std::mutex> lock(waitingMutex);
				didFinishInstalling = true;
				cv.notify_all();

				free(uuidString);
			};

			instproxy_uninstall(ipc, bundleIdentifier.c_str(), NULL, DeviceManagerUpdateAppDeletionStatus, uuidString);

			// Wait until we're finished installing;
			std::unique_lock<std::mutex> lock(waitingMutex);
			cv.wait(lock, [&didFinishInstalling] { return didFinishInstalling; });

			lock.unlock();

			if (serverError.has_value())
			{
				throw serverError.value();
			}

			cleanUp();
		}
		catch (std::exception& exception) {
			cleanUp();
			throw;
		}
	});
}

pplx::task<std::shared_ptr<WiredConnection>> DeviceManager::StartWiredConnection(std::shared_ptr<Device> altDevice)
{
	return pplx::create_task([=]() -> std::shared_ptr<WiredConnection> {
		idevice_t device = NULL;
		idevice_connection_t connection = NULL;

		/* Find Device */
		if (idevice_new_with_options(&device, altDevice->identifier().c_str(), IDEVICE_LOOKUP_USBMUX) != IDEVICE_E_SUCCESS)
		{
			throw ServerError(ServerErrorCode::DeviceNotFound);
		}

		/* Connect to Listening Socket */
		if (idevice_connect(device, DEVICE_LISTENING_SOCKET, &connection) != IDEVICE_E_SUCCESS)
		{
			idevice_free(device);
			throw ServerError(ServerErrorCode::ConnectionFailed);
		}

		idevice_free(device);

		auto wiredConnection = std::make_shared<WiredConnection>(altDevice, connection);
		return wiredConnection;
	});
}

pplx::task<std::shared_ptr<DebugConnection>> DeviceManager::StartDebugConnection(std::shared_ptr<Device> device)
{
	auto debugConnection = std::make_shared<DebugConnection>(device);

	return debugConnection->Connect()
	.then([=]() {
		return debugConnection;
	});
}

pplx::task<void> DeviceManager::InstallProvisioningProfiles(std::vector<std::shared_ptr<ProvisioningProfile>> provisioningProfiles, std::string deviceUDID, std::optional<std::set<std::string>> activeProfiles)
{
	return pplx::task<void>([=] {
		// Enforce only one installation at a time.
		this->_mutex.lock();

		idevice_t device = NULL;
		lockdownd_client_t client = NULL;
		afc_client_t afc = NULL;
		misagent_client_t mis = NULL;
		lockdownd_service_descriptor_t service = NULL;

		auto cleanUp = [&]() {
			if (service) {
				lockdownd_service_descriptor_free(service);
			}

			if (mis) {
				misagent_client_free(mis);
			}

			if (afc) {
				afc_client_free(afc);
			}

			if (client) {
				lockdownd_client_free(client);
			}

			if (device) {
				idevice_free(device);
			}

			this->_mutex.unlock();
		};

		try
		{
			/* Find Device */
			if (idevice_new_with_options(&device, deviceUDID.c_str(), (enum idevice_options)((int)IDEVICE_LOOKUP_NETWORK | (int)IDEVICE_LOOKUP_USBMUX)) != IDEVICE_E_SUCCESS)
			{
				throw ServerError(ServerErrorCode::DeviceNotFound);
			}

			/* Connect to Device */
			if (lockdownd_client_new_with_handshake(device, &client, "altserver") != LOCKDOWN_E_SUCCESS)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}

			/* Connect to Misagent */
			if (lockdownd_start_service(client, "com.apple.misagent", &service) != LOCKDOWN_E_SUCCESS || service == NULL)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}

			if (misagent_client_new(device, service, &mis) != MISAGENT_E_SUCCESS)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}

			if (activeProfiles.has_value())
			{
				// Remove all non-active free provisioning profiles.

				auto excludedBundleIdentifiers = activeProfiles.value();
				for (auto& profile : provisioningProfiles)
				{
					// Ensure we DO remove old versions of profiles we're about to install, even if they are active.
					excludedBundleIdentifiers.erase(profile->bundleIdentifier());
				}

				this->RemoveAllFreeProvisioningProfilesExcludingBundleIdentifiers(excludedBundleIdentifiers, mis);
			}
			else
			{
				// Remove only older versions of provisioning profiles we're about to install.

				std::set<std::string> bundleIdentifiers;
				for (auto& profile : provisioningProfiles)
				{
					bundleIdentifiers.insert(profile->bundleIdentifier());
				}

				this->RemoveProvisioningProfiles(bundleIdentifiers, mis);
			}

			for (auto& provisioningProfile : provisioningProfiles)
			{
				this->InstallProvisioningProfile(provisioningProfile, mis);
			}

			cleanUp();
		}
		catch (std::exception &exception)
		{
			cleanUp();
			throw;
		}
	});
}

pplx::task<void> DeviceManager::RemoveProvisioningProfiles(std::set<std::string> bundleIdentifiers, std::string deviceUDID)
{
	return pplx::task<void>([=] {
		// Enforce only one removal at a time.
		this->_mutex.lock();

		idevice_t device = NULL;
		lockdownd_client_t client = NULL;
		afc_client_t afc = NULL;
		misagent_client_t mis = NULL;
		lockdownd_service_descriptor_t service = NULL;

		auto cleanUp = [&]() {
			if (service) {
				lockdownd_service_descriptor_free(service);
			}

			if (mis) {
				misagent_client_free(mis);
			}

			if (afc) {
				afc_client_free(afc);
			}

			if (client) {
				lockdownd_client_free(client);
			}

			if (device) {
				idevice_free(device);
			}

			this->_mutex.unlock();
		};

		try
		{
			/* Find Device */
			if (idevice_new_with_options(&device, deviceUDID.c_str(), (enum idevice_options)((int)IDEVICE_LOOKUP_NETWORK | (int)IDEVICE_LOOKUP_USBMUX)) != IDEVICE_E_SUCCESS)
			{
				throw ServerError(ServerErrorCode::DeviceNotFound);
			}

			/* Connect to Device */
			if (lockdownd_client_new_with_handshake(device, &client, "altserver") != LOCKDOWN_E_SUCCESS)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}

			/* Connect to Misagent */
			if (lockdownd_start_service(client, "com.apple.misagent", &service) != LOCKDOWN_E_SUCCESS || service == NULL)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}

			if (misagent_client_new(device, service, &mis) != MISAGENT_E_SUCCESS)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}

			this->RemoveProvisioningProfiles(bundleIdentifiers, mis);

			cleanUp();
		}
		catch (std::exception& exception)
		{
			cleanUp();
			throw;
		}
	});
}

std::map<std::string, std::shared_ptr<ProvisioningProfile>> DeviceManager::RemoveProvisioningProfiles(std::set<std::string> bundleIdentifiers, misagent_client_t mis)
{
	return this->RemoveAllProvisioningProfiles(bundleIdentifiers, std::nullopt, false, mis);
}

std::map<std::string, std::shared_ptr<ProvisioningProfile>> DeviceManager::RemoveAllFreeProvisioningProfilesExcludingBundleIdentifiers(std::set<std::string> excludedBundleIdentifiers, misagent_client_t mis)
{
	return this->RemoveAllProvisioningProfiles(std::nullopt, excludedBundleIdentifiers, true, mis);
}

std::map<std::string, std::shared_ptr<ProvisioningProfile>> DeviceManager::RemoveAllProvisioningProfiles(std::optional<std::set<std::string>> includedBundleIdentifiers, std::optional<std::set<std::string>> excludedBundleIdentifiers, bool limitedToFreeProfiles, misagent_client_t mis)
{
	std::map<std::string, std::shared_ptr<ProvisioningProfile>> ignoredProfiles;
	std::map<std::string, std::shared_ptr<ProvisioningProfile>> removedProfiles;

	auto provisioningProfiles = this->CopyProvisioningProfiles(mis);

	for (auto& provisioningProfile : provisioningProfiles)
	{
		if (limitedToFreeProfiles && !provisioningProfile->isFreeProvisioningProfile())
		{
			continue;
		}

		if (includedBundleIdentifiers.has_value() && includedBundleIdentifiers->count(provisioningProfile->bundleIdentifier()) == 0)
		{
			continue;
		}

		if (excludedBundleIdentifiers.has_value() && excludedBundleIdentifiers->count(provisioningProfile->bundleIdentifier()) > 0)
		{
			// This provisioning profile has an excluded bundle identifier.
			// Ignore it, unless we've already ignored one with the same bundle identifier,
			// in which case remove whichever profile is the oldest.

			auto previousProfile = ignoredProfiles[provisioningProfile->bundleIdentifier()];
			if (previousProfile != NULL)
			{
				auto expirationDateA = provisioningProfile->expirationDate();
				auto expirationDateB = previousProfile->expirationDate();

				// We've already ignored a profile with this bundle identifier,
				// so make sure we only ignore the newest one and remove the oldest one.
				BOOL newerThanPreviousProfile = (timercmp(&expirationDateA, &expirationDateB, >) != 0);
				auto oldestProfile = newerThanPreviousProfile ? previousProfile : provisioningProfile;
				auto newestProfile = newerThanPreviousProfile ? provisioningProfile : previousProfile;

				ignoredProfiles[provisioningProfile->bundleIdentifier()] = newestProfile;

				// Don't cache this profile or else it will be reinstalled, so just remove it without caching.
				this->RemoveProvisioningProfile(oldestProfile, mis);
			}
			else
			{
				ignoredProfiles[provisioningProfile->bundleIdentifier()] = provisioningProfile;
			}

			continue;
		}

		auto preferredProfile = removedProfiles[provisioningProfile->bundleIdentifier()];
		if (preferredProfile != nullptr)
		{
			auto expirationDateA = provisioningProfile->expirationDate();
			auto expirationDateB = preferredProfile->expirationDate();

			if (timercmp(&expirationDateA, &expirationDateB, > ) != 0)
			{
				// provisioningProfile exires later than preferredProfile, so use provisioningProfile instead.
				removedProfiles[provisioningProfile->bundleIdentifier()] = provisioningProfile;
			}
		}
		else
		{
			removedProfiles[provisioningProfile->bundleIdentifier()] = provisioningProfile;
		}

		this->RemoveProvisioningProfile(provisioningProfile, mis);
	}

	return removedProfiles;
}

void DeviceManager::InstallProvisioningProfile(std::shared_ptr<ProvisioningProfile> profile, misagent_client_t mis)
{
	plist_t pdata = plist_new_data((const char*)profile->data().data(), profile->data().size());

	misagent_error_t result = misagent_install(mis, pdata);
	plist_free(pdata);

	if (result == MISAGENT_E_SUCCESS)
	{
		odslog("Installed profile: " << WideStringFromString(profile->bundleIdentifier()) << " (" << WideStringFromString(profile->uuid()) << ")");
	}
	else
	{
		int statusCode = misagent_get_status_code(mis);
		odslog("Failed to install provisioning profile: " << WideStringFromString(profile->bundleIdentifier()) << " (" << WideStringFromString(profile->uuid()) << "). Error code: " << statusCode);

		switch (statusCode)
		{
		case -402620383:
		{
			throw ServerError(ServerErrorCode::MaximumFreeAppLimitReached);
		}

		default:
		{
			std::ostringstream oss;
			oss << "Could not install profile '" << profile->bundleIdentifier() << "'";

			std::string localizedFailure = oss.str();

			std::map<std::string, std::any> userInfo = {
					{ NSLocalizedFailureErrorKey, localizedFailure },
					{ ProvisioningProfileBundleIDErrorKey, profile->bundleIdentifier() },
					{ UnderlyingErrorCodeErrorKey, std::to_string(statusCode) }
			};

			throw ServerError(ServerErrorCode::UnderlyingError, userInfo);
		}
		}
	}
}

void DeviceManager::RemoveProvisioningProfile(std::shared_ptr<ProvisioningProfile> profile, misagent_client_t mis)
{
	std::string uuid = profile->uuid();
	std::transform(uuid.begin(), uuid.end(), uuid.begin(), [](unsigned char c) { return std::tolower(c); });

	misagent_error_t result = misagent_remove(mis, uuid.c_str());
	if (result == MISAGENT_E_SUCCESS)
	{
		odslog("Removed profile: " << WideStringFromString(profile->bundleIdentifier()) << " (" << WideStringFromString(profile->uuid()) << ")");
	}
	else
	{
		int statusCode = misagent_get_status_code(mis);
		odslog("Failed to remove provisioning profile: " << WideStringFromString(profile->bundleIdentifier()) << " (" << WideStringFromString(profile->uuid()) << "). Error code: " << statusCode);

		switch (statusCode)
		{
		case -402620405:
		{
			std::map<std::string, std::any> userInfo = {
				{ ProvisioningProfileBundleIDErrorKey, profile->bundleIdentifier() },
			};

			throw ServerError(ServerErrorCode::ProfileNotFound, userInfo);
		}

		default:
		{
			std::ostringstream oss;
			oss << "Could not remove profile '" << profile->bundleIdentifier() << "'";

			std::string localizedFailure = oss.str();

			std::map<std::string, std::any> userInfo = {
					{ NSLocalizedFailureErrorKey, localizedFailure },
					{ ProvisioningProfileBundleIDErrorKey, profile->bundleIdentifier() },
					{ UnderlyingErrorCodeErrorKey, std::to_string(statusCode) }
			};

			throw ServerError(ServerErrorCode::UnderlyingError, userInfo);
		}
		}
	}
}

std::vector<std::shared_ptr<ProvisioningProfile>> DeviceManager::CopyProvisioningProfiles(misagent_client_t mis)
{
	std::vector<std::shared_ptr<ProvisioningProfile>> provisioningProfiles;

	plist_t profiles = NULL;

	if (misagent_copy_all(mis, &profiles) != MISAGENT_E_SUCCESS)
	{
		int statusCode = misagent_get_status_code(mis);

		std::string localizedFailure = "Could not copy provisioning profiles.";

		std::map<std::string, std::any> userInfo = {
				{ NSLocalizedFailureErrorKey, localizedFailure },
				{ UnderlyingErrorCodeErrorKey, std::to_string(statusCode) }
		};

		throw ServerError(ServerErrorCode::UnderlyingError, userInfo);
	}

	uint32_t profileCount = plist_array_get_size(profiles);
	for (int i = 0; i < profileCount; i++)
	{
		plist_t profile = plist_array_get_item(profiles, i);
		if (plist_get_node_type(profile) != PLIST_DATA)
		{
			continue;
		}

		char* bytes = NULL;
		uint64_t length = 0;

		plist_get_data_val(profile, &bytes, &length);
		if (bytes == NULL)
		{
			continue;
		}

		std::vector<unsigned char> data;
		data.reserve(length);

		for (int i = 0; i < length; i++)
		{
			data.push_back(bytes[i]);
		}

		free(bytes);

		auto provisioningProfile = std::make_shared<ProvisioningProfile>(data);
		provisioningProfiles.push_back(provisioningProfile);
	}

	plist_free(profiles);

	return provisioningProfiles;
}

pplx::task<bool> DeviceManager::IsDeveloperDiskImageMounted(std::shared_ptr<Device> altDevice)
{
	return pplx::create_task([=]() -> bool {
		idevice_t device = NULL;
		instproxy_client_t ipc = NULL;
		lockdownd_client_t client = NULL;
		lockdownd_service_descriptor_t service = NULL;
		mobile_image_mounter_client_t mim = NULL;

		auto cleanUp = [&]() {
			if (mim) {
				if (device != NULL && device->conn_type == CONNECTION_USBMUXD)
				{
					// For some reason, calling this method over WiFi may freeze AltServer.
					// Nothing bad *seems* to happen if we don't call it though, so just limit it to wired connections.
					mobile_image_mounter_hangup(mim);
				}

				mobile_image_mounter_free(mim);
			}

			if (service) {
				lockdownd_service_descriptor_free(service);
			}

			if (client) {
				lockdownd_client_free(client);
			}

			if (ipc) {
				instproxy_client_free(ipc);
			}

			if (device) {
				idevice_free(device);
			}
		};

		try
		{
			/* Find Device */
			if (idevice_new_with_options(&device, altDevice->identifier().c_str(), (enum idevice_options)((int)IDEVICE_LOOKUP_NETWORK | (int)IDEVICE_LOOKUP_USBMUX)) != IDEVICE_E_SUCCESS)
			{
				throw ServerError(ServerErrorCode::DeviceNotFound);
			}

			/* Connect to Device */
			if (lockdownd_client_new_with_handshake(device, &client, "altserver") != LOCKDOWN_E_SUCCESS)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}

			/* Connect to Mobile Image Mounter Proxy */
			if ((lockdownd_start_service(client, "com.apple.mobile.mobile_image_mounter", &service) != LOCKDOWN_E_SUCCESS) || service == NULL)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}

			mobile_image_mounter_error_t err = mobile_image_mounter_new(device, service, &mim);
			if (err != MOBILE_IMAGE_MOUNTER_E_SUCCESS)
			{
				auto error = ConnectionError::errorForMobileImageMounterError(err, altDevice);
				if (error.has_value())
				{
					throw* error;
				}
			}

			plist_t result = NULL;
			err = mobile_image_mounter_lookup_image(mim, "Developer", &result);
			if (err != MOBILE_IMAGE_MOUNTER_E_SUCCESS)
			{
				auto error = ConnectionError::errorForMobileImageMounterError(err, altDevice);
				if (error.has_value())
				{
					throw *error;
				}
			}

			bool isMounted = false;

			plist_dict_iter it = NULL;
			plist_dict_new_iter(result, &it);

			char* key = NULL;
			plist_t subnode = NULL;
			plist_dict_next_item(result, it, &key, &subnode);

			while (subnode)
			{
				// If the ImageSignature key in the returned plist contains a subentry the disk image is already uploaded.
				// Hopefully this works for older iOS versions as well.
				// (via https://github.com/Schlaubischlump/LocationSimulator/blob/fdbd93ad16be5f69111b571d71ed6151e850144b/LocationSimulator/MobileDevice/devicemount/deviceimagemounter.c)
				plist_type type = plist_get_node_type(subnode);
				if (strcmp(key, "ImageSignature") == 0 && PLIST_ARRAY == type)
				{
					isMounted = (plist_array_get_size(subnode) != 0);
				}

				free(key);
				key = NULL;

				if (isMounted)
				{
					break;
				}

				plist_dict_next_item(result, it, &key, &subnode);
			}

			free(it);

			cleanUp();

			return isMounted;
		}
		catch (std::exception& exception)
		{
			cleanUp();
			throw;
		}
	});
}

pplx::task<void> DeviceManager::InstallDeveloperDiskImage(std::string diskPath, std::string signaturePath, std::shared_ptr<Device> altDevice)
{
	return pplx::create_task([=]() -> pplx::task<void> {
		idevice_t device = NULL;
		instproxy_client_t ipc = NULL;
		lockdownd_client_t client = NULL;
		lockdownd_service_descriptor_t service = NULL;
		mobile_image_mounter_client_t mim = NULL;

		auto cleanUp = [&]() {
			if (mim) {
				if (device != NULL && device->conn_type == CONNECTION_USBMUXD)
				{
					// For some reason, calling this method over WiFi may freeze AltServer.
					// Nothing bad *seems* to happen if we don't call it though, so just limit it to wired connections.
					mobile_image_mounter_hangup(mim);
				}

				mobile_image_mounter_free(mim);
			}

			if (service) {
				lockdownd_service_descriptor_free(service);
			}

			if (client) {
				lockdownd_client_free(client);
			}

			if (ipc) {
				instproxy_client_free(ipc);
			}

			if (device) {
				idevice_free(device);
			}
		};

		try
		{
			/* Find Device */
			if (idevice_new_with_options(&device, altDevice->identifier().c_str(), (enum idevice_options)((int)IDEVICE_LOOKUP_NETWORK | (int)IDEVICE_LOOKUP_USBMUX)) != IDEVICE_E_SUCCESS)
			{
				throw ServerError(ServerErrorCode::DeviceNotFound);
			}

			/* Connect to Device */
			if (lockdownd_client_new_with_handshake(device, &client, "altserver") != LOCKDOWN_E_SUCCESS)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}

			/* Connect to Mobile Image Mounter Proxy */
			if ((lockdownd_start_service(client, "com.apple.mobile.mobile_image_mounter", &service) != LOCKDOWN_E_SUCCESS) || service == NULL)
			{
				throw ServerError(ServerErrorCode::ConnectionFailed);
			}

			mobile_image_mounter_error_t err = mobile_image_mounter_new(device, service, &mim);
			if (err != MOBILE_IMAGE_MOUNTER_E_SUCCESS)
			{
				auto error = ConnectionError::errorForMobileImageMounterError(err, altDevice);
				if (error.has_value())
				{
					throw* error;
				}
			}

			size_t diskSize = std::filesystem::file_size(diskPath.c_str());
			auto signature = readFile(signaturePath.c_str());

			FILE* file = fopen(diskPath.c_str(), "rb");
			err = mobile_image_mounter_upload_image(mim, "Developer", diskSize, (const char*)signature.data(), (size_t)signature.size(), DeviceManagerUploadFile, file);
			fclose(file);

			if (err != MOBILE_IMAGE_MOUNTER_E_SUCCESS)
			{
				auto error = ConnectionError::errorForMobileImageMounterError(err, altDevice);
				if (error.has_value())
				{
					throw* error;
				}
			}

			std::string destinationDiskPath = "/private/var/mobile/Media/PublicStaging/staging.dimage";

			plist_t result = NULL;
			err = mobile_image_mounter_mount_image(mim, destinationDiskPath.c_str(), (const char*)signature.data(), (size_t)signature.size(), "Developer", &result);
			if (err != MOBILE_IMAGE_MOUNTER_E_SUCCESS)
			{
				auto error = ConnectionError::errorForMobileImageMounterError(err, altDevice);
				if (error.has_value())
				{
					throw* error;
				}
			}

			plist_t errorDescriptionNode = plist_dict_get_item(result, "DetailedError");
			if (errorDescriptionNode != nullptr)
			{
				char* rawErrorDescription = nullptr;
				plist_get_string_val(errorDescriptionNode, &rawErrorDescription);

				std::string errorDescription = rawErrorDescription;
				plist_free(result);

				if (errorDescription.find("Failed to verify") != std::string::npos)
				{
					// iOS device needs to be rebooted in order to mount disk to /Developer.
					auto recoverySuggestion = "Please reboot " + altDevice->name() + " and try again.";

					// Provide recoverySuggestion as NSLocalizedFailureReasonErrorKey 
					// to make sure it's always displayed on client.
					std::map<std::string, std::any> userInfo = {
						{ UnderlyingErrorDomainErrorKey, ConnectionErrorDomain },
						{ UnderlyingErrorCodeErrorKey, std::to_string((int)ConnectionErrorCode::Unknown) },
						{ NSLocalizedFailureReasonErrorKey, recoverySuggestion}
					};

					throw ServerError(ServerErrorCode::UnderlyingError, userInfo);
				}
				else
				{
					// Installation failed, so we assume the developer disk is NOT compatible with this iOS version.
					std::map<std::string, std::any> userInfo = {
						{ OperatingSystemVersionErrorKey, altDevice->osVersion().stringValue() },
						{ NSFilePathErrorKey, diskPath }
					};

					auto osName = ALTOperatingSystemNameForDeviceType(altDevice->type());
					if (osName.has_value())
					{
						userInfo[OperatingSystemNameErrorKey] = *osName;
					}

					auto localizedFailureReason = ServerError(ServerErrorCode::IncompatibleDeveloperDisk, userInfo).localizedFailureReason();
					if (localizedFailureReason.has_value())
					{
						// WORKAROUND: AltKit 0.0.2 crashes when receiving IncompatibleDeveloperDisk
						// error code unless we explicitly provide localized failure reason.
						userInfo[NSLocalizedFailureReasonErrorKey] = *localizedFailureReason;
					}

					throw ServerError(ServerErrorCode::IncompatibleDeveloperDisk, userInfo);
				}
			}

			plist_free(result);

			// Must clean up from same thread we created objects on.
			cleanUp();

			// Verify the developer disk has been successfully installed.
			auto testConnection = std::make_shared<DebugConnection>(altDevice);
			return testConnection->Connect().then([testConnection, altDevice](pplx::task<void> task) {
				testConnection->Disconnect();
				task.get();
			});
		}
		catch (std::exception& exception)
		{
			cleanUp();
			throw;
		}
	})
	.then([altDevice](pplx::task<void> task) {
		try
		{
			task.get();
		}
		catch (Error& error)
		{
			std::string localizedFailure("The Developer disk image couldn't be installed to " + altDevice->name() + ".");
			error.setLocalizedFailure(localizedFailure);

			throw;
		}
	});
}

pplx::task<std::vector<InstalledApp>> DeviceManager::FetchInstalledApps(std::shared_ptr<Device> altDevice)
{
	return pplx::create_task([=] {
		idevice_t device = NULL;
		instproxy_client_t ipc = NULL;
		lockdownd_client_t client = NULL;
		lockdownd_service_descriptor_t service = NULL;
		plist_t options = NULL;

		auto cleanUp = [&]() {
			if (options) {
				instproxy_client_options_free(options);
			}

			if (service) {
				lockdownd_service_descriptor_free(service);
			}

			if (client) {
				lockdownd_client_free(client);
			}

			if (ipc) {
				instproxy_client_free(ipc);
			}

			if (device) {
				idevice_free(device);
			}
		};

		/* Find Device */
		if (idevice_new_with_options(&device, altDevice->identifier().c_str(), (enum idevice_options)((int)IDEVICE_LOOKUP_NETWORK | (int)IDEVICE_LOOKUP_USBMUX)) != IDEVICE_E_SUCCESS)
		{
			throw ServerError(ServerErrorCode::DeviceNotFound);
		}

		/* Connect to Device */
		if (lockdownd_client_new_with_handshake(device, &client, "AltServer") != LOCKDOWN_E_SUCCESS)
		{
			throw ServerError(ServerErrorCode::ConnectionFailed);
		}

		/* Connect to Installation Proxy */
		if ((lockdownd_start_service(client, "com.apple.mobile.installation_proxy", &service) != LOCKDOWN_E_SUCCESS) || service == NULL)
		{
			throw ServerError(ServerErrorCode::ConnectionFailed);
		}

		instproxy_error_t err = instproxy_client_new(device, service, &ipc);
		if (err != INSTPROXY_E_SUCCESS)
		{
			auto error = ConnectionError::errorForInstallationProxyError(err, altDevice);
			if (error.has_value())
			{
				throw* error;
			}
		}

		options = instproxy_client_options_new();
		instproxy_client_options_add(options, "ApplicationType", "User", NULL);

		plist_t plist = NULL;
		err = instproxy_browse(ipc, options, &plist);
		if (err != INSTPROXY_E_SUCCESS)
		{
			auto error = ConnectionError::errorForInstallationProxyError(err, altDevice);
			if (error.has_value())
			{
				throw* error;
			}
		}

		std::vector<InstalledApp> installedApps;

		for (int i = 0; i < plist_array_get_size(plist); i++)
		{
			auto appPlist = plist_array_get_item(plist, i);
			if (plist_dict_get_item(appPlist, "ALTBundleIdentifier") == NULL)
			{
				continue;
			}

			try {
				InstalledApp installedApp(appPlist);
				installedApps.push_back(installedApp);
			}
			catch (std::exception& e)
			{
				// Ignore exception
			}
		}

		plist_free(plist);

		return installedApps;
	});
}

pplx::task<std::shared_ptr<NotificationConnection>> DeviceManager::StartNotificationConnection(std::shared_ptr<Device> altDevice)
{
	return pplx::create_task([=]() -> std::shared_ptr<NotificationConnection> {
		idevice_t device = NULL;
		lockdownd_client_t lockdownClient = NULL;
		lockdownd_service_descriptor_t service = NULL;
		np_client_t client = NULL;

		/* Find Device */
		if (idevice_new_with_options(&device, altDevice->identifier().c_str(), IDEVICE_LOOKUP_USBMUX) != IDEVICE_E_SUCCESS)
		{
			throw ServerError(ServerErrorCode::DeviceNotFound);
		}

		/* Connect to Device */
		if (lockdownd_client_new_with_handshake(device, &lockdownClient, "altserver") != LOCKDOWN_E_SUCCESS)
		{
			idevice_free(device);
			throw ServerError(ServerErrorCode::ConnectionFailed);
		}

		/* Connect to Notification Proxy */
		if ((lockdownd_start_service(lockdownClient, "com.apple.mobile.notification_proxy", &service) != LOCKDOWN_E_SUCCESS) || service == NULL)
		{
			lockdownd_client_free(lockdownClient);
			idevice_free(device);

			throw ServerError(ServerErrorCode::ConnectionFailed);
		}

		/* Connect to Client */
		if (np_client_new(device, service, &client) != NP_E_SUCCESS)
		{
			lockdownd_service_descriptor_free(service);
			lockdownd_client_free(lockdownClient);
			idevice_free(device);

			throw ServerError(ServerErrorCode::ConnectionFailed);
		}

		lockdownd_service_descriptor_free(service);
		lockdownd_client_free(lockdownClient);
		idevice_free(device);

		auto notificationConnection = std::make_shared<NotificationConnection>(altDevice, client);
		return notificationConnection;
	});
}

std::vector<std::shared_ptr<Device>> DeviceManager::connectedDevices() const
{
    auto devices = this->availableDevices(false);
    return devices;
}

std::vector<std::shared_ptr<Device>> DeviceManager::availableDevices() const
{
    auto devices = this->availableDevices(true);
    return devices;
}

std::vector<std::shared_ptr<Device>> DeviceManager::availableDevices(bool includeNetworkDevices) const
{
    std::vector<std::shared_ptr<Device>> availableDevices;

    int count = 0;

    idevice_info_t* devices = NULL;
    if (idevice_get_device_list_extended(&devices, &count) < 0)
    {
        fprintf(stderr, "ERROR: Unable to retrieve device list!\n");
        return availableDevices;
    }

    for (int i = 0; i < count; i++)
    {
        idevice_info_t device_info = devices[i];
        char* udid = device_info->udid;

        idevice_t device = NULL;
        lockdownd_client_t client = NULL;

        char* device_name = NULL;
        char* device_type_string = NULL;
        char* device_version_string = NULL;

        plist_t device_type_plist = NULL;
        plist_t device_version_plist = NULL;

        auto cleanUp = [&]() {
            if (device_version_plist) {
                plist_free(device_version_plist);
            }

            if (device_type_plist) {
                plist_free(device_type_plist);
            }

            if (device_version_string) {
                free(device_version_string);
            }

            if (device_type_string) {
                free(device_type_string);
            }

            if (device_name) {
                free(device_name);
            }

            if (client) {
                lockdownd_client_free(client);
            }

            if (device) {
                idevice_free(device);
            }
        };

        if (includeNetworkDevices)
        {
            idevice_new_with_options(&device, udid, (enum idevice_options)((int)IDEVICE_LOOKUP_NETWORK | (int)IDEVICE_LOOKUP_USBMUX));
        }
        else
        {
            idevice_new_with_options(&device, udid, IDEVICE_LOOKUP_USBMUX);
        }

        if (!device)
        {
            continue;
        }

        int result = lockdownd_client_new(device, &client, "altserver");
        if (result != LOCKDOWN_E_SUCCESS)
        {
            fprintf(stderr, "ERROR: Connecting to device %s failed! (%d)\n", udid, result);

            cleanUp();
            continue;
        }

        if (lockdownd_get_device_name(client, &device_name) != LOCKDOWN_E_SUCCESS || device_name == NULL)
        {
            fprintf(stderr, "ERROR: Could not get device name!\n");

            cleanUp();
            continue;
        }

        if (lockdownd_get_value(client, NULL, "ProductType", &device_type_plist) != LOCKDOWN_E_SUCCESS)
        {
            odslog("ERROR: Could not get device type for " << device_name);

            cleanUp();
            continue;
        }

        plist_get_string_val(device_type_plist, &device_type_string);

        Device::Type deviceType = Device::Type::iPhone;
        if (std::string(device_type_string).find("iPhone") != std::string::npos ||
            std::string(device_type_string).find("iPod") != std::string::npos)
        {
            deviceType = Device::Type::iPhone;
        }
        else if (std::string(device_type_string).find("iPad") != std::string::npos)
        {
            deviceType = Device::Type::iPad;
        }
        else if (std::string(device_type_string).find("AppleTV") != std::string::npos)
        {
            deviceType = Device::Type::AppleTV;
        }
        else
        {
            odslog("Unknown device type " << device_type_string << " for " << device_name);

            cleanUp();
            continue;
        }

        if (lockdownd_get_value(client, NULL, "ProductVersion", &device_version_plist) != LOCKDOWN_E_SUCCESS)
        {
            odslog("ERROR: Could not get device type for " << device_name);

            cleanUp();
            continue;
        }

        plist_get_string_val(device_version_plist, &device_version_string);
        OperatingSystemVersion osVersion(device_version_string);

        bool isDuplicate = false;

        for (auto& device : availableDevices)
        {
            if (device->identifier() == udid)
            {
                // Duplicate.
                isDuplicate = true;
                break;
            }
        }

        if (isDuplicate)
        {
            cleanUp();
            continue;
        }

        auto altDevice = std::make_shared<Device>(device_name, udid, deviceType);
        altDevice->setOSVersion(osVersion);
        availableDevices.push_back(altDevice);

        cleanUp();
    }

    idevice_device_list_extended_free(devices);

    return availableDevices;
}

std::function<void(std::shared_ptr<Device>)> DeviceManager::connectedDeviceCallback() const
{
	return _connectedDeviceCallback;
}

void DeviceManager::setConnectedDeviceCallback(std::function<void(std::shared_ptr<Device>)> callback)
{
	_connectedDeviceCallback = callback;
}

std::function<void(std::shared_ptr<Device>)> DeviceManager::disconnectedDeviceCallback() const
{
	return _disconnectedDeviceCallback;
}

void DeviceManager::setDisconnectedDeviceCallback(std::function<void(std::shared_ptr<Device>)> callback)
{
	_disconnectedDeviceCallback = callback;
}

std::map<std::string, std::shared_ptr<Device>>& DeviceManager::cachedDevices()
{
	return _cachedDevices;
}

#pragma mark - Callbacks -

void DeviceManagerUpdateStatus(plist_t command, plist_t status, void *uuid)
{
	if (DeviceManager::instance()->_installationProgressHandlers.count((char*)uuid) == 0)
	{
		return;
	}
    
    int percent = 0;
    instproxy_status_get_percent_complete(status, &percent);

	char* name = NULL;
	char* description = NULL;
	uint64_t code = 0;
	instproxy_status_get_error(status, &name, &description, &code);

	double progress = ((double)percent / 100.0);

	auto progressHandler = DeviceManager::instance()->_installationProgressHandlers[(char*)uuid];
	progressHandler(progress, code, name, description);
}

void DeviceManagerUpdateAppDeletionStatus(plist_t command, plist_t status, void* uuid)
{
	char *statusName = NULL;
	instproxy_status_get_name(status, &statusName);

	char* errorName = NULL;
	char* errorDescription = NULL;
	uint64_t errorCode = 0;
	instproxy_status_get_error(status, &errorName, &errorDescription, &errorCode);

	if (std::string(statusName) == std::string("Complete") || errorCode != 0 || errorName != NULL)
	{
		auto completionHandler = DeviceManager::instance()->_deletionCompletionHandlers[(char*)uuid];
		if (completionHandler != NULL)
		{
			if (errorName == NULL)
			{
				errorName = (char*)"";
			}

			if (errorDescription == NULL)
			{
				errorDescription = (char*)"";
			}

			if (errorCode != 0 || std::string(errorName) != std::string())
			{
				odslog("Error removing app. " << errorCode << " (" << errorName << "). " << errorDescription);
				completionHandler(false, errorCode, errorName, errorDescription);
			}
			else
			{
				odslog("Finished removing app!");
				completionHandler(true, 0, errorName, errorDescription);
			}

			DeviceManager::instance()->_deletionCompletionHandlers.erase((char*)uuid);
		}
	}
}

void DeviceDidChangeConnectionStatus(const idevice_event_t* event, void* user_data)
{
	switch (event->event)
	{
	case IDEVICE_DEVICE_ADD:
	{
		auto devices = DeviceManager::instance()->connectedDevices();
		std::shared_ptr<Device> device = NULL;

		for (auto& d : devices)
		{
			if (d->identifier() == event->udid)
			{
				device = d;
				break;
			}
		}

		if (device == NULL)
		{
			return;
		}

		if (DeviceManager::instance()->cachedDevices().count(device->identifier()) > 0)
		{
			return;
		}

		odslog("Detected device:" << device->name().c_str());

		DeviceManager::instance()->cachedDevices()[device->identifier()] = device;

		if (DeviceManager::instance()->connectedDeviceCallback() != NULL)
		{
			DeviceManager::instance()->connectedDeviceCallback()(device);
		}
		
		break;
	}
	case IDEVICE_DEVICE_REMOVE:
	{
		auto devices = DeviceManager::instance()->cachedDevices();
		std::shared_ptr<Device> device = DeviceManager::instance()->cachedDevices()[event->udid];

		if (device == NULL)
		{
			return;
		}

		DeviceManager::instance()->cachedDevices().erase(device->identifier());

		if (DeviceManager::instance()->disconnectedDeviceCallback() != NULL)
		{
			DeviceManager::instance()->disconnectedDeviceCallback()(device);
		}

		break;
	}
	}
}

ssize_t DeviceManagerUploadFile(void* buffer, size_t size, void* user_data)
{
	return fread(buffer, 1, size, (FILE*)user_data);
}
