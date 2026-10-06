@echo off
rem Builds dirtyrammark.sys (the CMake target dirtyrammark_driver runs this) without the WDK installed: only the
rem kernel-mode headers and libs are needed (the Microsoft.Windows.WDK.x64 NuGet package, unpacked somewhere; pass its
rem content root as %WDK_ROOT%, i.e. the directory that contains Include\ and Lib\). The Windows SDK (shared headers) and
rem MSVC are expected to be installed.
rem
rem usage: make.bat [output dir] [intermediate dir]
rem
rem Run from a "x64 Native Tools Command Prompt". Then the driver is signed with a throw-away test certificate (see
rem sign.ps1); it loads only when test signing is on (bcdedit /set testsigning on, Secure Boot off).
setlocal
rem CL_DIR, CL_INCLUDE (set by CMake): MSVC bin and include directories, when not run from a Native Tools prompt
if not "%CL_DIR%"=="" set PATH=%CL_DIR:/=\%;%PATH%
if not "%CL_INCLUDE%"=="" set INCLUDE=%CL_INCLUDE:/=\%;%INCLUDE%
rem no MSVC on PATH (e.g. the build is driven by clang): load the Visual Studio x64 environment found with vswhere
where cl >nul 2>nul || call :vsenv || exit /b 1
if "%WDK_ROOT%"=="" (
    echo set WDK_ROOT to the unpacked Microsoft.Windows.WDK.x64 package ^(the folder with Include and Lib^)
    exit /b 1
)
set WDK_ROOT=%WDK_ROOT:/=\%
if "%WDK_VERSION%"=="" for /d %%V in ("%WDK_ROOT%\Include\10.*") do if exist "%%V\km\ntddk.h" set WDK_VERSION=%%~nxV
if "%WDK_VERSION%"=="" (
    echo no kernel-mode headers found in %WDK_ROOT%\Include
    exit /b 1
)
rem the NuGet WDK package has no ntdef.h & co: take the shared headers from the installed Windows SDK
for /d %%V in ("%ProgramFiles(x86)%\Windows Kits\10\Include\10.*") do if exist "%%V\shared\ntdef.h" set SDK_INC=%%V
set OUT=%~1
if "%OUT%"=="" set OUT=%~dp0..\build-driver
set OUT=%OUT:/=\%
set OBJ=%~2
if "%OBJ%"=="" set OBJ=%~dp0obj
set OBJ=%OBJ:/=\%
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%OBJ%" mkdir "%OBJ%"

cl /nologo /c /kernel /W4 /WX /O2 /GS- /Gy /Zl /Zi /Fo"%OBJ%\\" /Fd"%OBJ%\vc.pdb" ^
   /D_AMD64_ /DAMD64 /D_WIN64 /DNTDDI_VERSION=0x0A000007 /D_WIN32_WINNT=0x0A00 /DPOOL_NX_OPTIN=1 ^
   /I"%WDK_ROOT%\Include\%WDK_VERSION%\km" /I"%WDK_ROOT%\Include\%WDK_VERSION%\shared" /I"%SDK_INC%\shared" /I"%SDK_INC%\ucrt" ^
   "%~dp0dirtyrammark.c" || exit /b 1

link /nologo /DRIVER /SUBSYSTEM:NATIVE /ENTRY:DriverEntry /NODEFAULTLIB /RELEASE /OPT:REF /OPT:ICF /MERGE:_TEXT=.text ^
   /OUT:"%OUT%\dirtyrammark.sys" /PDB:"%OBJ%\dirtyrammark.pdb" ^
   "%OBJ%\dirtyrammark.obj" ^
   /LIBPATH:"%WDK_ROOT%\Lib\%WDK_VERSION%\km\x64" ntoskrnl.lib hal.lib wdmsec.lib BufferOverflowFastFailK.lib || exit /b 1

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0sign.ps1" "%OUT%\dirtyrammark.sys" || exit /b 1

:vsenv
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo cl.exe not found and vswhere is missing & exit /b 1)
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%I"
if "%VSDIR%"=="" (echo cl.exe not found: no Visual Studio with the C++ tools & exit /b 1)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
exit /b 0
