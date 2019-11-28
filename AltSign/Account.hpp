//
//  Account.hpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/8/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef Account_hpp
#define Account_hpp

/* The classes below are exported */
#pragma GCC visibility push(default)

#include <optional>

#include <cpprest/http_client.h>

#include <plist/plist.h>

class Account
{
public:
    Account();
    ~Account();
    
	Account(plist_t plist); /* throws */
    
    std::string appleID() const;
    std::string identifier() const;
    std::string firstName() const;
    std::string lastName() const;
    std::string name() const;
    std::string cookie() const;
    
    friend std::ostream& operator<<(std::ostream& os, const Account& account);
    
private:
    std::string _appleID;
    std::string _identifier;
    std::string _firstName;
    std::string _lastName;
    std::string _cookie;
};

#pragma GCC visibility pop

#endif /* Account_hpp */
