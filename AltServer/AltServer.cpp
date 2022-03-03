// HelloWindowsDesktop.cpp
// compile with: /D_UNICODE /DUNICODE /DWIN32 /D_WINDOWS /c

#include <windows.h>
#include <windowsx.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>
#include <CommCtrl.h>
#include <ShObjIdl.h>

#include <strsafe.h>

#include "resource.h"

#include <fstream>
#include <iterator>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <codecvt>

#include <combaseapi.h>

#pragma comment( lib, "gdiplus.lib" ) 
#include <gdiplus.h> 

// AltSign
#include "DeviceManager.hpp"
#include "Error.hpp"

#include "AltServerApp.h"

#include <pplx/pplxtasks.h>

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

extern std::string StringFromWideString(std::wstring wideString);
extern std::wstring WideStringFromString(std::string string);

#define odslog(msg) { std::stringstream ss; ss << msg << std::endl; OutputDebugStringA(ss.str().c_str()); }

std::string make_uuid()
{
	GUID guid;
	CoCreateGuid(&guid);

	std::ostringstream os;
	os << std::hex << std::setw(8) << std::setfill('0') << guid.Data1;
	os << '-';
	os << std::hex << std::setw(4) << std::setfill('0') << guid.Data2;
	os << '-';
	os << std::hex << std::setw(4) << std::setfill('0') << guid.Data3;
	os << '-';
	os << std::hex << std::setw(2) << std::setfill('0') << static_cast<short>(guid.Data4[0]);
	os << std::hex << std::setw(2) << std::setfill('0') << static_cast<short>(guid.Data4[1]);
	os << '-';
	os << std::hex << std::setw(2) << std::setfill('0') << static_cast<short>(guid.Data4[2]);
	os << std::hex << std::setw(2) << std::setfill('0') << static_cast<short>(guid.Data4[3]);
	os << std::hex << std::setw(2) << std::setfill('0') << static_cast<short>(guid.Data4[4]);
	os << std::hex << std::setw(2) << std::setfill('0') << static_cast<short>(guid.Data4[5]);
	os << std::hex << std::setw(2) << std::setfill('0') << static_cast<short>(guid.Data4[6]);
	os << std::hex << std::setw(2) << std::setfill('0') << static_cast<short>(guid.Data4[7]);

	std::string s(os.str());
	return s;
}

std::string temporary_directory()
{
	wchar_t rawTempDirectory[1024];

	int length = GetTempPath(1024, rawTempDirectory);

	std::wstring wideString(rawTempDirectory);

	std::wstring_convert<std::codecvt_utf8<wchar_t>> conv1;
	std::string tempDirectory = conv1.to_bytes(wideString);

	return tempDirectory;
}

std::vector<unsigned char> readFile(const char* filename)
{
	// open the file:
	std::ifstream file(filename, std::ios::binary);

	// Stop eating new lines in binary mode!!!
	file.unsetf(std::ios::skipws);

	// get its size:
	std::streampos fileSize;

	file.seekg(0, std::ios::end);
	fileSize = file.tellg();
	file.seekg(0, std::ios::beg);

	// reserve capacity
	std::vector<unsigned char> vec;
	vec.reserve(fileSize);

	// read the data:
	vec.insert(vec.begin(),
		std::istream_iterator<unsigned char>(file),
		std::istream_iterator<unsigned char>());

	return vec;
}

// Global variables

// The main window class name.
static TCHAR szWindowClass[] = _T("DesktopApp");

// The string that appears in the application's title bar.
static TCHAR szTitle[] = _T("Windows Desktop Guided Tour Application");

HINSTANCE hInst;

// Forward declarations of functions included in this code module:
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
BOOL CALLBACK LoginDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam);

std::optional<std::string> _ipaFilepath;

int CALLBACK WinMain(
	_In_ HINSTANCE hInstance,
	_In_ HINSTANCE hPrevInstance,
	_In_ LPSTR     lpCmdLine,
	_In_ int       nCmdShow
)
{
	WNDCLASSEX wcex;

	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = hInstance;
	wcex.hIcon = LoadIcon(hInstance, IDI_APPLICATION);
	wcex.hCursor = LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = szWindowClass;
	wcex.hIconSm = LoadIcon(wcex.hInstance, IDI_APPLICATION);

	if (!RegisterClassEx(&wcex))
	{
		MessageBox(NULL,
			_T("Call to RegisterClassEx failed!"),
			_T("Windows Desktop Guided Tour"),
			NULL);

		return 1;
	}

	// Store instance handle in our global variable
	hInst = hInstance;

	// The parameters to CreateWindow explained:
	// szWindowClass: the name of the application
	// szTitle: the text that appears in the title bar
	// WS_OVERLAPPEDWINDOW: the type of window to create
	// CW_USEDEFAULT, CW_USEDEFAULT: initial position (x, y)
	// 500, 100: initial size (width, length)
	// NULL: the parent of this window
	// NULL: this application does not have a menu bar
	// hInstance: the first parameter from WinMain
	// NULL: not used in this application
	/*HWND hWnd = CreateWindow(
		szWindowClass,
		szTitle,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		500, 100,
		NULL,
		NULL,
		hInstance,
		NULL
	);*/

	HWND hWnd = CreateWindowEx(
		WS_EX_CLIENTEDGE,
		szWindowClass,
		szTitle,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		500,
		300,
		HWND_MESSAGE,
		NULL,
		hInstance,
		NULL
	);

	if (!hWnd)
	{
		MessageBox(NULL,
			_T("Call to CreateWindow failed!"),
			_T("Windows Desktop Guided Tour"),
			NULL);

		return 1;
	}

	// The parameters to ShowWindow explained:
	// hWnd: the value returned from CreateWindow
	// nCmdShow: the fourth parameter from WinMain
	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	AltServerApp::instance()->Start(hWnd, hInst);

	// Main message loop:
	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return (int)msg.wParam;
}

#define ID_MENU_LAUNCH_AT_LOGIN 104
#define ID_MENU_CLOSE 103
#define ID_MENU_CHECK_FOR_UPDATES 105

#define NO_DEVICES 200
#define FIRST_DEVICE 201

#define NO_APPS 300
#define FIRST_APP 301

std::shared_ptr<Device> _selectedDevice;

//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return

static HMENU hPopupMenu = NULL;

std::optional<std::string> OpenFile()
{
	std::optional<std::string> filepath = std::nullopt;

	HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
	if (!SUCCEEDED(hr))
	{
		return filepath;
	}

	IFileOpenDialog* pFileOpen;
	hr = CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_ALL, IID_IFileOpenDialog, reinterpret_cast<void**>(&pFileOpen));
	if (SUCCEEDED(hr))
	{
		COMDLG_FILTERSPEC rgSpec[] = {
			{ L"iOS Applications", L"*.ipa"},
		};
		pFileOpen->SetFileTypes(ARRAYSIZE(rgSpec), rgSpec);

		hr = pFileOpen->Show(NULL);
		if (SUCCEEDED(hr))
		{
			IShellItem* pItem;
			hr = pFileOpen->GetResult(&pItem);
			if (SUCCEEDED(hr))
			{
				PWSTR pszFilePath;
				hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

				if (SUCCEEDED(hr))
				{
					filepath = StringFromWideString(pszFilePath);
					CoTaskMemFree(pszFilePath);
				}

				pItem->Release();
			}
		}

		pFileOpen->Release();
	}

	CoUninitialize();

	return filepath;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	PAINTSTRUCT ps;
	HDC hdc;
	TCHAR greeting[] = _T("Click to Log In.");

	switch (message)
	{
	case (WM_USER + 1):
	{
		switch (lParam)
		{
		case WM_LBUTTONUP:
		{
			// Get the position of the cursor
			POINT pCursor;
			GetCursorPos(&pCursor);

			bool isSideloadingIPA = GetKeyState(VK_SHIFT) & 0x8000; // Must check high-order bits for pressed down/up value.

			HMENU installMenu = CreatePopupMenu();
			HMENU enableJITMenu = CreatePopupMenu();

			hPopupMenu = CreatePopupMenu();

			auto devices = DeviceManager::instance()->availableDevices();
            std::shared_ptr<std::map<std::shared_ptr<Device>, std::vector<InstalledApp>>> installedAppsByDevice(new std::map<std::shared_ptr<Device>, std::vector<InstalledApp>>);

            std::shared_ptr<std::mutex> mtx(new std::mutex);

			if (devices.size() == 0)
			{
				AppendMenu(installMenu, MF_STRING | MF_GRAYED | MF_DISABLED, NO_DEVICES, L"No Connected Devices");
				AppendMenu(enableJITMenu, MF_STRING | MF_GRAYED | MF_DISABLED, NO_DEVICES, L"No Connected Devices");
			}
			else
			{
				for (int i = 0; i < devices.size(); i++)
				{
					auto device = devices[i];
					auto name = WideStringFromString(device->name());

					AppendMenu(installMenu, MF_STRING, FIRST_DEVICE + i, name.c_str());

					HMENU appsMenu = CreatePopupMenu();
					AppendMenu(appsMenu, MF_STRING | MF_GRAYED | MF_DISABLED, NO_DEVICES, L"No Sideloaded Apps");
					AppendMenu(enableJITMenu, MF_STRING | MF_POPUP, (UINT)appsMenu, name.c_str());

					DeviceManager::instance()->FetchInstalledApps(device)
					.then([installedAppsByDevice, appsMenu, mtx, i, device](std::vector<InstalledApp> installedApps) {
						odslog("Fetched " << installedApps.size() << " apps for " << device->name() << "!");						
						RemoveMenu(appsMenu, 0, MF_BYPOSITION);

						std::sort(installedApps.begin(), installedApps.end());

						for (int j = 0; j < installedApps.size(); j++)
						{
							auto installedApp = installedApps[j];

							int index = FIRST_APP + j | (i << 12);
							AppendMenu(appsMenu, MF_STRING, index, WideStringFromString(installedApp.name()).c_str());
						}

						mtx->lock();
						(*installedAppsByDevice)[device] = installedApps;
						mtx->unlock();
					})
					.then([=](pplx::task<void> task) {
						try
						{
							task.get();
						}
						catch (Error& e)
						{
							odslog("Error fetching installed apps for " << StringFromWideString(name) << ": " << e.localizedDescription());
						}
						catch (std::exception& e)
						{
							odslog("Error fetching installed apps for " << StringFromWideString(name) << ": " << e.what());
						};
					});
				}
			}

			if (AltServerApp::instance()->automaticallyLaunchAtLogin())
			{
				AppendMenu(hPopupMenu, MF_STRING | MF_CHECKED, ID_MENU_LAUNCH_AT_LOGIN, L"Automatically Launch at Startup");
			}
			else
			{
				AppendMenu(hPopupMenu, MF_STRING, ID_MENU_LAUNCH_AT_LOGIN, L"Automatically Launch at Startup");
			}
			
			const wchar_t* installTitle = isSideloadingIPA ? L"Sideload .ipa" : L"Install AltStore";
			AppendMenu(hPopupMenu, MF_STRING | MF_POPUP, (UINT)installMenu, installTitle);
			AppendMenu(hPopupMenu, MF_STRING | MF_POPUP, (UINT)enableJITMenu, L"Enable JIT");

			AppendMenu(hPopupMenu, MF_STRING, ID_MENU_CHECK_FOR_UPDATES, L"Check for Updates...");
			AppendMenu(hPopupMenu, MF_STRING, ID_MENU_CLOSE, L"Close");

			// Popup the menu with cursor position as the coordinates to pop it up

			SetForegroundWindow(hWnd);

			int id = TrackPopupMenu(hPopupMenu, TPM_LEFTBUTTON | TPM_RIGHTALIGN | TPM_RETURNCMD, pCursor.x, pCursor.y, 0, hWnd, NULL);

			PostMessage(hWnd, WM_NULL, 0, 0);

			if (id == ID_MENU_CLOSE)
			{
				PostMessage(hWnd, WM_CLOSE, 0, 0);
			}
			else if (id == ID_MENU_LAUNCH_AT_LOGIN)
			{
				auto launchAtLogin = AltServerApp::instance()->automaticallyLaunchAtLogin();
				AltServerApp::instance()->setAutomaticallyLaunchAtLogin(!launchAtLogin);
			}
			else if (id == ID_MENU_CHECK_FOR_UPDATES)
			{
				AltServerApp::instance()->CheckForUpdates();
			}
			else if (id == NO_DEVICES || id == NO_APPS)
			{
				// Ignore
			}
			else if (id >= FIRST_APP)
			{
				// Enable JIT

				int appIndex = (id & 0xFFF) - FIRST_APP;
				int deviceIndex = (id >> 12);

				auto device = devices[deviceIndex];

				auto apps = (*installedAppsByDevice)[device];
				auto app = apps[appIndex];
				
				auto task = AltServerApp::instance()->EnableJIT(app, device);

				try {
					task.get();
				}
				catch (Error& error)
				{
					odslog("Error: " << error.domain() << " (" << error.code() << ").")
				}
				catch (std::exception& exception)
				{
					odslog("Exception: " << exception.what());
				}
			}
			else if (id >= FIRST_DEVICE)
			{
				// Install app

				int index = id - FIRST_DEVICE;

				auto device = devices[index];
				_selectedDevice = device;

				if (isSideloadingIPA)
				{
					_ipaFilepath = OpenFile();

					if (!_ipaFilepath.has_value())
					{
						break;
					}
				}
				else
				{
					_ipaFilepath = std::nullopt;
				}

				// Show Auth dialog.
				int result = DialogBox(NULL, MAKEINTRESOURCE(ID_LOGIN), hWnd, LoginDlgProc);
			}
		}
		default: break;
		}

		break;
	}
	case WM_PAINT:
		hdc = BeginPaint(hWnd, &ps);

		// Here your application is laid out.
		// For this introduction, we just print out "Hello, Windows desktop!"
		// in the top left corner.
		TextOut(hdc,
			5, 5,
			greeting, _tcslen(greeting));
		// End application-specific layout section.

		EndPaint(hWnd, &ps);
		break;

	case WM_DESTROY:
		AltServerApp::instance()->Stop();
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
		break;
	}

	return 0;
}

BOOL CALLBACK LoginDlgProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	HWND appleIDTextField = GetDlgItem(hwnd, IDC_EDIT1);
	HWND passwordTextField = GetDlgItem(hwnd, IDC_EDIT2);
	HWND installButton = GetDlgItem(hwnd, IDOK);

	switch (Message)
	{
	case WM_INITDIALOG:
	{
		Edit_SetCueBannerText(appleIDTextField, _T("Apple ID"));
		Edit_SetCueBannerText(passwordTextField, _T("Password"));

		Button_Enable(installButton, false);

		break;
	}

	case WM_CTLCOLORSTATIC:
	{
		if (GetDlgCtrlID((HWND)lParam) == IDC_DESCRIPTION)
		{
			HBRUSH success = (HBRUSH)GetStockObject(HOLLOW_BRUSH);
			SetBkMode((HDC)wParam, TRANSPARENT);
			return (BOOL)success;
		}

		break;
	}

	case WM_COMMAND:
		switch (HIWORD(wParam))
		{
		case EN_CHANGE:
		{
			/*PostMessage(hWnd, WM_CLOSE, 0, 0);
			break;*/

			int appleIDLength = Edit_GetTextLength(appleIDTextField);
			int passwordLength = Edit_GetTextLength(passwordTextField);

			if (appleIDLength == 0 || passwordLength == 0)
			{
				Button_Enable(installButton, false);
			}
			else
			{
				Button_Enable(installButton, true);
			}

			break;
		}
		}

		switch (LOWORD(wParam))
		{
		case IDOK:
		{
			wchar_t appleID[512];
			wchar_t password[512];

			Edit_GetText(appleIDTextField, appleID, 512);
			Edit_GetText(passwordTextField, password, 512);
			
			auto task = AltServerApp::instance()->InstallApplication(_ipaFilepath, _selectedDevice, StringFromWideString(appleID), StringFromWideString(password));

			EndDialog(hwnd, IDOK);

			try
			{
				task.get();
			}
			catch (Error& error)
			{
				odslog("Error: " << error.domain() << " (" << error.code() << ").")
			}
			catch (std::exception& exception)
			{
				odslog("Exception: " << exception.what());
			}

			odslog("Finished!");

			break;
		}

		case IDCANCEL:
			EndDialog(hwnd, IDCANCEL);
			break;
		}

		default:
			return FALSE;
	}
	return TRUE;
}