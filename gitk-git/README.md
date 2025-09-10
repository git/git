Gitk - The Git Repository Browser
=================================

Gitk is a graphical Git repository browser. It displays the commit
history of a Git repository as a graph, showing the relationships
between commits, branches, and tags.

Usage
=====

To view the history of the current repository:
```bash
gitk
```

To view the history of specific files or directories:
```bash
gitk path/to/file
gitk path/to/directory
```

To view a specific branch or range of commits:
```bash
gitk branch-name
gitk v1.0..v2.0
```

For more usage examples and options, see the [gitk manual](https://git-scm.com/docs/gitk).

Building
========

Gitk is a Tcl/Tk application. It requires Tcl/Tk to be installed on
your system.

Running directly
----------------

Gitk can be run from the source directory without installation:

```bash
./gitk
```

This allows for quick testing of changes.

Installation
------------

To install system-wide, you can use either `make` or `meson`:

```bash
# Install to default location ($HOME/bin)
make install

# Install to system-wide location
sudo make install prefix=/usr/local

# Install to custom location
make install prefix=/opt/gitk

# Using Meson
meson setup builddir
meson compile -C builddir
meson install -C builddir
```

Both build systems will handle setting the correct Tcl/Tk interpreter
path and installing translation files.

Contributing
============

Contributions are welcome! The preferred method for submitting patches
is via email to the Git mailing list, as this allows for more thorough
review and broader community feedback. However, GitHub pull requests
are also accepted.

All commits must be signed off (use `git commit --signoff`) and should
have commit messages prefixed with `gitk:`.

Email Patches
-------------

Send patches to git@vger.kernel.org and CC j6t@kdbg.org. See the Git
project's [patch submission guidelines](https://git-scm.com/docs/SubmittingPatches)
for detailed instructions on creating and sending patches.

License
=======

Gitk is distributed under the GNU General Public License, either
version 2, or (at your option) any later version.
