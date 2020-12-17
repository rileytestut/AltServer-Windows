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

Device::Device()
{
}

Device::~Device()
{
}

Device::Device(std::string name, std::string identifier, Device::Type type) : _name(name), _identifier(identifier), _type(type)
{
}

Device::Device(plist_t plist)
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
