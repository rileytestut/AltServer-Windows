//
//  AnisetteData.h
//  AltSign-Windows
//
//  Created by Riley Testut on 11/26/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#pragma once

/* The classes below are exported */
#pragma GCC visibility push(default)

#include <WinSock2.h>

#include <optional>
#include <string>

#include <ctime>

#include <cpprest/json.h>

class AnisetteData
{
public:
	AnisetteData();
	~AnisetteData();

	AnisetteData(std::string machineID, 
		std::string oneTimePassword, 
		std::string localUserID, 
		unsigned long long routingInfo,
		std::string deviceUniqueIdentifier,
		std::string deviceSerialNumber,
		std::string deviceDescription, 
		struct timeval date,
		std::string locale,
		std::string timeZone);

	std::string machineID() const;
	std::string oneTimePassword() const;
	std::string localUserID() const;
	unsigned long long routingInfo() const;
	std::string deviceUniqueIdentifier() const;
	std::string deviceSerialNumber() const;
	std::string deviceDescription() const;
	TIMEVAL date() const;
	std::string locale() const;
	std::string timeZone() const;

	web::json::value json() const;

	friend std::ostream& operator<<(std::ostream& os, const AnisetteData& anisetteData);

private:
	std::string _machineID;
	std::string _oneTimePassword;
	std::string _localUserID;
	unsigned long long _routingInfo;
	std::string _deviceUniqueIdentifier;
	std::string _deviceSerialNumber;
	std::string _deviceDescription;
	TIMEVAL _date;
	std::string _locale;
	std::string _timeZone;
};

#pragma GCC visibility pop

