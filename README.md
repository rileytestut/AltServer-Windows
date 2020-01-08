# AltServer-Windows on VS 2019
In order to compile AltServer on VS 2019 there a few things required.

1. clone the repository **recursively** and checkout the experimental branch
1. install and bootstrap [VCPKG](https://github.com/microsoft/vcpkg) 
1. with vckpg install the following packages (it takes reaaaaaally long): cpprestsdk dirent mdnsresponder
1. in the folder (root)\Dependencies\libmobiledevice-vs run get-source to download the source code
1. download an compile [WinSparkle](https://github.com/vslavik/winsparkle); you will need to copy the generated LIB in
(root)\Dependencies\release
1. in the project LDID change the language standard from c++14 to c++17, turn off intellisense errors (misleading) and fix the two
errors fixed in [this PR](https://github.com/rileytestut/AltServer-Windows/pull/1)
1. add the following import in Connection.cpp: `#include <codecvt>`
1. in DeviceManager.cpp replace `idevice_new` call with `idevice_new_with_options(&device, udid, IDEVICE_LOOKUP_USBMUX);`
1. in the same file replace `idevice_new_ignore_network` with `idevice_new_with_options(&device, udid, (idevice_options)(IDEVICE_LOOKUP_USBMUX | IDEVICE_LOOKUP_NETWORK));`

The code should now compile, download the certificate, sign and install (still investigating why this steps fails)
