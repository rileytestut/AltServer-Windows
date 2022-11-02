#pragma once

#include <memory>
#include <functional>

#include "Error.hpp"

class AnisetteData;

enum class AnisetteErrorCode
{
	iTunesNotInstalled,
	iCloudNotInstalled,
	MissingApplicationSupportFolder,
	MissingAOSKit,
	MissingObjc,
	MissingFoundation,
	InvalidiTunesInstallation,
};

class AnisetteError : public Error
{
public:
	AnisetteError(AnisetteErrorCode code, std::map<std::string, std::any> userInfo = {}) : Error((int)code, userInfo)
	{
	}

	virtual std::string domain() const
	{
		return "AltServer.AppleProgramError";
	}

	virtual std::optional<std::string> localizedFailureReason() const
	{
		switch ((AnisetteErrorCode)this->code())
		{
		case AnisetteErrorCode::iTunesNotInstalled: return "iTunes Not Found";
		case AnisetteErrorCode::iCloudNotInstalled: return "iCloud Not Found";
		case AnisetteErrorCode::MissingApplicationSupportFolder: return "Missing 'Application Support' in 'Apple' Folder.";
		case AnisetteErrorCode::MissingAOSKit: return "Missing 'AOSKit.dll' in 'Internet Services' Folder.";
		case AnisetteErrorCode::MissingFoundation: return "Missing 'Foundation.dll' in 'Apple Application Support' Folder.";
		case AnisetteErrorCode::MissingObjc: return "Missing 'objc.dll' in 'Apple Application Support' Folder.";
		case AnisetteErrorCode::InvalidiTunesInstallation: return "Invalid iTunes installation.";
		}

		return std::nullopt;
	}
};

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
	bool LoadiCloudDependencies();

	bool loadedDependencies;
};

