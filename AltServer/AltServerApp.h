//
//  AltServerApp.hpp
//  AltServer-Windows
//
//  Created by Riley Testut on 8/30/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#pragma once

class AltServerApp
{
public:
	static AltServerApp *instance();

	void Start();

private:
	AltServerApp();
	~AltServerApp();

	static AltServerApp *_instance;
};
