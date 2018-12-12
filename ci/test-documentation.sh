#!/usr/bin/env bash
#
# Perform sanity checks on documentation and build it.
#

. ${0%/*}/lib.sh

test -n "$ALREADY_HAVE_ASCIIDOCTOR" ||
gem install asciidoctor

make check-builtins
make check-docs

# Build docs with AsciiDoc
make doc > >(tee stdout.log) 2> >(tee stderr.log >&2)
! test -s stderr.log
test -s Documentation/git.html
test -s Documentation/git.xml
test -s Documentation/git.1
grep '<meta name="generator" content="AsciiDoc ' Documentation/git.html

rm -f stdout.log stderr.log
check_unignored_build_artifacts

# Build docs with AsciiDoctor
make clean
make USE_ASCIIDOCTOR=1 doc > >(tee stdout.log) 2> >(tee stderr.log >&2)
sed '/^GIT_VERSION = / d' stderr.log
! test -s stderr.log
test -s Documentation/git.html
grep '<meta name="generator" content="Asciidoctor ' Documentation/git.html

rm -f stdout.log stderr.log
check_unignored_build_artifacts

save_good_tree
