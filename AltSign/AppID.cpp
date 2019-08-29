//
//  AppID.cpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/12/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include "AppID.hpp"

#include "Error.hpp"

AppID::AppID()
{
}

AppID::~AppID()
{
}

AppID::AppID(plist_t plist)
{
    auto nameNode = plist_dict_get_item(plist, "name");
    auto identifierNode = plist_dict_get_item(plist, "appIdId");
    auto bundleIdentifierNode = plist_dict_get_item(plist, "identifier");
    
    if (nameNode == nullptr || identifierNode == nullptr || bundleIdentifierNode == nullptr)
    {
        throw APIError(APIErrorCode::InvalidResponse);
    }
    
    char *name = nullptr;
    plist_get_string_val(nameNode, &name);
    
    char *identifier = nullptr;
    plist_get_string_val(identifierNode, &identifier);
    
    char *bundleIdentifier = nullptr;
    plist_get_string_val(bundleIdentifierNode, &bundleIdentifier);
    
    _name = name;
    _identifier = identifier;
    _bundleIdentifier = bundleIdentifier;
}

#pragma mark - Description -

std::ostream& operator<<(std::ostream& os, const AppID& appID)
{
    os << "Name: " << appID.name() << " ID: " << appID.identifier() << " BundleID: " << appID.bundleIdentifier();
    return os;
}

#pragma mark - Getters -

std::string AppID::name() const
{
    return _name;
}

std::string AppID::identifier() const
{
    return _identifier;
}

std::string AppID::bundleIdentifier() const
{
    return _bundleIdentifier;
}
