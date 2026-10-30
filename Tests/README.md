# External anisette native tests

Requires MSVC v142, a Windows 10 SDK, Python 3, and the headers/import library/DLL
from the `cpprestsdk.v142` NuGet package, version **2.10.15**. Extract the package
outside the source tree. `CppRestSdkRoot` below is its `build/native` directory
(containing `include/`, `x86/` and `x64/`). This is an isolated test dependency;
it does not change AltServer's existing dependency setup.

Run from a Visual Studio developer shell at the repository root:

```powershell
msbuild Tests\ExternalAnisetteTests.vcxproj /p:Configuration=Release /p:Platform=x64 /p:CppRestSdkRoot=C:\deps\cpprestsdk\build\native
& Tests\build\x64\ExternalAnisetteTests.exe
python Tests\external_anisette_http_tests.py Tests\build\x64\ExternalAnisetteTests.exe
```

Repeat using `Platform=Win32` and `Tests/build/Win32/ExternalAnisetteTests.exe` for
the 32-bit Windows target. The 18 native cases and 9 loopback HTTP cases use only
synthetic data. The timeout case deliberately waits for the SDK's 15-second limit.
No Apple account, attached device, installed AltServer, or public provider is used.

`AuthenticationCompile.vcxproj` additionally compiles the changed production
authentication translation unit against the repository's crypto/plist headers,
without linking an app or supplying Apple credentials:

```powershell
msbuild Tests\AuthenticationCompile.vcxproj /p:Configuration=Release /p:Platform=x64 /p:CppRestSdkRoot=C:\deps\cpprestsdk\build\native
```

This compile check and the adapter tests do not replace the full application's
build, link, and end-to-end sign-in/2FA/device tests.
