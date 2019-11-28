//
//  AnisetteData.cpp
//  AltSign-Windows
//
//  Created by Riley Testut on 11/26/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include "AnisetteData.h"
#include <ostream>

extern std::string StringFromWideString(std::wstring wideString);
extern std::wstring WideStringFromString(std::string string);


AnisetteData::AnisetteData() : _routingInfo(0), _date({ 0 })
{
}

AnisetteData::~AnisetteData()
{
}

AnisetteData::AnisetteData(std::string machineID,
	std::string oneTimePassword,
	std::string localUserID,
	unsigned long long routingInfo,
	std::string deviceUniqueIdentifier,
	std::string deviceSerialNumber,
	std::string deviceDescription,
	struct timeval date,
	std::string locale,
	std::string timeZone) :
	_machineID(machineID), _oneTimePassword(oneTimePassword), _localUserID(localUserID), _routingInfo(routingInfo), 
	_deviceUniqueIdentifier(deviceUniqueIdentifier), _deviceSerialNumber(deviceSerialNumber), _deviceDescription(deviceDescription),
	_date(date), _locale(locale), _timeZone(timeZone)
{
}

//AnisetteData::AnisetteData(web::json::value json)
//{
//	auto machineID = StringFromWideString(json[L"machineID"].as_string());
//	auto oneTimePassword = StringFromWideString(json[L"oneTimePassword"].as_string());
//	auto localUserID = StringFromWideString(json[L"localUserID"].as_string());
//	auto routingInfo = json[L"routingInfo"].as_integer();
//	auto deviceUniqueIdentifier = StringFromWideString(json[L"deviceUniqueIdentifier"].as_string());
//	auto deviceSerialNumber = StringFromWideString(json[L"deviceSerialNumber"].as_string());
//	auto deviceDescription = StringFromWideString(json[L"deviceDescription"].as_string());
//	auto date = StringFromWideString(json[L"date"].as_string());
//	auto locale = StringFromWideString(json[L"locale"].as_string());
//	auto timeZone = StringFromWideString(json[L"timeZone"].as_string());
//
//	_machineID = machineID;
//	_oneTimePassword = oneTimePassword;
//	_localUserID = localUserID;
//	_routingInfo = routingInfo;
//	_deviceUniqueIdentifier = deviceUniqueIdentifier;
//	_deviceSerialNumber = deviceSerialNumber;
//	_deviceDescription = deviceDescription;
//	_date = date;
//	_locale = locale;
//	_timeZone = timeZone;
//}

std::ostream& operator<<(std::ostream& os, const AnisetteData& anisetteData)
{
	time_t time;
	struct tm* tm;
	char dateString[64];

	time = anisetteData.date().tv_sec;
	tm = localtime(&time);

	strftime(dateString, sizeof dateString, "%FT%T%z", tm);

	os << "MachineID : " << anisetteData.machineID() <<
		"\nOne-Time Password: " << anisetteData.oneTimePassword() <<
		"\nLocal User ID: " << anisetteData.localUserID() <<
		"\nDevice UDID: " << anisetteData.deviceUniqueIdentifier() <<
		"\nDevice Description: " << anisetteData.deviceDescription() <<
		"\nDate: " << dateString;

	return os;
}

std::string AnisetteData::machineID() const
{
	return _machineID;
}

std::string AnisetteData::oneTimePassword() const
{
	return _oneTimePassword;
}

std::string AnisetteData::localUserID() const
{
	return _localUserID;
}

unsigned long long AnisetteData::routingInfo() const
{
	return _routingInfo;
}

std::string AnisetteData::deviceUniqueIdentifier() const
{
	return _deviceUniqueIdentifier;
}

std::string AnisetteData::deviceSerialNumber() const
{
	return _deviceSerialNumber;
}

std::string AnisetteData::deviceDescription() const
{
	return _deviceDescription;
}

TIMEVAL AnisetteData::date() const
{
	return _date;
}

std::string AnisetteData::locale() const
{
	return _locale;
}

std::string AnisetteData::timeZone() const
{
	return _timeZone;
}

web::json::value AnisetteData::json() const
{
	time_t time;
	struct tm* tm;
	char dateString[64];

	time = this->date().tv_sec;
	tm = localtime(&time);

	strftime(dateString, sizeof dateString, "%FT%T%z", tm);

	auto json = web::json::value();
	json[L"machineID"] = web::json::value::string(WideStringFromString(this->machineID()));
	json[L"oneTimePassword"] = web::json::value::string(WideStringFromString(this->oneTimePassword()));
	json[L"localUserID"] = web::json::value::string(WideStringFromString(this->localUserID()));
	json[L"routingInfo"] = web::json::value::string(WideStringFromString(std::to_string(this->routingInfo())));
	json[L"deviceUniqueIdentifier"] = web::json::value::string(WideStringFromString(this->deviceUniqueIdentifier()));
	json[L"deviceSerialNumber"] = web::json::value::string(WideStringFromString(this->deviceSerialNumber()));
	json[L"deviceDescription"] = web::json::value::string(WideStringFromString(this->deviceDescription()));
	json[L"date"] = web::json::value::string(WideStringFromString(dateString));
	json[L"locale"] = web::json::value::string(WideStringFromString(this->locale()));
	json[L"timeZone"] = web::json::value::string(WideStringFromString(this->timeZone()));

	return json;
}

