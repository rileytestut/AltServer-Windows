//
//  Certificate.hpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/12/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef Certificate_hpp
#define Certificate_hpp

/* The classes below are exported */
#pragma GCC visibility push(default)

#include <string>
#include <vector>
#include <optional>

#include <plist/plist.h>
#include <cpprest/json.h>

class Certificate
{
public:
    Certificate();
    ~Certificate();
    
    Certificate(plist_t plist) /* throws */;
	Certificate(web::json::value plist) /* throws */;
    Certificate(std::vector<unsigned char>& data);
	Certificate(std::vector<unsigned char>& p12Data, std::string password);
    
    std::string name() const;
    std::string serialNumber() const;
	std::optional<std::string> identifier() const;
	std::optional<std::string> machineName() const;

	std::optional<std::string> machineIdentifier() const;
	void setMachineIdentifier(std::optional<std::string> machineIdentifier);
    
    std::optional<std::vector<unsigned char>> data() const;
	std::optional<std::vector<unsigned char>> encryptedP12Data(std::string password) const;
    
    std::optional<std::vector<unsigned char>> privateKey() const;
    void setPrivateKey(std::optional<std::vector<unsigned char>> privateKey);
    
    std::optional<std::vector<unsigned char>> p12Data() const;
    
    friend std::ostream& operator<<(std::ostream& os, const Certificate& certificate);
    
private:
    std::string _name;
    std::string _serialNumber;
	std::optional<std::string> _identifier;
	std::optional<std::string> _machineName;
	std::optional<std::string> _machineIdentifier;
    
    std::optional<std::vector<unsigned char>> _data;
    std::optional<std::vector<unsigned char>> _privateKey;
    
    void ParseData(std::vector<unsigned char>& data);
};

#pragma GCC visibility pop

#endif /* Certificate_hpp */
