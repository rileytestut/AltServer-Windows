//
//  AppleAPISession.h
//  AltSign-Windows
//
//  Created by Riley Testut on 11/26/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#pragma once

/* The classes below are exported */
#pragma GCC visibility push(default)

class AnisetteData;

#include <optional>
#include <string>
#include <memory>

class AppleAPISession
{
public:
	AppleAPISession();
	~AppleAPISession();

	AppleAPISession(std::string dsid, std::string authToken, std::shared_ptr<AnisetteData> anisetteData);

	std::string dsid() const;
	std::string authToken() const;
	std::shared_ptr<AnisetteData> anisetteData() const;

	friend std::ostream& operator<<(std::ostream& os, const AppleAPISession& session);

private:
	std::string _dsid;
	std::string _authToken;
	std::shared_ptr<AnisetteData> _anisetteData;
};

#pragma GCC visibility pop

