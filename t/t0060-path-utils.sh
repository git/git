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

test_done
