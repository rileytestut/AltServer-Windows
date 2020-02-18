#pragma once

#include <memory>
#include <functional>

class AnisetteData;

class AnisetteDataManager
{
public:
	static AnisetteDataManager* instance();

	std::shared_ptr<AnisetteData> FetchAnisetteData();
	bool LoadDependencies();

	bool ResetProvisioning();

private:
	AnisetteDataManager();
	~AnisetteDataManager();

	static AnisetteDataManager* _instance;

	bool ReprovisionDevice(std::function<void(void)> provisionCallback);
};

