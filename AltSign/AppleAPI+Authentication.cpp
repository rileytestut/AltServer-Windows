//  Heavily based on sample code provided by Kabir Oberai (https://github.com/kabiroberai)

#include "AppleAPI.hpp"

#include "AnisetteData.h"

// Core Crypto
extern "C" {
#include <corecrypto/ccsrp.h>
#include <corecrypto/ccdrbg.h>
#include <corecrypto/ccsrp_gp.h>
#include <corecrypto/ccdigest.h>
#include <corecrypto/ccsha2.h>
#include <corecrypto/ccpbkdf2.h>
#include <corecrypto/cchmac.h>
#include <corecrypto/ccaes.h>
#include <corecrypto/ccpad.h>
}

#include <ostream>

using namespace std;
using namespace utility;                    // Common utilities like string conversions
using namespace web;                        // Common features like URIs.
using namespace web::http;                  // Common HTTP functionality
using namespace web::http::client;          // HTTP client features
using namespace concurrency::streams;       // Asynchronous streams

extern std::string make_uuid();

extern std::string StringFromWideString(std::wstring wideString);
extern std::wstring WideStringFromString(std::string string);

extern bool decompress(const uint8_t* input, size_t input_size, std::vector<uint8_t>& output);

#define odslog(msg) { std::stringstream ss; ss << msg << std::endl; OutputDebugStringA(ss.str().c_str()); }

//struct ccrng_state* ccDRBGGetRngState(void);

static const char ALTHexCharacters[] = "0123456789abcdef";

struct ccrng_state* RNG = NULL;

std::vector<unsigned char> DataFromBytes(const char* bytes, size_t count)
{
	std::vector<unsigned char> data;
	data.reserve(count);
	for (int i = 0; i < count; i++)
	{
		data.push_back(bytes[i]);
	}

	return data;
}

void ALTDigestUpdateString(const struct ccdigest_info* di_info, struct ccdigest_ctx* di_ctx, std::string string)
{
	ccdigest_update(di_info, di_ctx, string.length(), string.c_str());
}

void ALTDigestUpdateData(const struct ccdigest_info* di_info, struct ccdigest_ctx* di_ctx, std::vector<unsigned char>& data)
{
	uint32_t data_len = (uint32_t)data.size(); // 4 bytes for length
	ccdigest_update(di_info, di_ctx, sizeof(data_len), &data_len);
	ccdigest_update(di_info, di_ctx, data_len, data.data());
}

std::optional<std::vector<unsigned char>> ALTPBKDF2SRP(const struct ccdigest_info* di_info, bool isS2k, std::string password, std::vector<unsigned char>& salt, int iterations)
{
	const struct ccdigest_info* password_di_info = ccsha256_di();
	char* digest_raw = (char*)malloc(password_di_info->output_size);
	const char* passwordUTF8 = password.c_str();
	ccdigest(password_di_info, strlen(passwordUTF8), passwordUTF8, digest_raw);

	size_t final_digest_len = password_di_info->output_size * (isS2k ? 1 : 2);
	char* digest = (char*)malloc(final_digest_len);

	if (isS2k)
	{
		memcpy(digest, digest_raw, final_digest_len);
	}
	else
	{
		for (size_t i = 0; i < password_di_info->output_size; i++)
		{
			char byte = digest_raw[i];
			digest[i * 2 + 0] = ALTHexCharacters[(byte >> 4) & 0x0F];
			digest[i * 2 + 1] = ALTHexCharacters[(byte >> 0) & 0x0F];
		}
	}

	char *outputBytes = (char*)malloc(di_info->output_size);

	if (ccpbkdf2_hmac(di_info, final_digest_len, digest, salt.size(), salt.data(), iterations, di_info->output_size, outputBytes) != 0)
	{
		return std::nullopt;
	}

	auto data = DataFromBytes(outputBytes, di_info->output_size);
	return std::make_optional(data);
}

std::vector<unsigned char> ALTCreateSessionKey(ccsrp_ctx_t srp_ctx, const char* key_name)
{
	size_t key_len;
	const void* session_key = ccsrp_get_session_key(srp_ctx, &key_len);

	const struct ccdigest_info* di_info = ccsha256_di();

	size_t length = strlen(key_name);

	size_t hmac_len = di_info->output_size;
	unsigned char* hmac_bytes = (unsigned char*)malloc(hmac_len);
	cchmac(di_info, key_len, session_key, length, key_name, hmac_bytes);

	auto data = DataFromBytes((const char *)hmac_bytes, hmac_len);
	return data;
}

std::optional<std::vector<unsigned char>> ALTDecryptDataCBC(ccsrp_ctx_t srp_ctx, std::vector<unsigned char>& spd)
{
	auto extraDataKey = ALTCreateSessionKey(srp_ctx, "extra data key:");
	auto extraDataIV = ALTCreateSessionKey(srp_ctx, "extra data iv:");

	char* decryptedBytes = (char*)malloc(spd.size());

	const struct ccmode_cbc* decrypt_mode = ccaes_cbc_decrypt_mode();

	cccbc_iv* iv = (cccbc_iv*)malloc(decrypt_mode->block_size);
	if (extraDataIV.data())
	{
		memcpy(iv, extraDataIV.data(), decrypt_mode->block_size);
	}
	else
	{
		memset(iv, 0, decrypt_mode->block_size);
	}

	cccbc_ctx* ctx_buf = (cccbc_ctx*)malloc(decrypt_mode->size);
	decrypt_mode->init(decrypt_mode, ctx_buf, extraDataKey.size(), extraDataKey.data());

	size_t length = ccpad_pkcs7_decrypt(decrypt_mode, ctx_buf, iv, spd.size(), spd.data(), decryptedBytes);
	if (length > spd.size())
	{
		return std::nullopt;
	}

	auto decryptedData = DataFromBytes((const char*)decryptedBytes, length);
	return decryptedData;
}

std::optional<std::vector<unsigned char>> ALTDecryptDataGCM(std::vector<unsigned char>& sk, std::vector<unsigned char>& encryptedData)
{
	const struct ccmode_gcm* decrypt_mode = ccaes_gcm_decrypt_mode();

	ccgcm_ctx* gcm_ctx = (ccgcm_ctx*)malloc(decrypt_mode->size);
	decrypt_mode->init(decrypt_mode, gcm_ctx, sk.size(), sk.data());

	if (encryptedData.size() < 35)
	{
		odslog("ERROR: Encrypted token too short.");
		return std::nullopt;
	}

	if (cc_cmp_safe(3, encryptedData.data(), "XYZ"))
	{
		odslog("ERROR: Encrypted token wrong version!");
		return std::nullopt;
	}

	decrypt_mode->set_iv(gcm_ctx, 16, encryptedData.data() + 3);
	decrypt_mode->gmac(gcm_ctx, 3, encryptedData.data());

	size_t decrypted_len = encryptedData.size() - 35;

	char* decryptedBytes = (char*)malloc(decrypted_len);

	decrypt_mode->gcm(gcm_ctx, decrypted_len, encryptedData.data() + 16 + 3, decryptedBytes);

	char tag[16];
	decrypt_mode->finalize(gcm_ctx, 16, tag);

	if (cc_cmp_safe(16, encryptedData.data() + decrypted_len + 19, tag))
	{
		odslog("ERROR: Invalid tag version.");
		return std::nullopt;
	}

	auto decryptedData = DataFromBytes((const char*)decryptedBytes, decrypted_len);
	return decryptedData;
}

std::vector<unsigned char> ALTCreateAppTokensChecksum(std::vector<unsigned char>& sk, std::string adsid, std::vector<std::string> apps)
{
	const struct ccdigest_info* di_info = ccsha256_di();
	size_t hmac_size = cchmac_di_size(di_info);
	struct cchmac_ctx* hmac_ctx = (struct cchmac_ctx*)malloc(hmac_size);
	cchmac_init(di_info, hmac_ctx, sk.size(), sk.data());

	const char* key = "apptokens";
	cchmac_update(di_info, hmac_ctx, strlen(key), key);

	const char* adsidUTF8 = adsid.c_str();
	cchmac_update(di_info, hmac_ctx, strlen(adsidUTF8), adsidUTF8);

	for (auto app : apps)
	{
		cchmac_update(di_info, hmac_ctx, app.size(), app.c_str());
	}

	char* checksumBytes = (char*)malloc(di_info->output_size);
	cchmac_final(di_info, hmac_ctx, (unsigned char *)checksumBytes);

	auto checksum = DataFromBytes(checksumBytes, di_info->output_size);
	return checksum;
}

pplx::task<std::pair<std::shared_ptr<Account>, std::shared_ptr<AppleAPISession>>> AppleAPI::Authenticate(
	std::string unsanitizedAppleID,
	std::string password,
	std::shared_ptr<AnisetteData> anisetteData,
	std::optional<std::function <pplx::task<std::optional<std::string>>(void)>> verificationHandler)
{
	if (RNG == nullptr)
	{
		RNG = ccrng(NULL);
	}

	// Authenticating only works with lowercase email address, even if Apple ID contains capital letters.
	auto sanitizedAppleID = unsanitizedAppleID;
	std::transform(sanitizedAppleID.begin(), sanitizedAppleID.end(), sanitizedAppleID.begin(), [](unsigned char c) { 
		return std::tolower(c); 
	});

	auto adsidValue = std::make_shared<std::string>("");
	auto sessionValue = std::make_shared<AppleAPISession>();

	time_t time;
	struct tm* tm;
	char dateString[64];

	time = anisetteData->date().tv_sec;
	tm = localtime(&time);

	strftime(dateString, sizeof dateString, "%FT%T%z", tm);

	std::map<std::string, plist_t> clientDictionary = {
		{ "bootstrap", plist_new_bool(true) },
		{ "icscrec", plist_new_bool(true) },
		{ "loc", plist_new_string(anisetteData->locale().c_str()) },
		{ "pbe", plist_new_bool(false) },
		{ "prkgen", plist_new_bool(true) },
		{ "svct", plist_new_string("iCloud") },
		{ "X-Apple-I-Client-Time", plist_new_string(dateString) },
		{ "X-Apple-Locale", plist_new_string(anisetteData->locale().c_str()) },
		{ "X-Apple-I-TimeZone", plist_new_string(anisetteData->timeZone().c_str()) },
		{ "X-Apple-I-MD", plist_new_string(anisetteData->oneTimePassword().c_str()) },
		{ "X-Apple-I-MD-LU", plist_new_string(anisetteData->localUserID().c_str()) },
		{ "X-Apple-I-MD-M", plist_new_string(anisetteData->machineID().c_str()) },
		{ "X-Apple-I-MD-RINFO", plist_new_uint(anisetteData->routingInfo()) },
		{ "X-Mme-Device-Id", plist_new_string(anisetteData->deviceUniqueIdentifier().c_str()) },
		{ "X-Apple-I-SRL-NO", plist_new_string(anisetteData->deviceSerialNumber().c_str()) }
	};

	/* Begin CoreCrypto Logic */
	ccsrp_const_gp_t gp = ccsrp_gp_rfc5054_2048();

	const struct ccdigest_info* di_info = ccsha256_di();
	struct ccdigest_ctx* di_ctx = (struct ccdigest_ctx*)malloc(ccdigest_di_size(di_info));
	ccdigest_init(di_info, di_ctx);

	const struct ccdigest_info* srp_di = ccsha256_di();
	ccsrp_ctx_t srp_ctx = (ccsrp_ctx_t)malloc(ccsrp_sizeof_srp(di_info, gp));
	ccsrp_ctx_init(srp_ctx, srp_di, gp);

	HDR(srp_ctx)->blinding_rng = ccrng(NULL);
	HDR(srp_ctx)->flags.noUsernameInX = true;

	std::vector<std::string> ps = { "s2k", "s2k_fo" };
	ALTDigestUpdateString(di_info, di_ctx, ps[0]);
	ALTDigestUpdateString(di_info, di_ctx, ",");
	ALTDigestUpdateString(di_info, di_ctx, ps[1]);

	size_t A_size = ccsrp_exchange_size(srp_ctx);
	char* A_bytes = (char*)malloc(A_size);
	ccsrp_client_start_authentication(srp_ctx, ccrng(NULL), A_bytes);

	auto A_data = DataFromBytes(A_bytes, A_size);

	ALTDigestUpdateString(di_info, di_ctx, "|");

	auto psPlist = plist_new_array();
	for (auto value : ps)
	{
		plist_array_append_item(psPlist, plist_new_string(value.c_str()));
	}	

	auto cpd = plist_new_dict();
	for (auto pair : clientDictionary)
	{
		plist_dict_set_item(cpd, pair.first.c_str(), pair.second);
	}

	std::map<std::string, plist_t> parameters = {
		{ "A2k", plist_new_data((const char *)A_bytes, A_size) },
		{ "ps", psPlist },
		{ "cpd", cpd },
		{ "u", plist_new_string(sanitizedAppleID.c_str()) },
		{ "o", plist_new_string("init") }
	};

	auto task = this->SendAuthenticationRequest(parameters, anisetteData)
		.then([=](plist_t plist) {

		size_t M_size = ccsrp_get_session_key_length(srp_ctx);
		char* M_bytes = (char*)malloc(M_size);

		auto spNode = plist_dict_get_item(plist, "sp");
		if (spNode == nullptr)
		{
			throw APIError(APIErrorCode::InvalidResponse);
		}

		char* sp = nullptr;
		plist_get_string_val(spNode, &sp);

		bool isS2K = (std::string(sp) == "s2k");

		ALTDigestUpdateString(di_info, di_ctx, "|");

		if (sp)
		{
			ALTDigestUpdateString(di_info, di_ctx, sp);
		}

		auto cNode = plist_dict_get_item(plist, "c");
		auto saltNode = plist_dict_get_item(plist, "s");
		auto iterationsNode = plist_dict_get_item(plist, "i");
		auto bNode = plist_dict_get_item(plist, "B");

		if (cNode == nullptr || saltNode == nullptr || iterationsNode == nullptr || bNode == nullptr)
		{
			throw APIError(APIErrorCode::InvalidResponse);
		}

		char* c = nullptr;
		plist_get_string_val(cNode, &c);

		char* saltBytes = nullptr;
		uint64_t saltSize = 0;
		plist_get_data_val(saltNode, &saltBytes, &saltSize);

		uint64_t iterations = 0;
		plist_get_uint_val(iterationsNode, &iterations);

		char* B_bytes = nullptr;
		uint64_t B_size = 0;
		plist_get_data_val(bNode, &B_bytes, &B_size);

		auto salt = DataFromBytes((const char*)saltBytes, saltSize);
		auto B_data = DataFromBytes((const char*)B_bytes, B_size);

		auto passwordKey = ALTPBKDF2SRP(di_info, isS2K, password, salt, iterations);
		if (passwordKey == ::nullopt)
		{
			throw APIError(APIErrorCode::AuthenticationHandshakeFailed);
		}

		int result = ccsrp_client_process_challenge(srp_ctx, sanitizedAppleID.c_str(), passwordKey->size(), passwordKey->data(),
			salt.size(), salt.data(), B_data.data(), M_bytes);
		if (result != 0)
		{
			throw APIError(APIErrorCode::AuthenticationHandshakeFailed);
		}

		time_t time;
		struct tm* tm;
		char dateString[64];

		time = anisetteData->date().tv_sec;
		tm = localtime(&time);

		strftime(dateString, sizeof dateString, "%FT%T%z", tm);

		std::map<std::string, plist_t> clientDictionary = {
		{ "bootstrap", plist_new_bool(true) },
		{ "icscrec", plist_new_bool(true) },
		{ "loc", plist_new_string(anisetteData->locale().c_str()) },
		{ "pbe", plist_new_bool(false) },
		{ "prkgen", plist_new_bool(true) },
		{ "svct", plist_new_string("iCloud") },
		{ "X-Apple-I-Client-Time", plist_new_string(dateString) },
		{ "X-Apple-Locale", plist_new_string(anisetteData->locale().c_str()) },
		{ "X-Apple-I-TimeZone", plist_new_string(anisetteData->timeZone().c_str()) },
		{ "X-Apple-I-MD", plist_new_string(anisetteData->oneTimePassword().c_str()) },
		{ "X-Apple-I-MD-LU", plist_new_string(anisetteData->localUserID().c_str()) },
		{ "X-Apple-I-MD-M", plist_new_string(anisetteData->machineID().c_str()) },
		{ "X-Apple-I-MD-RINFO", plist_new_uint(anisetteData->routingInfo()) },
		{ "X-Mme-Device-Id", plist_new_string(anisetteData->deviceUniqueIdentifier().c_str()) },
		{ "X-Apple-I-SRL-NO", plist_new_string(anisetteData->deviceSerialNumber().c_str()) }
		};

		auto cpd = plist_new_dict();
		for (auto pair : clientDictionary)
		{
			plist_dict_set_item(cpd, pair.first.c_str(), pair.second);
		}

		std::map<std::string, plist_t> parameters = {
			{ "c", plist_new_string(c) },
			{ "M1", plist_new_data((const char*)M_bytes, M_size) },
			{ "cpd", cpd },
			{ "u", plist_new_string(sanitizedAppleID.c_str()) },
			{ "o", plist_new_string("complete") }
		};

		return this->SendAuthenticationRequest(parameters, anisetteData);
			}).then([=](plist_t plist) {

				auto M2_node = plist_dict_get_item(plist, "M2");
				if (M2_node == nullptr)
				{
					odslog("ERROR: M2 data not found!");
					throw APIError(APIErrorCode::InvalidResponse);
				}

				char* M2_bytes = nullptr;
				uint64_t M2_size = 0;
				plist_get_data_val(M2_node, &M2_bytes, &M2_size);

				if (!ccsrp_client_verify_session(srp_ctx, (const uint8_t*)M2_bytes))
				{
					odslog("ERROR: Failed to verify session.");
					throw APIError(APIErrorCode::AuthenticationHandshakeFailed);
				}

				ALTDigestUpdateString(di_info, di_ctx, "|");

				std::vector<unsigned char> spd;
				auto spdNode = plist_dict_get_item(plist, "spd");
				if (spdNode != nullptr)
				{
					char* spdBytes = nullptr;
					uint64_t spdSize = 0;
					plist_get_data_val(spdNode, &spdBytes, &spdSize);

					spd = DataFromBytes(spdBytes, spdSize);
					ALTDigestUpdateData(di_info, di_ctx, spd);
				}

				ALTDigestUpdateString(di_info, di_ctx, "|");

				auto scNode = plist_dict_get_item(plist, "sc");
				if (scNode != nullptr)
				{
					char* scBytes = nullptr;
					uint64_t scSize = 0;
					plist_get_data_val(scNode, &scBytes, &scSize);

					auto sc = DataFromBytes(scBytes, scSize);
					ALTDigestUpdateData(di_info, di_ctx, sc);
				}

				ALTDigestUpdateString(di_info, di_ctx, "|");

				auto npNode = plist_dict_get_item(plist, "np");
				if (npNode == nullptr)
				{
					odslog("ERROR: Missing np dictionary.");
					throw APIError(APIErrorCode::InvalidResponse);
				}

				char* npBytes = nullptr;
				uint64_t npSize = 0;
				plist_get_data_val(npNode, &npBytes, &npSize);

				auto np = DataFromBytes(npBytes, npSize);
				ALTDigestUpdateData(di_info, di_ctx, np);

				size_t digest_len = di_info->output_size;
				if (np.size() != digest_len)
				{
					odslog("ERROR: Neg proto hash is too short.");
					throw APIError(APIErrorCode::AuthenticationHandshakeFailed);
				}

				unsigned char* digest = (unsigned char*)malloc(digest_len);
				di_info->final(di_info, di_ctx, digest);

				auto hmacKey = ALTCreateSessionKey(srp_ctx, "HMAC key:");
				unsigned char* hmac_out = (unsigned char*)malloc(digest_len);
				cchmac(di_info, hmacKey.size(), hmacKey.data(), digest_len, digest, hmac_out);

				odslog("HMAC_OUT:");
				for (int i = 0; i < digest_len; i++)
				{
					char str[8];
					char byte = ((char*)hmac_out)[i];
					_itoa(byte, str, 10);

					odslog("Byte:" << str);
				}

				odslog("NP:");
				for (int i = 0; i < digest_len; i++)
				{
					char str[8];
					char byte = ((char*)npBytes)[i];
					_itoa(byte, str, 10);

					odslog("Byte:" << str);
				}

				/*
				if (cc_cmp_safe(digest_len, hmac_out, np.data()))
				{
					odslog("ERROR: Invalid neg prot hmac.");
					throw APIError(APIErrorCode::AuthenticationHandshakeFailed);
				}*/

				auto decryptedData = ALTDecryptDataCBC(srp_ctx, spd);
				if (decryptedData == ::nullopt)
				{
					odslog("ERROR: Could not decrypt login response.");
					throw APIError(APIErrorCode::AuthenticationHandshakeFailed);
				}

				odslog("Data: " << decryptedData->data());

				plist_t decryptedPlist = nullptr;
				plist_from_xml((const char *)decryptedData->data(), (int)decryptedData->size(), &decryptedPlist);

				if (decryptedPlist == nullptr)
				{
					odslog("ERROR: Could not parse decrypted login response plist!");
					throw APIError(APIErrorCode::InvalidResponse);
				}

				auto adsidNode = plist_dict_get_item(decryptedPlist, "adsid");
				auto idmsTokenNode = plist_dict_get_item(decryptedPlist, "GsIdmsToken");

				if (adsidNode == nullptr || idmsTokenNode == nullptr)
				{
					odslog("ERROR: adsid and /or idmsToken is nil.");
					throw APIError(APIErrorCode::InvalidResponse);
				}

				char* adsid = nullptr;
				plist_get_string_val(adsidNode, &adsid);

				char* idmsToken = nullptr;
				plist_get_string_val(idmsTokenNode, &idmsToken);

				auto statusDictionary = plist_dict_get_item(plist, "Status");
				if (statusDictionary == nullptr)
				{
					throw APIError(APIErrorCode::InvalidResponse);
				}

				std::optional<std::string> authType = std::nullopt;

				auto authTypeNode = plist_dict_get_item(statusDictionary, "au");
				if (authTypeNode != nullptr)
				{
					char* rawAuthType = nullptr;
					plist_get_string_val(authTypeNode, &rawAuthType);

					authType = rawAuthType;
				}

				if (authType == "trustedDeviceSecondaryAuth")
				{
					odslog("Requires trusted device two factor...");

					if (verificationHandler.has_value())
					{
						return this->RequestTrustedDeviceTwoFactorCode(adsid, idmsToken, anisetteData, *verificationHandler)
						.then([=](bool success) {
							return this->Authenticate(unsanitizedAppleID, password, anisetteData, std::nullopt);
						});
					}
					else
					{
						throw APIError(APIErrorCode::RequiresTwoFactorAuthentication);
					}					
				}
				else if (authType == "secondaryAuth")
				{
					odslog("Requires SMS two factor...");

					if (verificationHandler.has_value())
					{
						return this->RequestSMSTwoFactorCode(adsid, idmsToken, anisetteData, *verificationHandler)
							.then([=](bool success) {
							return this->Authenticate(unsanitizedAppleID, password, anisetteData, std::nullopt);
						});
					}
					else
					{
						throw APIError(APIErrorCode::RequiresTwoFactorAuthentication);
					}
				}
				else
				{
					auto skNode = plist_dict_get_item(decryptedPlist, "sk");
					auto cNode = plist_dict_get_item(decryptedPlist, "c");

					if (skNode == nullptr || cNode == nullptr)
					{
						odslog("ERROR: No ak and /or c data.");
						throw APIError(APIErrorCode::InvalidResponse);
					}

					char* skBytes = nullptr;
					uint64_t skSize = 0;
					plist_get_data_val(skNode, &skBytes, &skSize);

					char* cBytes = nullptr;
					uint64_t cSize = 0;
					plist_get_data_val(cNode, &cBytes, &cSize);

					auto sk = DataFromBytes((const char*)skBytes, skSize);

					auto appsNode = plist_new_array();
					plist_array_append_item(appsNode, plist_new_string("com.apple.gs.xcode.auth"));

					auto checksum = ALTCreateAppTokensChecksum(sk, adsid, { "com.apple.gs.xcode.auth" });

					time_t time;
					struct tm* tm;
					char dateString[64];

					time = anisetteData->date().tv_sec;
					tm = localtime(&time);

					strftime(dateString, sizeof dateString, "%FT%T%z", tm);

					std::map<std::string, plist_t> clientDictionary = {
					{ "bootstrap", plist_new_bool(true) },
					{ "icscrec", plist_new_bool(true) },
					{ "loc", plist_new_string(anisetteData->locale().c_str()) },
					{ "pbe", plist_new_bool(false) },
					{ "prkgen", plist_new_bool(true) },
					{ "svct", plist_new_string("iCloud") },
					{ "X-Apple-I-Client-Time", plist_new_string(dateString) },
					{ "X-Apple-Locale", plist_new_string(anisetteData->locale().c_str()) },
					{ "X-Apple-I-TimeZone", plist_new_string(anisetteData->timeZone().c_str()) },
					{ "X-Apple-I-MD", plist_new_string(anisetteData->oneTimePassword().c_str()) },
					{ "X-Apple-I-MD-LU", plist_new_string(anisetteData->localUserID().c_str()) },
					{ "X-Apple-I-MD-M", plist_new_string(anisetteData->machineID().c_str()) },
					{ "X-Apple-I-MD-RINFO", plist_new_uint(anisetteData->routingInfo()) },
					{ "X-Mme-Device-Id", plist_new_string(anisetteData->deviceUniqueIdentifier().c_str()) },
					{ "X-Apple-I-SRL-NO", plist_new_string(anisetteData->deviceSerialNumber().c_str()) }
					};

					auto cpd = plist_new_dict();
					for (auto pair : clientDictionary)
					{
						plist_dict_set_item(cpd, pair.first.c_str(), pair.second);
					}

					std::map<std::string, plist_t> parameters = {
						{ "u", plist_new_string(adsid) },
						{ "app", appsNode },
						{ "c", plist_new_data((const char *)cBytes, cSize) },
						{ "t", plist_new_string(idmsToken) },
						{ "checksum", plist_new_data((const char*)checksum.data(), checksum.size()) },
						{ "cpd", cpd },
						{ "o", plist_new_string("apptokens") }
					};

					*adsidValue = std::string(adsid);
					return this->FetchAuthToken(parameters, sk, anisetteData)
					.then([=](std::string token) {
						auto session = std::make_shared<AppleAPISession>(*adsidValue, token, anisetteData);
						*sessionValue = *session;

						return this->FetchAccount(session);
					})
					.then([=](std::shared_ptr<Account> account) -> std::pair<std::shared_ptr<Account>, std::shared_ptr<AppleAPISession>> {
						return std::make_pair(account, sessionValue);
					});
				}
			});

	return task;
}

pplx::task<std::string> AppleAPI::FetchAuthToken(std::map<std::string, plist_t> requestParameters, std::vector<unsigned char> sk, std::shared_ptr<AnisetteData> anisetteData)
{
	auto apps = requestParameters["app"];
	auto appNode = plist_array_get_item(apps, 0);

	char* appName = nullptr;
	plist_get_string_val(appNode, &appName);

	std::string app(appName);

	return this->SendAuthenticationRequest(requestParameters, anisetteData)
	.then([=](plist_t plist) {

		auto encryptedTokenNode = plist_dict_get_item(plist, "et");
		if (encryptedTokenNode == nullptr)
		{
			throw APIError(APIErrorCode::InvalidResponse);
		}

		char* encryptedTokenBytes = nullptr;
		uint64_t encryptedTokenSize = 0;
		plist_get_data_val(encryptedTokenNode, &encryptedTokenBytes, &encryptedTokenSize);

		auto sk_copy = sk;

		auto encryptedToken = DataFromBytes(encryptedTokenBytes, encryptedTokenSize);
		auto decryptedToken = ALTDecryptDataGCM(sk_copy, encryptedToken);

		if (decryptedToken == ::nullopt)
		{
			odslog("ERROR: Failed to decrypt apptoken.");
			throw APIError(APIErrorCode::InvalidResponse);
		}

		plist_t decryptedTokenPlist = nullptr;
		plist_from_xml((const char *)decryptedToken->data(), decryptedToken->size(), &decryptedTokenPlist);

		if (decryptedTokenPlist == nullptr)
		{
			odslog("ERROR: Could not parse decrypted apptoken plist.");
			throw APIError(APIErrorCode::InvalidResponse);
		}

		auto tokensNode = plist_dict_get_item(decryptedTokenPlist, "t");
		if (tokensNode == nullptr)
		{
			throw APIError(APIErrorCode::InvalidResponse);
		}

		auto tokenDictionary = plist_dict_get_item(tokensNode, app.c_str());
		if (tokenDictionary == nullptr)
		{
			throw APIError(APIErrorCode::InvalidResponse);
		}

		auto tokenNode = plist_dict_get_item(tokenDictionary, "token");
		if (tokenNode == nullptr)
		{
			throw APIError(APIErrorCode::InvalidResponse);
		}

		char* token = nullptr;
		plist_get_string_val(tokenNode, &token);

		odslog("Got token for " << app << "!\nValue : " << token);

		return std::string(token);
	});
}

pplx::task<bool> AppleAPI::RequestTrustedDeviceTwoFactorCode(
	std::string dsid,
	std::string idmsToken,
	std::shared_ptr<AnisetteData> anisetteData,
	const std::function <pplx::task<std::optional<std::string>>(void)>& verificationHandler)
{
	std::string requestURL = "/auth/verify/trusteddevice";
	std::string verifyURL = "/grandslam/GsService2/validate";

	auto request = this->MakeTwoFactorCodeRequest(requestURL, dsid, idmsToken, anisetteData);

	auto task = this->gsaClient().request(request)
		.then([=](http_response response)
			{
				return response.content_ready();
			})
		.then([=](http_response response)
			{
				odslog("Received 2FA response status code: " << response.status_code());
				return response.extract_vector();
			})
				.then([=](std::vector<unsigned char> decompressedData)
					{
						return verificationHandler();
					})
				.then([=](std::optional<std::string> verificationCode) {
						if (!verificationCode.has_value())
						{
							throw APIError(APIErrorCode::RequiresTwoFactorAuthentication);
						}

						// Send verification code request.
						auto request = this->MakeTwoFactorCodeRequest(verifyURL, dsid, idmsToken, anisetteData);
						request.headers().add(L"security-code", WideStringFromString(*verificationCode));

						return this->gsaClient().request(request);
					})
				.then([=](http_response response)
					{
						return response.content_ready();
					})
				.then([=](http_response response)
					{
						odslog("Received 2FA response status code: " << response.status_code());
						return response.extract_vector();
					})
				.then([=](std::vector<unsigned char> compressedData)
					{
						std::vector<uint8_t> decompressedData;

						if (compressedData.size() > 2 && compressedData[0] == '<' && compressedData[1] == '?')
						{
							// Already decompressed
							decompressedData = compressedData;
						}
						else
						{
							decompress((const uint8_t*)compressedData.data(), (size_t)compressedData.size(), decompressedData);
						}

						std::string decompressedXML = std::string(decompressedData.begin(), decompressedData.end());

						plist_t plist = nullptr;
						plist_from_xml(decompressedXML.c_str(), (int)decompressedXML.size(), &plist);

						if (plist == nullptr)
						{
							throw APIError(APIErrorCode::InvalidResponse);
						}

						return plist;
					})
				.then([this](plist_t plist)
					{
						// Handle verification code response.
						return this->ProcessTwoFactorResponse<bool>(plist, [](auto plist) {
							auto node = plist_dict_get_item(plist, "ec");
							if (node)
							{
								uint64_t errorCode = 0;
								plist_get_uint_val(node, &errorCode);

								if (errorCode != 0)
								{
									throw APIError(APIErrorCode::InvalidResponse);
								}
							}

							return true;
						}, [=](auto resultCode) -> optional<APIError>
						{
							switch (resultCode)
							{
							case -21669:
								return std::make_optional<APIError>(APIErrorCode::IncorrectVerificationCode);

							default:
								return std::nullopt;
							}
						});
					});

			return task;
}

pplx::task<bool> AppleAPI::RequestSMSTwoFactorCode(
	std::string dsid,
	std::string idmsToken,
	std::shared_ptr<AnisetteData> anisetteData,
	const std::function <pplx::task<std::optional<std::string>>(void)>& verificationHandler)
{
	auto requestURL = "/auth/verify/phone/put?mode=sms";
	auto verifyURL = "/auth/verify/phone/securitycode?referrer=/auth/verify/phone/put";

	auto request = this->MakeTwoFactorCodeRequest(requestURL, dsid, idmsToken, anisetteData);
	request.set_method(web::http::methods::POST);

	auto phoneNumberNode = plist_new_string("1");

	auto serverInfoNode = plist_new_dict();
	plist_dict_set_item(serverInfoNode, "phoneNumber.id", phoneNumberNode);

	auto bodyPlist = plist_new_dict();
	plist_dict_set_item(bodyPlist, "serverInfo", serverInfoNode);

	char* bodyXML = NULL;
	uint32_t length = 0;
	plist_to_xml(bodyPlist, &bodyXML, &length);

	request.set_body(bodyXML);

	free(bodyXML);
	plist_free(bodyPlist);

	auto task = this->gsaClient().request(request)
		.then([=](http_response response)
			{
				return response.content_ready();
			})
		.then([=](http_response response)
			{
				odslog("Received 2FA response status code: " << response.status_code());
				return response.extract_vector();
			})
		.then([=](std::vector<unsigned char> decompressedData)
			{
				return verificationHandler();
			})
		.then([=](std::optional<std::string> verificationCode) 
			{
				if (!verificationCode.has_value())
				{
					throw APIError(APIErrorCode::RequiresTwoFactorAuthentication);
				}

				auto request = this->MakeTwoFactorCodeRequest(verifyURL, dsid, idmsToken, anisetteData);
				request.set_method(web::http::methods::POST);

				auto securityCodeNode = plist_new_string(verificationCode->c_str());
				auto modeNode = plist_new_string("sms");
				auto phoneNumberNode = plist_new_string("1");

				auto serverInfoNode = plist_new_dict();
				plist_dict_set_item(serverInfoNode, "mode", modeNode);
				plist_dict_set_item(serverInfoNode, "phoneNumber.id", phoneNumberNode);

				auto bodyPlist = plist_new_dict();
				plist_dict_set_item(bodyPlist, "securityCode.code", securityCodeNode);
				plist_dict_set_item(bodyPlist, "serverInfo", serverInfoNode);

				char* bodyXML = NULL;
				uint32_t length = 0;
				plist_to_xml(bodyPlist, &bodyXML, &length);

				request.set_body(bodyXML);

				free(bodyXML);
				plist_free(bodyPlist);

				return this->gsaClient().request(request);
			})
		.then([=](http_response response)
			{
				return response.content_ready();
			})
		.then([=](http_response response)
			{
				odslog("Received verify 2FA response status code: " << response.status_code());

				if (response.status_code() != 200 || !response.headers().has(L"X-Apple-PE-Token"))
				{
					// PE token is included in headers if we sent correct verification code.
					throw APIError(APIErrorCode::IncorrectVerificationCode);
				}

				return true;
			});

	return task;
}

pplx::task<std::shared_ptr<Account>> AppleAPI::FetchAccount(std::shared_ptr<AppleAPISession> session)
{
	std::map<std::string, std::string> parameters = {};
	auto task = this->SendRequest("viewDeveloper.action", parameters, session, nullptr)
		.then([=](plist_t plist)->std::shared_ptr<Account>
		{	
			auto account = this->ProcessResponse<shared_ptr<Account>>(plist, [](auto plist)
				{
					auto node = plist_dict_get_item(plist, "developer");
					if (node == nullptr)
					{
						throw APIError(APIErrorCode::InvalidResponse);
					}

					auto account = make_shared<Account>(node);
					return account;

				}, [=](auto resultCode) -> optional<APIError>
				{
					return nullopt;
				});

			return account;
		});

	return task;
}

pplx::task<plist_t> AppleAPI::SendAuthenticationRequest(std::map<std::string, plist_t> requestParameters,
	std::shared_ptr<AnisetteData> anisetteData)
{
	auto header = plist_new_dict();
	plist_dict_set_item(header, "Version", plist_new_string("1.0.1"));

	auto requestDictionary = plist_new_dict();
	for (auto& parameter : requestParameters)
	{
		plist_dict_set_item(requestDictionary, parameter.first.c_str(), parameter.second);
	}

	std::map<std::string, plist_t> parameters = {
		{ "Header", header },
		{ "Request", requestDictionary }
	};

	auto plist = plist_new_dict();
	for (auto& parameter : parameters)
	{
		plist_dict_set_item(plist, parameter.first.c_str(), parameter.second);
	}

	char* plistXML = nullptr;
	uint32_t length = 0;
	plist_to_xml(plist, &plistXML, &length);

	std::map<utility::string_t, utility::string_t> headers = {
		{L"Content-Type", L"text/x-xml-plist"},
		{L"X-Mme-Client-Info", WideStringFromString(anisetteData->deviceDescription())},
		{L"Accept", L"*/*"},
		{L"User-Agent", L"akd/1.0 CFNetwork/978.0.7 Darwin/18.7.0"}
	};

	uri_builder builder(U("/grandslam/GsService2"));

	http_request request(methods::POST);
	request.set_request_uri(builder.to_string());
	request.set_body(plistXML);

	for (auto& pair : headers)
	{
		if (request.headers().has(pair.first))
		{
			request.headers().remove(pair.first);
		}

		request.headers().add(pair.first, pair.second);
	}

	auto task = this->gsaClient().request(request)
		.then([=](http_response response)
			{
				return response.content_ready();
			})
		.then([=](http_response response)
			{
				odslog("Received auth response status code: " << response.status_code());
				return response.extract_vector();
			})
				.then([=](std::vector<unsigned char> compressedData)
					{
						std::vector<uint8_t> decompressedData = compressedData;

						std::string decompressedXML = std::string(decompressedData.begin(), decompressedData.end());

						plist_t plist = nullptr;
						plist_from_xml(decompressedXML.c_str(), (int)decompressedXML.size(), &plist);

						if (plist == nullptr)
						{
							throw APIError(APIErrorCode::InvalidResponse);
						}

						return plist;
					})
		.then([=](plist_t plist)
          {
				auto dictionary = plist_dict_get_item(plist, "Response");
				if (dictionary == NULL)
				{
					throw APIError(APIErrorCode::InvalidResponse);
				}

				auto statusNode = plist_dict_get_item(dictionary, "Status");
				if (statusNode == NULL)
				{
					throw APIError(APIErrorCode::InvalidResponse);
				}

				auto node = plist_dict_get_item(statusNode, "ec");
				int64_t resultCode = 0;
				
				auto type = plist_get_node_type(node);
				switch (type)
				{
				case PLIST_STRING:
				{
					char* value = nullptr;
					plist_get_string_val(node, &value);

					resultCode = atoi(value);
					break;
				}

				case PLIST_UINT:
				{
					uint64_t value = 0;
					plist_get_uint_val(node, &value);

					resultCode = (int64_t)value;
					break;
				}

				case PLIST_REAL:
				{
					double value = 0;
					plist_get_real_val(node, &value);

					resultCode = (int64_t)value;
					break;
				}

				default:
					break;
				}

				switch (resultCode)
				{
				case 0: return dictionary;
				case -29004: throw APIError(APIErrorCode::InvalidAnisetteData);
				default:
				{
					auto descriptionNode = plist_dict_get_item(statusNode, "em");
					if (descriptionNode == nullptr)
					{
						throw APIError(APIErrorCode::InvalidResponse);
					}

					char* errorDescription = nullptr;
					plist_get_string_val(descriptionNode, &errorDescription);

					if (errorDescription == nullptr)
					{
						throw APIError(APIErrorCode::InvalidResponse);
					}

					std::stringstream ss;
					ss << errorDescription << " (" << resultCode << ")";

					throw LocalizedError((int)resultCode, ss.str());
				}
				}
          });

		free(plistXML);
		plist_free(plist);

		return task;
}

web::http::http_request AppleAPI::MakeTwoFactorCodeRequest(
	std::string url,
	std::string dsid,
	std::string idmsToken,
	std::shared_ptr<AnisetteData> anisetteData)
{
	auto encodedURI = web::uri::encode_uri(WideStringFromString(url));
	uri_builder builder(encodedURI);

	uri requestURI = builder.to_string();

	std::string identityToken = dsid + ":" + idmsToken;

	std::vector<unsigned char> identityTokenData(identityToken.begin(), identityToken.end());
	auto encodedIdentityToken = utility::conversions::to_base64(identityTokenData);

	time_t time;
	struct tm* tm;
	char dateString[64];

	time = anisetteData->date().tv_sec;
	tm = localtime(&time);

	strftime(dateString, sizeof dateString, "%FT%T%z", tm);

	std::map<utility::string_t, utility::string_t> headers = {
		{L"Accept", L"application/x-buddyml"},
		{L"Accept-Language", L"en-us"},
		{L"Content-Type", L"application/x-plist"},
		{L"User-Agent", L"Xcode"},
		{L"X-Apple-App-Info", L"com.apple.gs.xcode.auth"},
		{L"X-Xcode-Version", L"11.2 (11B41)"},

		{L"X-Apple-Identity-Token", encodedIdentityToken},
		{L"X-Apple-I-MD-M", WideStringFromString(anisetteData->machineID()) },
		{L"X-Apple-I-MD", WideStringFromString(anisetteData->oneTimePassword()) },
		{L"X-Apple-I-MD-LU", WideStringFromString(anisetteData->localUserID()) },
		{L"X-Apple-I-MD-RINFO", WideStringFromString(std::to_string(anisetteData->routingInfo())) },

		{L"X-Mme-Device-Id", WideStringFromString(anisetteData->deviceUniqueIdentifier()) },
		{L"X-Mme-Client-Info", WideStringFromString(anisetteData->deviceDescription()) },
		{L"X-Apple-I-Client-Time", WideStringFromString(dateString) },
		{L"X-Apple-Locale", WideStringFromString(anisetteData->locale()) },
		{L"X-Apple-I-TimeZone", WideStringFromString(anisetteData->timeZone()) },
	};

	http_request request(methods::GET);
	request.set_request_uri(requestURI);

	for (auto& pair : headers)
	{
		if (request.headers().has(pair.first))
		{
			request.headers().remove(pair.first);
		}

		request.headers().add(pair.first, pair.second);
	}

	return request;
}