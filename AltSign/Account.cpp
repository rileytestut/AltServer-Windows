//
//  Account.cpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/8/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include <iostream>

#include "PrefixHeader.pch"

Account::Account(plist_t plist) /* throws */
{
	auto appleIDNode = plist_dict_get_item(plist, "email");
    auto identifierNode = plist_dict_get_item(plist, "personId");

	auto firstNameNode = plist_dict_get_item(plist, "firstName");
	if (firstNameNode == nullptr)
	{
		firstNameNode = plist_dict_get_item(plist, "dsFirstName");
	}

	auto lastNameNode = plist_dict_get_item(plist, "lastName");
	if (lastNameNode == nullptr)
	{
		lastNameNode = plist_dict_get_item(plist, "dsLastName");
	}
    
    if (appleIDNode == nullptr || identifierNode == nullptr || firstNameNode == nullptr || lastNameNode == nullptr)
    {
        throw APIError(APIErrorCode::InvalidResponse);
    }
    
    uint64_t identifier = 0;
    switch (plist_get_node_type(identifierNode))
    {
        case PLIST_UINT:
            plist_get_uint_val(identifierNode, &identifier);
            break;
            
        case PLIST_REAL:
        {
            double value = 0;
            plist_get_real_val(identifierNode, &value);
            identifier = (uint64_t)value;
            break;
        }
            
        default:
            break;
    }

	char* appleID = nullptr;
	plist_get_string_val(appleIDNode, &appleID);
    
    char *firstName = nullptr;
    plist_get_string_val(firstNameNode, &firstName);
    
    char *lastName = nullptr;
    plist_get_string_val(lastNameNode, &lastName);
    
	_appleID = appleID;
    _identifier = std::to_string(identifier);
    _firstName = firstName;
    _lastName = lastName;
}

Account::Account()
{
}

Account::~Account()
{
}

#pragma mark - Description -

std::ostream& operator<<(std::ostream& os, const Account& account)
{
    os << "Name: " << account.name() << " Apple ID: " << account.appleID();
    return os;
}

#pragma mark - Getters -

std::string Account::appleID() const
{
    return _appleID;
}

std::string Account::identifier() const
{
    return _identifier;
}

std::string Account::firstName() const
{
    return _firstName;
}

std::string Account::lastName() const
{
    return _lastName;
}

std::string Account::name() const
{
    return firstName() + " " + lastName();
}

std::string Account::cookie() const
{
    return _cookie;
}

