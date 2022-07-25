@ECHO OFF
REM ================================================================
REM This script is an optional step. It copies the *.dll and *.pdb
REM files (created by vcpkg_install.bat) into the top-level directory
REM of the repo so that you can type "./git.exe" and find them without
REM having to fixup your PATH.
REM
REM NOTE: Because the names of some DLL files change between DEBUG and
REM NOTE: RELEASE builds when built using "vcpkg.exe", you will need
REM NOTE: to copy up the corresponding version.
REM ================================================================

	SETLOCAL EnableDelayedExpansion

	@FOR /F "delims=" %%D IN ("%~dp0") DO @SET cwd=%%~fD
	cd %cwd%

	SET arch=x64-windows
	SET inst=%cwd%vcpkg\installed\%arch%

	IF [%1]==[release] (
		echo Copying RELEASE mode DLLs to repo root...
	) ELSE IF [%1]==[debug] (
		SET inst=%inst%\debug
		echo Copying DEBUG mode DLLs to repo root...
	) ELSE (
		echo ERROR: Invalid argument.
		echo Usage: %~0 release
		echo Usage: %~0 debug
		EXIT /B 1
	)

	xcopy /e/s/v/y %inst%\bin\*.dll ..\..\
	xcopy /e/s/v/y %inst%\bin\*.pdb ..\..\

	xcopy /e/s/v/y %inst%\bin\*.dll ..\..\t\helper\
	xcopy /e/s/v/y %inst%\bin\*.pdb ..\..\t\helper\

	EXIT /B 0
