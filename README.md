# The `vs/master` and `vs/main` branches are deprecated and disabled

For years, Git for Windows maintained a separate branch (`vs/master`) that came with Visual Studio project files so that users could easily clone that branch and build the source code in Visual Studio.

As of v2.29.0, this changed: Git for Windows (and Git!) now support building on Windows using [CMake](https://cmake.org/). This is how to use this in Visual Studio:

Open the worktree as a folder. Visual Studio 2019 and later will detect the CMake configuration automatically and set everything up for you, ready to build. You can then run the tests in `t/` via a regular Git Bash.

A couple of notes:

- Visual Studio also has the option of opening `CMakeLists.txt` directly; Using this option, Visual Studio will not find the source code, though, therefore the `File>Open>Folder...` option is preferred.
- The first time you open it, dependencies such as OpenSSL and cURL will be built as part of CMake's configuring phase. This will take a while.
