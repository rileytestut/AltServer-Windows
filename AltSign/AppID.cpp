//
//  AppID.cpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/12/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include "AppID.hpp"

#include "Error.hpp"

std::string AppIDFeatureAppGroups = "APG3427HIY";

AppID::AppID()
{
}

AppID::~AppID()
{
	for (auto& pair : _features)
	{
		plist_free(pair.second);
	}
}

AppID::AppID(const AppID& appID)
{
	_name = appID.name();
	_identifier = appID.identifier();
	_bundleIdentifier = appID.bundleIdentifier();

	// Deep copy features
	this->setFeatures(appID.features());
}

AppID& AppID::operator=(const AppID& appID)
{
	if (this == &appID)
	{
		return *this;
	}

	_name = appID.name();
	_identifier = appID.identifier();
	_bundleIdentifier = appID.bundleIdentifier();

	// Deep copy features
	this->setFeatures(appID.features());

	return *this;
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

	auto allFeaturesNode = plist_dict_get_item(plist, "features");
	auto enabledFeaturesNode = plist_dict_get_item(plist, "enabledFeatures");

	std::map<std::string, plist_t> features;
	if (allFeaturesNode != NULL && enabledFeaturesNode != NULL)
	{
		for (int i = 0; i < plist_array_get_size(enabledFeaturesNode); i++)
		{
			auto featureNode = plist_array_get_item(enabledFeaturesNode, i);

			char *featureName = nullptr;
			plist_get_string_val(featureNode, &featureName);

			auto valueNode = plist_copy(plist_dict_get_item(allFeaturesNode, featureName));
			features[featureName] = valueNode;
		}
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

	_features = features;
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

std::map<std::string, plist_t> AppID::features() const
{
	return _features;
}

void AppID::setFeatures(std::map<std::string, plist_t> features)
{
	for (auto& pair : _features)
	{
		// Free previous features.
		plist_free(pair.second);
	}

	std::map<std::string, plist_t> copiedFeatures;
	for (auto& pair : features)
	{
		copiedFeatures[pair.first] = plist_copy(pair.second);
	}

	_features = copiedFeatures;
}
