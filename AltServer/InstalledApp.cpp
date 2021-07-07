#include "InstalledApp.h"
#include "ServerError.hpp"

InstalledApp::InstalledApp(plist_t plist)
{
	auto nameNode = plist_dict_get_item(plist, "CFBundleName");
	auto identifierNode = plist_dict_get_item(plist, "CFBundleIdentifier");
	auto executableNode = plist_dict_get_item(plist, "CFBundleExecutable");

	if (nameNode == NULL || identifierNode == NULL || executableNode == NULL)
	{
		throw ServerError(ServerErrorCode::InvalidApp);
	}

	char* name = NULL;
	plist_get_string_val(nameNode, &name);
	this->_name = name;

	char* identifier = NULL;
	plist_get_string_val(identifierNode, &identifier);
	this->_bundleIdentifier = identifier;

	char* executable = NULL;
	plist_get_string_val(executableNode, &executable);
	this->_executableName = executable;
}

InstalledApp::~InstalledApp()
{
}

bool InstalledApp::operator<(const InstalledApp& app) const
{
	if (this->name() == app.name())
	{
		return this->bundleIdentifier() < app.bundleIdentifier();
	}
	else
	{
		return this->name() < app.name();
	}
}

std::string InstalledApp::name() const
{
	return _name;
}

std::string InstalledApp::bundleIdentifier() const
{
	return _bundleIdentifier;
}

std::string InstalledApp::executableName() const
{
	return _executableName;
}