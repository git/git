@ECHO OFF
REM ================================================================
REM This script installs the "vcpkg" source package manager and uses
REM it to build the third-party libraries that git requires when it
REM is built using MSVC.
REM
REM [1] Install VCPKG.
REM     [a] Create <root>/compat/vcbuild/vcpkg/
REM     [b] Download "vcpkg".
REM     [c] Compile using the currently installed version of VS.
REM     [d] Create <root>/compat/vcbuild/vcpkg/vcpkg.exe
REM
REM [2] Install third-party libraries.
REM     [a] Download each (which may also install CMAKE).
REM     [b] Compile in RELEASE mode and install in:
REM         vcpkg/installed/<arch>/{bin,lib}
REM     [c] Compile in DEBUG mode and install in:
REM         vcpkg/installed/<arch>/debug/{bin,lib}
REM     [d] Install headers in:
REM         vcpkg/installed/<arch>/include
REM
REM [3] Create a set of MAKE definitions for the top-level
REM     Makefile to allow "make MSVC=1" to find the above
REM     third-party libraries.
REM     [a] Write vcpkg/VCPGK-DEFS
REM
REM https://blogs.msdn.microsoft.com/vcblog/2016/09/19/vcpkg-a-tool-to-acquire-and-build-c-open-source-libraries-on-windows/
REM https://github.com/Microsoft/vcpkg
REM https://vcpkg.readthedocs.io/en/latest/
REM ================================================================

	SETLOCAL EnableDelayedExpansion

	@FOR /F "delims=" %%D IN ("%~dp0") DO @SET cwd=%%~fD
	cd %cwd%

	dir vcpkg\vcpkg.exe >nul 2>nul && GOTO :install_libraries

	git.exe version 2>nul
	IF ERRORLEVEL 1 (
	echo "***"
	echo "Git not found. Please adjust your CMD path or Git install option."
	echo "***"
	EXIT /B 1 )

	echo Fetching vcpkg in %cwd%vcpkg
	git.exe clone https://github.com/Microsoft/vcpkg vcpkg
	IF ERRORLEVEL 1 ( EXIT /B 1 )

	cd vcpkg
	echo Building vcpkg
	powershell -exec bypass scripts\bootstrap.ps1
	IF ERRORLEVEL 1 ( EXIT /B 1 )

	echo Successfully installed %cwd%vcpkg\vcpkg.exe

:install_libraries
	SET arch=x64-windows

	echo Installing third-party libraries...
	FOR %%i IN (zlib expat libiconv openssl libssh2 curl) DO (
	    cd %cwd%vcpkg
	    IF NOT EXIST "packages\%%i_%arch%" CALL :sub__install_one %%i
	    IF ERRORLEVEL 1 ( EXIT /B 1 )
	)

:install_defines
	cd %cwd%
	SET inst=%cwd%vcpkg\installed\%arch%

	echo vcpkg_inc=-I"%inst%\include">VCPKG-DEFS
	echo vcpkg_rel_lib=-L"%inst%\lib">>VCPKG-DEFS
	echo vcpkg_rel_bin="%inst%\bin">>VCPKG-DEFS
	echo vcpkg_dbg_lib=-L"%inst%\debug\lib">>VCPKG-DEFS
	echo vcpkg_dbg_bin="%inst%\debug\bin">>VCPKG-DEFS

	EXIT /B 0


:sub__install_one
	echo     Installing package %1...

	.\vcpkg.exe install %1:%arch%
	IF ERRORLEVEL 1 ( EXIT /B 1 )

	echo     Finished %1
	goto :EOF
