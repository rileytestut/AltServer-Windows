//
//  DeveloperDiskManager.cpp
//  AltServer-Windows
//
//  Created by Riley Testut on 7/1/21.
//  Copyright © 2021 Riley Testut. All rights reserved.
//

#include "DeveloperDiskManager.h"
#include "AltServerApp.h"

#include "Archiver.hpp"

#include <WS2tcpip.h>

#include <cpprest/filestream.h>

#include <iostream>
#include <fstream>

#define odslog(msg) { std::stringstream ss; ss << msg << std::endl; OutputDebugStringA(ss.str().c_str()); }

extern std::string StringFromWideString(std::wstring wideString);
extern std::wstring WideStringFromString(std::string string);
extern std::string temporary_directory();
extern std::string make_uuid();

using namespace web;
using namespace web::http;
using namespace web::http::client;
using namespace concurrency::streams;

DeveloperDiskManager::DeveloperDiskManager() : 
#if STAGING
	_client(U("https://f000.backblazeb2.com"))
#else
	_client(U("https://cdn.altstore.io"))
#endif
{
}

DeveloperDiskManager::~DeveloperDiskManager()
{
}

pplx::task<std::pair<std::string, std::string>> DeveloperDiskManager::DownloadDeveloperDisk(std::shared_ptr<Device> device)
{
	return pplx::create_task([=] {

		std::string osName;
		switch (device->type())
		{
		case Device::Type::iPhone:
		case Device::Type::iPad:
			osName = "iOS";
			break;

		case Device::Type::AppleTV:
			osName = "tvOS";
			break;

		default: throw DeveloperDiskError(DeveloperDiskErrorCode::UnsupportedOperatingSystem);
		}

		std::string osVersion = std::to_string(device->osVersion().majorVersion) + "." + std::to_string(device->osVersion().minorVersion);

		fs::path developerDiskOSPath = AltServerApp::instance()->developerDisksDirectoryPath();
		developerDiskOSPath.append(osName);
		fs::create_directory(developerDiskOSPath);

		fs::path developerDiskDirectoryPath(developerDiskOSPath);
		developerDiskDirectoryPath.append(osVersion);
		fs::create_directory(developerDiskDirectoryPath);

		fs::path developerDiskPath(developerDiskDirectoryPath);
		developerDiskPath.append("DeveloperDiskImage.dmg");

		fs::path developerDiskSignaturePath(developerDiskDirectoryPath);
		developerDiskSignaturePath.append("DeveloperDiskImage.dmg.signature");

		if (fs::exists(developerDiskPath) && fs::exists(developerDiskSignaturePath))
		{
			return pplx::create_task([developerDiskPath, developerDiskSignaturePath]() {
				return std::make_pair(developerDiskPath.string(), developerDiskSignaturePath.string());
			});
		}
		else
		{
			return this->FetchDeveloperDiskURLs()
				.then([=](web::json::value json) {
				auto allDisks = json[L"disks"];
				if (!allDisks.has_object_field(WideStringFromString(osName.c_str())))
				{
					throw DeveloperDiskError(DeveloperDiskErrorCode::UnknownDownloadURL);
				}

				auto disks = allDisks[WideStringFromString(osName.c_str())];
				if (!disks.has_object_field(WideStringFromString(osVersion.c_str())))
				{
					throw DeveloperDiskError(DeveloperDiskErrorCode::UnknownDownloadURL);
				}

				auto diskURLs = disks[WideStringFromString(osVersion.c_str())];
				if (!diskURLs.has_string_field(L"archive") && !(diskURLs.has_string_field(L"disk") && diskURLs.has_string_field(L"signature")))
				{
					throw DeveloperDiskError(DeveloperDiskErrorCode::UnknownDownloadURL);
				}

				if (diskURLs.has_string_field(L"archive"))
				{
					// Download archive then unzip

					auto archiveURL = StringFromWideString(diskURLs[L"archive"].as_string());
					return this->DownloadDiskArchive(archiveURL);
				}
				else
				{
					// Download files directly

					auto diskURL = StringFromWideString(diskURLs[L"disk"].as_string());
					auto signatureURL = StringFromWideString(diskURLs[L"signature"].as_string());
					return this->DownloadDisk(diskURL, signatureURL);
				}				
			})
			.then([developerDiskPath, developerDiskSignaturePath](std::pair<std::string, std::string> paths) {
				try {
					fs::rename(fs::path(paths.first), developerDiskPath);
					fs::rename(fs::path(paths.second), developerDiskSignaturePath);
				}
				catch (std::exception& e) {
					fs::remove(fs::path(paths.first));
					fs::remove(fs::path(paths.second));
					throw;
				}
				
				return std::make_pair(developerDiskPath.string(), developerDiskSignaturePath.string());
			});
		}
	});
};

pplx::task<web::json::value> DeveloperDiskManager::FetchDeveloperDiskURLs()
{
#if STAGING
	auto encodedURI = web::uri::encode_uri(L"/file/altstore-staging/altserver/developerdisks.json");
#else
	auto encodedURI = web::uri::encode_uri(L"/file/altstore/altserver/developerdisks.json");
#endif
	uri_builder builder(encodedURI);

	http_request request(methods::GET);
	request.set_request_uri(builder.to_string());

	return this->client().request(request)
	.then([=](http_response response)
		{
			return response.content_ready();
		})
	.then([=](http_response response)
		{
			odslog("Received response status code: " << response.status_code());
			return response.extract_vector();
		})
	.then([=](std::vector<unsigned char> decompressedData)
	{
		std::string decompressedJSON = std::string(decompressedData.begin(), decompressedData.end());

		if (decompressedJSON.size() == 0)
		{
			return json::value::object();
		}

		utility::stringstream_t s;
		s << WideStringFromString(decompressedJSON);

		auto json = json::value::parse(s);
		return json;
	});
}

pplx::task<size_t> DeveloperDiskManager::DownloadFile(std::string downloadURL, fs::path destinationPath)
{
	auto outputFile = std::make_shared<ostream>();

	// Open stream to output file.
	return fstream::open_ostream(WideStringFromString(destinationPath.string()))
		.then([=](ostream file)
			{
				*outputFile = file;

				// Decode -> encode handles both already encoded URLs and raw URLs.
				auto decodedURI = web::uri::decode(WideStringFromString(downloadURL));
				auto encodedURI = web::uri::encode_uri(decodedURI);
				uri_builder builder(encodedURI);

				http_client client(builder.to_uri());
				return client.request(methods::GET);
			})
		.then([=](http_response response)
			{
				printf("Received download response status code:%u\n", response.status_code());

				// Write response body into the file.
				return response.body().read_to_end(outputFile->streambuf());
			})
		.then([=](size_t size)
			{
				outputFile->close();
				return size;
			});
}

pplx::task<std::pair<std::string, std::string>> DeveloperDiskManager::DownloadDiskArchive(std::string archiveURL)
{
    fs::path temporaryPath(temporary_directory());
    temporaryPath.append(make_uuid());

    fs::path archivePath(temporaryPath);
    archivePath.append("archive.zip");

    return pplx::create_task([=] {
        fs::create_directory(temporaryPath);
        return this->DownloadFile(archiveURL, archivePath);
    })
	.then([=](size_t size) {
		UnzipArchive(archivePath.string(), temporaryPath.string());

		fs::path diskPath;
		fs::path signaturePath;

		for (const auto& entry : fs::recursive_directory_iterator(temporaryPath))
		{
			auto extension = entry.path().extension().string();
			std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) {
				return std::tolower(c);
			});

			if (extension == ".dmg")
			{
				diskPath = entry.path();
			}
			else if (extension == ".signature")
			{
				signaturePath = entry.path();
			}
		}

		if (diskPath.empty() || signaturePath.empty())
		{
			throw DeveloperDiskError(DeveloperDiskErrorCode::DownloadedDiskNotFound);
		}

		fs::path destinationDiskPath(temporary_directory());
		destinationDiskPath.append(make_uuid());

		fs::path destinationSignaturePath(temporary_directory());
		destinationSignaturePath.append(make_uuid());

		try {
			fs::rename(diskPath, destinationDiskPath);
			fs::rename(signaturePath, destinationSignaturePath);
		}
		catch (std::exception& e) {
			fs::remove(destinationDiskPath);
			fs::remove(destinationSignaturePath);
			throw;
		}

		return std::make_pair(destinationDiskPath.string(), destinationSignaturePath.string());
	})
	.then([=](pplx::task<std::pair<std::string, std::string>> task) {
		try { fs::remove(temporaryPath); }
		catch (std::exception& e) { /* Ignore */ }

		return task.get();
	});
}

pplx::task<std::pair<std::string, std::string>> DeveloperDiskManager::DownloadDisk(std::string diskURL, std::string signatureURL)
{
	fs::path temporaryPath(temporary_directory());
	temporaryPath.append(make_uuid());

	fs::path diskPath(temporaryPath);
	diskPath.append("DeveloperDiskImage.dmg");

	fs::path signaturePath(temporaryPath);
	signaturePath.append("DeveloperDiskImage.dmg.signature");

    return pplx::create_task([=] {
        fs::create_directory(temporaryPath);
        return this->DownloadFile(diskURL, diskPath);
    })
	.then([=](size_t diskSize) {
		return this->DownloadFile(signatureURL, signaturePath.string());
	})
	.then([=](size_t signatureSize) {
		return std::make_pair(diskPath.string(), signaturePath.string());
	})
	.then([=](pplx::task<std::pair<std::string, std::string>> task) {
		try { 
			return task.get(); 
		}
		catch (std::exception& e)
		{
			// Only remove directory if error was thrown.
			try { fs::remove(temporaryPath); }
			catch (std::exception& e) { /* Ignore */ }

			throw;
		}
	});
}

web::http::client::http_client DeveloperDiskManager::client() const
{
	return this->_client;
}