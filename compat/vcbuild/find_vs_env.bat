@ECHO OFF
REM ================================================================
REM You can use either GCC (the default) or MSVC to build git
REM using the GIT-SDK command line tools.
REM        $ make
REM        $ make MSVC=1
REM
REM GIT-SDK BASH windows inherit environment variables with all of
REM the bin/lib/include paths for GCC.  It DOES NOT inherit values
REM for the corresponding MSVC tools.
REM
REM During normal (non-git) Windows development, you launch one
REM of the provided "developer command prompts" to set environment
REM variables for the MSVC tools.
REM
REM Therefore, to allow MSVC command line builds of git from BASH
REM and MAKE, we must blend these two different worlds.  This script
REM attempts to do that.
REM ================================================================
REM This BAT file starts in a plain (non-developer) command prompt,
REM searches for the "best" commmand prompt setup script, installs
REM it into the current CMD process, and exports the various MSVC
REM environment variables for use by MAKE.
REM
REM The output of this script should be written to a make "include
REM file" and referenced by the top-level Makefile.
REM
REM See "config.mak.uname" (look for compat/vcbuild/MSVC-DEFS-GEN).
REM ================================================================
REM The provided command prompts are custom to each VS release and
REM filled with lots of internal knowledge (such as Registry settings);
REM even their names vary by release, so it is not appropriate for us
REM to look inside them.  Rather, just run them in a subordinate
REM process and extract the settings we need.
REM ================================================================
REM
REM Current (VS2017 and beyond)
REM -------------------
REM Visual Studio 2017 introduced a new installation layout and
REM support for side-by-side installation of multiple versions of
REM VS2017.  Furthermore, these can all coexist with installations
REM of previous versions of VS (which have a completely different
REM layout on disk).
REM
REM VS2017 Update 2 introduced a "vswhere.exe" command:
REM https://github.com/Microsoft/vswhere
REM https://blogs.msdn.microsoft.com/heaths/2017/02/25/vswhere-available/
REM https://blogs.msdn.microsoft.com/vcblog/2017/03/06/finding-the-visual-c-compiler-tools-in-visual-studio-2017/
REM
REM VS2015
REM ------
REM Visual Studio 2015 uses the traditional VcVarsAll.
REM
REM Earlier Versions
REM ----------------
REM Currently unsupported.
REM
REM ================================================================
REM Note: Throughout this script we use "dir <path> && <cmd>" rather
REM than "if exist <path>" because of script problems with pathnames
REM containing spaces.
REM ================================================================

REM Sanitize PATH to prevent git-sdk paths from confusing "wmic.exe"
REM (called internally in some of the system BAT files).
SET PATH=%SystemRoot%\system32;%SystemRoot%;%SystemRoot%\System32\Wbem;

REM ================================================================

:current
   SET vs_where=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe
   dir "%vs_where%" >nul 2>nul && GOTO have_vs_where
   GOTO not_2017

:have_vs_where
   REM Try to use VsWhere to get the location of VsDevCmd.

   REM Keep VsDevCmd from cd'ing away.
   SET VSCMD_START_DIR=.

   REM Get the root of the VS product installation.
   FOR /F "usebackq tokens=*" %%i IN (`"%vs_where%" -latest -requires Microsoft.VisualStudio.Workload.NativeDesktop -property installationPath`) DO @SET vs_ip=%%i

   SET vs_devcmd=%vs_ip%\Common7\Tools\VsDevCmd.bat
   dir "%vs_devcmd%" >nul 2>nul && GOTO have_vs_devcmd
   GOTO not_2017

:have_vs_devcmd
   REM Use VsDevCmd to setup the environment of this process.
   REM Setup CL for building 64-bit apps using 64-bit tools.
   @call "%vs_devcmd%" -no_logo -arch=x64 -host_arch=x64

   SET tgt=%VSCMD_ARG_TGT_ARCH%

   SET mn=%VCToolsInstallDir%
   SET msvc_includes=-I"%mn%INCLUDE"
   SET msvc_libs=-L"%mn%lib\%tgt%"
   SET msvc_bin_dir=%mn%bin\Host%VSCMD_ARG_HOST_ARCH%\%tgt%

   SET sdk_dir=%WindowsSdkDir%
   SET sdk_ver=%WindowsSDKVersion%
   SET si=%sdk_dir%Include\%sdk_ver%
   SET sdk_includes=-I"%si%ucrt" -I"%si%um" -I"%si%shared"
   SET sl=%sdk_dir%lib\%sdk_ver%
   SET sdk_libs=-L"%sl%ucrt\%tgt%" -L"%sl%um\%tgt%"

   SET vs_ver=%VisualStudioVersion%

   GOTO print_vars

REM ================================================================

:not_2017
   REM See if VS2015 is installed.

   SET vs_2015_bat=C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\vcvarsall.bat
   dir "%vs_2015_bat%" >nul 2>nul && GOTO have_vs_2015
   GOTO not_2015

:have_vs_2015
   REM Use VcVarsAll like the "x64 Native" command prompt.
   REM Setup CL for building 64-bit apps using 64-bit tools.
   @call "%vs_2015_bat%" amd64

   REM Note that in VS2015 they use "x64" in some contexts and "amd64" in others.
   SET mn=C:\Program Files (x86)\Microsoft Visual Studio 14.0\VC\
   SET msvc_includes=-I"%mn%INCLUDE"
   SET msvc_libs=-L"%mn%lib\amd64"
   SET msvc_bin_dir=%mn%bin\amd64

   SET sdk_dir=%WindowsSdkDir%
   SET sdk_ver=%WindowsSDKVersion%
   SET si=%sdk_dir%Include\%sdk_ver%
   SET sdk_includes=-I"%si%ucrt" -I"%si%um" -I"%si%shared" -I"%si%winrt"
   SET sl=%sdk_dir%lib\%sdk_ver%
   SET sdk_libs=-L"%sl%ucrt\x64" -L"%sl%um\x64"

   SET vs_ver=%VisualStudioVersion%

   GOTO print_vars

REM ================================================================

:not_2015
   echo "ERROR: unsupported VS version (older than VS2015)" >&2
   EXIT /B 1

REM ================================================================

:print_vars
   REM Dump the essential vars to stdout to allow the main
   REM Makefile to include it.  See config.mak.uname.
   REM Include DOS-style and BASH-style path for bin dir.

   echo msvc_bin_dir=%msvc_bin_dir%
   SET X1=%msvc_bin_dir:C:=/C%
   SET X2=%X1:\=/%
   echo msvc_bin_dir_msys=%X2%

   echo msvc_includes=%msvc_includes%
   echo msvc_libs=%msvc_libs%

   echo sdk_includes=%sdk_includes%
   echo sdk_libs=%sdk_libs%

   echo vs_ver=%vs_ver%

   EXIT /B 0
