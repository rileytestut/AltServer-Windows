//
//  Device.hpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/10/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef Device_hpp
#define Device_hpp

/* The classes below are exported */
#pragma GCC visibility push(default)

#include <optional>
#include <string>
#include <iostream>

#include <plist/plist.h>

struct OperatingSystemVersion
{
	int majorVersion;
	int minorVersion;
	int patchVersion;

	OperatingSystemVersion(int major, int minor, int patch);
	OperatingSystemVersion(std::string string);

	std::string stringValue() const;
};

class Device
{
public:
	enum Type
	{
		iPhone = 1 << 1,
		iPad = 1 << 2,
		AppleTV = 1 << 3,

		None = 0,
		All = (iPhone | iPad | AppleTV)
	};

    Device();
    ~Device();
    
    Device(std::string name, std::string identifier, Device::Type type);
    Device(plist_t plist) /* throws */;
    
    std::string name() const;
    std::string identifier() const;
	Device::Type type() const;

	void setOSVersion(OperatingSystemVersion version);
	OperatingSystemVersion osVersion() const;
    
    friend std::ostream& operator<<(std::ostream& os, const Device& device);
    
private:
    std::string _name;
    std::string _identifier;
	Device::Type _type;
	OperatingSystemVersion _osVersion;
};

std::optional<std::string> ALTOperatingSystemNameForDeviceType(Device::Type deviceType);

#pragma GCC visibility pop

#endif /* Device_hpp */
