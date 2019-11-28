//
//  Archiver.cpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/12/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#include <boost/filesystem.hpp>

#include "Archiver.hpp"
#include "Error.hpp"

extern "C" {
#include "zip.h"
#include "unzip.h"
}

#ifdef _WIN32
#include <io.h>
#define access    _access_s
#else
#include <sys/stat.h>
#include <unistd.h>
 #include <dirent.h>
#endif

int ALTReadBufferSize = 8192;
int ALTMaxFilenameLength = 512;

#ifdef _Win32
char ALTDirectoryDeliminator = '\\';
#else
char ALTDirectoryDeliminator = '/';
#endif

#define READ_BUFFER_SIZE 8192
#define MAX_FILENAME 512

namespace fs = boost::filesystem;

static bool endsWith(const std::string& str, const std::string& suffix)
{
    return str.size() >= suffix.size() && 0 == str.compare(str.size()-suffix.size(), suffix.size(), suffix);
}

static bool startsWith(const std::string& str, const std::string& prefix)
{
    return str.size() >= prefix.size() && 0 == str.compare(0, prefix.size(), prefix);
}


std::string UnzipAppBundle(std::string filepath, std::string outputDirectory)
{
    if (outputDirectory[outputDirectory.size() - 1] != ALTDirectoryDeliminator)
    {
        outputDirectory += ALTDirectoryDeliminator;
    }
    
    unzFile zipFile = unzOpen(filepath.c_str());
    if (zipFile == NULL)
    {
        throw ArchiveError(ArchiveErrorCode::NoSuchFile);
    }
    
    FILE *outputFile = nullptr;
    
    auto finish = [&outputFile, &zipFile](void)
    {
        if (outputFile != nullptr)
        {
            fclose(outputFile);
        }
        
        unzCloseCurrentFile(zipFile);
        unzClose(zipFile);
    };
    
    unz_global_info zipInfo;
    if (unzGetGlobalInfo(zipFile, &zipInfo) != UNZ_OK)
    {
        finish();
        throw ArchiveError(ArchiveErrorCode::CorruptFile);
    }
    
    fs::path payloadDirectoryPath = fs::path(outputDirectory).append("Payload");
    if (!fs::exists(payloadDirectoryPath))
    {
        fs::create_directory(payloadDirectoryPath);
    }
    
    char buffer[ALTReadBufferSize];
    
    for (int i = 0; i < zipInfo.number_entry; i++)
    {
        unz_file_info info;
        char cFilename[ALTMaxFilenameLength];
        
        if (unzGetCurrentFileInfo(zipFile, &info, cFilename, ALTMaxFilenameLength, NULL, 0, NULL, 0) != UNZ_OK)
        {
            finish();
            throw ArchiveError(ArchiveErrorCode::Unknown);
        }
        
        std::string filename(cFilename);
        if (startsWith(filename, "__MACOSX"))
        {
            if (i + 1 < zipInfo.number_entry)
            {
                if (unzGoToNextFile(zipFile) != UNZ_OK)
                {
                    finish();
                    throw ArchiveError(ArchiveErrorCode::Unknown);
                }
            }
            
            continue;
        }
        
        boost::filesystem::path filepath = fs::path(outputDirectory).append(filename);
        boost::filesystem::path parentDirectory = (filename[filename.size() - 1] == ALTDirectoryDeliminator) ? filepath.parent_path().parent_path() : filepath.parent_path();
        
        if (!fs::exists(parentDirectory))
        {
            fs::create_directory(parentDirectory);
        }
        
        if (filename[filename.size() - 1] == ALTDirectoryDeliminator)
        {
            // Directory
            fs::create_directory(filepath);
        }
        else
        {
            // File
            if (unzOpenCurrentFile(zipFile) != UNZ_OK)
            {
                finish();
                throw ArchiveError(ArchiveErrorCode::Unknown);
            }
            
            outputFile = fopen(filepath.c_str(), "wb");
            if (outputFile == NULL)
            {
                finish();
                throw ArchiveError(ArchiveErrorCode::UnknownWrite);
            }
            
            int result = UNZ_OK;
            
            do
            {
                result = unzReadCurrentFile(zipFile, buffer, ALTReadBufferSize);
                
                if (result < 0)
                {
                    finish();
                    throw ArchiveError(ArchiveErrorCode::Unknown);
                }
                
                size_t count = fwrite(buffer, result, 1, outputFile);
                if (result > 0 && count != 1)
                {
                    finish();
                    throw ArchiveError(ArchiveErrorCode::UnknownWrite);
                }
                
            } while (result > 0);
            
            short permissions = (info.external_fa >> 16) & 0x01FF;
            chmod(filepath.c_str(), permissions);
            
            fclose(outputFile);
            outputFile = NULL;
        }
        
        unzCloseCurrentFile(zipFile);
                
        if (i + 1 < zipInfo.number_entry)
        {
            if (unzGoToNextFile(zipFile) != UNZ_OK)
            {
                finish();
                throw ArchiveError(ArchiveErrorCode::Unknown);
            }
        }
    }
    
    for (auto & p : boost::filesystem::directory_iterator(payloadDirectoryPath))
    {
        auto filename = p.path().filename().string();
        
        auto lowercaseFilename = filename;
        std::transform(lowercaseFilename.begin(), lowercaseFilename.end(), lowercaseFilename.begin(), [](unsigned char c) {
            return std::tolower(c);
        });
        
        if (!endsWith(lowercaseFilename, ".app"))
        {
            continue;
        }
        
        auto appBundlePath = payloadDirectoryPath;
        appBundlePath.append(filename);
        
        auto outputPath = outputDirectory;
        outputPath.append(filename);
        
        if (boost::filesystem::exists(outputPath))
        {
            boost::filesystem::remove(outputPath);
        }
        
        boost::filesystem::rename(appBundlePath, outputPath);
        
        finish();
        
        boost::filesystem::remove(payloadDirectoryPath);
        
        return outputPath;
    }
    
    throw SignError(SignError(SignErrorCode::MissingAppBundle));
}


void WriteFileToZipFile(zipFile *zipFile, fs::path filepath, fs::path relativePath)
{
    bool isDirectory = fs::is_directory(filepath);
    
    fs::path filename = relativePath;
    
    zip_fileinfo fileInfo = {};
    
    char *bytes = nullptr;
    unsigned int fileSize = 0;
    
    if (isDirectory)
    {
        // Remove leading directory slash.
        if (filename.string()[0] == ALTDirectoryDeliminator)
        {
            filename = std::string(filename.string().begin() + 1, filename.string().end());
        }
        
        // Add trailing directory slash.
        if (filename.string()[filename.size() - 1] != ALTDirectoryDeliminator)
        {
            filename = filename.string() + ALTDirectoryDeliminator;
        }
    }
    else
    {
        fs::file_status status = fs::status(filepath);
        
        short permissions = status.permissions();
        long shiftedPermissions = 0100000 + permissions;
        
        uLong permissionsLong = (uLong)shiftedPermissions;
        
        fileInfo.external_fa = (unsigned int)(permissionsLong << 16L);
        
        std::ifstream ifs(filepath.string());
        std::vector<char> data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
        
        bytes = data.data();
        fileSize = (unsigned int)data.size();
    }
    
    if (zipOpenNewFileInZip(*zipFile, filename.c_str(), &fileInfo, NULL, 0, NULL, 0, NULL, Z_DEFLATED, Z_DEFAULT_COMPRESSION) != ZIP_OK)
    {
        throw ArchiveError(ArchiveErrorCode::UnknownWrite);
    }
    
    if (zipWriteInFileInZip(*zipFile, bytes, fileSize) != ZIP_OK)
    {
        zipCloseFileInZip(*zipFile);
        throw ArchiveError(ArchiveErrorCode::UnknownWrite);
    }
}

std::string ZipAppBundle(std::string filepath)
{
    fs::path appBundlePath = filepath;
    
    auto appBundleFilename = appBundlePath.filename();
    auto appName = appBundlePath.filename().stem().string();
    
    auto ipaName = appName + ".ipa";
    auto ipaPath = appBundlePath.remove_filename().append(ipaName);
    
    if (fs::exists(ipaPath))
    {
        fs::remove(ipaPath);
    }
    
    zipFile zipFile = zipOpen(ipaPath.c_str(), APPEND_STATUS_CREATE);
    if (zipFile == nullptr)
    {
        throw ArchiveError(ArchiveErrorCode::UnknownWrite);
    }
    
    fs::path payloadDirectory = "Payload";
    fs::path appBundleDirectory = payloadDirectory.append(appBundleFilename.string());
    
    fs::path rootPath = fs::relative("", appBundleDirectory);
    
    for (auto& entry: fs::recursive_directory_iterator(rootPath))
    {
        auto filepath = entry.path();
        auto relativePath = entry.path().relative_path();
        
        WriteFileToZipFile(&zipFile, filepath, relativePath);
    }
    
    WriteFileToZipFile(&zipFile, payloadDirectory, payloadDirectory);
    WriteFileToZipFile(&zipFile, appBundleDirectory, appBundleDirectory);
    
    zipClose(zipFile, NULL);
    
    return ipaPath.string();
}
