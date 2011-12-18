#!/bin/sh
#
# Copyright (c) 2008 David Reiss
#

test_description='Test various path utilities'

. ./test-lib.sh

norm_path() {
	test_expect_success $3 "normalize path: $1 => $2" \
	"test \"\$(test-path-utils normalize_path_copy '$1')\" = '$2'"
}

# On Windows, we are using MSYS's bash, which mangles the paths.
# Absolute paths are anchored at the MSYS installation directory,
# which means that the path / accounts for this many characters:
rootoff=$(test-path-utils normalize_path_copy / | wc -c)
# Account for the trailing LF:
if test $rootoff = 2; then
	rootoff=	# we are on Unix
else
	rootoff=$(($rootoff-1))
fi

ancestor() {
	# We do some math with the expected ancestor length.
	expected=$3
	if test -n "$rootoff" && test "x$expected" != x-1; then
		expected=$(($expected+$rootoff))
	fi
	test_expect_success "longest ancestor: $1 $2 => $expected" \
	"actual=\$(test-path-utils longest_ancestor_length '$1' '$2') &&
	 test \"\$actual\" = '$expected'"
}

# Absolute path tests must be skipped on Windows because due to path mangling
# the test program never sees a POSIX-style absolute path
case $(uname -s) in
*MINGW*)
	;;
*)
	test_set_prereq POSIX
	;;
esac

norm_path "" ""
norm_path . ""
norm_path ./ ""
norm_path ./. ""
norm_path ./.. ++failed++
norm_path ../. ++failed++
norm_path ./../.// ++failed++
norm_path dir/.. ""
norm_path dir/sub/../.. ""
norm_path dir/sub/../../.. ++failed++
norm_path dir dir
norm_path dir// dir/
norm_path ./dir dir
norm_path dir/. dir/
norm_path dir///./ dir/
norm_path dir//sub/.. dir/
norm_path dir/sub/../ dir/
norm_path dir/sub/../. dir/
norm_path dir/s1/../s2/ dir/s2/
norm_path d1/s1///s2/..//../s3/ d1/s3/
norm_path d1/s1//../s2/../../d2 d2
norm_path d1/.../d2 d1/.../d2
norm_path d1/..././../d2 d1/d2

norm_path / / POSIX
norm_path // / POSIX
norm_path /// / POSIX
norm_path /. / POSIX
norm_path /./ / POSIX
norm_path /./.. ++failed++ POSIX
norm_path /../. ++failed++ POSIX
norm_path /./../.// ++failed++ POSIX
norm_path /dir/.. / POSIX
norm_path /dir/sub/../.. / POSIX
norm_path /dir/sub/../../.. ++failed++ POSIX
norm_path /dir /dir POSIX
norm_path /dir// /dir/ POSIX
norm_path /./dir /dir POSIX
norm_path /dir/. /dir/ POSIX
norm_path /dir///./ /dir/ POSIX
norm_path /dir//sub/.. /dir/ POSIX
norm_path /dir/sub/../ /dir/ POSIX
norm_path //dir/sub/../. /dir/ POSIX
norm_path /dir/s1/../s2/ /dir/s2/ POSIX
norm_path /d1/s1///s2/..//../s3/ /d1/s3/ POSIX
norm_path /d1/s1//../s2/../../d2 /d2 POSIX
norm_path /d1/.../d2 /d1/.../d2 POSIX
norm_path /d1/..././../d2 /d1/d2 POSIX

ancestor / "" -1
ancestor / / -1
ancestor /foo "" -1
ancestor /foo : -1
ancestor /foo ::. -1
ancestor /foo ::..:: -1
ancestor /foo / 0
ancestor /foo /fo -1
ancestor /foo /foo -1
ancestor /foo /foo/ -1
ancestor /foo /bar -1
ancestor /foo /bar/ -1
ancestor /foo /foo/bar -1
ancestor /foo /foo:/bar/ -1
ancestor /foo /foo/:/bar/ -1
ancestor /foo /foo::/bar/ -1
ancestor /foo /:/foo:/bar/ 0
ancestor /foo /foo:/:/bar/ 0
ancestor /foo /:/bar/:/foo 0
ancestor /foo/bar "" -1
ancestor /foo/bar / 0
ancestor /foo/bar /fo -1
ancestor /foo/bar foo -1
ancestor /foo/bar /foo 4
ancestor /foo/bar /foo/ 4
ancestor /foo/bar /foo/ba -1
ancestor /foo/bar /:/fo 0
ancestor /foo/bar /foo:/foo/ba 4
ancestor /foo/bar /bar -1
ancestor /foo/bar /bar/ -1
ancestor /foo/bar /fo: -1
ancestor /foo/bar :/fo -1
ancestor /foo/bar /foo:/bar/ 4
ancestor /foo/bar /:/foo:/bar/ 4
ancestor /foo/bar /foo:/:/bar/ 4
ancestor /foo/bar /:/bar/:/fo 0
ancestor /foo/bar /:/bar/ 0
ancestor /foo/bar .:/foo/. 4
ancestor /foo/bar .:/foo/.:.: 4
ancestor /foo/bar /foo/./:.:/bar 4
ancestor /foo/bar .:/bar -1

test_expect_success 'strip_path_suffix' '
	test c:/msysgit = $(test-path-utils strip_path_suffix \
		c:/msysgit/libexec//git-core libexec/git-core)
'
test_done
