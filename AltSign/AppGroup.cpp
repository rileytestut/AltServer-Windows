//
//  AppGroup.cpp
//  AltSign-Windows
//
//  Created by Riley Testut on 5/19/20.
//  Copyright (c) 2020 Riley Testut. All rights reserved.
//

#include "AppGroup.hpp"

#include "Error.hpp"

AppGroup::AppGroup()
{
}

AppGroup::~AppGroup()
{
}

AppGroup::AppGroup(plist_t plist)
{
	auto nameNode = plist_dict_get_item(plist, "name");
	auto identifierNode = plist_dict_get_item(plist, "applicationGroup");
	auto groupIdentifierNode = plist_dict_get_item(plist, "identifier");

	if (nameNode == nullptr || identifierNode == nullptr || groupIdentifierNode == nullptr)
	{
		throw APIError(APIErrorCode::InvalidResponse);
	}

	char* name = nullptr;
	plist_get_string_val(nameNode, &name);

	char* identifier = nullptr;
	plist_get_string_val(identifierNode, &identifier);

	char* groupIdentifier = nullptr;
	plist_get_string_val(groupIdentifierNode, &groupIdentifier);

	_name = name;
	_identifier = identifier;
	_groupIdentifier = groupIdentifier;
}

#pragma mark - Description -

std::ostream& operator<<(std::ostream& os, const AppGroup& appGroup)
{
	os << "Name: " << appGroup.name() << " ID: " << appGroup.identifier() << " GroupID: " << appGroup.groupIdentifier();
	return os;
}

#pragma mark - Getters -

std::string AppGroup::name() const
{
	return _name;
}

std::string AppGroup::identifier() const
{
	return _identifier;
}

std::string AppGroup::groupIdentifier() const
{
	return _groupIdentifier;
}
