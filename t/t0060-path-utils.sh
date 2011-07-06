#!/bin/sh
#
# Copyright (c) 2008 David Reiss
#

test_description='Test various path utilities'

. ./test-lib.sh

norm_abs() {
	test_expect_success "normalize absolute" \
	"test \$(test-path-utils normalize_absolute_path '$1') = '$2'"
}

ancestor() {
	test_expect_success "longest ancestor" \
	"test \$(test-path-utils longest_ancestor_length '$1' '$2') = '$3'"
}

norm_abs "" /
norm_abs / /
norm_abs // /
norm_abs /// /
norm_abs /. /
norm_abs /./ /
norm_abs /./.. /
norm_abs /../. /
norm_abs /./../.// /
norm_abs /dir/.. /
norm_abs /dir/sub/../.. /
norm_abs /dir /dir
norm_abs /dir// /dir
norm_abs /./dir /dir
norm_abs /dir/. /dir
norm_abs /dir///./ /dir
norm_abs /dir//sub/.. /dir
norm_abs /dir/sub/../ /dir
norm_abs //dir/sub/../. /dir
norm_abs /dir/s1/../s2/ /dir/s2
norm_abs /d1/s1///s2/..//../s3/ /d1/s3
norm_abs /d1/s1//../s2/../../d2 /d2
norm_abs /d1/.../d2 /d1/.../d2
norm_abs /d1/..././../d2 /d1/d2

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
ancestor /foo/bar :://foo/. 4
ancestor /foo/bar :://foo/.:: 4
ancestor /foo/bar //foo/./::/bar 4
ancestor /foo/bar ::/bar -1

test_done
