//
//  ProvisioningProfile.cpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/12/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include "ProvisioningProfile.hpp"
#include "Certificate.hpp"
#include "Error.hpp"

#include <winsock2.h>

#include <limits.h>
#include <stddef.h>

#include <time.h>

#include <sstream>

#if SIZE_MAX == UINT_MAX
typedef int ssize_t;        /* common 32 bit case */
#elif SIZE_MAX == ULONG_MAX
typedef long ssize_t;       /* linux 64 bits */
#elif SIZE_MAX == ULLONG_MAX
typedef long long ssize_t;  /* windows 64 bits */
#elif SIZE_MAX == USHRT_MAX
typedef short ssize_t;      /* is this even possible? */
#else
#error platform has exotic SIZE_MAX
#endif

#define ASN1_SEQUENCE 0x30
#define ASN1_CONTAINER 0xA0
#define ASN1_OBJECT_IDENTIFIER 0x06
#define ASN1_OCTET_STRING 0x04

extern std::vector<unsigned char> readFile(const char* filename);

#define odslog(msg) { std::stringstream ss; ss << msg << std::endl; OutputDebugStringA(ss.str().c_str()); }

#define SECONDS_FROM_1970_TO_APPLE_REFERENCE_DATE 978307200

ProvisioningProfile::ProvisioningProfile() : _entitlements(nullptr)
{
}

ProvisioningProfile::~ProvisioningProfile()
{
    if (this->_entitlements != nullptr)
    {
        plist_free(this->_entitlements);
        this->_entitlements = nullptr;
    }
}

ProvisioningProfile::ProvisioningProfile(const ProvisioningProfile& other) : _entitlements(nullptr)
{
    this->Copy(other);
}

ProvisioningProfile& ProvisioningProfile::operator=(const ProvisioningProfile& other)
{
    if (this == &other)
    {
        return *this;
    }

    ProvisioningProfile temp(other);
    this->Copy(temp);
    return *this;
}

void ProvisioningProfile::Copy(const ProvisioningProfile& other)
{
    this->_name = other._name;
    this->_identifier = other._identifier;
    this->_uuid = other._uuid;
    this->_bundleIdentifier = other._bundleIdentifier;
    this->_teamIdentifier = other._teamIdentifier;
    this->_creationDateSeconds = other._creationDateSeconds;
    this->_creationDateMicroseconds = other._creationDateMicroseconds;
    this->_expirationDateSeconds = other._expirationDateSeconds;
    this->_expirationDateMicroseconds = other._expirationDateMicroseconds;
    this->_isFreeProvisioningProfile = other._isFreeProvisioningProfile;
    this->_data = other._data;

    if (this->_entitlements != nullptr)
    {
        plist_free(this->_entitlements);
    }
    
    // plist_copy returns nullptr if given nullptr.
    this->_entitlements = plist_copy(other._entitlements);
}

ProvisioningProfile::ProvisioningProfile(plist_t plist) : _entitlements(nullptr)
{
    auto identifierNode = plist_dict_get_item(plist, "provisioningProfileId");
    auto dataNode = plist_dict_get_item(plist, "encodedProfile");

    if (identifierNode == nullptr || dataNode == nullptr)
    {
        throw APIError(APIErrorCode::InvalidResponse);
    }

    char *bytes = nullptr;
    uint64_t length = 0;
    plist_get_data_val(dataNode, &bytes, &length);

    std::vector<unsigned char> data;
	data.reserve(length);
    for (int i = 0; i < length; i++)
    {
        data.push_back(bytes[i]);
    }

    free(bytes);
    
    try
    {
        this->ParseData(data);
    }
    catch (std::exception& exception)
    {
        throw APIError(APIErrorCode::InvalidResponse);
    }
    
    char *identifier = nullptr;
    plist_get_string_val(identifierNode, &identifier);
    
    _identifier = identifier;
    _data = data;

    free(identifier);
}

ProvisioningProfile::ProvisioningProfile(std::string filepath) : _entitlements(nullptr) /* throws */
{
    auto data = readFile(filepath.c_str());
    this->ParseData(data);
}

ProvisioningProfile::ProvisioningProfile(std::vector<unsigned char>& data) : _entitlements(nullptr) /* throws */
{
    this->ParseData(data);
}

// Heavily inspired by libimobiledevice/ideviceprovision.c
// https://github.com/libimobiledevice/libimobiledevice/blob/ddba0b5efbcab483e80be10130c5c797f9ac8d08/tools/ideviceprovision.c#L98
void ProvisioningProfile::ParseData(std::vector<unsigned char> &encodedData)
{
    // Helper blocks
    auto itemSize = [](unsigned char *pointer) -> size_t
    {
        size_t size = -1;
        
        char bsize = *(pointer + 1);
        if (bsize & 0x80)
        {
            switch (bsize & 0xF)
            {
                case 2:
                {
                    uint16_t value = *(uint16_t *)(pointer + 2);
                    size = ntohs(value);
                    break;
                }
                    
                case 3:
                {
                    uint32_t value = *(uint32_t *)(pointer + 2);
                    size = ntohl(value) >> 8;
                    break;
                }
                    
                case 4:
                {
                    uint32_t value = *(uint32_t *)(pointer + 2);
                    size = ntohl(value);
                    break;
                }
                    
                default:
                    break;
            }
        }
        else
        {
            size = (size_t)bsize;
        }
        
        return size;
    };
    
    auto advanceToNextItem = [](unsigned char *pointer) -> unsigned char *
    {
        unsigned char *nextItem = pointer;
        
        char bsize = *(pointer + 1);
        if (bsize & 0x80)
        {
            nextItem += 2 + (bsize & 0xF);
        }
        else
        {
            nextItem += 3;
        }
        
        return nextItem;
    };
    
    auto skipNextItem = [&itemSize](unsigned char *pointer) -> unsigned char *
    {
        size_t size = itemSize(pointer);
        
        unsigned char *nextItem = pointer + 2 + size;
        return nextItem;
    };
    
    /* Start parsing */
    unsigned char *pointer = (unsigned char *)encodedData.data();
    if (!pointer || *pointer != ASN1_SEQUENCE)
    {
        throw SignError(SignErrorCode::InvalidProvisioningProfile);
    }
    
    pointer = advanceToNextItem(pointer);
    if (!pointer || *pointer != ASN1_OBJECT_IDENTIFIER)
    {
        throw SignError(SignErrorCode::InvalidProvisioningProfile);
    }
    
    pointer = skipNextItem(pointer);
    if (!pointer || *pointer != ASN1_CONTAINER)
    {
        throw SignError(SignErrorCode::InvalidProvisioningProfile);
    }
    
    pointer = advanceToNextItem(pointer);
    if (!pointer || *pointer != ASN1_SEQUENCE)
    {
        throw SignError(SignErrorCode::InvalidProvisioningProfile);
    }
    
    pointer = advanceToNextItem(pointer);
    
    // Skip 2 items.
    for (int i = 0; i < 2; i++)
    {
        pointer = skipNextItem(pointer);
    }
    
    if (!pointer || *pointer != ASN1_SEQUENCE)
    {
        throw SignError(SignErrorCode::InvalidProvisioningProfile);
    }
    
    pointer = advanceToNextItem(pointer);
    if (!pointer || *pointer != ASN1_OBJECT_IDENTIFIER)
    {
        throw SignError(SignErrorCode::InvalidProvisioningProfile);
    }
    
    pointer = skipNextItem(pointer);
    if (!pointer || *pointer != ASN1_CONTAINER)
    {
        throw SignError(SignErrorCode::InvalidProvisioningProfile);
    }
    
    pointer = advanceToNextItem(pointer);
    if (!pointer || *pointer != ASN1_OCTET_STRING)
    {
        throw SignError(SignErrorCode::InvalidProvisioningProfile);
    }
    
    size_t length = itemSize(pointer);
    pointer = advanceToNextItem(pointer);
    
    plist_t parsedPlist = nullptr;
    plist_from_memory((const char *)pointer, (unsigned int)length, &parsedPlist);
    
    if (parsedPlist == nullptr)
    {
        throw SignError(SignErrorCode::InvalidProvisioningProfile);
    }

    char* name = nullptr;
    char* uuid = nullptr;
    char* teamIdentifier = nullptr;
    char* rawApplicationIdentifier = nullptr;

    auto cleanUp = [&name, &uuid, &teamIdentifier, &rawApplicationIdentifier, &parsedPlist]() {
        plist_free(parsedPlist);

        if (name != nullptr)
        {
            free(name);
        }

        if (uuid != nullptr)
        {
            free(uuid);
        }

        if (teamIdentifier != nullptr)
        {
            free(teamIdentifier);
        }

        if (rawApplicationIdentifier != nullptr)
        {
            free(rawApplicationIdentifier);
        }
    };

    try
    {
        auto nameNode = plist_dict_get_item(parsedPlist, "Name");
        auto uuidNode = plist_dict_get_item(parsedPlist, "UUID");
        auto teamIdentifiersNode = plist_dict_get_item(parsedPlist, "TeamIdentifier");
        auto creationDateNode = plist_dict_get_item(parsedPlist, "CreationDate");
        auto expirationDateNode = plist_dict_get_item(parsedPlist, "ExpirationDate");
        auto entitlementsNode = plist_dict_get_item(parsedPlist, "Entitlements");

        if (nameNode == nullptr || uuidNode == nullptr || teamIdentifiersNode == nullptr || creationDateNode == nullptr || expirationDateNode == nullptr || entitlementsNode == nullptr)
        {
            throw SignError(SignErrorCode::InvalidProvisioningProfile);
        }

        auto teamIdentifierNode = plist_array_get_item(teamIdentifiersNode, 0);
        if (teamIdentifierNode == nullptr)
        {
            throw SignError(SignErrorCode::InvalidProvisioningProfile);
        }

        auto isFreeProvisioningProfileNode = plist_dict_get_item(parsedPlist, "LocalProvision");
        if (isFreeProvisioningProfileNode != nullptr)
        {
            uint8_t isFreeProvisioningProfile = 0;
            plist_get_bool_val(isFreeProvisioningProfileNode, &isFreeProvisioningProfile);

            _isFreeProvisioningProfile = (isFreeProvisioningProfile != 0);
        }
        else
        {
            _isFreeProvisioningProfile = 0;
        }

        plist_get_string_val(nameNode, &name);
        plist_get_string_val(uuidNode, &uuid);
        plist_get_string_val(teamIdentifierNode, &teamIdentifier);

        int32_t create_sec = 0;
        int32_t create_usec = 0;
        plist_get_date_val(creationDateNode, &create_sec, &create_usec);

        int32_t expiration_sec = 0;
        int32_t expiration_usec = 0;
        plist_get_date_val(expirationDateNode, &expiration_sec, &expiration_usec);

        plist_t bundleIdentifierNode = plist_dict_get_item(entitlementsNode, "application-identifier");
        if (bundleIdentifierNode == nullptr)
        {
            throw SignError(SignErrorCode::InvalidProvisioningProfile);
        }

        plist_get_string_val(bundleIdentifierNode, &rawApplicationIdentifier);
        std::string applicationIdentifier(rawApplicationIdentifier);

        size_t location = applicationIdentifier.find(".");
        if (location == std::string::npos)
        {
            throw SignError(SignErrorCode::InvalidProvisioningProfile);
        }

        std::string bundleIdentifier(applicationIdentifier.begin() + location + 1, applicationIdentifier.end());

        _name = name;
        _uuid = uuid;
        _teamIdentifier = teamIdentifier;
        _bundleIdentifier = bundleIdentifier;

        _creationDateSeconds = create_sec + SECONDS_FROM_1970_TO_APPLE_REFERENCE_DATE;
        _creationDateMicroseconds = create_usec;

        _expirationDateSeconds = expiration_sec + SECONDS_FROM_1970_TO_APPLE_REFERENCE_DATE;
        _expirationDateMicroseconds = expiration_usec;

        _entitlements = plist_copy(entitlementsNode);

        _data = encodedData;

        cleanUp();
    }
    catch (std::exception& e)
    {
        cleanUp();
        throw;
    }
}

#pragma mark - Getters -

std::string ProvisioningProfile::name() const
{
    return _name;
}

std::optional<std::string> ProvisioningProfile::identifier() const
{
    return _identifier;
}

std::string ProvisioningProfile::uuid() const
{
    return _uuid;
}

std::string ProvisioningProfile::bundleIdentifier() const
{
    return _bundleIdentifier;
}

std::string ProvisioningProfile::teamIdentifier() const
{
    return _teamIdentifier;
}

std::vector<unsigned char> ProvisioningProfile::data() const
{
    return _data;
}

timeval ProvisioningProfile::creationDate() const
{
	timeval creationDate = { this->_creationDateSeconds, this->_creationDateMicroseconds };
    return creationDate;
}

timeval ProvisioningProfile::expirationDate() const
{
	timeval expirationDate = { this->_expirationDateSeconds, this->_expirationDateMicroseconds };
	return expirationDate;
}

plist_t ProvisioningProfile::entitlements() const
{
	return _entitlements;
}

bool ProvisioningProfile::isFreeProvisioningProfile() const
{
	return _isFreeProvisioningProfile;
}