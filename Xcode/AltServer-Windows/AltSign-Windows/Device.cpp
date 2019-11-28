//
//  Device.cpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/10/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include "Device.hpp"

#include "Error.hpp"

Device::Device()
{
}

Device::~Device()
{
}

Device::Device(std::string name, std::string identifier) : _name(name), _identifier(identifier)
{
}

Device::Device(plist_t plist)
{
    auto nameNode = plist_dict_get_item(plist, "name");
    auto identifierNode = plist_dict_get_item(plist, "deviceNumber");
    
    if (nameNode == nullptr || identifierNode == nullptr)
    {
        throw APIError(APIErrorCode::InvalidResponse);
    }
    
    char *name = nullptr;
    plist_get_string_val(nameNode, &name);
    
    char *identifier = nullptr;
    plist_get_string_val(identifierNode, &identifier);
    
    _name = name;
    _identifier = identifier;
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
