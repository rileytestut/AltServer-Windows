//
//  Team.hpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/9/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef Team_hpp
#define Team_hpp

/* The classes below are exported */
#pragma GCC visibility push(default)

#include <cpprest/http_client.h>
#include <plist/plist.h>

#include "Account.hpp"

class Account;

class Team
{
public:
    enum Type
    {
        Unknown,
        Free,
        Individual,
        Organization
    };
    
    Team();
    ~Team();
    
    Team(std::shared_ptr<Account> account, plist_t plist) /* throws */;
    
    friend std::ostream& operator<<(std::ostream& os, const Team& account);
    
    std::string name() const;
    std::string identifier() const;
    Type type() const;
    
    std::shared_ptr<Account> account() const;
    
private:
    
    std::string _name;
    std::string _identifier;
    
    Type _type;
    std::shared_ptr<Account> _account;
};

#pragma GCC visibility pop

#endif /* Team_hpp */
