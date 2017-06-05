@echo off
setlocal EnableDelayedExpansion
goto init

:usage
echo Usage: %0 ^<repository^> ^<new_workdir^> [^<branch^>]
echo.
echo If UAC is enabled, use runas with /env argument:
echo   runas /env /user:administrator "git-new-workdir path\to\repo path\to\new\workdir"
echo.
echo /env is important. If omitted, created files will be not be accessible.
exit /b 127

:die
pause
exit /b 128

:init
if #%2# == ## goto usage

rem git="%COMSPEC%" /c git
set orig_git=%1
set new_workdir=%2
set branch=%3

rem want to make sure that what is pointed to has a .git directory ...
if not exist %orig_git% echo Not a git repository: %orig_git% && goto die

rem for /F %%d in ('%git% rev-parse --git-dir') do set git_dir=%%~fd
rem for /F "usebackq" %%d in (`cd %orig_dit% ^&^& echo %cd%`) do echo %%d
for /F "usebackq" %%d in (`cd %orig_git% ^&^& git rev-parse --git-dir`) do set git_dir=%%d

if %git_dir%==.git (
  set git_dir=%orig_git%\.git
) else if %git_dir%==. (
  set git_dir=%orig_git%
)

rem don't link to a configured bare repository
for /F "usebackq" %%d in (`git --git-dir="%git_dir%" config --bool --get core.bare`) do set is_bare=%%d
if %is_bare%==true echo %git_dir% has core.bare set to true, remove from %git_dir%/config to use %0 && goto die

rem don't link to a workdir
for %%d in ("%git_dir%\config") do set attribs=%%~ad
if "%attribs:~-1%"=="l" echo %orig_git% is a working directory only, please specify a complete repository. && goto die

rem don't recreate a workdir over an existing repository
if exist %new_workdir% echo destination directory '%new_workdir%' already exists. && goto die

rem make sure the links use full paths
for %%d in ("%git_dir%") do set git_dir=%%~fd

rem create the workdir
md %new_workdir%\.git || echo unable to create '%new_workdir%'! && goto die

rem create the links to the original repo.  explicitly exclude index, HEAD and
rem logs/HEAD from the list since they are purely related to the current working
rem directory, and should not be shared.
for %%d in (refs logs\refs objects info hooks remotes rr-cache svn) do (
  if exist "%git_dir%\%%d" (
    if %%d==logs\refs (
      if not exist "%new_workdir%\.git\%%d" md "%new_workdir%\.git\logs"
    )
    mklink /d "%new_workdir%\.git\%%d" "%git_dir%\%%d" || echo Failed to link '%%d' && goto die
  )
)
for %%f in (config packed-refs) do (
  if exist "%git_dir%\%%f" (
    mklink "%new_workdir%\.git\%%f" "%git_dir%\%%f" || echo Failed to link '%%d' && goto die
  )
)

rem now setup the workdir
rem copy the HEAD from the original repository as a default branch
copy "%git_dir%\HEAD" "%new_workdir%\.git\HEAD" >NUL

rem checkout the branch (either the same as HEAD from the original repository, or
rem the one that was asked for)
call git --git-dir="%new_workdir%\.git" --work-tree="%new_workdir%" checkout -f %branch% || echo Failed to checkout && goto die
