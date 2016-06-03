Instructions for building Git for Windows using VS2015.
================================================================

Installing third-party dependencies:
====================================

[1] Install nuget.exe somewhere on your system and add it to your PATH.
    https://docs.nuget.org/consume/command-line-reference
    https://dist.nuget.org/index.html

[2] Download required nuget packages for third-party libraries.
    Using a terminal window, type:

        make -C compat/vcbuild

    This will download the packages, unpack them into GEN.PKGS,
    and populate the {include, lib, bin} directories in GEN.DEPS.


Building Git for Windows using VS2015:
======================================

[3] Build 64-bit version of Git for Windows.
    Using a terminal window:

        make MSVC=1 DEBUG=1


[4] Add compat/vcbuild/GEN.DEPS/bin to your PATH.

[5] You should then be able to run the test suite and any interactive
    commands.

[6] To debug/profile in VS, open the git.exe in VS and run/debug
    it.  (Be sure to add GEN.DEPS/bin to the PATH in the debug
    dialog.)


TODO List:
==========

[A] config.mak.uname currently contains hard-coded paths
    to the various MSVC and SDK libraries for the 64-bit
    version of the compilers and libaries.

    See: SANE_TOOL_PATH, MSVC_DEPS, MSVC_SDK*, MSVC_VCDIR.

    Long term, we need to figure out how to properly import
    values for %VCINSTALLDIR%, %LIB%, %LIBPATH%, and the
    other values normally set by "vsvars32.bat" when a
    developer command prompt is started.  This would also
    allow us to switch between 32- and 64-bit tool chains.

[B] Currently, we leave the third-party DLLs we reference in
    "compat/vcbuild/GEN.DEPS/bin".  We need an installer
    step to move them next to git.exe (or into libexec/git-core).

[C] We need to build SLN or VCPROJ files.
