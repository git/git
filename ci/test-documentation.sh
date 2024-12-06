#!/usr/bin/env bash
#
# Perform sanity checks on documentation and build it.
#

. ${0%/*}/lib.sh

filter_log () {
	sed -e '/^GIT_VERSION=/d' \
	    -e "/constant Gem::ConfigMap is deprecated/d" \
	    -e '/^    \* new asciidoc flags$/d' \
	    -e '/stripped namespace before processing/d' \
	    -e '/Attributed.*IDs for element/d' \
	    -e '/SyntaxWarning: invalid escape sequence/d' \
	    "$1"
}

make check-builtins
make check-docs

# Build docs with AsciiDoc
make doc > >(tee stdout.log) 2> >(tee stderr.raw >&2)
cat stderr.raw
filter_log stderr.raw >stderr.log
test ! -s stderr.log
test -s Documentation/git.html
test -s Documentation/git.xml
test -s Documentation/git.1
grep '<meta name="generator" content="AsciiDoc ' Documentation/git.html

rm -f stdout.log stderr.log stderr.raw
check_unignored_build_artifacts

# Build docs with AsciiDoctor
make clean
make USE_ASCIIDOCTOR=1 doc > >(tee stdout.log) 2> >(tee stderr.raw >&2)
cat stderr.raw
filter_log stderr.raw >stderr.log
test ! -s stderr.log
test -s Documentation/git.html
grep '<meta name="generator" content="Asciidoctor ' Documentation/git.html

rm -f stdout.log stderr.log stderr.raw
check_unignored_build_artifacts

save_good_tree
