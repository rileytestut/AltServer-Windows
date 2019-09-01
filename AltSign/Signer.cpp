//
//  Signer.cpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/12/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include "Signer.hpp"
#include "Error.hpp"
#include "Archiver.hpp"
#include "Application.hpp"

#include "ldid.hpp"

#include <openssl/pkcs12.h>
#include <openssl/pem.h>
#include <openssl/applink.c>

#include <filesystem>

#include <thread>         // std::this_thread::sleep_for
#include <chrono>         // std::chrono::seconds

#include <WS2tcpip.h>

namespace fs = std::filesystem;

extern std::string make_uuid();

#define odslog(msg) { std::stringstream ss; ss << msg << std::endl; OutputDebugStringA(ss.str().c_str()); }

std::string CertificatesContent(std::shared_ptr<Certificate> altCertificate)
{
    fs::path pemPath = "C:\\Users\\User\\Desktop\\apple.pem";
    if (!fs::exists(pemPath))
    {
        throw SignError(SignErrorCode::MissingAppleRootCertificate);
    }
    
    auto altCertificateP12Data = altCertificate->p12Data();
    if (!altCertificateP12Data.has_value())
    {
        throw SignError(SignErrorCode::InvalidCertificate);
    }
    
    BIO *inputP12Buffer = BIO_new(BIO_s_mem());
    BIO_write(inputP12Buffer, altCertificateP12Data->data(), (int)altCertificateP12Data->size());
    
    auto inputP12 = d2i_PKCS12_bio(inputP12Buffer, NULL);
    
    // Extract key + certificate from .p12.
    EVP_PKEY *key = nullptr;
    X509 *certificate = nullptr;
    PKCS12_parse(inputP12, "", &key, &certificate, NULL);
    
    // Open .pem from file.
    auto pemFile = fopen((const char *)pemPath.string().c_str(), "r");
    
    // Extract certificates from .pem.
    auto *certificates = sk_X509_new(NULL);
    while (auto certificate = PEM_read_X509(pemFile, NULL, NULL, NULL))
    {
        sk_X509_push(certificates, certificate);
    }
    
    // Create new .p12 in memory with private key and certificate chain.
    char emptyString[] = "";
    auto outputP12 = PKCS12_create(emptyString, emptyString, key, certificate, certificates, 0, 0, 0, 0, 0);
    
    BIO *outputP12Buffer = BIO_new(BIO_s_mem());
    i2d_PKCS12_bio(outputP12Buffer, outputP12);
    
    char *buffer = NULL;
    int size = (int)BIO_get_mem_data(outputP12Buffer, &buffer);

	// Create string before freeing memory.
	std::string output((const char*)buffer, size);
    
    // Free .p12 structures
    PKCS12_free(inputP12);
    PKCS12_free(outputP12);
    
    BIO_free(inputP12Buffer);
    BIO_free(outputP12Buffer);
    
    // Close files
    fclose(pemFile);
    
    return output;
}

Signer::Signer(std::shared_ptr<Team> team, std::shared_ptr<Certificate> certificate) : _team(team), _certificate(certificate)
{
}

Signer::~Signer()
{
	int i = 0;
}

void Signer::SignApp(std::string path, std::vector<std::shared_ptr<ProvisioningProfile>> profiles)
{   
    fs::path appPath = fs::path(path);

	auto pathExtension = appPath.extension().string();
	std::transform(pathExtension.begin(), pathExtension.end(), pathExtension.begin(), [](unsigned char c) {
		return std::tolower(c);
	});
    
    std::optional<fs::path> ipaPath;
    fs::path appBundlePath;
    
    try
    {
        if (pathExtension == ".ipa")
        {
            ipaPath = appPath;
            
            auto uuid = make_uuid();
            auto outputDirectoryPath = appPath.remove_filename().append(uuid);
            
            fs::create_directory(outputDirectoryPath);
            
            appBundlePath = UnzipAppBundle(appPath.string(), outputDirectoryPath.string());
        }
        else
        {
            appBundlePath = appPath;
        }
        
        std::map<std::string, std::string> entitlementsByFilepath;
        
        auto profileForApp = [&profiles](Application &app) -> std::shared_ptr<ProvisioningProfile> {
            for (auto& profile : profiles)
            {
                if (profile->bundleIdentifier() == app.bundleIdentifier())
                {
                    return profile;
                }
            }
            
            return nullptr;
        };
        
        auto prepareApp = [&profileForApp, &entitlementsByFilepath](Application &app)
        {
            auto profile = profileForApp(app);
            if (profile == nullptr)
            {
                throw SignError(SignErrorCode::MissingProvisioningProfile);
            }
            
            fs::path profilePath = fs::path(app.path()).append("embedded.mobileprovision");
            
			std::ofstream fout(profilePath.string(), std::ios::out | std::ios::binary);
			fout.write((char*)& profile->data()[0], profile->data().size() * sizeof(char));
			fout.close();
            
            plist_t entitlements = profile->entitlements();
            
            char *entitlementsString = nullptr;
            uint32_t entitlementsSize = 0;
            plist_to_xml(entitlements, &entitlementsString, &entitlementsSize);
            
            entitlementsByFilepath[app.path()] = entitlementsString;
        };
        
        Application app(appBundlePath.string());
        prepareApp(app);
        
        // Sign application
        ldid::DiskFolder appBundle(app.path());
        std::string key = CertificatesContent(this->certificate());
        
        ldid::Sign("", appBundle, key, "",
                   ldid::fun([&](const std::string &path, const std::string &binaryEntitlements) -> std::string {
            std::string filepath;
            
            if (path.size() == 0)
            {
                filepath = app.path();
            }
            else
            {
                filepath = fs::path(app.path()).append(path).string();
            }
            
            auto entitlements = entitlementsByFilepath[filepath];
            return entitlements;
        }),
                   ldid::fun([&](const std::string &string) {
			odslog("Signing: " << string);
//            progress.completedUnitCount += 1;
        }),
                   ldid::fun([&](const double signingProgress) {
			odslog("Signing Progress: " << signingProgress);
        }));

		// Wait for resigning to finish.
		std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        // Zip app back up.
        if (ipaPath.has_value())
        {
            auto resignedPath = ZipAppBundle(appBundlePath.string());
            
            if (fs::exists(*ipaPath))
            {
                fs::remove(*ipaPath);
            }
            
            fs::rename(*ipaPath, resignedPath);
        }

		return;
    }
    catch (std::exception& e)
    {
        if (!ipaPath.has_value())
        {
            return;
        }
        
        fs::remove(*ipaPath);
        
        throw e;
    }
}

std::shared_ptr<Team> Signer::team() const
{
    return _team;
}

std::shared_ptr<Certificate> Signer::certificate() const
{
    return _certificate;
}
