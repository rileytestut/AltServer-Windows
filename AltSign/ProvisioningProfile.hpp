//
//  ProvisioningProfile.hpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/12/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef ProvisioningProfile_hpp
#define ProvisioningProfile_hpp

/* The classes below are exported */
#pragma GCC visibility push(default)

#include <optional>
#include <string>
#include <vector>

#include <plist/plist.h>
//#include <plist/Date.h>

class ProvisioningProfile
{
public:
    ProvisioningProfile();
    ~ProvisioningProfile();
    
    ProvisioningProfile(plist_t plist) /* throws */;
    ProvisioningProfile(std::vector<unsigned char>& data) /* throws */;
    ProvisioningProfile(std::string filepath) /* throws */;
    
    std::string name() const;
    std::optional<std::string> identifier() const;
    std::string uuid() const;
    
    std::string bundleIdentifier() const;
    std::string teamIdentifier() const;
    
   /* PList::Date creationDate() const;
    PList::Date expirationDate() const;*/
    
    plist_t entitlements() const;
    
    std::vector<unsigned char> data() const;
    
    friend std::ostream& operator<<(std::ostream& os, const ProvisioningProfile& profile);
    
private:
    std::string _name;
    std::optional<std::string> _identifier;
    std::string _uuid;
    
    std::string _bundleIdentifier;
    std::string _teamIdentifier;
    
    /*PList::Date _creationDate;
    PList::Date _expirationDate;*/
    
    plist_t _entitlements;
    
    std::vector<unsigned char> _data;
    
    void ParseData(std::vector<unsigned char>& data);
};

#pragma GCC visibility pop

#endif /* ProvisioningProfile_hpp */
