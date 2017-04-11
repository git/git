#!/bin/sh
#
# Perform sanity checks on documentation and build it.
#

set -e

make check-builtins
make check-docs

# Build docs with AsciiDoc
make doc
test -s Documentation/git.html
test -s Documentation/git.xml
test -s Documentation/git.1
grep '<meta name="generator" content="AsciiDoc ' Documentation/git.html

# Build docs with AsciiDoctor
make clean
make USE_ASCIIDOCTOR=1 doc
test -s Documentation/git.html
grep '<meta name="generator" content="Asciidoctor ' Documentation/git.html
