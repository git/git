# Install script for directory: /workspaces/git/contrib/buildsystems

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "/usr/local")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Debug")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Install shared libraries without execute permission?
if(NOT DEFINED CMAKE_INSTALL_SO_NO_EXE)
  set(CMAKE_INSTALL_SO_NO_EXE "1")
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/git" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/git")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/git"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "/workspaces/git/build/git")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/git" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/git")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/git")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-daemon" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-daemon")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-daemon"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/libexec/git-core" TYPE EXECUTABLE FILES "/workspaces/git/build/git-daemon")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-daemon" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-daemon")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-daemon")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-http-backend" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-http-backend")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-http-backend"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/libexec/git-core" TYPE EXECUTABLE FILES "/workspaces/git/build/git-http-backend")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-http-backend" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-http-backend")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-http-backend")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-sh-i18n--envsubst" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-sh-i18n--envsubst")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-sh-i18n--envsubst"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/libexec/git-core" TYPE EXECUTABLE FILES "/workspaces/git/build/git-sh-i18n--envsubst")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-sh-i18n--envsubst" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-sh-i18n--envsubst")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-sh-i18n--envsubst")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/git-shell" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/git-shell")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/git-shell"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "/workspaces/git/build/git-shell")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/git-shell" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/git-shell")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/git-shell")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/scalar" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/scalar")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/scalar"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE EXECUTABLE FILES "/workspaces/git/build/scalar")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/scalar" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/scalar")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/bin/scalar")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-http-fetch" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-http-fetch")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-http-fetch"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/libexec/git-core" TYPE EXECUTABLE FILES "/workspaces/git/build/git-http-fetch")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-http-fetch" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-http-fetch")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-http-fetch")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-http-push" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-http-push")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-http-push"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/libexec/git-core" TYPE EXECUTABLE FILES "/workspaces/git/build/git-http-push")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-http-push" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-http-push")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-http-push")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-imap-send" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-imap-send")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-imap-send"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/libexec/git-core" TYPE EXECUTABLE FILES "/workspaces/git/build/git-imap-send")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-imap-send" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-imap-send")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-imap-send")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-remote-http" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-remote-http")
    file(RPATH_CHECK
         FILE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-remote-http"
         RPATH "")
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/libexec/git-core" TYPE EXECUTABLE FILES "/workspaces/git/build/git-remote-http")
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-remote-http" AND
     NOT IS_SYMLINK "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-remote-http")
    if(CMAKE_INSTALL_DO_STRIP)
      execute_process(COMMAND "/usr/bin/strip" "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/libexec/git-core/git-remote-http")
    endif()
  endif()
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/bin" TYPE PROGRAM FILES "/workspaces/git/build/git-cvsserver")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/bin/git-receive-pack)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/bin/git-upload-archive)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/bin/git-upload-pack)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core/git)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git-shell /usr/local/libexec/git-core/git-shell)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-add)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-am)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-annotate)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-apply)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-archive)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-backfill)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-bisect)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-blame)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-branch)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-bugreport)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-bundle)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-cat-file)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-check-attr)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-check-ignore)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-check-mailmap)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-check-ref-format)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-checkout--worker)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-checkout-index)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-checkout)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-clean)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-clone)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-column)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-commit-graph)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-commit-tree)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-commit)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-config)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-count-objects)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-credential-cache--daemon)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-credential-cache)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-credential-store)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-credential)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-describe)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-diagnose)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-diff-files)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-diff-index)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-diff-pairs)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-diff-tree)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-diff)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-difftool)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-fast-export)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-fast-import)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-fetch-pack)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-fetch)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-fmt-merge-msg)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-for-each-ref)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-for-each-repo)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-fsck)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-fsmonitor--daemon)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-gc)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-get-tar-commit-id)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-grep)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-hash-object)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-help)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-hook)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-index-pack)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-init-db)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-interpret-trailers)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-log)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-ls-files)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-ls-remote)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-ls-tree)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-mailinfo)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-mailsplit)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-merge-base)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-merge-file)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-merge-index)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-merge-ours)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-merge-recursive)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-merge-tree)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-merge)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-mktag)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-mktree)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-multi-pack-index)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-mv)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-name-rev)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-notes)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-pack-objects)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-pack-redundant)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-pack-refs)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-patch-id)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-prune-packed)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-prune)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-pull)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-push)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-range-diff)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-read-tree)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-rebase)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-receive-pack)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-reflog)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-refs)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-remote-ext)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-remote-fd)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-remote)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-repack)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-replace)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-replay)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-rerere)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-reset)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-rev-list)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-rev-parse)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-revert)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-rm)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-send-pack)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-shortlog)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-show-branch)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-show-index)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-show-ref)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-sparse-checkout)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-stash)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-stripspace)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-submodule--helper)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-symbolic-ref)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-tag)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-unpack-file)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-unpack-objects)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-update-index)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-update-ref)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-update-server-info)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-upload-archive)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-upload-pack)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-var)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-verify-commit)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-verify-pack)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-verify-tag)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-worktree)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-write-tree)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-cherry)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-cherry-pick)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-format-patch)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-fsck-objects)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-init)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-maintenance)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-merge-subtree)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-restore)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-show)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-stage)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-status)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-switch)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-version)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK /usr/local/bin/git /usr/local/libexec/git-core//git-whatchanged)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK  /usr/local/libexec/git-core/git-remote-http /usr/local/libexec/git-core//git-remote-https)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK  /usr/local/libexec/git-core/git-remote-http /usr/local/libexec/git-core//git-remote-ftp)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(CREATE_LINK  /usr/local/libexec/git-core/git-remote-http /usr/local/libexec/git-core//git-remote-ftps)
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/libexec/git-core" TYPE PROGRAM FILES
    "/workspaces/git/build/git-difftool--helper"
    "/workspaces/git/build/git-filter-branch"
    "/workspaces/git/build/git-merge-octopus"
    "/workspaces/git/build/git-merge-one-file"
    "/workspaces/git/build/git-merge-resolve"
    "/workspaces/git/build/git-mergetool"
    "/workspaces/git/build/git-quiltimport"
    "/workspaces/git/build/git-request-pull"
    "/workspaces/git/build/git-submodule"
    "/workspaces/git/build/git-web--browse"
    "/workspaces/git/build/git-mergetool--lib"
    "/workspaces/git/build/git-sh-i18n"
    "/workspaces/git/build/git-sh-setup"
    "/workspaces/git/build/git-instaweb"
    "/workspaces/git/build/git-archimport.perl"
    "/workspaces/git/build/git-cvsexportcommit.perl"
    "/workspaces/git/build/git-cvsimport.perl"
    "/workspaces/git/build/git-cvsserver.perl"
    "/workspaces/git/build/git-send-email.perl"
    "/workspaces/git/build/git-svn.perl"
    "/workspaces/git/build/git-p4"
    )
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/libexec/git-core" TYPE DIRECTORY FILES "/workspaces/git/contrib/buildsystems/../../mergetools")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/perl5" TYPE DIRECTORY FILES "/workspaces/git/build/perl/build/lib/" FILES_MATCHING REGEX "/[^/]*\\.pm$")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/git-core/templates" TYPE DIRECTORY FILES "/workspaces/git/build/templates/blt/")
endif()

if("x${CMAKE_INSTALL_COMPONENT}x" STREQUAL "xUnspecifiedx" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share" TYPE DIRECTORY FILES "/workspaces/git/build/po/build/locale")
endif()

if(CMAKE_INSTALL_COMPONENT)
  set(CMAKE_INSTALL_MANIFEST "install_manifest_${CMAKE_INSTALL_COMPONENT}.txt")
else()
  set(CMAKE_INSTALL_MANIFEST "install_manifest.txt")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
file(WRITE "/workspaces/git/build/${CMAKE_INSTALL_MANIFEST}"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
