//
//  Application.cpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/12/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include "Application.hpp"

#include "Error.hpp"
#include "ldid.hpp"

#include <fstream>
#include <filesystem>
#include <WinSock2.h>

#define odslog(msg) { std::stringstream ss; ss << msg << std::endl; OutputDebugStringA(ss.str().c_str()); }

extern std::vector<unsigned char> readFile(const char* filename);

namespace fs = std::filesystem;

Application::Application()
{
}

Application::~Application()
{
	for (auto& pair : _entitlements)
	{
		plist_free(pair.second);
	}
}

Application::Application(const Application& app)
{
	_name = app.name();
	_bundleIdentifier = app.bundleIdentifier();
	_version = app.version();
	_path = app.path();

	// Don't assign _entitlementsString or _entitlements,
	// since each copy will create its own entitlements lazily.
	// Otherwise there might be duplicate frees in deconstructor.
}

Application& Application::operator=(const Application& app)
{
	if (this == &app)
	{
		return *this;
	}

	_name = app.name();
	_bundleIdentifier = app.bundleIdentifier();
	_version = app.version();
	_path = app.path();

	return *this;
}

Application::Application(std::string appBundlePath)
{
    fs::path path(appBundlePath);
    path.append("Info.plist");

	auto plistData = readFile(path.string().c_str());

    plist_t plist = nullptr;
    plist_from_memory((const char *)plistData.data(), (int)plistData.size(), &plist);
    if (plist == nullptr)
    {
        throw SignError(SignErrorCode::InvalidApp);
    }
    
	auto nameNode = plist_dict_get_item(plist, "CFBundleDisplayName");
	if (nameNode == nullptr)
	{
		nameNode = plist_dict_get_item(plist, "CFBundleName");
	}
	
    auto bundleIdentifierNode = plist_dict_get_item(plist, "CFBundleIdentifier");
    auto versionNode = plist_dict_get_item(plist, "CFBundleShortVersionString");

    if (nameNode == nullptr || bundleIdentifierNode == nullptr || versionNode == nullptr)
    {
		plist_free(plist);
        throw SignError(SignErrorCode::InvalidApp);
    }
    
    char *name = nullptr;
    plist_get_string_val(nameNode, &name);

    char *bundleIdentifier = nullptr;
    plist_get_string_val(bundleIdentifierNode, &bundleIdentifier);
    
    char *version = nullptr;
    plist_get_string_val(versionNode, &version);

    _name = name;
    _bundleIdentifier = bundleIdentifier;
    _version = version;
    _path = appBundlePath;

	free(name);
	free(bundleIdentifier);
	free(version);

	plist_free(plist);
}


#pragma mark - Description -

std::ostream& operator<<(std::ostream& os, const Application& app)
{
    os << "Name: " << app.name() << " ID: " << app.bundleIdentifier();
    return os;
}
    
#pragma mark - Getters -
    
std::string Application::name() const
{
    return _name;
}

std::string Application::bundleIdentifier() const
{
    return _bundleIdentifier;
}

std::string Application::version() const
{
    return _version;
}

std::string Application::path() const
{
    return _path;
}

std::shared_ptr<ProvisioningProfile> Application::provisioningProfile()
{
	if (_provisioningProfile == NULL)
	{
		fs::path path(this->path());
		path.append("embedded.mobileprovision");

		_provisioningProfile = std::make_shared<ProvisioningProfile>(path.string());
	}

	return _provisioningProfile;
}

std::vector<std::shared_ptr<Application>> Application::appExtensions() const
{
	std::vector<std::shared_ptr<Application>> appExtensions;

	fs::path plugInsPath(this->path());
	plugInsPath.append("PlugIns");

	if (!fs::exists(plugInsPath))
	{
		return appExtensions;
	}

	for (auto& file : fs::directory_iterator(plugInsPath))
	{
		if (file.path().extension() != ".appex")
		{
			continue;
		}

		auto appExtension = std::make_shared<Application>(file.path().string());
		if (appExtension == nullptr)
		{
			continue;
		}

		appExtensions.push_back(appExtension);
	}

	return appExtensions;
}

std::string Application::entitlementsString()
{
	if (_entitlementsString == "")
	{
		_entitlementsString = ldid::Entitlements(this->path());
	}

	return _entitlementsString;
}

std::map<std::string, plist_t> Application::entitlements()
{
	if (_entitlements.size() == 0)
	{
		auto rawEntitlements = this->entitlementsString();

		plist_t plist = nullptr;
		plist_from_memory((const char*)rawEntitlements.data(), (int)rawEntitlements.size(), &plist);

		if (plist != nullptr)
		{
			std::map<std::string, plist_t> entitlements;
			char* key = NULL;
			plist_t node = NULL;

			plist_dict_iter it = NULL;
			plist_dict_new_iter(plist, &it);
			plist_dict_next_item(plist, it, &key, &node);

			while (node != nullptr)
			{
				entitlements[key] = plist_copy(node);

				node = NULL;
				free(key);
				key = NULL;
				plist_dict_next_item(plist, it, &key, &node);
			}

			free(it);
			plist_free(plist);

			_entitlements = entitlements;
		}
		else
		{
			odslog("Error parsing entitlements:\n" << rawEntitlements);
		}
	}

	return _entitlements;
}

bool Application::isAltStoreApp() const
{
	auto isAltStoreApp = this->bundleIdentifier().find("com.rileytestut.AltStore") != std::string::npos;
	return isAltStoreApp;
}