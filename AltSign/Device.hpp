//
//  Device.hpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/10/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef Device_hpp
#define Device_hpp

/* The classes below are exported */
#pragma GCC visibility push(default)

#include <optional>
#include <string>

#include <plist/plist.h>

class Device
{
public:
    Device();
    ~Device();
    
    Device(std::string name, std::string identifier);
    Device(plist_t plist) /* throws */;
    
    std::string name() const;
    std::string identifier() const;
    
    friend std::ostream& operator<<(std::ostream& os, const Device& device);
    
private:
    std::string _name;
    std::string _identifier;
};

#pragma GCC visibility pop

#endif /* Device_hpp */
