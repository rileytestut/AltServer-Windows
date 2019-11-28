//
//  AppID.hpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/12/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef AppID_hpp
#define AppID_hpp

/* The classes below are exported */
#pragma GCC visibility push(default)

#include <optional>
#include <string>

#include <plist/plist.h>

class AppID
{
public:
    AppID();
    ~AppID();
    
    AppID(plist_t plist) /* throws */;
    
    std::string name() const;
    std::string identifier() const;
    std::string bundleIdentifier() const;
    
    friend std::ostream& operator<<(std::ostream& os, const AppID& appID);
    
private:
    std::string _name;
    std::string _identifier;
    std::string _bundleIdentifier;
};

#pragma GCC visibility pop

#endif /* AppID_hpp */
