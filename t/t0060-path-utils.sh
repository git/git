#!/bin/sh
#
# Copyright (c) 2008 David Reiss
#

test_description='Test various path utilities'

. ./test-lib.sh

norm_path() {
	expected=$(test-path-utils print_path "$2")
	test_expect_success $3 "normalize path: $1 => $2" \
	"test \"\$(test-path-utils normalize_path_copy '$1')\" = '$expected'"
}

relative_path() {
	expected=$(test-path-utils print_path "$3")
	test_expect_success $4 "relative path: $1 $2 => $3" \
	"test \"\$(test-path-utils relative_path '$1' '$2')\" = '$expected'"
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

# Some absolute path tests should be skipped on Windows due to path mangling
# on POSIX-style absolute paths
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

norm_path / /
norm_path // / POSIX
norm_path /// / POSIX
norm_path /. /
norm_path /./ / POSIX
norm_path /./.. ++failed++ POSIX
norm_path /../. ++failed++
norm_path /./../.// ++failed++ POSIX
norm_path /dir/.. / POSIX
norm_path /dir/sub/../.. / POSIX
norm_path /dir/sub/../../.. ++failed++ POSIX
norm_path /dir /dir
norm_path /dir// /dir/
norm_path /./dir /dir
norm_path /dir/. /dir/
norm_path /dir///./ /dir/
norm_path /dir//sub/.. /dir/
norm_path /dir/sub/../ /dir/
norm_path //dir/sub/../. /dir/ POSIX
norm_path /dir/s1/../s2/ /dir/s2/
norm_path /d1/s1///s2/..//../s3/ /d1/s3/
norm_path /d1/s1//../s2/../../d2 /d2
norm_path /d1/.../d2 /d1/.../d2
norm_path /d1/..././../d2 /d1/d2

ancestor / / -1
ancestor /foo / 0
ancestor /foo /fo -1
ancestor /foo /foo -1
ancestor /foo /bar -1
ancestor /foo /foo/bar -1
ancestor /foo /foo:/bar -1
ancestor /foo /:/foo:/bar 0
ancestor /foo /foo:/:/bar 0
ancestor /foo /:/bar:/foo 0
ancestor /foo/bar / 0
ancestor /foo/bar /fo -1
ancestor /foo/bar /foo 4
ancestor /foo/bar /foo/ba -1
ancestor /foo/bar /:/fo 0
ancestor /foo/bar /foo:/foo/ba 4
ancestor /foo/bar /bar -1
ancestor /foo/bar /fo -1
ancestor /foo/bar /foo:/bar 4
ancestor /foo/bar /:/foo:/bar 4
ancestor /foo/bar /foo:/:/bar 4
ancestor /foo/bar /:/bar:/fo 0
ancestor /foo/bar /:/bar 0
ancestor /foo/bar /foo 4
ancestor /foo/bar /foo:/bar 4
ancestor /foo/bar /bar -1

test_expect_success 'strip_path_suffix' '
	test c:/msysgit = $(test-path-utils strip_path_suffix \
		c:/msysgit/libexec//git-core libexec/git-core)
'

test_expect_success 'absolute path rejects the empty string' '
	test_must_fail test-path-utils absolute_path ""
'

test_expect_success 'real path rejects the empty string' '
	test_must_fail test-path-utils real_path ""
'

test_expect_success POSIX 'real path works on absolute paths 1' '
	nopath="hopefully-absent-path" &&
	test "/" = "$(test-path-utils real_path "/")" &&
	test "/$nopath" = "$(test-path-utils real_path "/$nopath")"
'

test_expect_success 'real path works on absolute paths 2' '
	nopath="hopefully-absent-path" &&
	# Find an existing top-level directory for the remaining tests:
	d=$(pwd -P | sed -e "s|^\([^/]*/[^/]*\)/.*|\1|") &&
	test "$d" = "$(test-path-utils real_path "$d")" &&
	test "$d/$nopath" = "$(test-path-utils real_path "$d/$nopath")"
'

test_expect_success POSIX 'real path removes extra leading slashes' '
	nopath="hopefully-absent-path" &&
	test "/" = "$(test-path-utils real_path "///")" &&
	test "/$nopath" = "$(test-path-utils real_path "///$nopath")" &&
	# Find an existing top-level directory for the remaining tests:
	d=$(pwd -P | sed -e "s|^\([^/]*/[^/]*\)/.*|\1|") &&
	test "$d" = "$(test-path-utils real_path "//$d")" &&
	test "$d/$nopath" = "$(test-path-utils real_path "//$d/$nopath")"
'

test_expect_success 'real path removes other extra slashes' '
	nopath="hopefully-absent-path" &&
	# Find an existing top-level directory for the remaining tests:
	d=$(pwd -P | sed -e "s|^\([^/]*/[^/]*\)/.*|\1|") &&
	test "$d" = "$(test-path-utils real_path "$d///")" &&
	test "$d/$nopath" = "$(test-path-utils real_path "$d///$nopath")"
'

test_expect_success SYMLINKS 'real path works on symlinks' '
	mkdir first &&
	ln -s ../.git first/.git &&
	mkdir second &&
	ln -s ../first second/other &&
	mkdir third &&
	dir="$(cd .git; pwd -P)" &&
	dir2=third/../second/other/.git &&
	test "$dir" = "$(test-path-utils real_path $dir2)" &&
	file="$dir"/index &&
	test "$file" = "$(test-path-utils real_path $dir2/index)" &&
	basename=blub &&
	test "$dir/$basename" = "$(cd .git && test-path-utils real_path "$basename")" &&
	ln -s ../first/file .git/syml &&
	sym="$(cd first; pwd -P)"/file &&
	test "$sym" = "$(test-path-utils real_path "$dir2/syml")"
'

test_expect_success SYMLINKS 'prefix_path works with absolute paths to work tree symlinks' '
	ln -s target symlink &&
	test "$(test-path-utils prefix_path prefix "$(pwd)/symlink")" = "symlink"
'

test_expect_success 'prefix_path works with only absolute path to work tree' '
	echo "" >expected &&
	test-path-utils prefix_path prefix "$(pwd)" >actual &&
	test_cmp expected actual
'

test_expect_success 'prefix_path rejects absolute path to dir with same beginning as work tree' '
	test_must_fail test-path-utils prefix_path prefix "$(pwd)a"
'

test_expect_success SYMLINKS 'prefix_path works with absolute path to a symlink to work tree having  same beginning as work tree' '
	git init repo &&
	ln -s repo repolink &&
	test "a" = "$(cd repo && test-path-utils prefix_path prefix "$(pwd)/../repolink/a")"
'

relative_path /foo/a/b/c/	/foo/a/b/	c/
relative_path /foo/a/b/c/	/foo/a/b	c/
relative_path /foo/a//b//c/	///foo/a/b//	c/		POSIX
relative_path /foo/a/b		/foo/a/b	./
relative_path /foo/a/b/		/foo/a/b	./
relative_path /foo/a		/foo/a/b	../
relative_path /			/foo/a/b/	../../../
relative_path /foo/a/c		/foo/a/b/	../c
relative_path /foo/a/c		/foo/a/b	../c
relative_path /foo/x/y		/foo/a/b/	../../x/y
relative_path /foo/a/b		"<empty>"	/foo/a/b
relative_path /foo/a/b 		"<null>"	/foo/a/b
relative_path foo/a/b/c/	foo/a/b/	c/
relative_path foo/a/b/c/	foo/a/b		c/
relative_path foo/a/b//c	foo/a//b	c
relative_path foo/a/b/		foo/a/b/	./
relative_path foo/a/b/		foo/a/b		./
relative_path foo/a		foo/a/b		../
relative_path foo/x/y		foo/a/b		../../x/y
relative_path foo/a/c		foo/a/b		../c
relative_path foo/a/b		/foo/x/y	foo/a/b
relative_path /foo/a/b		foo/x/y		/foo/a/b
relative_path d:/a/b		D:/a/c		../b		MINGW
relative_path C:/a/b		D:/a/c		C:/a/b		MINGW
relative_path foo/a/b		"<empty>"	foo/a/b
relative_path foo/a/b 		"<null>"	foo/a/b
relative_path "<empty>"		/foo/a/b	./
relative_path "<empty>"		"<empty>"	./
relative_path "<empty>"		"<null>"	./
relative_path "<null>"		"<empty>"	./
relative_path "<null>"		"<null>"	./
relative_path "<null>"		/foo/a/b	./

test_done
