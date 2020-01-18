#pragma once

#include <memory>

class AnisetteData;

class AnisetteDataManager
{
public:
	static AnisetteDataManager* instance();

	std::shared_ptr<AnisetteData> FetchAnisetteData();
	bool LoadDependencies();

private:
	AnisetteDataManager();
	~AnisetteDataManager();

	static AnisetteDataManager* _instance;

	bool ReprovisionDevice();
	bool EndProvisionDevice();
};

