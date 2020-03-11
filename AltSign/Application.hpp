//
//  Application.hpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/12/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef Application_hpp
#define Application_hpp

/* The classes below are exported */
#pragma GCC visibility push(default)

#include <optional>
#include <string>
#include <memory>

#include <plist/plist.h>

#include "ProvisioningProfile.hpp"

class Application
{
public:
    Application();
    ~Application();
    
    Application(std::string appBundlePath) /* throws */;
    
    std::string name() const;
    std::string bundleIdentifier() const;
    std::string version() const;
    std::string path() const;

	std::shared_ptr<ProvisioningProfile> provisioningProfile();
	std::vector<std::shared_ptr<Application>> appExtensions() const;
    
    friend std::ostream& operator<<(std::ostream& os, const Application& app);
    
private:
    std::string _name;
    std::string _bundleIdentifier;
    std::string _version;
    std::string _path;

	std::shared_ptr<ProvisioningProfile> _provisioningProfile;
};

#pragma GCC visibility pop

#endif /* Application_hpp */
