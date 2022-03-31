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
#include <map>

#include <plist/plist.h>

extern const char* ALTEntitlementApplicationIdentifier;
extern const char* ALTEntitlementKeychainAccessGroups;
extern const char* ALTEntitlementAppGroups;
extern const char* ALTEntitlementGetTaskAllow;
extern const char* ALTEntitlementTeamIdentifier;
extern const char* ALTEntitlementInterAppAudio;

extern const char* ALTFeatureGameCenter;
extern const char* ALTFeatureAppGroups;
extern const char* ALTFeatureInterAppAudio;

std::optional<std::string> ALTEntitlementForFeature(std::string feature);
std::optional<std::string> ALTFeatureForEntitlement(std::string entitlement);

class AppID
{
public:
    AppID();
    ~AppID();

	AppID(const AppID& appID);
	AppID& operator=(const AppID& appID);
    
    AppID(plist_t plist) /* throws */;
    
    std::string name() const;
    std::string identifier() const;
    std::string bundleIdentifier() const;

	std::map<std::string, plist_t> features() const;
	void setFeatures(std::map<std::string, plist_t> features);
    
    friend std::ostream& operator<<(std::ostream& os, const AppID& appID);
    
private:
    std::string _name;
    std::string _identifier;
    std::string _bundleIdentifier;

	std::map<std::string, plist_t> _features;
};

#pragma GCC visibility pop

#endif /* AppID_hpp */
