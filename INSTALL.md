# Git Installation

Normally, you can just run:

```sh
make
make install
```

This will install the Git programs in your own `~/bin/` directory. If you want to do a global install, you can use:

```sh
make prefix=/usr all doc info  # as yourself
sudo make prefix=/usr install install-doc install-html install-info  # as root
```

(Or use `prefix=/usr/local`, if preferred.) The built results have paths encoded based on `$prefix`, so running `make all; make prefix=/usr install` will not work.

## Custom Build Configuration

The `Makefile` documents many variables that affect the way Git is built. You can override them either from the command line or in a `config.mak` file.

Alternatively, you can use the `autoconf`-generated `./configure` script to set up install paths:

```sh
make configure  # as yourself
./configure --prefix=/usr  # as yourself
make all doc  # as yourself
sudo make install install-doc install-html  # as root
```

## Profile Feedback Build

For a faster Git build, you can enable profile feedback:

```sh
make prefix=/usr profile
sudo make prefix=/usr PROFILE=BUILD install
```

This runs the complete test suite as a training workload and rebuilds Git with the generated profile feedback, making it a few percent faster on CPU-intensive workloads. Alternatively, you can use the Git benchmark suite for a quicker profile feedback build:

```sh
make prefix=/usr profile-fast
sudo make prefix=/usr PROFILE=BUILD install
```

If you just want to install a profile-optimized version of Git into your home directory:

```sh
make profile-install
```

or

```sh
make profile-fast-install
```

**Caveat:** Profile-optimized builds take much longer as the Git tree must be built twice. Additionally, `ccache` must be disabled, the test suite runs on a single CPU, and the process generates extra compiler warnings.

## Issues of Note

### Conflicting Programs

Older versions of GNU Interactive Tools (`pre-4.9.2`) installed a program called `git`, which conflicts with Git. As of version `4.9.2`, it was renamed to `gnuit`, resolving the issue. 

### Running Git Without Installing

You can test-drive Git without installing it by running the Git binary from the `bin-wrappers/` directory or by prepending it to your `$PATH`:

```sh
export PATH=$(pwd)/bin-wrappers:$PATH
```

Alternatively, the traditional way involved setting environment variables:

```sh
export GIT_EXEC_PATH=$(pwd)
export PATH=$(pwd):$PATH
export GITPERLLIB=$(pwd)/perl/build/lib
```

### Perl Considerations

By default, Git ships with various Perl scripts unless `NO_PERL` is specified. However, it does not use `ExtUtils::MakeMaker` to determine Perl library locations, which can be an issue on some systems. You can manually set the `perllibdir` prefix:

```sh
prefix=/usr perllibdir=/usr/$(/usr/bin/perl -MConfig -wle 'print substr $Config{installsitelib}, 1 + length $Config{siteprefixexp}')
```

### External Dependencies

Git is mostly self-sufficient but relies on a few external libraries:

- **zlib** (required for compression)
- **ssh** (for network push/pull operations)
- **A POSIX-compliant shell** (needed for scripts like `bisect` and `request-pull`)
- **Perl 5.26.0+** (for `git send-email`, `git svn`, etc.)
- **libcurl 7.61.0+** (for HTTP(S) repositories and `git-imap-send`)
- **expat** (for `git-http-push` remote lock management)
- **wish (Tcl/Tk)** (for `gitk` and `git-gui`)
- **gettext** (for localization; can be disabled with `NO_GETTEXT`)
- **Python 2.7+** (for `git-p4` Perforce integration)

Many of these dependencies can be disabled using `NO_<LIBRARY>=YesPlease` in `config.mak` or on the command line.

## Documentation

To build and install the documentation suite, you need the `asciidoc/xmlto` toolchain. The default `make all` target does **not** build documentation.

```sh
make doc  # Builds man and HTML docs
make man  # Builds only man pages
make html  # Builds only HTML docs
make info  # Builds only info docs
```

To install:

```sh
make install-doc  # Installs man pages
make install-man  # Installs only man pages
make install-html  # Installs only HTML docs
make install-info  # Installs only info docs
```

### Preformatted Documentation

For preformatted documentation:

```sh
make quick-install-doc
make quick-install-man
make quick-install-html
```

This requires cloning `git-htmldocs` and `git-manpages` repositories next to your Git source tree.

### Additional Dependencies

- `makeinfo` and `docbook2X` (for info files; version `0.8.3` works)
- `dblatex` (for PDFs; version `0.2.7+` works)
- `asciidoc` (`8.4.1+` required)
- `Asciidoctor` (requires Ruby; use `USE_ASCIIDOCTOR=YesPlease`)

## Building Documentation on Cygwin

On Cygwin, ensure your `/etc/xml/catalog` contains:

```xml
<?xml version="1.0"?>
<!DOCTYPE catalog PUBLIC
   "-//OASIS//DTD Entity Resolution XML Catalog V1.0//EN"
   "http://www.oasis-open.org/committees/entity/release/1.0/catalog.dtd"
>
<catalog xmlns="urn:oasis:names:tc:entity:xmlns:xml:catalog">
  <rewriteURI
    uriStartString="http://docbook.sourceforge.net/release/xsl/current"
    rewritePrefix="/usr/share/sgml/docbook/xsl-stylesheets"
  />
  <rewriteURI
    uriStartString="http://www.oasis-open.org/docbook/xml/4.5"
    rewritePrefix="/usr/share/sgml/docbook/xml-dtd-4.5"
  />
</catalog>
```

To generate this file, run:

```sh
xmlcatalog --noout \
   --add rewriteURI \
      http://docbook.sourceforge.net/release/xsl/current \
      /usr/share/sgml/docbook/xsl-stylesheets \
   /etc/xml/catalog

xmlcatalog --noout \
   --add rewriteURI \
       http://www.oasis-open.org/docbook/xml/4.5/xsl/current \
       /usr/share/sgml/docbook/xml-dtd-4.5 \
   /etc/xml/catalog
```

This should resolve issues when building documentation on Cygwin.
