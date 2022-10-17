# sdl2 cmake project-config input for ./configure scripts

set(prefix "/opt/local/i686-w64-mingw32") 
set(exec_prefix "${prefix}")
set(libdir "${exec_prefix}/lib")
set(includedir "${prefix}/include")
set(SDL2_PREFIX "${prefix}")
set(SDL2_EXEC_PREFIX "${exec_prefix}")
set(SDL2_LIBDIR "${libdir}")
set(SDL2_INCLUDE_DIRS "${includedir}/SDL2")
set(SDL2_LIBRARIES "-L${SDL2_LIBDIR}  -lmingw32 -lSDL2main -lSDL2 -mwindows")
string(STRIP "${SDL2_LIBRARIES}" SDL2_LIBRARIES)

if(NOT TARGET SDL2::SDL2)
  # provide SDL2::SDL2, SDL2::SDL2main and SDL2::SDL2-static targets, like SDL2Config.cmake does, for compatibility

  # Remove -lSDL2 as that is handled by CMake, note the space at the end so it does not replace e.g. -lSDL2main
  # This may require "libdir" beeing set (from above)
  string(REPLACE "-lSDL2 " "" SDL2_EXTRA_LINK_FLAGS " -lmingw32 -lSDL2main -lSDL2 -mwindows ")
  # also get rid of -lSDL2main, if you want to link against that use both SDL2::SDL2main and SDL2::SDL2 (in that order)
  # (SDL2Config.cmake has the same behavior)
  string(REPLACE "-lSDL2main" "" SDL2_EXTRA_LINK_FLAGS ${SDL2_EXTRA_LINK_FLAGS})
  string(STRIP "${SDL2_EXTRA_LINK_FLAGS}" SDL2_EXTRA_LINK_FLAGS)
  string(REPLACE "-lSDL2 " "" SDL2_EXTRA_LINK_FLAGS_STATIC " -Wl,--dynamicbase -Wl,--nxcompat -lm -ldinput8 -ldxguid -ldxerr8 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lshell32 -lsetupapi -lversion -luuid ")
  string(STRIP "${SDL2_EXTRA_LINK_FLAGS_STATIC}" SDL2_EXTRA_LINK_FLAGS_STATIC)

if(WIN32 AND NOT MSVC)
  # MINGW needs very special handling, because the link order must be exactly -lmingw32 -lSDL2main -lSDL2
  # for it to work at all (and -mwindows somewhere); a normal SHARED IMPORTED or STATIC IMPORTED library always puts itself first
  # so handle this like a header-only lib and put everything in INTERFACE_LINK_LIBRARIES

  add_library(SDL2::SDL2 INTERFACE IMPORTED)
  set_target_properties(SDL2::SDL2 PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
    INTERFACE_LINK_LIBRARIES "-L${SDL2_LIBDIR} -lSDL2")

  add_library(SDL2::SDL2main INTERFACE IMPORTED)
  set_target_properties(SDL2::SDL2main PROPERTIES
    INTERFACE_LINK_LIBRARIES "-L${SDL2_LIBDIR} -lmingw32 -lSDL2main -mwindows")

else() # (not WIN32) or MSVC

  add_library(SDL2::SDL2 SHARED IMPORTED)
  set_target_properties(SDL2::SDL2 PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
    IMPORTED_LOCATION "${SDL2_LIBDIR}/${CMAKE_SHARED_LIBRARY_PREFIX}SDL2${CMAKE_SHARED_LIBRARY_SUFFIX}")

  if(MSVC)
    # This file is generated when building SDL2 with autotools and MinGW, and MinGW/dlltool
    # isn't able to generate .lib files that are usable by recent MSVC versions 
    # (something about "module unsafe for SAFESEH"; SAFESEH is enabled by default in MSVC).
    # The .lib file for SDL2.dll *could* be generated with `gendef SDL2.dll` and then
    # `lib.exe /machine:x86 /def:SDL2.def /out:SDL2.lib` (or /machine:amd64)
    # but that requires lib.exe from a Visual Studio installation - and that still doesn't
    # give you a static SDL2main.lib with SAFESEH support that you'll need (unless you don't use SDL2main)
    # Note that when building SDL2 with CMake and MSVC, the result works with both MinGW and MSVC.

    message(FATAL_ERROR, "This build of libSDL2 only supports MinGW, not MSVC (Visual C++), because it lacks .lib files!")
    # MSVC needs SDL2.lib set as IMPORTED_IMPLIB to link against (comment out message() call above if you added SDL2.lib yourself)
    set_target_properties(SDL2::SDL2 PROPERTIES IMPORTED_IMPLIB "${SDL2_LIBDIR}/SDL2.lib")
  else()
    # this mustn't be set for MSVC, so do it here in an extra call here
    set_target_properties(SDL2::SDL2 PROPERTIES INTERFACE_LINK_LIBRARIES  "${SDL2_EXTRA_LINK_FLAGS}")
  endif()

  add_library(SDL2::SDL2main STATIC IMPORTED)
  set_target_properties(SDL2::SDL2main PROPERTIES
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
    IMPORTED_LOCATION "${SDL2_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}SDL2main${CMAKE_STATIC_LIBRARY_SUFFIX}")

endif() # (not WIN32) or MSVC

  add_library(SDL2::SDL2-static STATIC IMPORTED)
  set_target_properties(SDL2::SDL2-static PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
    IMPORTED_LINK_INTERFACE_LANGUAGES "C"
    IMPORTED_LOCATION "${SDL2_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}SDL2${CMAKE_STATIC_LIBRARY_SUFFIX}"
    INTERFACE_LINK_LIBRARIES "${SDL2_EXTRA_LINK_FLAGS_STATIC}")

endif() # NOT TARGET SDL2::SDL2
