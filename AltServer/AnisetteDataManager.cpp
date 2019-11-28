#include "AnisetteDataManager.h"
#include <WinSock2.h>
#include <sstream>

#include "AnisetteData.h"
#include "AltServerApp.h"

#define id void*
#define SEL void*

typedef id(__cdecl* GETCLASSFUNC)(const char *name);
typedef id(__cdecl* REGISTERSELFUNC)(const char *name);
typedef id(__cdecl* SENDMSGFUNC)(id self, void *_cmd);
typedef id(__cdecl* SENDMSGFUNC_OBJ)(id self, void* _cmd, id parameter1);
typedef id(__cdecl* SENDMSGFUNC_INT)(id self, void* _cmd, int parameter1);

typedef id*(__cdecl* COPYMETHODLISTFUNC)(id cls, unsigned int* outCount);
typedef id(__cdecl* GETMETHODNAMEFUNC)(id method);
typedef const char*(__cdecl* GETSELNAMEFUNC)(SEL sel);
typedef id(__cdecl* GETOBJCCLASSFUNC)(id obj);

typedef id(__cdecl* CREATEACCOUNTFUNC)(void*, id username, id password, void*);
typedef id(__cdecl* COPYACCOUNTINFOFUNC)(void);
typedef id(__cdecl* COPYAUTHINFOFUNC)(void*, void*, void*, void*);

GETCLASSFUNC objc_getClass;
REGISTERSELFUNC sel_registerName;
SENDMSGFUNC objc_msgSend;
COPYMETHODLISTFUNC class_copyMethodList;
GETMETHODNAMEFUNC method_getName;
GETSELNAMEFUNC sel_getName;
GETOBJCCLASSFUNC object_getClass;

CREATEACCOUNTFUNC AOSAccountCreate;
COPYACCOUNTINFOFUNC AOSAccountCopyInfo;
COPYAUTHINFOFUNC AOSAccountCopyAuthInfo;

#define odslog(msg) { std::stringstream ss; ss << msg << std::endl; OutputDebugStringA(ss.str().c_str()); }

class ObjcObject
{
public:
	id isa;

	std::string description() const
	{
		id descriptionSEL = sel_registerName("description");
		id descriptionNSString = objc_msgSend((void*)this, descriptionSEL);

		id cDescriptionSEL = sel_registerName("UTF8String");
		const char* cDescription = ((const char* (*)(id, SEL))objc_msgSend)(descriptionNSString, cDescriptionSEL);

		std::string description(cDescription);
		return description;
	}
};

void convert_filetime(struct timeval* out_tv, const FILETIME* filetime)
{
	// Microseconds between 1601-01-01 00:00:00 UTC and 1970-01-01 00:00:00 UTC
	static const uint64_t EPOCH_DIFFERENCE_MICROS = 11644473600000000ull;

	// First convert 100-ns intervals to microseconds, then adjust for the
	// epoch difference
	uint64_t total_us = (((uint64_t)filetime->dwHighDateTime << 32) | (uint64_t)filetime->dwLowDateTime) / 10;
	total_us -= EPOCH_DIFFERENCE_MICROS;

	// Convert to (seconds, microseconds)
	out_tv->tv_sec = (time_t)(total_us / 1000000);
	out_tv->tv_usec = (long)(total_us % 1000000);
}

AnisetteDataManager* AnisetteDataManager::_instance = nullptr;

AnisetteDataManager* AnisetteDataManager::instance()
{
	if (_instance == 0)
	{
		_instance = new AnisetteDataManager();
	}

	return _instance;
}

AnisetteDataManager::AnisetteDataManager()
{
}

AnisetteDataManager::~AnisetteDataManager()
{
}

std::shared_ptr<AnisetteData> AnisetteDataManager::FetchAnisetteData()
{
	BOOL result = SetCurrentDirectoryA("C:\\Program Files (x86)\\Common Files\\Apple\\Apple Application Support");
	DWORD dwError = GetLastError();

	HINSTANCE objcLibrary = LoadLibrary(TEXT("C:\\Program Files (x86)\\Common Files\\Apple\\Apple Application Support\\objc.dll"));
	HINSTANCE foundationLibrary = LoadLibrary(TEXT("C:\\Program Files (x86)\\Common Files\\Apple\\Apple Application Support\\Foundation.dll"));
	HINSTANCE AOSKit = LoadLibrary(TEXT("C:\\Program Files (x86)\\Common Files\\Apple\\Internet Services\\AOSKit.dll"));

	dwError = GetLastError();

	if (objcLibrary == NULL)
	{
		return NULL;
	}

	objc_getClass = (GETCLASSFUNC)GetProcAddress(objcLibrary, "objc_getClass");
	sel_registerName = (REGISTERSELFUNC)GetProcAddress(objcLibrary, "sel_registerName");
	objc_msgSend = (SENDMSGFUNC)GetProcAddress(objcLibrary, "objc_msgSend");

	class_copyMethodList = (COPYMETHODLISTFUNC)GetProcAddress(objcLibrary, "class_copyMethodList");
	method_getName = (GETMETHODNAMEFUNC)GetProcAddress(objcLibrary, "method_getName");
	sel_getName = (GETSELNAMEFUNC)GetProcAddress(objcLibrary, "sel_getName");
	object_getClass = (GETOBJCCLASSFUNC)GetProcAddress(objcLibrary, "object_getClass");

	if (objc_getClass == NULL)
	{
		return NULL;
	}

	ObjcObject* NSString = (ObjcObject*)objc_getClass("NSString");
	id stringInit = sel_registerName("stringWithUTF8String:");

	/* One-Time Pasword */
	ObjcObject* dsidString = (ObjcObject*)((id(*)(id, SEL, const char*))objc_msgSend)(NSString, stringInit, "-2");
	ObjcObject* machineIDKey = (ObjcObject*)((id(*)(id, SEL, const char*))objc_msgSend)(NSString, stringInit, "X-Apple-MD-M");
	ObjcObject* otpKey = (ObjcObject*)((id(*)(id, SEL, const char*))objc_msgSend)(NSString, stringInit, "X-Apple-MD");

	ObjcObject* AOSUtilities = (ObjcObject*)objc_getClass("AOSUtilities");
	ObjcObject* headers = (ObjcObject*)((id(*)(id, SEL, id))objc_msgSend)(AOSUtilities, sel_registerName("retrieveOTPHeadersForDSID:"), dsidString);

	ObjcObject* machineID = (ObjcObject*)((id(*)(id, SEL, id))objc_msgSend)(headers, sel_registerName("objectForKey:"), machineIDKey);
	ObjcObject* otp = (ObjcObject*)((id(*)(id, SEL, id))objc_msgSend)(headers, sel_registerName("objectForKey:"), otpKey);

	odslog("OTP: " << otp->description() << " MachineID: " << machineID->description());

	/* Device Hardware */
	ObjcObject* AOSRequest = (ObjcObject*)objc_getClass("AOSRequest");
	ObjcObject* deviceDescription = (ObjcObject*)objc_msgSend(AOSRequest, sel_registerName("clientInfo"));

	FILETIME systemTime;
	GetSystemTimeAsFileTime(&systemTime);

	TIMEVAL date;
	convert_filetime(&date, &systemTime);

	std::string localUserID = "3610B4CFE5ACDB1974E73C90873045FBA85CCE62EEF83F363328FB14C40E8FC9";//"e4518186-506d-41b9-8eba-0ccc9bc9b881";
	std::string deviceUniqueIdentifier = AltServerApp::instance()->serverID();
	std::string deviceSerialNumber = "C02LKHBBFD57";

	auto anisetteData = std::make_shared<AnisetteData>(
		machineID->description(),
		otp->description(),
		localUserID,
		17106176,
		deviceUniqueIdentifier,
		deviceSerialNumber,
		//deviceDescription->description(), 
		"<MacBookPro15,1> <Mac OS X;10.15;19A583> <com.apple.AuthKit/1 (com.apple.dt.Xcode/15518)>",
		date,
		"en_US",
		"PST");

	odslog(*anisetteData);

	return anisetteData;
}