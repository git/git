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

check_docs () {
	test -s "$1"/Documentation/git.html &&
	test -s "$1"/Documentation/git.xml &&
	test -s "$1"/Documentation/git.1 &&
	grep "<meta name=\"generator\" content=\"$2 " "$1"/Documentation/git.html
}

make check-builtins
make check-docs

# Build docs with AsciiDoc
make doc > >(tee stdout.log) 2> >(tee stderr.raw >&2)
cat stderr.raw
filter_log stderr.raw >stderr.log
test ! -s stderr.log
check_docs . AsciiDoc

rm -f stdout.log stderr.log stderr.raw
check_unignored_build_artifacts

# Build docs with AsciiDoctor
make clean
make USE_ASCIIDOCTOR=1 doc > >(tee stdout.log) 2> >(tee stderr.raw >&2)
cat stderr.raw
filter_log stderr.raw >stderr.log
test ! -s stderr.log
check_docs . Asciidoctor

rm -f stdout.log stderr.log stderr.raw
check_unignored_build_artifacts

# Build docs with Meson and AsciiDoc
meson setup build-asciidoc -Ddocs=html,man -Ddocs_backend=asciidoc
meson compile -C build-asciidoc
check_docs build-asciidoc AsciiDoc
rm -rf build-asciidoc

# Build docs with Meson and AsciiDoctor
meson setup build-asciidoctor -Ddocs=html,man -Ddocs_backend=asciidoctor
meson compile -C build-asciidoctor
check_docs build-asciidoctor Asciidoctor
rm -rf build-asciidoctor

save_good_tree
