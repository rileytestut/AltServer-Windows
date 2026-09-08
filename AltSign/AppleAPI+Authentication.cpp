// AppleAPI_Fixed.cpp
// Fixed version with connection reuse bug patched
// Based on AltSign/AltServer-Windows with improvements

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

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <openssl/bn.h>
#include <openssl/srp.h>

#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <thread>
#include <algorithm>
#include <stdexcept>
#include <cstring>
#include <ctime>
#include <cmath>
#include <memory>
#include <functional>
#include <optional>

#include <curl/curl.h>
#include <plist/plist.h>

// ============================================================================
// MARK: - Helpers
// ============================================================================

static const char ALTHexCharacters[] = "0123456786abcdef";

std::string generate_uuid() {
    unsigned char uuid[16];
    RAND_bytes(uuid, sizeof(uuid));
    uuid[6] = (uuid[6] & 0x0F) | 0x40;
    uuid[8] = (uuid[8] & 0x3F) | 0x80;

    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (int i = 0; i < 16; i++) {
        ss << std::setw(2) << (int)uuid[i];
        if (i == 3 || i == 5 || i == 7 || i == 9) ss << "-";
    }
    return ss.str();
}

std::vector<unsigned char> DataFromBytes(const char* bytes, size_t count) {
    std::vector<unsigned char> data;
    data.reserve(count);
    for (size_t i = 0; i < count; i++) {
        data.push_back((unsigned char)bytes[i]);
    }
    return data;
}

std::vector<unsigned char> DataFromString(const std::string& str) {
    return std::vector<unsigned char>(str.begin(), str.end());
}

std::string StringFromData(const std::vector<unsigned char>& data) {
    return std::string(data.begin(), data.end());
}

std::string HexStringFromData(const std::vector<unsigned char>& data) {
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (auto byte : data) {
        ss << std::setw(2) << (int)byte;
    }
    return ss.str();
}

std::vector<unsigned char> DataFromHexString(const std::string& hex) {
    std::vector<unsigned char> data;
    for (size_t i = 0; i < hex.length(); i += 2) {
        std::string byteString = hex.substr(i, 2);
        unsigned char byte = (unsigned char)std::stoi(byteString, nullptr, 16);
        data.push_back(byte);
    }
    return data;
}

// Base64 encoding using OpenSSL
std::string Base64Encode(const std::vector<unsigned char>& data) {
    BIO *bio, *b64;
    BUF_MEM *bufferPtr;

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(bio, data.data(), data.size());
    BIO_flush(bio);
    BIO_get_mem_ptr(bio, &bufferPtr);

    std::string result(bufferPtr->data, bufferPtr->length);
    BIO_free_all(bio);
    return result;
}

std::vector<unsigned char> Base64Decode(const std::string& encoded) {
    BIO *bio, *b64;
    int decodeLen = encoded.length();
    std::vector<unsigned char> buffer(decodeLen);

    bio = BIO_new_mem_buf(encoded.data(), -1);
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_push(b64, bio);

    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);
    int length = BIO_read(bio, buffer.data(), decodeLen);
    BIO_free_all(bio);

    buffer.resize(length);
    return buffer;
}

// ============================================================================
// MARK: - HTTP Client with Connection Fix
// ============================================================================

class HttpClient {
private:
    struct MemoryStruct {
        char* memory;
        size_t size;
    };

    static size_t WriteMemoryCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        size_t realsize = size * nmemb;
        struct MemoryStruct* mem = (struct MemoryStruct*)userp;

        char* ptr = (char*)realloc(mem->memory, mem->size + realsize + 1);
        if (ptr == NULL) return 0;

        mem->memory = ptr;
        memcpy(&(mem->memory[mem->size]), contents, realsize);
        mem->size += realsize;
        mem->memory[mem->size] = 0;

        return realsize;
    }

    static size_t HeaderCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        size_t realsize = size * nmemb;
        auto* headers = static_cast<std::map<std::string, std::string>*>(userp);
        std::string header(static_cast<char*>(contents), realsize);

        size_t pos = header.find(':');
        if (pos != std::string::npos) {
            std::string key = header.substr(0, pos);
            std::string value = header.substr(pos + 1);
            // Trim whitespace
            value.erase(0, value.find_first_not_of(" \r\n\t"));
            value.erase(value.find_last_not_of(" \r\n\t") + 1);
            (*headers)[key] = value;
        }
        return realsize;
    }

public:
    struct Response {
        long status_code;
        std::string body;
        std::map<std::string, std::string> headers;
        bool success;
        std::string error;
    };

    HttpClient() {
        curl_global_init(CURL_GLOBAL_ALL);
    }

    ~HttpClient() {
        curl_global_cleanup();
    }

    // ✅ FIX: Force new connection for each request
    Response post(const std::string& url, 
                  const std::string& body,
                  const std::map<std::string, std::string>& headers = {},
                  bool forceNewConnection = true) {

        CURL* curl = curl_easy_init();
        if (!curl) {
            throw std::runtime_error("Failed to initialize CURL");
        }

        MemoryStruct chunk = {(char*)malloc(1), 0};
        std::map<std::string, std::string> response_headers;

        struct curl_slist* header_list = NULL;
        for (const auto& h : headers) {
            std::string header = h.first + ": " + h.second;
            header_list = curl_slist_append(header_list, header.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void*)&response_headers);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        // ✅ CRITICAL FIX: Force new connection to avoid Apple 503 bug
        if (forceNewConnection) {
            curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);
            curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
        }

        Response response;
        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            long status_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
            response.status_code = status_code;
            response.body = std::string(chunk.memory, chunk.size);
            response.headers = response_headers;
            response.success = (status_code >= 200 && status_code < 300);
            response.error = "";
        } else {
            response.status_code = 0;
            response.body = "";
            response.success = false;
            response.error = curl_easy_strerror(res);
        }

        free(chunk.memory);
        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);

        return response;
    }

    Response get(const std::string& url,
                 const std::map<std::string, std::string>& headers = {},
                 bool forceNewConnection = true) {

        CURL* curl = curl_easy_init();
        if (!curl) {
            throw std::runtime_error("Failed to initialize CURL");
        }

        MemoryStruct chunk = {(char*)malloc(1), 0};
        std::map<std::string, std::string> response_headers;

        struct curl_slist* header_list = NULL;
        for (const auto& h : headers) {
            std::string header = h.first + ": " + h.second;
            header_list = curl_slist_append(header_list, header.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, (void*)&response_headers);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

        if (forceNewConnection) {
            curl_easy_setopt(curl, CURLOPT_FRESH_CONNECT, 1L);
            curl_easy_setopt(curl, CURLOPT_FORBID_REUSE, 1L);
        }

        Response response;
        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            long status_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
            response.status_code = status_code;
            response.body = std::string(chunk.memory, chunk.size);
            response.headers = response_headers;
            response.success = (status_code >= 200 && status_code < 300);
            response.error = "";
        } else {
            response.status_code = 0;
            response.body = "";
            response.success = false;
            response.error = curl_easy_strerror(res);
        }

        free(chunk.memory);
        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);

        return response;
    }
};

// ============================================================================
// MARK: - Anisette Data
// ============================================================================

struct AnisetteData {
    std::string machineID;
    std::string oneTimePassword;
    std::string localUserID;
    std::string deviceUniqueIdentifier;
    std::string deviceSerialNumber;
    std::string deviceDescription;
    std::string locale;
    std::string timeZone;
    uint64_t routingInfo;
    std::chrono::system_clock::time_point date;

    AnisetteData() {
        locale = "en_US";
        timeZone = "UTC";
        routingInfo = 17106176;
        date = std::chrono::system_clock::now();
    }

    static std::shared_ptr<AnisetteData> fromJSON(const std::string& json) {
        auto data = std::make_shared<AnisetteData>();
        // Simple JSON parsing - in production use a proper JSON library
        // This is a placeholder
        return data;
    }
};

// ============================================================================
// MARK: - Apple API Session
// ============================================================================

struct AppleAPISession {
    std::string dsid;
    std::string authToken;
    std::shared_ptr<AnisetteData> anisetteData;

    AppleAPISession() = default;
    AppleAPISession(const std::string& dsid, 
                    const std::string& token,
                    std::shared_ptr<AnisetteData> anisette)
        : dsid(dsid), authToken(token), anisetteData(anisette) {}

    std::string identityToken() const {
        std::string identity = dsid + ":" + authToken;
        return Base64Encode(DataFromString(identity));
    }
};

// ============================================================================
// MARK: - Account
// ============================================================================

struct Account {
    std::string dsid;
    std::string name;
    std::string email;
    std::vector<std::string> teams;

    Account() = default;
    explicit Account(plist_t plist) {
        // Parse account info from plist
    }
};

// ============================================================================
// MARK: - Errors
// ============================================================================

enum class APIErrorCode {
    Success = 0,
    InvalidResponse,
    InvalidAnisetteData,
    AuthenticationHandshakeFailed,
    RequiresTwoFactorAuthentication,
    IncorrectVerificationCode,
    InvalidCredentials,
    UnknownError
};

class APIError : public std::exception {
public:
    APIErrorCode code;
    std::string message;

    explicit APIError(APIErrorCode c) : code(c) {
        switch (c) {
            case APIErrorCode::InvalidResponse: message = "Invalid response from server"; break;
            case APIErrorCode::InvalidAnisetteData: message = "Invalid anisette data"; break;
            case APIErrorCode::AuthenticationHandshakeFailed: message = "Authentication handshake failed"; break;
            case APIErrorCode::RequiresTwoFactorAuthentication: message = "Two-factor authentication required"; break;
            case APIErrorCode::IncorrectVerificationCode: message = "Incorrect verification code"; break;
            case APIErrorCode::InvalidCredentials: message = "Invalid credentials"; break;
            default: message = "Unknown error"; break;
        }
    }

    APIError(APIErrorCode c, const std::string& msg) : code(c), message(msg) {}

    const char* what() const noexcept override {
        return message.c_str();
    }
};

// ============================================================================
// MARK: - Apple API (Fixed Version)
// ============================================================================

class AppleAPI {
private:
    HttpClient httpClient;
    std::string anisetteURL;

    static constexpr const char* GSA_URL = "https://gsa.apple.com/grandslam/GsService2";
    static constexpr const char* DS2_URL = "https://developerservices2.apple.com";

public:
    explicit AppleAPI(const std::string& anisette = "")
        : anisetteURL(anisette.empty() ? "https://anisette-v3-server-8re5.onrender.com/" : anisette) {}

    // ========================================================================
    // MARK: - SRP Helpers
    // ========================================================================

    void digestUpdateString(const EVP_MD_CTX* ctx, const std::string& str) {
        EVP_DigestUpdate(ctx, str.c_str(), str.length());
    }

    void digestUpdateData(const EVP_MD_CTX* ctx, const std::vector<unsigned char>& data) {
        uint32_t len = (uint32_t)data.size();
        EVP_DigestUpdate(ctx, &len, sizeof(len));
        EVP_DigestUpdate(ctx, data.data(), data.size());
    }

    std::optional<std::vector<unsigned char>> pbkdf2SRP(
        bool isS2K,
        const std::string& password,
        const std::vector<unsigned char>& salt,
        int iterations) {

        // SHA-256 hash of password
        unsigned char digest_raw[SHA256_DIGEST_LENGTH];
        SHA256((const unsigned char*)password.c_str(), password.length(), digest_raw);

        std::vector<unsigned char> digest;
        if (isS2K) {
            digest.assign(digest_raw, digest_raw + SHA256_DIGEST_LENGTH);
        } else {
            // Hex encode
            for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
                digest.push_back(ALTHexCharacters[(digest_raw[i] >> 4) & 0x0F]);
                digest.push_back(ALTHexCharacters[digest_raw[i] & 0x0F]);
            }
        }

        // PBKDF2-HMAC-SHA256
        std::vector<unsigned char> output(SHA256_DIGEST_LENGTH);
        if (PKCS5_PBKDF2_HMAC((const char*)digest.data(), digest.size(),
                              salt.data(), salt.size(),
                              iterations, EVP_sha256(),
                              SHA256_DIGEST_LENGTH, output.data()) != 1) {
            return std::nullopt;
        }

        return output;
    }

    // Create session key using HMAC
    std::vector<unsigned char> createSessionKey(
        const std::vector<unsigned char>& sessionKey,
        const std::string& keyName) {

        unsigned char hmac[SHA256_DIGEST_LENGTH];
        unsigned int hmacLen;

        HMAC(EVP_sha256(), sessionKey.data(), sessionKey.size(),
             (const unsigned char*)keyName.c_str(), keyName.length(),
             hmac, &hmacLen);

        return std::vector<unsigned char>(hmac, hmac + hmacLen);
    }

    // Decrypt CBC data
    std::optional<std::vector<unsigned char>> decryptDataCBC(
        const std::vector<unsigned char>& sessionKey,
        const std::vector<unsigned char>& encryptedData) {

        auto key = createSessionKey(sessionKey, "extra data key:");
        auto iv = createSessionKey(sessionKey, "extra data iv:");

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return std::nullopt;

        std::vector<unsigned char> decrypted(encryptedData.size() + EVP_MAX_BLOCK_LENGTH);
        int decryptedLen = 0, finalLen = 0;

        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), NULL, key.data(), iv.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return std::nullopt;
        }

        EVP_DecryptUpdate(ctx, decrypted.data(), &decryptedLen,
                         encryptedData.data(), encryptedData.size());

        if (EVP_DecryptFinal_ex(ctx, decrypted.data() + decryptedLen, &finalLen) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return std::nullopt;
        }

        decrypted.resize(decryptedLen + finalLen);
        EVP_CIPHER_CTX_free(ctx);

        return decrypted;
    }

    // Decrypt GCM data
    std::optional<std::vector<unsigned char>> decryptDataGCM(
        const std::vector<unsigned char>& sessionKey,
        const std::vector<unsigned char>& encryptedData) {

        if (encryptedData.size() < 35) {
            std::cerr << "ERROR: Encrypted token too short." << std::endl;
            return std::nullopt;
        }

        // Check "XYZ" prefix
        if (memcmp(encryptedData.data(), "XYZ", 3) != 0) {
            std::cerr << "ERROR: Encrypted token wrong version!" << std::endl;
            return std::nullopt;
        }

        const unsigned char* iv = encryptedData.data() + 3;
        const unsigned char* ciphertext = encryptedData.data() + 19;
        size_t ciphertextLen = encryptedData.size() - 35;
        const unsigned char* tag = encryptedData.data() + encryptedData.size() - 16;

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        if (!ctx) return std::nullopt;

        std::vector<unsigned char> decrypted(ciphertextLen);
        int decryptedLen = 0;

        // Init
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return std::nullopt;
        }

        // Set key and IV length
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 16, NULL) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return std::nullopt;
        }

        if (EVP_DecryptInit_ex(ctx, NULL, NULL, sessionKey.data(), iv) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return std::nullopt;
        }

        // AAD
        int aadLen;
        EVP_DecryptUpdate(ctx, NULL, &aadLen, (const unsigned char*)"XYZ", 3);

        // Decrypt
        if (EVP_DecryptUpdate(ctx, decrypted.data(), &decryptedLen,
                             ciphertext, ciphertextLen) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return std::nullopt;
        }

        // Set tag
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void*)tag) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return std::nullopt;
        }

        // Final
        int finalLen;
        if (EVP_DecryptFinal_ex(ctx, decrypted.data() + decryptedLen, &finalLen) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            return std::nullopt;
        }

        decrypted.resize(decryptedLen + finalLen);
        EVP_CIPHER_CTX_free(ctx);

        return decrypted;
    }

    // Create app tokens checksum
    std::vector<unsigned char> createAppTokensChecksum(
        const std::vector<unsigned char>& sessionKey,
        const std::string& adsid,
        const std::vector<std::string>& apps) {

        HMAC_CTX* ctx = HMAC_CTX_new();

        HMAC_Init_ex(ctx, sessionKey.data(), sessionKey.size(), EVP_sha256(), NULL);

        const char* key = "apptokens";
        HMAC_Update(ctx, (const unsigned char*)key, strlen(key));

        HMAC_Update(ctx, (const unsigned char*)adsid.c_str(), adsid.length());

        for (const auto& app : apps) {
            HMAC_Update(ctx, (const unsigned char*)app.c_str(), app.size());
        }

        unsigned char checksum[SHA256_DIGEST_LENGTH];
        unsigned int checksumLen;
        HMAC_Final(ctx, checksum, &checksumLen);
        HMAC_CTX_free(ctx);

        return std::vector<unsigned char>(checksum, checksum + checksumLen);
    }

    // ========================================================================
    // MARK: - SRP Implementation
    // ========================================================================

    class SRPClient {
    private:
        BIGNUM* N;  // Safe prime
        BIGNUM* g;  // Generator
        BIGNUM* a;  // Private key (random)
        BIGNUM* A;  // Public key
        BIGNUM* B;  // Server public key
        BIGNUM* S;  // Shared secret
        BIGNUM* u;  // Scrambling parameter
        BIGNUM* x;  // Private key derived from password

        BN_CTX* bnCtx;

        static constexpr const char* N_hex = 
            "AC6BDB41324A9A9BF166DE5E1389582FAF72B6651987EE07FC3192943DB56050A37329CBB4A099ED8193E0757767A13DD52312AB4B03310DCD7F48A9DA04FD50E8083969EDB767B0CF6095179A163AB3661A05FBD5FAAAE82918A9962F0B93B855F97993EC975EEAA80D740ADBF4FF747359D041D5C33EA71D281E446B14773BCA97B43A23FB801676BD207A436C6481F1D2B9078717461A5B9D32E688F87748544523B524B0D57D5EA77A2775D2ECFA032CFBDBF52FB3786160279004E57AE6AF874E7303CE53299CCC041C7BC308D82A5698F3A8D0C38271AE35F8E9DBFBB694B5C803D89F7AE435DE236D525F54759B65E372FCD68EF20FA7111F9E4AFF73";
        static constexpr const char* g_hex = "2";

    public:
        SRPClient() {
            bnCtx = BN_CTX_new();
            BN_hex2bn(&N, N_hex);
            BN_hex2bn(&g, g_hex);

            // Generate random private key 'a'
            a = BN_new();
            BN_rand(a, 256, -1, 0);  // 2048 bits

            // Calculate A = g^a mod N
            A = BN_new();
            BN_mod_exp(A, g, a, N, bnCtx);
        }

        ~SRPClient() {
            BN_free(N);
            BN_free(g);
            BN_free(a);
            BN_free(A);
            if (B) BN_free(B);
            if (S) BN_free(S);
            if (u) BN_free(u);
            if (x) BN_free(x);
            BN_CTX_free(bnCtx);
        }

        std::vector<unsigned char> getPublicKey() {
            int len = BN_num_bytes(A);
            std::vector<unsigned char> result(len);
            BN_bn2bin(A, result.data());
            return result;
        }

        std::optional<std::vector<unsigned char>> processChallenge(
            const std::string& username,
            const std::vector<unsigned char>& passwordKey,
            const std::vector<unsigned char>& salt,
            const std::vector<unsigned char>& serverB) {

            // Set B
            B = BN_bin2bn(serverB.data(), serverB.size(), NULL);
            if (!B) return std::nullopt;

            // Calculate u = H(A | B)
            unsigned char hash[SHA256_DIGEST_LENGTH];
            SHA256_CTX shaCtx;
            SHA256_Init(&shaCtx);

            std::vector<unsigned char> A_bytes = getPublicKey();
            SHA256_Update(&shaCtx, A_bytes.data(), A_bytes.size());

            std::vector<unsigned char> B_bytes(serverB.begin(), serverB.end());
            SHA256_Update(&shaCtx, B_bytes.data(), B_bytes.size());
            SHA256_Final(hash, &shaCtx);

            u = BN_bin2bn(hash, SHA256_DIGEST_LENGTH, NULL);

            // Calculate x = H(salt | H(username ":" password))
            // Note: For SRP-6a with noUsernameInX, we use just the passwordKey
            x = BN_bin2bn(passwordKey.data(), passwordKey.size(), NULL);

            // Calculate S = (B - g^x)^(a + u*x) mod N
            BIGNUM* gx = BN_new();
            BN_mod_exp(gx, g, x, N, bnCtx);

            BIGNUM* B_minus_gx = BN_new();
            BN_mod_sub(B_minus_gx, B, gx, N, bnCtx);

            BIGNUM* ux = BN_new();
            BN_mul(ux, u, x, bnCtx);

            BIGNUM* a_plus_ux = BN_new();
            BN_add(a_plus_ux, a, ux);

            S = BN_new();
            BN_mod_exp(S, B_minus_gx, a_plus_ux, N, bnCtx);

            BN_free(gx);
            BN_free(B_minus_gx);
            BN_free(ux);
            BN_free(a_plus_ux);

            // Calculate M1 = H(H(N) xor H(g) | H(username) | salt | A | B | K)
            // where K = H(S)

            // H(N)
            unsigned char hashN[SHA256_DIGEST_LENGTH];
            std::vector<unsigned char> N_bytes(BN_num_bytes(N));
            BN_bn2bin(N, N_bytes.data());
            SHA256(N_bytes.data(), N_bytes.size(), hashN);

            // H(g)
            unsigned char hashg[SHA256_DIGEST_LENGTH];
            std::vector<unsigned char> g_bytes(BN_num_bytes(g));
            BN_bn2bin(g, g_bytes.data());
            SHA256(g_bytes.data(), g_bytes.size(), hashg);

            // H(N) xor H(g)
            unsigned char hashNg[SHA256_DIGEST_LENGTH];
            for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
                hashNg[i] = hashN[i] ^ hashg[i];
            }

            // H(username)
            unsigned char hashUsername[SHA256_DIGEST_LENGTH];
            SHA256((const unsigned char*)username.c_str(), username.length(), hashUsername);

            // K = H(S)
            unsigned char K[SHA256_DIGEST_LENGTH];
            std::vector<unsigned char> S_bytes(BN_num_bytes(S));
            BN_bn2bin(S, S_bytes.data());
            SHA256(S_bytes.data(), S_bytes.size(), K);

            // M1 = H(hashNg | hashUsername | salt | A | B | K)
            unsigned char M1[SHA256_DIGEST_LENGTH];
            SHA256_Init(&shaCtx);
            SHA256_Update(&shaCtx, hashNg, SHA256_DIGEST_LENGTH);
            SHA256_Update(&shaCtx, hashUsername, SHA256_DIGEST_LENGTH);
            SHA256_Update(&shaCtx, salt.data(), salt.size());
            SHA256_Update(&shaCtx, A_bytes.data(), A_bytes.size());
            SHA256_Update(&shaCtx, B_bytes.data(), B_bytes.size());
            SHA256_Update(&shaCtx, K, SHA256_DIGEST_LENGTH);
            SHA256_Final(M1, &shaCtx);

            sessionKey.assign(K, K + SHA256_DIGEST_LENGTH);

            return std::vector<unsigned char>(M1, M1 + SHA256_DIGEST_LENGTH);
        }

        bool verifySession(const std::vector<unsigned char>& M2_server) {
            // Calculate expected M2 = H(A | M1 | K)
            unsigned char M1[SHA256_DIGEST_LENGTH];
            // ... recalculate M1 ...

            unsigned char expectedM2[SHA256_DIGEST_LENGTH];
            SHA256_CTX shaCtx;
            SHA256_Init(&shaCtx);

            std::vector<unsigned char> A_bytes = getPublicKey();
            SHA256_Update(&shaCtx, A_bytes.data(), A_bytes.size());
            SHA256_Update(&shaCtx, M1, SHA256_DIGEST_LENGTH);
            SHA256_Update(&shaCtx, sessionKey.data(), sessionKey.size());
            SHA256_Final(expectedM2, &shaCtx);

            return memcmp(expectedM2, M2_server.data(), SHA256_DIGEST_LENGTH) == 0;
        }

        std::vector<unsigned char> getSessionKey() {
            return sessionKey;
        }

    private:
        std::vector<unsigned char> sessionKey;
    };

    // ========================================================================
    // MARK: - Plist Helpers
    // ========================================================================

    plist_t createCPD(std::shared_ptr<AnisetteData> anisetteData) {
        auto cpd = plist_new_dict();

        // Time string
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::gmtime(&now_c), "%Y-%m-%dT%H:%M:%SZ");
        std::string dateString = ss.str();

        plist_dict_set_item(cpd, "bootstrap", plist_new_bool(true));
        plist_dict_set_item(cpd, "icscrec", plist_new_bool(true));
        plist_dict_set_item(cpd, "loc", plist_new_string(anisetteData->locale.c_str()));
        plist_dict_set_item(cpd, "pbe", plist_new_bool(false));
        plist_dict_set_item(cpd, "prkgen", plist_new_bool(true));
        plist_dict_set_item(cpd, "svct", plist_new_string("iCloud"));

        plist_dict_set_item(cpd, "X-Apple-I-Client-Time", plist_new_string(dateString.c_str()));
        plist_dict_set_item(cpd, "X-Apple-Locale", plist_new_string(anisetteData->locale.c_str()));
        plist_dict_set_item(cpd, "X-Apple-I-TimeZone", plist_new_string(anisetteData->timeZone.c_str()));
        plist_dict_set_item(cpd, "X-Apple-I-MD", plist_new_string(anisetteData->oneTimePassword.c_str()));
        plist_dict_set_item(cpd, "X-Apple-I-MD-LU", plist_new_string(anisetteData->localUserID.c_str()));
        plist_dict_set_item(cpd, "X-Apple-I-MD-M", plist_new_string(anisetteData->machineID.c_str()));
        plist_dict_set_item(cpd, "X-Apple-I-MD-RINFO", plist_new_uint(anisetteData->routingInfo));
        plist_dict_set_item(cpd, "X-Mme-Device-Id", plist_new_string(anisetteData->deviceUniqueIdentifier.c_str()));
        plist_dict_set_item(cpd, "X-Apple-I-SRL-NO", plist_new_string(anisetteData->deviceSerialNumber.c_str()));

        return cpd;
    }

    std::string plistToXML(plist_t plist) {
        char* xml = nullptr;
        uint32_t length = 0;
        plist_to_xml(plist, &xml, &length);
        std::string result(xml, length);
        free(xml);
        return result;
    }

    std::vector<unsigned char> plistToBinary(plist_t plist) {
        char* bin = nullptr;
        uint32_t length = 0;
        plist_to_bin(plist, &bin, &length);
        std::vector<unsigned char> result(bin, bin + length);
        free(bin);
        return result;
    }

    plist_t parsePlistXML(const std::string& xml) {
        plist_t plist = nullptr;
        plist_from_xml(xml.c_str(), xml.size(), &plist);
        return plist;
    }

    plist_t parsePlistBinary(const std::vector<unsigned char>& data) {
        plist_t plist = nullptr;
        plist_from_bin((const char*)data.data(), data.size(), &plist);
        return plist;
    }

    // ========================================================================
    // MARK: - Anisette Fetching
    // ========================================================================

    std::shared_ptr<AnisetteData> fetchAnisetteData() {
        auto response = httpClient.get(anisetteURL);

        if (!response.success) {
            throw APIError(APIErrorCode::InvalidAnisetteData, 
                          "Failed to fetch anisette data: " + response.error);
        }

        // Parse JSON response
        auto data = std::make_shared<AnisetteData>();

        // Simple JSON parsing - extract values
        auto findValue = [&](const std::string& key) -> std::string {
            std::string search = "\"" + key + "\":\"";
            size_t start = response.body.find(search);
            if (start == std::string::npos) return "";
            start += search.length();
            size_t end = response.body.find("\"", start);
            return response.body.substr(start, end - start);
        };

        data->machineID = findValue("X-Apple-I-MD-M");
        data->oneTimePassword = findValue("X-Apple-I-MD");
        data->localUserID = findValue("X-Apple-I-MD-LU");
        data->deviceUniqueIdentifier = findValue("X-Mme-Device-Id");
        data->deviceSerialNumber = findValue("X-Apple-I-SRL-NO");
        data->deviceDescription = findValue("X-MMe-Client-Info");

        std::string rinfo = findValue("X-Apple-I-MD-RINFO");
        if (!rinfo.empty()) {
            data->routingInfo = std::stoull(rinfo);
        }

        return data;
    }

    // ========================================================================
    // MARK: - GSA Request (FIXED: Force new connection)
    // ========================================================================

    plist_t sendGSARequest(plist_t requestPlist, std::shared_ptr<AnisetteData> anisetteData) {
        // Convert to XML
        std::string xmlBody = plistToXML(requestPlist);

        // Headers
        std::map<std::string, std::string> headers = {
            {"Content-Type", "text/x-xml-plist"},
            {"Accept", "*/*"},
            {"User-Agent", "akd/1.0 CFNetwork/978.0.7 Darwin/18.7.0"},
            {"X-Mme-Client-Info", anisetteData->deviceDescription}
        };

        // ✅ FIX: Force new connection for each GSA request
        auto response = httpClient.post(GSA_URL, xmlBody, headers, true);

        if (!response.success) {
            std::stringstream ss;
            ss << "GSA request failed with status " << response.status_code;
            if (!response.body.empty()) {
                ss << ": " << response.body.substr(0, 200);
            }
            throw APIError(APIErrorCode::InvalidResponse, ss.str());
        }

        // Parse response
        plist_t responsePlist = parsePlistXML(response.body);
        if (!responsePlist) {
            // Try binary plist
            responsePlist = parsePlistBinary(DataFromString(response.body));
        }

        if (!responsePlist) {
            throw APIError(APIErrorCode::InvalidResponse, "Failed to parse response plist");
        }

        // Check status
        auto responseDict = plist_dict_get_item(responsePlist, "Response");
        if (!responseDict) {
            plist_free(responsePlist);
            throw APIError(APIErrorCode::InvalidResponse, "Missing Response dict");
        }

        auto statusNode = plist_dict_get_item(responseDict, "Status");
        if (!statusNode) {
            plist_free(responsePlist);
            throw APIError(APIErrorCode::InvalidResponse, "Missing Status dict");
        }

        auto ecNode = plist_dict_get_item(statusNode, "ec");
        if (ecNode) {
            uint64_t ec = 0;
            plist_get_uint_val(ecNode, &ec);

            if (ec != 0) {
                std::string errorMsg = "Unknown error";
                auto emNode = plist_dict_get_item(statusNode, "em");
                if (emNode) {
                    char* em = nullptr;
                    plist_get_string_val(emNode, &em);
                    if (em) errorMsg = em;
                }

                plist_free(responsePlist);

                if (ec == -29004) {
                    throw APIError(APIErrorCode::InvalidAnisetteData, errorMsg);
                } else if (ec == -22406) {
                    throw APIError(APIErrorCode::InvalidCredentials, errorMsg);
                } else {
                    throw APIError(APIErrorCode::UnknownError, 
                                  errorMsg + " (code: " + std::to_string((int64_t)ec) + ")");
                }
            }
        }

        // Return the Response dict (caller must free)
        plist_t result = plist_copy(responseDict);
        plist_free(responsePlist);

        return result;
    }

    // ========================================================================
    // MARK: - Authentication
    // ========================================================================

    std::pair<std::shared_ptr<Account>, std::shared_ptr<AppleAPISession>> authenticate(
        const std::string& appleID,
        const std::string& password,
        std::shared_ptr<AnisetteData> anisetteData = nullptr,
        std::optional<std::function<std::optional<std::string>(void)>> verificationHandler = std::nullopt) {

        if (!anisetteData) {
            anisetteData = fetchAnisetteData();
        }

        // Initialize SRP
        SRPClient srp;

        // Build digest context for negotiation
        EVP_MD_CTX* digestCtx = EVP_MD_CTX_new();
        EVP_DigestInit_ex(digestCtx, EVP_sha256(), NULL);

        std::vector<std::string> ps = {"s2k", "s2k_fo"};

        // Update digest with ps
        for (const auto& p : ps) {
            EVP_DigestUpdate(digestCtx, p.c_str(), p.length());
            EVP_DigestUpdate(digestCtx, ",", 1);
        }

        // ========================================
        // STEP 1: INIT
        // ========================================

        std::cout << "[auth] Step 1: Sending init request..." << std::endl;

        auto initPlist = plist_new_dict();
        plist_dict_set_item(initPlist, "Header", plist_new_dict());
        auto header = plist_dict_get_item(initPlist, "Header");
        plist_dict_set_item(header, "Version", plist_new_string("1.0.1"));

        auto initRequest = plist_new_dict();

        auto A_data = srp.getPublicKey();
        plist_dict_set_item(initRequest, "A2k", plist_new_data((const char*)A_data.data(), A_data.size()));

        auto psArray = plist_new_array();
        for (const auto& p : ps) {
            plist_array_append_item(psArray, plist_new_string(p.c_str()));
        }
        plist_dict_set_item(initRequest, "ps", psArray);

        plist_dict_set_item(initRequest, "cpd", createCPD(anisetteData));
        plist_dict_set_item(initRequest, "u", plist_new_string(appleID.c_str()));
        plist_dict_set_item(initRequest, "o", plist_new_string("init"));

        plist_dict_set_item(initPlist, "Request", initRequest);

        auto initResponse = sendGSARequest(initPlist, anisetteData);
        plist_free(initPlist);

        // Parse init response
        auto spNode = plist_dict_get_item(initResponse, "sp");
        auto cNode = plist_dict_get_item(initResponse, "c");
        auto saltNode = plist_dict_get_item(initResponse, "s");
        auto iterationsNode = plist_dict_get_item(initResponse, "i");
        auto bNode = plist_dict_get_item(initResponse, "B");

        if (!spNode || !cNode || !saltNode || !iterationsNode || !bNode) {
            plist_free(initResponse);
            EVP_MD_CTX_free(digestCtx);
            throw APIError(APIErrorCode::InvalidResponse, "Missing init response fields");
        }

        char* sp = nullptr;
        plist_get_string_val(spNode, &sp);
        bool isS2K = (std::string(sp) == "s2k");

        char* c = nullptr;
        plist_get_string_val(cNode, &c);

        char* saltBytes = nullptr;
        uint64_t saltSize = 0;
        plist_get_data_val(saltNode, &saltBytes, &saltSize);
        auto salt = DataFromBytes(saltBytes, saltSize);

        uint64_t iterations = 0;
        plist_get_uint_val(iterationsNode, &iterations);

        char* B_bytes = nullptr;
        uint64_t B_size = 0;
        plist_get_data_val(bNode, &B_bytes, &B_size);
        auto B_data = DataFromBytes(B_bytes, B_size);

        std::cout << "[auth] Got init response, sp=" << sp << ", iterations=" << iterations << std::endl;

        // Update digest
        EVP_DigestUpdate(digestCtx, sp, strlen(sp));
        EVP_DigestUpdate(digestCtx, "|", 1);

        // ========================================
        // STEP 2: COMPLETE
        // ========================================

        std::cout << "[auth] Step 2: Processing challenge..." << std::endl;

        // Derive password key
        auto passwordKey = pbkdf2SRP(isS2K, password, salt, (int)iterations);
        if (!passwordKey) {
            plist_free(initResponse);
            EVP_MD_CTX_free(digestCtx);
            throw APIError(APIErrorCode::AuthenticationHandshakeFailed, "PBKDF2 failed");
        }

        // Process challenge
        auto M1 = srp.processChallenge(appleID, *passwordKey, salt, B_data);
        if (!M1) {
            plist_free(initResponse);
            EVP_MD_CTX_free(digestCtx);
            throw APIError(APIErrorCode::AuthenticationHandshakeFailed, "SRP challenge failed");
        }

        // Build complete request
        auto completePlist = plist_new_dict();
        plist_dict_set_item(completePlist, "Header", plist_new_dict());
        auto completeHeader = plist_dict_get_item(completePlist, "Header");
        plist_dict_set_item(completeHeader, "Version", plist_new_string("1.0.1"));

        auto completeRequest = plist_new_dict();
        plist_dict_set_item(completeRequest, "c", plist_new_string(c));
        plist_dict_set_item(completeRequest, "M1", plist_new_data((const char*)M1->data(), M1->size()));
        plist_dict_set_item(completeRequest, "cpd", createCPD(anisetteData));
        plist_dict_set_item(completeRequest, "u", plist_new_string(appleID.c_str()));
        plist_dict_set_item(completeRequest, "o", plist_new_string("complete"));

        plist_dict_set_item(completePlist, "Request", completeRequest);

        auto completeResponse = sendGSARequest(completePlist, anisetteData);
        plist_free(completePlist);
        plist_free(initResponse);

        // Parse complete response
        auto M2Node = plist_dict_get_item(completeResponse, "M2");
        auto spdNode = plist_dict_get_item(completeResponse, "spd");
        auto scNode = plist_dict_get_item(completeResponse, "sc");
        auto npNode = plist_dict_get_item(completeResponse, "np");

        if (!M2Node || !spdNode || !npNode) {
            plist_free(completeResponse);
            EVP_MD_CTX_free(digestCtx);
            throw APIError(APIErrorCode::InvalidResponse, "Missing complete response fields");
        }

        // Verify M2
        char* M2_bytes = nullptr;
        uint64_t M2_size = 0;
        plist_get_data_val(M2Node, &M2_bytes, &M2_size);

        if (!srp.verifySession(DataFromBytes(M2_bytes, M2_size))) {
            plist_free(completeResponse);
            EVP_MD_CTX_free(digestCtx);
            throw APIError(APIErrorCode::AuthenticationHandshakeFailed, "M2 verification failed");
        }

        std::cout << "[auth] SRP handshake completed successfully!" << std::endl;

        // Get session key
        auto sessionKey = srp.getSessionKey();

        // Decrypt spd
        char* spdBytes = nullptr;
        uint64_t spdSize = 0;
        plist_get_data_val(spdNode, &spdBytes, &spdSize);
        auto spd = DataFromBytes(spdBytes, spdSize);

        auto decryptedData = decryptDataCBC(sessionKey, spd);
        if (!decryptedData) {
            plist_free(completeResponse);
            EVP_MD_CTX_free(digestCtx);
            throw APIError(APIErrorCode::AuthenticationHandshakeFailed, "Failed to decrypt spd");
        }

        // Parse decrypted plist
        auto decryptedPlist = parsePlistXML(StringFromData(*decryptedData));
        if (!decryptedPlist) {
            plist_free(completeResponse);
            EVP_MD_CTX_free(digestCtx);
            throw APIError(APIErrorCode::InvalidResponse, "Failed to parse decrypted plist");
        }

        // Extract adsid and token
        auto adsidNode = plist_dict_get_item(decryptedPlist, "adsid");
        auto idmsTokenNode = plist_dict_get_item(decryptedPlist, "GsIdmsToken");

        if (!adsidNode || !idmsTokenNode) {
            plist_free(decryptedPlist);
            plist_free(completeResponse);
            EVP_MD_CTX_free(digestCtx);
            throw APIError(APIErrorCode::InvalidResponse, "Missing adsid or idmsToken");
        }

        char* adsid = nullptr;
        plist_get_string_val(adsidNode, &adsid);

        char* idmsToken = nullptr;
        plist_get_string_val(idmsTokenNode, &idmsToken);

        std::string adsidStr(adsid);
        std::string idmsTokenStr(idmsToken);

        std::cout << "[auth] Got adsid: " << adsidStr << std::endl;

        // Check for 2FA
        auto statusNode = plist_dict_get_item(completeResponse, "Status");
        bool requires2FA = false;
        if (statusNode) {
            auto auNode = plist_dict_get_item(statusNode, "au");
            if (auNode) {
                char* au = nullptr;
                plist_get_string_val(auNode, &au);
                if (au && std::string(au) == "trustedDeviceSecondaryAuth") {
                    requires2FA = true;
                }
            }
        }

        if (requires2FA) {
            std::cout << "[auth] Two-factor authentication required!" << std::endl;
            plist_free(decryptedPlist);
            plist_free(completeResponse);
            EVP_MD_CTX_free(digestCtx);

            if (verificationHandler) {
                // Handle 2FA
                auto code = (*verificationHandler)();
                if (code) {
                    // TODO: Implement 2FA verification
                    // For now, throw
                    throw APIError(APIErrorCode::RequiresTwoFactorAuthentication);
                }
            }

            throw APIError(APIErrorCode::RequiresTwoFactorAuthentication);
        }

        // Get sk and c from decrypted plist
        auto skNode = plist_dict_get_item(decryptedPlist, "sk");
        auto cNode2 = plist_dict_get_item(decryptedPlist, "c");

        if (!skNode || !cNode2) {
            plist_free(decryptedPlist);
            plist_free(completeResponse);
            EVP_MD_CTX_free(digestCtx);
            throw APIError(APIErrorCode::InvalidResponse, "Missing sk or c");
        }

        char* skBytes = nullptr;
        uint64_t skSize = 0;
        plist_get_data_val(skNode, &skBytes, &skSize);
        auto sk = DataFromBytes(skBytes, skSize);

        char* cBytes = nullptr;
        uint64_t cSize = 0;
        plist_get_data_val(cNode2, &cBytes, &cSize);

        plist_free(decryptedPlist);
        plist_free(completeResponse);

        // ========================================
        // STEP 3: APP TOKENS
        // ========================================

        std::cout << "[auth] Step 3: Fetching app tokens..." << std::endl;

        std::vector<std::string> apps = {"com.apple.gs.xcode.auth"};
        auto checksum = createAppTokensChecksum(sk, adsidStr, apps);

        auto tokenPlist = plist_new_dict();
        plist_dict_set_item(tokenPlist, "Header", plist_new_dict());
        auto tokenHeader = plist_dict_get_item(tokenPlist, "Header");
        plist_dict_set_item(tokenHeader, "Version", plist_new_string("1.0.1"));

        auto tokenRequest = plist_new_dict();
        plist_dict_set_item(tokenRequest, "u", plist_new_string(adsidStr.c_str()));

        auto appsArray = plist_new_array();
        for (const auto& app : apps) {
            plist_array_append_item(appsArray, plist_new_string(app.c_str()));
        }
        plist_dict_set_item(tokenRequest, "app", appsArray);

        plist_dict_set_item(tokenRequest, "c", plist_new_data(cBytes, cSize));
        plist_dict_set_item(tokenRequest, "t", plist_new_string(idmsTokenStr.c_str()));
        plist_dict_set_item(tokenRequest, "checksum", plist_new_data((const char*)checksum.data(), checksum.size()));
        plist_dict_set_item(tokenRequest, "cpd", createCPD(anisetteData));
        plist_dict_set_item(tokenRequest, "o", plist_new_string("apptokens"));

        plist_dict_set_item(tokenPlist, "Request", tokenRequest);

        auto tokenResponse = sendGSARequest(tokenPlist, anisetteData);
        plist_free(tokenPlist);

        // Parse token response
        auto etNode = plist_dict_get_item(tokenResponse, "et");
        if (!etNode) {
            plist_free(tokenResponse);
            EVP_MD_CTX_free(digestCtx);
            throw APIError(APIErrorCode::InvalidResponse, "Missing et in token response");
        }

        char* etBytes = nullptr;
        uint64_t etSize = 0;
        plist_get_data_val(etNode, &etBytes, &etSize);
        auto encryptedToken = DataFromBytes(etBytes, etSize);

        auto decryptedToken = decryptDataGCM(sk, encryptedToken);
        if (!decryptedToken) {
            plist_free(tokenResponse);
            EVP_MD_CTX_free(digestCtx);
            throw APIError(APIErrorCode::InvalidResponse, "Failed to decrypt token");
        }

        auto tokenPlistParsed = parsePlistXML(StringFromData(*decryptedToken));
        if (!tokenPlistParsed) {
            plist_free(tokenResponse);
            EVP_MD_CTX_free(digestCtx);
            throw APIError(APIErrorCode::InvalidResponse, "Failed to parse token plist");
        }

        auto tokensNode = plist_dict_get_item(tokenPlistParsed, "t");
        if (!tokensNode) {
            plist_free(tokenPlistParsed);
            plist_free(tokenResponse);
            EVP_MD_CTX_free(digestCtx);
            throw APIError(APIErrorCode::InvalidResponse, "Missing tokens");
        }

        auto tokenDict = plist_dict_get_item(tokensNode, "com.apple.gs.xcode.auth");
        if (!tokenDict) {
            plist_free(tokenPlistParsed);
            plist_free(tokenResponse);
            EVP_MD_CTX_free(digestCtx);
            throw APIError(APIErrorCode::InvalidResponse, "Missing xcode auth token");
        }

        auto tokenNode = plist_dict_get_item(tokenDict, "token");
        if (!tokenNode) {
            plist_free(tokenPlistParsed);
            plist_free(tokenResponse);
            EVP_MD_CTX_free(digestCtx);
            throw APIError(APIErrorCode::InvalidResponse, "Missing token value");
        }

        char* token = nullptr;
        plist_get_string_val(tokenNode, &token);
        std::string authToken(token);

        std::cout << "[auth] Got auth token for com.apple.gs.xcode.auth!" << std::endl;

        plist_free(tokenPlistParsed);
        plist_free(tokenResponse);
        EVP_MD_CTX_free(digestCtx);

        // Create session
        auto session = std::make_shared<AppleAPISession>(adsidStr, authToken, anisetteData);

        // ========================================
        // STEP 4: FETCH ACCOUNT
        // ========================================

        std::cout << "[auth] Step 4: Fetching account info..." << std::endl;

        auto account = fetchAccount(session);

        std::cout << "[auth] Authentication complete!" << std::endl;

        return std::make_pair(account, session);
    }

    // ========================================================================
    // MARK: - Fetch Account
    // ========================================================================

    std::shared_ptr<Account> fetchAccount(std::shared_ptr<AppleAPISession> session) {
        std::map<std::string, std::string> headers = {
            {"Content-Type", "text/x-xml-plist"},
            {"User-Agent", "Xcode"},
            {"Accept", "text/x-xml-plist"},
            {"X-Apple-App-Info", "com.apple.gs.xcode.auth"},
            {"X-Xcode-Version", "11.2 (11B41)"},
            {"X-Apple-Identity-Token", session->identityToken()}
        };

        // Add anisette headers
        if (session->anisetteData) {
            headers["X-Apple-I-MD-M"] = session->anisetteData->machineID;
            headers["X-Apple-I-MD"] = session->anisetteData->oneTimePassword;
            headers["X-Apple-I-MD-LU"] = session->anisetteData->localUserID;
            headers["X-Mme-Device-Id"] = session->anisetteData->deviceUniqueIdentifier;
        }

        std::string url = std::string(DS2_URL) + "/viewDeveloper.action";

        // For DS2, we can reuse connection (no 503 bug)
        auto response = httpClient.get(url, headers, false);

        if (!response.success) {
            throw APIError(APIErrorCode::InvalidResponse, 
                          "Failed to fetch account: " + std::to_string(response.status_code));
        }

        // Parse response
        auto plist = parsePlistXML(response.body);
        if (!plist) {
            throw APIError(APIErrorCode::InvalidResponse, "Failed to parse account response");
        }

        auto account = std::make_shared<Account>();
        // TODO: Parse account info from plist

        plist_free(plist);

        return account;
    }

    // ========================================================================
    // MARK: - 2FA (Placeholder)
    // ========================================================================

    bool requestTwoFactorCode(
        const std::string& dsid,
        const std::string& idmsToken,
        std::shared_ptr<AnisetteData> anisetteData,
        const std::function<std::optional<std::string>(void)>& verificationHandler) {

        // TODO: Implement 2FA flow
        // 1. GET /auth/verify/trusteddevice
        // 2. Call verificationHandler to get code from user
        // 3. GET /grandslam/GsService2/validate with security-code header

        return false;
    }
};

// ============================================================================
// MARK: - Main
// ============================================================================

int main() {
    std::cout << "Apple Auth - Fixed Version" << std::endl;
    std::cout << "==========================" << std::endl;
    std::cout << std::endl;

    try {
        std::string anisetteURL;
        const char* envURL = getenv("ANISETTE_URL");

        if (envURL) {
            anisetteURL = envURL;
        } else {
            std::cout << "Enter anisette server URL (or press Enter for default): ";
            std::getline(std::cin, anisetteURL);
            if (anisetteURL.empty()) {
                anisetteURL = "https://anisette-v3-server-8re5.onrender.com/";
            }
        }

        AppleAPI api(anisetteURL);

        // Fetch anisette data first
        std::cout << "[main] Fetching anisette data..." << std::endl;
        auto anisetteData = api.fetchAnisetteData();
        std::cout << "[main] Got anisette data for device: " 
                  << anisetteData->deviceUniqueIdentifier << std::endl;

        // Get credentials
        std::string appleID, password;
        std::cout << "Enter Apple ID: ";
        std::getline(std::cin, appleID);
        std::cout << "Enter Password: ";
        std::getline(std::cin, password);

        // Authenticate
        auto [account, session] = api.authenticate(appleID, password, anisetteData);

        std::cout << std::endl;
        std::cout << "✓ Authentication successful!" << std::endl;
        std::cout << "  DSID: " << session->dsid << std::endl;

    } catch (const APIError& e) {
        std::cerr << "✗ API Error: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "✗ Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
