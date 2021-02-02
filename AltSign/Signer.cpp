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

const char* AppleRootCertificateData = ""
"-----BEGIN CERTIFICATE-----\n"
"MIIEuzCCA6OgAwIBAgIBAjANBgkqhkiG9w0BAQUFADBiMQswCQYDVQQGEwJVUzET\n"
"MBEGA1UEChMKQXBwbGUgSW5jLjEmMCQGA1UECxMdQXBwbGUgQ2VydGlmaWNhdGlv\n"
"biBBdXRob3JpdHkxFjAUBgNVBAMTDUFwcGxlIFJvb3QgQ0EwHhcNMDYwNDI1MjE0\n"
"MDM2WhcNMzUwMjA5MjE0MDM2WjBiMQswCQYDVQQGEwJVUzETMBEGA1UEChMKQXBw\n"
"bGUgSW5jLjEmMCQGA1UECxMdQXBwbGUgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkx\n"
"FjAUBgNVBAMTDUFwcGxlIFJvb3QgQ0EwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAw\n"
"ggEKAoIBAQDkkakJH5HbHkdQ6wXtXnmELes2oldMVeyLGYne+Uts9QerIjAC6Bg+\n"
"+FAJ039BqJj50cpmnCRrEdCju+QbKsMflZ56DKRHi1vUFjczy8QPTc4UadHJGXL1\n"
"XQ7Vf1+b8iUDulWPTV0N8WQ1IxVLFVkds5T39pyez1C6wVhQZ48ItCD3y6wsIG9w\n"
"tj8BMIy3Q88PnT3zK0koGsj+zrW5DtleHNbLPbU6rfQPDgCSC7EhFi501TwN22IW\n"
"q6NxkkdTVcGvL0Gz+PvjcM3mo0xFfh9Ma1CWQYnEdGILEINBhzOKgbEwWOxaBDKM\n"
"aLOPHd5lc/9nXmW8Sdh2nzMUZaF3lMktAgMBAAGjggF6MIIBdjAOBgNVHQ8BAf8E\n"
"BAMCAQYwDwYDVR0TAQH/BAUwAwEB/zAdBgNVHQ4EFgQUK9BpR5R2Cf70a40uQKb3\n"
"R01/CF4wHwYDVR0jBBgwFoAUK9BpR5R2Cf70a40uQKb3R01/CF4wggERBgNVHSAE\n"
"ggEIMIIBBDCCAQAGCSqGSIb3Y2QFATCB8jAqBggrBgEFBQcCARYeaHR0cHM6Ly93\n"
"d3cuYXBwbGUuY29tL2FwcGxlY2EvMIHDBggrBgEFBQcCAjCBthqBs1JlbGlhbmNl\n"
"IG9uIHRoaXMgY2VydGlmaWNhdGUgYnkgYW55IHBhcnR5IGFzc3VtZXMgYWNjZXB0\n"
"YW5jZSBvZiB0aGUgdGhlbiBhcHBsaWNhYmxlIHN0YW5kYXJkIHRlcm1zIGFuZCBj\n"
"b25kaXRpb25zIG9mIHVzZSwgY2VydGlmaWNhdGUgcG9saWN5IGFuZCBjZXJ0aWZp\n"
"Y2F0aW9uIHByYWN0aWNlIHN0YXRlbWVudHMuMA0GCSqGSIb3DQEBBQUAA4IBAQBc\n"
"NplMLXi37Yyb3PN3m/J20ncwT8EfhYOFG5k9RzfyqZtAjizUsZAS2L70c5vu0mQP\n"
"y3lPNNiiPvl4/2vIB+x9OYOLUyDTOMSxv5pPCmv/K/xZpwUJfBdAVhEedNO3iyM7\n"
"R6PVbyTi69G3cN8PReEnyvFteO3ntRcXqNx+IjXKJdXZD9Zr1KIkIxH3oayPc4Fg\n"
"xhtbCS+SsvhESPBgOJ4V9T0mZyCKM2r3DYLP3uujL/lTaltkwGMzd/c6ByxW69oP\n"
"IQ7aunMZT7XZNn/Bh1XZp5m5MkL72NVxnn6hUrcbvZNCJBIqxw8dtk2cXmPIS4AX\n"
"UKqK1drk/NAJBzewdXUh\n"
"-----END CERTIFICATE-----\n";

const char* AppleWWDRCertificateData = ""
"-----BEGIN CERTIFICATE-----\n"
"MIIEUTCCAzmgAwIBAgIQfK9pCiW3Of57m0R6wXjF7jANBgkqhkiG9w0BAQsFADBi\n"
"MQswCQYDVQQGEwJVUzETMBEGA1UEChMKQXBwbGUgSW5jLjEmMCQGA1UECxMdQXBw\n"
"bGUgQ2VydGlmaWNhdGlvbiBBdXRob3JpdHkxFjAUBgNVBAMTDUFwcGxlIFJvb3Qg\n"
"Q0EwHhcNMjAwMjE5MTgxMzQ3WhcNMzAwMjIwMDAwMDAwWjB1MUQwQgYDVQQDDDtB\n"
"cHBsZSBXb3JsZHdpZGUgRGV2ZWxvcGVyIFJlbGF0aW9ucyBDZXJ0aWZpY2F0aW9u\n"
"IEF1dGhvcml0eTELMAkGA1UECwwCRzMxEzARBgNVBAoMCkFwcGxlIEluYy4xCzAJ\n"
"BgNVBAYTAlVTMIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA2PWJ/KhZ\n"
"C4fHTJEuLVaQ03gdpDDppUjvC0O/LYT7JF1FG+XrWTYSXFRknmxiLbTGl8rMPPbW\n"
"BpH85QKmHGq0edVny6zpPwcR4YS8Rx1mjjmi6LRJ7TrS4RBgeo6TjMrA2gzAg9Dj\n"
"+ZHWp4zIwXPirkbRYp2SqJBgN31ols2N4Pyb+ni743uvLRfdW/6AWSN1F7gSwe0b\n"
"5TTO/iK1nkmw5VW/j4SiPKi6xYaVFuQAyZ8D0MyzOhZ71gVcnetHrg21LYwOaU1A\n"
"0EtMOwSejSGxrC5DVDDOwYqGlJhL32oNP/77HK6XF8J4CjDgXx9UO0m3JQAaN4LS\n"
"VpelUkl8YDib7wIDAQABo4HvMIHsMBIGA1UdEwEB/wQIMAYBAf8CAQAwHwYDVR0j\n"
"BBgwFoAUK9BpR5R2Cf70a40uQKb3R01/CF4wRAYIKwYBBQUHAQEEODA2MDQGCCsG\n"
"AQUFBzABhihodHRwOi8vb2NzcC5hcHBsZS5jb20vb2NzcDAzLWFwcGxlcm9vdGNh\n"
"MC4GA1UdHwQnMCUwI6AhoB+GHWh0dHA6Ly9jcmwuYXBwbGUuY29tL3Jvb3QuY3Js\n"
"MB0GA1UdDgQWBBQJ/sAVkPmvZAqSErkmKGMMl+ynsjAOBgNVHQ8BAf8EBAMCAQYw\n"
"EAYKKoZIhvdjZAYCAQQCBQAwDQYJKoZIhvcNAQELBQADggEBAK1lE+j24IF3RAJH\n"
"Qr5fpTkg6mKp/cWQyXMT1Z6b0KoPjY3L7QHPbChAW8dVJEH4/M/BtSPp3Ozxb8qA\n"
"HXfCxGFJJWevD8o5Ja3T43rMMygNDi6hV0Bz+uZcrgZRKe3jhQxPYdwyFot30ETK\n"
"XXIDMUacrptAGvr04NM++i+MZp+XxFRZ79JI9AeZSWBZGcfdlNHAwWx/eCHvDOs7\n"
"bJmCS1JgOLU5gm3sUjFTvg+RTElJdI+mUcuER04ddSduvfnSXPN/wmwLCTbiZOTC\n"
"NwMUGdXqapSqqdv+9poIZ4vvK7iqF0mDr8/LvOnP6pVxsLRFoszlh6oKw0E6eVza\n"
"UDSdlTs=\n"
"-----END CERTIFICATE-----\n";

namespace fs = std::filesystem;

extern std::string make_uuid();

#define odslog(msg) { std::stringstream ss; ss << msg << std::endl; OutputDebugStringA(ss.str().c_str()); }

std::string CertificatesContent(std::shared_ptr<Certificate> altCertificate)
{
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
    
	// Prepare certificate chain of trust.
	auto* certificates = sk_X509_new(NULL);

	BIO* rootCertificateBuffer = BIO_new_mem_buf(AppleRootCertificateData, strlen(AppleRootCertificateData));
	auto rootCertificate = PEM_read_bio_X509(rootCertificateBuffer, NULL, NULL, NULL);
	if (rootCertificate != NULL)
	{
		sk_X509_push(certificates, rootCertificate);
	}

	BIO* wwdrCertificateBuffer = BIO_new_mem_buf(AppleWWDRCertificateData, strlen(AppleWWDRCertificateData));
	auto wwdrCertificate = PEM_read_bio_X509(wwdrCertificateBuffer, NULL, NULL, NULL);
	if (wwdrCertificate != NULL)
	{
		sk_X509_push(certificates, wwdrCertificate);
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

	BIO_free(wwdrCertificateBuffer);
	BIO_free(rootCertificateBuffer);
    
    BIO_free(inputP12Buffer);
    BIO_free(outputP12Buffer);
    
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

		for (auto appExtension : app.appExtensions())
		{
			prepareApp(*appExtension);
		}
        
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
                filepath = fs::canonical(fs::path(app.path()).append(path)).string();
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
        
        throw;
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
