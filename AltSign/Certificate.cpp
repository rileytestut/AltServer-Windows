//
//  Certificate.cpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/12/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include "Certificate.hpp"
#include "Error.hpp"

#include <openssl/pem.h>
#include <openssl/pkcs12.h>

#include <cpprest/http_client.h>

extern std::string StringFromWideString(std::wstring wideString);
extern std::wstring WideStringFromString(std::string string);

std::string kCertificatePEMPrefix = "-----BEGIN CERTIFICATE-----";
std::string kCertificatePEMSuffix = "-----END CERTIFICATE-----";

Certificate::Certificate()
{
}

Certificate::~Certificate()
{
}

Certificate::Certificate(plist_t plist)
{
	auto dataNode = plist_dict_get_item(plist, "certContent");

    if (dataNode != nullptr)
    {
        char *bytes = nullptr;
        uint64_t size = 0;
        plist_get_data_val(dataNode, &bytes, &size);
        
        std::vector<unsigned char> data;
		data.reserve(size);

        for (int i = 0; i < size; i++)
        {
            data.push_back(bytes[i]);
        }
        
        this->ParseData(data);
    }
	else
	{
		auto nameNode = plist_dict_get_item(plist, "name");
		auto serialNumberNode = plist_dict_get_item(plist, "serialNumber");
		if (serialNumberNode == nullptr)
		{
			serialNumberNode = plist_dict_get_item(plist, "serialNum");
		}

		if (nameNode == nullptr || serialNumberNode == nullptr)
		{
			throw APIError(APIErrorCode::InvalidResponse);
		}

		char* name = nullptr;
		plist_get_string_val(nameNode, &name);

		char* serialNumber = nullptr;
		plist_get_string_val(serialNumberNode, &serialNumber);

		_name = name;
		_serialNumber = serialNumber;
	}    

	auto machineNameNode = plist_dict_get_item(plist, "machineName");
	auto machineIdentifierNode = plist_dict_get_item(plist, "machineId");

	if (machineNameNode != nullptr)
	{
		char* machineName = nullptr;
		plist_get_string_val(machineNameNode, &machineName);

		_machineName = machineName;
	}

	if (machineIdentifierNode != nullptr)
	{
		char* machineIdentifier = nullptr;
		plist_get_string_val(machineIdentifierNode, &machineIdentifier);

		_machineIdentifier = machineIdentifier;
	}
}

Certificate::Certificate(std::vector<unsigned char>& p12Data, std::string password)
{
	BIO* inputP12Buffer = BIO_new(BIO_s_mem());
	BIO_write(inputP12Buffer, p12Data.data(), (int)p12Data.size());

	PKCS12* inputP12 = d2i_PKCS12_bio(inputP12Buffer, NULL);

	// Extract key + certificate from .p12.
	EVP_PKEY* key;
	X509* certificate;
	PKCS12_parse(inputP12, password.c_str(), &key, &certificate, NULL);

	if (key == nullptr || certificate == nullptr)
	{
		throw APIError(APIErrorCode::InvalidResponse);
	}

	BIO* pemBuffer = BIO_new(BIO_s_mem());
	PEM_write_bio_X509(pemBuffer, certificate);

	BIO* privateKeyBuffer = BIO_new(BIO_s_mem());
	PEM_write_bio_PrivateKey(privateKeyBuffer, key, NULL, NULL, 0, NULL, NULL);

	char* pemBytes = NULL;
	int pemSize = BIO_get_mem_data(pemBuffer, &pemBytes);

	char* privateKeyBytes = NULL;
	int privateKeySize = BIO_get_mem_data(privateKeyBuffer, &privateKeyBytes);

	std::vector<unsigned char> pemData;
	pemData.reserve(pemSize);
	for (int i = 0; i < pemSize; i++)
	{
		p12Data.push_back(pemBytes[i]);
	}

	std::vector<unsigned char> privateKey;
	privateKey.reserve(privateKeySize);
	for (int i = 0; i < privateKeySize; i++)
	{
		privateKey.push_back(privateKeyBytes[i]);
	}

	this->ParseData(pemData);
}

Certificate::Certificate(std::vector<unsigned char>& data)
{
    this->ParseData(data);
}

void Certificate::ParseData(std::vector<unsigned char>& data)
{
    std::vector<unsigned char> pemData;
    
    std::string prefix(data.begin(), data.begin() + std::min(data.size(), kCertificatePEMPrefix.size()));
    if (prefix != kCertificatePEMPrefix)
    {
        // Convert to proper PEM format before storing.
        utility::string_t base64Data = utility::conversions::to_base64(data);
        
        std::stringstream ss;
        ss << kCertificatePEMPrefix << std::endl << StringFromWideString(base64Data) << std::endl << kCertificatePEMSuffix;
        
        auto content = ss.str();
        pemData = std::vector<unsigned char>(content.begin(), content.end());
    }
	else
	{
		pemData = data;
	}
    
    BIO *certificateBuffer = BIO_new(BIO_s_mem());
    BIO_write(certificateBuffer, pemData.data(), (int)pemData.size());
    
    X509 *certificate = nullptr;
    PEM_read_bio_X509(certificateBuffer, &certificate, 0, 0);
    if (certificate == nullptr)
    {
        throw APIError(APIErrorCode::InvalidResponse);
    }
    
    /* Certificate Common Name */
    X509_NAME *subject = X509_get_subject_name(certificate);
    int index = X509_NAME_get_index_by_NID(subject, NID_commonName, -1);
    if (index == -1)
    {
        throw APIError(APIErrorCode::InvalidResponse);
    }
    
    X509_NAME_ENTRY *nameEntry = X509_NAME_get_entry(subject, index);
    ASN1_STRING *nameData = X509_NAME_ENTRY_get_data(nameEntry);
    char *cName = (char *)ASN1_STRING_data(nameData);
    
    
    /* Serial Number */
    ASN1_INTEGER *serialNumberData = X509_get_serialNumber(certificate);
    BIGNUM *number = ASN1_INTEGER_to_BN(serialNumberData, NULL);
    if (number == nullptr)
    {
        throw APIError(APIErrorCode::InvalidResponse);
    }
    
    char *cSerialNumber = BN_bn2hex(number);
    
    if (cName == nullptr || cSerialNumber == nullptr)
    {
        throw APIError(APIErrorCode::InvalidResponse);
    }
    
    std::string serialNumber(cSerialNumber);
    serialNumber.erase(0, std::min(serialNumber.find_first_not_of('0'), serialNumber.size() - 1)); // Remove leading zeroes.
    
    _name = cName;
    _serialNumber = serialNumber;
    _data = pemData;
}

#pragma mark - Description -

std::ostream& operator<<(std::ostream& os, const Certificate& certificate)
{
    os << "Name: " << certificate.name() << " SN: " << certificate.serialNumber();
    return os;
}

#pragma mark - Getters -

std::string Certificate::name() const
{
    return _name;
}

std::string Certificate::serialNumber() const
{
    return _serialNumber;
}

std::optional<std::string> Certificate::machineName() const
{
	return _machineName;
}

std::optional<std::string> Certificate::machineIdentifier() const
{
	return _machineIdentifier;
}

std::optional<std::vector<unsigned char>> Certificate::data() const
{
    return _data;
}

std::optional<std::vector<unsigned char>> Certificate::privateKey() const
{
    return _privateKey;
}

void Certificate::setPrivateKey(std::optional<std::vector<unsigned char>> privateKey)
{
    _privateKey = privateKey;
}

std::optional<std::vector<unsigned char>> Certificate::p12Data() const
{
	return this->encryptedP12Data("");
}

std::optional<std::vector<unsigned char>> Certificate::encryptedP12Data(std::string password) const
{
	if (!this->data().has_value())
	{
		return std::nullopt;
	}

	BIO* certificateBuffer = BIO_new(BIO_s_mem());
	BIO* privateKeyBuffer = BIO_new(BIO_s_mem());

	BIO_write(certificateBuffer, this->data()->data(), (int)this->data()->size());

	if (this->privateKey().has_value())
	{
		BIO_write(privateKeyBuffer, this->privateKey()->data(), (int)this->privateKey()->size());
	}

	X509* certificate = nullptr;
	PEM_read_bio_X509(certificateBuffer, &certificate, 0, 0);

	EVP_PKEY* privateKey = nullptr;
	PEM_read_bio_PrivateKey(privateKeyBuffer, &privateKey, 0, 0);

	char emptyString[] = "";
	PKCS12* outputP12 = PKCS12_create((char *)password.c_str(), emptyString, privateKey, certificate, NULL, 0, 0, 0, 0, 0);

	BIO* p12Buffer = BIO_new(BIO_s_mem());
	i2d_PKCS12_bio(p12Buffer, outputP12);

	char* buffer = NULL;
	int size = (int)BIO_get_mem_data(p12Buffer, &buffer);

	std::vector<unsigned char> p12Data;
	p12Data.reserve(size);
	for (int i = 0; i < size; i++)
	{
		p12Data.push_back(buffer[i]);
	}

	BIO_free(p12Buffer);
	PKCS12_free(outputP12);

	EVP_PKEY_free(privateKey);
	X509_free(certificate);

	BIO_free(privateKeyBuffer);
	BIO_free(certificateBuffer);

	return p12Data;
}
