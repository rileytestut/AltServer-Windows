//
//  Team.cpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/9/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include "Team.hpp"

#include "PrefixHeader.pch"

Team::Team()
{
}

Team::~Team()
{
}

Team::Team(std::shared_ptr<Account> account, plist_t plist) /* throws */
{
    auto nameNode = plist_dict_get_item(plist, "name");
    auto identifierNode = plist_dict_get_item(plist, "teamId");
    auto typeNode = plist_dict_get_item(plist, "type");
    
    if (nameNode == nullptr || identifierNode == nullptr || typeNode == nullptr)
    {
        throw APIError(APIErrorCode::InvalidResponse);
    }
    
    char *name = nullptr;
    plist_get_string_val(nameNode, &name);
    
    char *identifier = 0;
    plist_get_string_val(identifierNode, &identifier);
    
    char *teamType = nullptr;
    plist_get_string_val(typeNode, &teamType);
    
    Team::Type type = Type::Unknown;
    
    if (std::string(teamType) == "Company/Organization")
    {
        type = Type::Organization;
    }
    else if (std::string(teamType) == "Individual")
    {
        type = Type::Individual;
        
        plist_t memberships = plist_dict_get_item(plist, "memberships");
        if (memberships != nullptr && plist_array_get_size(memberships) == 1)
        {
            plist_t membership = plist_array_get_item(memberships, 0);
            
            plist_t nameNode = plist_dict_get_item(membership, "name");
            if (nameNode != nullptr)
            {
                char *rawName = nullptr;
                plist_get_string_val(nameNode, &rawName);
                
                // Make lowercase.
                std::string name = rawName;
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
                    return std::tolower(c);
                });
                
                if (name.find("free") != std::string::npos)
                {
                    type = Type::Free;
                }
            }
        }
    }
    else
    {
        type = Type::Unknown;
    }
    
    _name = name;
    _identifier = identifier;
    _type = type;
    _account = account;
}

#pragma mark - Description -

std::ostream& operator<<(std::ostream& os, const Team& team)
{
    os << "Name: " << team.name();
    return os;
}

#pragma mark - Getters -

std::string Team::name() const
{
    return _name;
}

std::string Team::identifier() const
{
    return _identifier;
}

Team::Type Team::type() const
{
    return _type;
}

std::shared_ptr<Account> Team::account() const
{
    return _account;
}
