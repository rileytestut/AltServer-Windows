#pragma once

#include <windows.h>

#include <string>
#include <vector>
#include <debugapi.h>

#define altlog(msg) { std::wstringstream ss; ss << "[ALTLog] " << msg << std::endl; OutputDebugStringW(ss.str().c_str()); }

extern std::string StringFromWideString(std::wstring wideString);
extern std::wstring WideStringFromString(std::string string);

extern std::string make_uuid();
extern std::string temporary_directory();
extern std::vector<unsigned char> readFile(const char* filename);