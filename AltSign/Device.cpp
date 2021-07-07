//
//  Device.cpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/10/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include "Device.hpp"

#include "Error.hpp"

#include <algorithm>
#include <sstream>
#include <iostream>

OperatingSystemVersion::OperatingSystemVersion(int major, int minor, int patch) : majorVersion(major), minorVersion(minor), patchVersion(patch)
{
}

OperatingSystemVersion::OperatingSystemVersion(std::string string) : majorVersion(0), minorVersion(0), patchVersion(0)
{
	std::istringstream stream(string.c_str());

	std::string majorVersion;
	if (!std::getline(stream, majorVersion, '.'))
	{
		return;
	}
	this->majorVersion = std::stoi(majorVersion);

	std::string minorVersion;
	if (!std::getline(stream, minorVersion, '.'))
	{
		return;
	}
	this->minorVersion = std::stoi(minorVersion);

	std::string patchVersion;
	if (!std::getline(stream, patchVersion, '.'))
	{
		return;
	}
	this->patchVersion = std::stoi(patchVersion);
}

Device::Device() : _osVersion(0, 0, 0)
{
}

Device::~Device()
{
}

Device::Device(std::string name, std::string identifier, Device::Type type) : _name(name), _identifier(identifier), _type(type), _osVersion(0, 0, 0)
{
}

Device::Device(plist_t plist) : _osVersion(0, 0, 0)
{
    auto nameNode = plist_dict_get_item(plist, "name");
    auto identifierNode = plist_dict_get_item(plist, "deviceNumber");
	auto deviceTypeNode = plist_dict_get_item(plist, "deviceClass");
    
    if (nameNode == nullptr || identifierNode == nullptr)
    {
        throw APIError(APIErrorCode::InvalidResponse);
    }
    
    char *name = nullptr;
    plist_get_string_val(nameNode, &name);
    
    char *identifier = nullptr;
    plist_get_string_val(identifierNode, &identifier);

	Device::Type deviceType = Device::Type::None;
	if (deviceTypeNode != nullptr)
	{
		char* rawDeviceClass = nullptr;
		plist_get_string_val(deviceTypeNode, &rawDeviceClass);

		std::string deviceClass = rawDeviceClass;
		std::transform(deviceClass.begin(), deviceClass.end(), deviceClass.begin(), ::tolower);

		if (deviceClass == "iphone" || deviceClass == "ipod")
		{
			deviceType = Device::Type::iPhone;
		}
		else if (deviceClass == "ipad")
		{
			deviceType = Device::Type::iPad;
		}
		else if (deviceClass == "tvos")
		{
			deviceType = Device::Type::AppleTV;
		}
	}	
    
    _name = name;
    _identifier = identifier;
	_type = deviceType;
}

#pragma mark - Description -

std::ostream& operator<<(std::ostream& os, const Device& device)
{
    os << "Name: " << device.name() << " UDID: " << device.identifier();
    return os;
}

#pragma mark - Getters -

std::string Device::name() const
{
    return _name;
}

std::string Device::identifier() const
{
    return _identifier;
}

Device::Type Device::type() const
{
	return _type;
}

void Device::setOSVersion(OperatingSystemVersion version)
{
	_osVersion = version;
}

OperatingSystemVersion Device::osVersion() const
{
	return _osVersion;
}