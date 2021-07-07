#pragma once

#include <plist/plist.h>

#include <string>

class InstalledApp
{
public:
	InstalledApp(plist_t plist);
	~InstalledApp();

	std::string name() const;
	std::string bundleIdentifier() const;
	std::string executableName() const;

	bool operator<(const InstalledApp& app) const;

private:
	std::string _name;
	std::string _bundleIdentifier;
	std::string _executableName;
};

