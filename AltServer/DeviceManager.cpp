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

#include <filesystem>

#include <iostream>
#include <fstream>
#include <sstream>
#include <condition_variable>

#include "Archiver.hpp"
#include "ServerError.hpp"
#include "ProvisioningProfile.hpp"

#include <WinSock2.h>

#define odslog(msg) { std::wstringstream ss; ss << msg << std::endl; OutputDebugStringW(ss.str().c_str()); }

extern std::string StringFromWideString(std::wstring wideString);
extern std::wstring WideStringFromString(std::string string);

void DeviceManagerUpdateStatus(plist_t command, plist_t status, void *udid);

namespace fs = std::filesystem;

extern std::string make_uuid();
extern std::string temporary_directory();
extern std::vector<unsigned char> readFile(const char* filename);

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

pplx::task<void> DeviceManager::InstallApp(std::string appFilepath, std::string deviceUDID, std::function<void(double)> progressCompletionHandler)
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

		auto removedProfilesDirectoryPath = temporaryDirectory;
		removedProfilesDirectoryPath.append(make_uuid());

		auto preferredProfiles = std::make_shared<std::map<std::string, std::shared_ptr<ProvisioningProfile>>>();

		auto finish = [this, preferredProfiles, removedProfilesDirectoryPath, temporaryDirectory, &uuidString]
		(idevice_t device, lockdownd_client_t client, instproxy_client_t ipc, afc_client_t afc, misagent_client_t mis, lockdownd_service_descriptor_t service)
		{
			if (fs::exists(removedProfilesDirectoryPath))
			{
				for (auto& file : fs::directory_iterator(removedProfilesDirectoryPath))
				{
					try
					{
						ProvisioningProfile profile(file.path().string());

						auto preferredProfile = (*preferredProfiles)[profile.bundleIdentifier()];
						if (preferredProfile == nullptr || preferredProfile->uuid() != profile.uuid())
						{
							continue;
						}

						plist_t pdata = plist_new_data((const char*)profile.data().data(), profile.data().size());

						if (misagent_install(mis, pdata) == MISAGENT_E_SUCCESS)
						{
							odslog("Reinstalled profile: " << WideStringFromString(profile.bundleIdentifier()) << " (" << WideStringFromString(profile.uuid()) << ")");
						}
						else
						{
							int code = misagent_get_status_code(mis);
							odslog("Failed to reinstall provisioning profile: " << WideStringFromString(profile.bundleIdentifier()) << " (" << WideStringFromString(profile.uuid()) << "). Error code: " << code)
						}
					}
					catch (std::exception& e)
					{
					}
				}
			}

			fs::remove_all(temporaryDirectory);

			instproxy_client_free(ipc);
			afc_client_free(afc);
			lockdownd_client_free(client);
			misagent_client_free(mis);
			idevice_free(device);
			lockdownd_service_descriptor_free(service);

			free(uuidString);
			uuidString = NULL;

			this->_mutex.unlock();
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

			/* Find Device */
			if (idevice_new(&device, deviceUDID.c_str()) != IDEVICE_E_SUCCESS)
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

			this->WriteDirectory(afc, appBundlePath.string(), destinationPath.string(), [&writtenFiles,  numberOfFiles, progressCompletionHandler](std::string filepath) {
				writtenFiles++;

				double progress = (double)writtenFiles / (double)numberOfFiles;
				double weightedProgress = progress * 0.75;
				progressCompletionHandler(weightedProgress);
			});

			std::cout << "Finished writing to device." << std::endl;


			if (service)
			{
				lockdownd_service_descriptor_free(service);
				service = NULL;
			}

			/* Provisioning Profiles */
			auto provisioningProfilePath = appBundlePath.append("embedded.mobileprovision");

			if (fs::exists(provisioningProfilePath.string()))
			{
				ProvisioningProfile installationProvisioningProfile(provisioningProfilePath.string());
				fs::create_directory(removedProfilesDirectoryPath);

				plist_t profiles = NULL;

				if (misagent_copy_all(mis, &profiles) != MISAGENT_E_SUCCESS)
				{
					throw ServerError(ServerErrorCode::ConnectionFailed);
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

					auto provisioningProfile = std::make_shared<ProvisioningProfile>(data);

					if (provisioningProfile->teamIdentifier() != installationProvisioningProfile.teamIdentifier())
					{
						std::cout << "Ignoring: " << provisioningProfile->bundleIdentifier() << " (" << provisioningProfile->uuid() << ")" << std::endl;
						continue;
					}

					auto preferredProfile = (*preferredProfiles)[provisioningProfile->bundleIdentifier()];
					if (preferredProfile != nullptr)
					{
						auto expirationDateA = provisioningProfile->expirationDate();
						auto expirationDateB = preferredProfile->expirationDate();

						if (timercmp(&expirationDateA, &expirationDateB, >) != 0)
						{
							// provisioningProfile exires later than preferredProfile, so use provisioningProfile instead.
							(*preferredProfiles)[provisioningProfile->bundleIdentifier()] = provisioningProfile;
						}
					}
					else
					{
						(*preferredProfiles)[provisioningProfile->bundleIdentifier()] = provisioningProfile;
					}

					std::string filename = make_uuid() + ".mobileprovision";

					fs::path filepath = removedProfilesDirectoryPath;
					filepath.append(filename);

					auto profileData = provisioningProfile->data();

					std::ofstream fout(filepath.string(), std::ios::out | std::ios::binary);
					fout.write((char*)& profileData[0], data.size() * sizeof(char));
					fout.close();

					odslog("Copied to" << filepath);

					if (misagent_remove(mis, provisioningProfile->uuid().c_str()) == MISAGENT_E_SUCCESS)
					{
						odslog("Removed provisioning profile: " << WideStringFromString(provisioningProfile->bundleIdentifier()) << " (" << WideStringFromString(provisioningProfile->uuid()) << ")");
					}
					else
					{
						int code = misagent_get_status_code(mis);
						odslog("Failed to remove provisioning profile: " << WideStringFromString(provisioningProfile->bundleIdentifier()) << " (" << WideStringFromString(provisioningProfile->uuid()) << ") Error: " << code);
					}
				}

				lockdownd_client_free(client);
				client = NULL;
			}

			std::mutex waitingMutex;
			std::condition_variable cv;

			std::optional<ServerError> installationError = std::nullopt;

			bool didBeginInstalling = false;
			bool didFinishInstalling = false;

			this->_installationProgressHandlers[UUID] = [device, client, ipc, afc, mis, service, finish, progressCompletionHandler, 
				&waitingMutex, &cv, &didBeginInstalling, &didFinishInstalling, &installationError](double progress, int resultCode) {
				double weightedProgress = progress * 0.25;
				double adjustedProgress = weightedProgress + 0.75;

				if (progress == 0 && didBeginInstalling)
				{
					if (resultCode != 0)
					{
						if (resultCode == -402620383)
						{
							installationError = std::make_optional<ServerError>(ServerErrorCode::MaximumFreeAppLimitReached);
						}
						else
						{
							installationError = std::make_optional<ServerError>(ServerErrorCode::InstallationFailed);
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

			if (installationError.has_value())
			{
				throw installationError.value();
			}

			finish(device, client, ipc, afc, mis, service);
		}
		catch (ServerError& error)
		{
			// Specifically catch ServerError to preserve type information when we re-throw.
			finish(device, client, ipc, afc, mis, service);

			throw error;
		}
		catch (std::exception& exception)
		{
			// MUST finish so we restore provisioning profiles.
			finish(device, client, ipc, afc, mis, service);

			throw exception;
		}
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
	odslog("Writing File: " << destinationPath.c_str());
    
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
    char **udids = NULL;
    if (idevice_get_device_list(&udids, &count) < 0)
    {
        fprintf(stderr, "ERROR: Unable to retrieve device list!\n");
        return availableDevices;
    }
    
    for (int i = 0; i < count; i++)
    {
        char *udid = udids[i];
        
        idevice_t device = NULL;
        
        if (includeNetworkDevices)
        {
            idevice_new(&device, udid);
        }
        else
        {
            idevice_new_ignore_network(&device, udid);
        }
        
        if (!device)
        {
            continue;
        }
        
        lockdownd_client_t client = NULL;
        int result = lockdownd_client_new(device, &client, "altserver");
        if (result != LOCKDOWN_E_SUCCESS)
        {
            fprintf(stderr, "ERROR: Connecting to device %s failed! (%d)\n", udid, result);
            
            idevice_free(device);
            
            continue;
        }
        
        char *device_name = NULL;
        if (lockdownd_get_device_name(client, &device_name) != LOCKDOWN_E_SUCCESS || device_name == NULL)
        {
            fprintf(stderr, "ERROR: Could not get device name!\n");
            
            lockdownd_client_free(client);
            idevice_free(device);
            
            continue;
        }
        
        lockdownd_client_free(client);
        idevice_free(device);

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
			continue;
		}
        
		auto altDevice = std::make_shared<Device>(device_name, udid);
        availableDevices.push_back(altDevice);
        
        if (device_name != NULL)
        {
            free(device_name);
        }
    }
    
    idevice_device_list_free(udids);
    
    return availableDevices;
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
	progressHandler(progress, code);
}
