//
//  DeviceManager.hpp
//  AltServer-Windows
//
//  Created by Riley Testut on 8/13/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef DeviceManager_hpp
#define DeviceManager_hpp

#include "Device.hpp"

#include <vector>
#include <map>
#include <mutex>

#include <pplx/pplxtasks.h>
#include <libimobiledevice/afc.h>

class DeviceManager
{
public:
    static DeviceManager *instance();
    
    DeviceManager();
    
    std::vector<std::shared_ptr<Device>> connectedDevices() const;
    std::vector<std::shared_ptr<Device>> availableDevices() const;
    
	pplx::task<void> InstallApp(std::string filepath, std::string deviceUDID, std::function<void(double)> progressCompletionHandler);
    
private:
    ~DeviceManager();
    
    static DeviceManager *_instance;

	std::mutex _mutex;

	std::map<std::string, std::function<void(double, int, char *, char *)>> _installationProgressHandlers;
    
    std::vector<std::shared_ptr<Device>> availableDevices(bool includeNetworkDevices) const;
    
    void WriteDirectory(afc_client_t client, std::string directoryPath, std::string destinationPath, std::function<void(std::string)> wroteFileCallback);
    void WriteFile(afc_client_t client, std::string filepath, std::string destinationPath, std::function<void(std::string)> wroteFileCallback);

	friend void DeviceManagerUpdateStatus(plist_t command, plist_t status, void* uuid);
};

#endif /* DeviceManager_hpp */
