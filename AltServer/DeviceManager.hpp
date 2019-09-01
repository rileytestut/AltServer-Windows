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
#include <pplx/pplxtasks.h>

#include <libimobiledevice/afc.h>

class DeviceManager
{
public:
    static DeviceManager *instance();
    
    DeviceManager();
    
    std::vector<std::shared_ptr<Device>> connectedDevices() const;
    std::vector<std::shared_ptr<Device>> availableDevices() const;
    
    std::map<std::string, int> _installationProgress;
    std::map<std::string, std::function<void(int progress)>> _installationCompletionHandlers;
    
    void InstallApp(std::string filepath, std::string deviceUDID);
    
private:
    ~DeviceManager();
    
    static DeviceManager *_instance;
    
    std::vector<std::shared_ptr<Device>> availableDevices(bool includeNetworkDevices) const;
    
    void WriteDirectory(afc_client_t client, std::string directoryPath, std::string destinationPath);
    void WriteFile(afc_client_t client, std::string filepath, std::string destinationPath);
};

#endif /* DeviceManager_hpp */
