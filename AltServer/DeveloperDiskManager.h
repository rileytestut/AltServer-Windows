//
//  DeveloperDiskManager.hpp
//  AltServer-Windows
//
//  Created by Riley Testut on 7/1/21.
//  Copyright © 2021 Riley Testut. All rights reserved.
//

#pragma once

#include <pplx/pplxtasks.h>
#include <filesystem>

#include <cpprest/http_client.h>
#include <cpprest/json.h>

#include "Device.hpp"

#include "Error.hpp"

enum class DeveloperDiskErrorCode
{
	UnknownDownloadURL,
	UnsupportedOperatingSystem,
	DownloadedDiskNotFound
};

class DeveloperDiskError : public Error
{
public:
	DeveloperDiskError(DeveloperDiskErrorCode code) : Error((int)code)
	{
	}

	virtual std::string domain() const
	{
		return "com.rileytestut.AltServer.DeveloperDisk";
	}

	virtual std::optional<std::string> localizedFailureReason() const
	{
		switch ((DeveloperDiskErrorCode)this->code())
		{
		case DeveloperDiskErrorCode::UnknownDownloadURL:
			return "The URL to download the Developer disk image could not be determined.";

		case DeveloperDiskErrorCode::UnsupportedOperatingSystem:
			return "The device's operating system does not support installing Developer disk images.";

		case DeveloperDiskErrorCode::DownloadedDiskNotFound:
			return "DeveloperDiskImage.dmg and its signature could not be found in the downloaded archive.";
		}
	}
};

class DeveloperDiskManager
{
public:
	DeveloperDiskManager();
	~DeveloperDiskManager();

	pplx::task<std::pair<std::string, std::string>> DownloadDeveloperDisk(std::shared_ptr<Device> device);

	bool IsDeveloperDiskCompatible(std::shared_ptr<Device> device);
	void SetDeveloperDiskCompatible(bool compatible, std::shared_ptr<Device> device);

private:
	web::http::client::http_client _client;
	web::http::client::http_client client() const;

	pplx::task<web::json::value> FetchDeveloperDiskURLs();

	pplx::task<size_t> DownloadFile(std::string downloadURL, std::filesystem::path destinationPath);
	pplx::task<std::pair<std::string, std::string>> DownloadDiskArchive(std::string archiveURL);
	pplx::task<std::pair<std::string, std::string>> DownloadDisk(std::string diskURL, std::string signatureURL);

	std::optional<std::string> DeveloperDiskCompatibilityID(std::shared_ptr<Device> device);
};

