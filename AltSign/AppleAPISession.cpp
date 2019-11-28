//
//  AppleAPISession.cpp
//  AltSign-Windows
//
//  Created by Riley Testut on 11/26/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include "AppleAPISession.h"

#include "AnisetteData.h"
#include <ostream>

AppleAPISession::AppleAPISession()
{
}

AppleAPISession::~AppleAPISession()
{
}

AppleAPISession::AppleAPISession(std::string dsid, std::string authToken, std::shared_ptr<AnisetteData> anisetteData) :
	_dsid(dsid), _authToken(authToken), _anisetteData(anisetteData)
{
}

std::ostream& operator<<(std::ostream& os, const AppleAPISession& session)
{
	os << "DSID : " << session.dsid() <<
		"\nAuth Token: " << session.authToken() <<
		"\nAnisette Data: " << session.anisetteData();

	return os;
}

std::string AppleAPISession::dsid() const
{
	return _dsid;
}

std::string AppleAPISession::authToken() const
{
	return _authToken;
}

std::shared_ptr<AnisetteData> AppleAPISession::anisetteData() const
{
	return _anisetteData;
}

