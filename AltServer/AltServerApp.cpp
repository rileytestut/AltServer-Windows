//
//  AltServerApp.cpp
//  AltServer-Windows
//
//  Created by Riley Testut on 8/30/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include "AltServerApp.h"

#include "ConnectionManager.hpp"

AltServerApp* AltServerApp::_instance = nullptr;

AltServerApp* AltServerApp::instance()
{
	if (_instance == 0)
	{
		_instance = new AltServerApp();
	}

	return _instance;
}

AltServerApp::AltServerApp()
{
}

AltServerApp::~AltServerApp()
{
}

void AltServerApp::Start()
{
	ConnectionManager::instance()->Start();
}