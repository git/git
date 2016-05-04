#!/bin/sh
#
# Perform sanity checks on documentation and build it.
#

set -e

make check-builtins
make check-docs
make doc

test -s Documentation/git.html
test -s Documentation/git.xml
test -s Documentation/git.1
