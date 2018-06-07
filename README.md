Git for Windows
===============

[![Open in Visual Studio Code](https://img.shields.io/static/v1?logo=visualstudiocode&label=&message=Open%20in%20Visual%20Studio%20Code&labelColor=2c2c32&color=007acc&logoColor=007acc)](https://open.vscode.dev/git-for-windows/git)
[![Build status](https://github.com/git-for-windows/git/workflows/CI/badge.svg)](https://github.com/git-for-windows/git/actions?query=branch%3Amain+event%3Apush)
[![Join the chat at https://gitter.im/git-for-windows/git](https://badges.gitter.im/Join%20Chat.svg)](https://gitter.im/git-for-windows/git?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

This is [Git for Windows](http://git-for-windows.github.io/), the Windows port
of [Git](http://git-scm.com/).

The Git for Windows project is run using a [governance
model](http://git-for-windows.github.io/governance-model.html). If you
encounter problems, you can report them as [GitHub
issues](https://github.com/git-for-windows/git/issues), discuss them on Git
for Windows' [Google Group](http://groups.google.com/group/git-for-windows),
and [contribute bug
fixes](https://github.com/git-for-windows/git/wiki/How-to-participate).

To build Git for Windows, please either install [Git for Windows'
SDK](https://gitforwindows.org/#download-sdk), start its `git-bash.exe`, `cd`
to your Git worktree and run `make`, or open the Git worktree as a folder in
Visual Studio.

To verify that your build works, use one of the following methods:

- If you want to test the built executables within Git for Windows' SDK,
  prepend `<worktree>/bin-wrappers` to the `PATH`.
- Alternatively, run `make install` in the Git worktree.
- If you need to test this in a full installer, run `sdk build
  git-and-installer`.
- You can also "install" Git into an existing portable Git via `make install
  DESTDIR=<dir>` where `<dir>` refers to the top-level directory of the
  portable Git. In this instance, you will want to prepend that portable Git's
  `/cmd` directory to the `PATH`, or test by running that portable Git's
  `git-bash.exe` or `git-cmd.exe`.
- If you built using a recent Visual Studio, you can use the menu item
  `Build>Install git` (you will want to click on `Project>CMake Settings for
  Git` first, then click on `Edit JSON` and then point `installRoot` to the
  `mingw64` directory of an already-unpacked portable Git).

  As in the previous  bullet point, you will then prepend `/cmd` to the `PATH`
  or run using the portable Git's `git-bash.exe` or `git-cmd.exe`.
- If you want to run the built executables in-place, but in a CMD instead of
  inside a Bash, you can run a snippet like this in the `git-bash.exe` window
  where Git was built (ensure that the `EOF` line has no leading spaces), and
  then paste into the CMD window what was put in the clipboard:

  ```sh
  clip.exe <<EOF
  set GIT_EXEC_PATH=$(cygpath -aw .)
  set PATH=$(cygpath -awp ".:contrib/scalar:/mingw64/bin:/usr/bin:$PATH")
  set GIT_TEMPLATE_DIR=$(cygpath -aw templates/blt)
  set GITPERLLIB=$(cygpath -aw perl/build/lib)
  EOF
  ```
- If you want to run the built executables in-place, but outside of Git for
  Windows' SDK, and without an option to set/override any environment
  variables (e.g. in Visual Studio's debugger), you can call the Git executable
  by its absolute path and use the `--exec-path` option, like so:

  ```cmd
  C:\git-sdk-64\usr\src\git\git.exe --exec-path=C:\git-sdk-64\usr\src\git help
  ```

  Note: for this to work, you have to hard-link (or copy) the `.dll` files from
  the `/mingw64/bin` directory to the Git worktree, or add the `/mingw64/bin`
  directory to the `PATH` somehow or other.

To make sure that you are testing the correct binary, call `./git.exe version`
in the Git worktree, and then call `git version` in a directory/window where
you want to test Git, and verify that they refer to the same version (you may
even want to pass the command-line option `--build-options` to look at the
exact commit from which the Git version was built).

Git - fast, scalable, distributed revision control system
=========================================================

Git is a fast, scalable, distributed revision control system with an
unusually rich command set that provides both high-level operations
and full access to internals.

Git is an Open Source project covered by the GNU General Public
License version 2 (some parts of it are under different licenses,
compatible with the GPLv2). It was originally written by Linus
Torvalds with help of a group of hackers around the net.

Please read the file [INSTALL][] for installation instructions.

Many Git online resources are accessible from <https://git-scm.com/>
including full documentation and Git related tools.

See [Documentation/gittutorial.txt][] to get started, then see
[Documentation/giteveryday.txt][] for a useful minimum set of commands, and
`Documentation/git-<commandname>.txt` for documentation of each command.
If git has been correctly installed, then the tutorial can also be
read with `man gittutorial` or `git help tutorial`, and the
documentation of each command with `man git-<commandname>` or `git help
<commandname>`.

CVS users may also want to read [Documentation/gitcvs-migration.txt][]
(`man gitcvs-migration` or `git help cvs-migration` if git is
installed).

The user discussion and development of core Git take place on the Git
mailing list -- everyone is welcome to post bug reports, feature
requests, comments and patches to git@vger.kernel.org (read
[Documentation/SubmittingPatches][] for instructions on patch submission
and [Documentation/CodingGuidelines][]).

Those wishing to help with error message, usage and informational message
string translations (localization l10) should see [po/README.md][]
(a `po` file is a Portable Object file that holds the translations).

To subscribe to the list, send an email to <git+subscribe@vger.kernel.org>
(see https://subspace.kernel.org/subscribing.html for details). The mailing
list archives are available at <https://lore.kernel.org/git/>,
<https://marc.info/?l=git> and other archival sites.
The core git mailing list is plain text (no HTML!).

Issues which are security relevant should be disclosed privately to
the Git Security mailing list <git-security@googlegroups.com>.

The maintainer frequently sends the "What's cooking" reports that
list the current status of various development topics to the mailing
list.  The discussion following them give a good reference for
project status, development direction and remaining tasks.

The name "git" was given by Linus Torvalds when he wrote the very
first version. He described the tool as "the stupid content tracker"
and the name as (depending on your mood):

 - random three-letter combination that is pronounceable, and not
   actually used by any common UNIX command.  The fact that it is a
   mispronunciation of "get" may or may not be relevant.
 - stupid. contemptible and despicable. simple. Take your pick from the
   dictionary of slang.
 - "global information tracker": you're in a good mood, and it actually
   works for you. Angels sing, and a light suddenly fills the room.
 - "goddamn idiotic truckload of sh*t": when it breaks

[INSTALL]: INSTALL
[Documentation/gittutorial.txt]: Documentation/gittutorial.txt
[Documentation/giteveryday.txt]: Documentation/giteveryday.txt
[Documentation/gitcvs-migration.txt]: Documentation/gitcvs-migration.txt
[Documentation/SubmittingPatches]: Documentation/SubmittingPatches
[Documentation/CodingGuidelines]: Documentation/CodingGuidelines
[po/README.md]: po/README.md
