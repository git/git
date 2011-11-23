#!/bin/sh

test_description='test GIT_CEILING_DIRECTORIES'
. ./test-lib.sh

test_prefix() {
	test_expect_success "$1" \
	"test '$2' = \"\$(git rev-parse --show-prefix)\""
}

test_fail() {
	test_expect_success "$1: prefix" '
		test_expect_code 128 git rev-parse --show-prefix
	'
}

TRASH_ROOT="$PWD"
ROOT_PARENT=$(dirname "$TRASH_ROOT")


unset GIT_CEILING_DIRECTORIES
test_prefix no_ceil ""

export GIT_CEILING_DIRECTORIES

GIT_CEILING_DIRECTORIES=""
test_prefix ceil_empty ""

GIT_CEILING_DIRECTORIES="$ROOT_PARENT"
test_prefix ceil_at_parent ""

GIT_CEILING_DIRECTORIES="$ROOT_PARENT/"
test_prefix ceil_at_parent_slash ""

GIT_CEILING_DIRECTORIES="$TRASH_ROOT"
test_prefix ceil_at_trash ""

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/"
test_prefix ceil_at_trash_slash ""

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/sub"
test_prefix ceil_at_sub ""

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/sub/"
test_prefix ceil_at_sub_slash ""


mkdir -p sub/dir || exit 1
cd sub/dir || exit 1

unset GIT_CEILING_DIRECTORIES
test_prefix subdir_no_ceil "sub/dir/"

export GIT_CEILING_DIRECTORIES

GIT_CEILING_DIRECTORIES=""
test_prefix subdir_ceil_empty "sub/dir/"

GIT_CEILING_DIRECTORIES="$TRASH_ROOT"
test_fail subdir_ceil_at_trash

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/"
test_fail subdir_ceil_at_trash_slash

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/sub"
test_fail subdir_ceil_at_sub

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/sub/"
test_fail subdir_ceil_at_sub_slash

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/sub/dir"
test_prefix subdir_ceil_at_subdir "sub/dir/"

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/sub/dir/"
test_prefix subdir_ceil_at_subdir_slash "sub/dir/"


GIT_CEILING_DIRECTORIES="$TRASH_ROOT/su"
test_prefix subdir_ceil_at_su "sub/dir/"

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/su/"
test_prefix subdir_ceil_at_su_slash "sub/dir/"

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/sub/di"
test_prefix subdir_ceil_at_sub_di "sub/dir/"

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/sub/di"
test_prefix subdir_ceil_at_sub_di_slash "sub/dir/"

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/subdi"
test_prefix subdir_ceil_at_subdi "sub/dir/"

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/subdi"
test_prefix subdir_ceil_at_subdi_slash "sub/dir/"


GIT_CEILING_DIRECTORIES="/foo:$TRASH_ROOT/sub"
test_fail second_of_two

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/sub:/bar"
test_fail first_of_two

GIT_CEILING_DIRECTORIES="/foo:$TRASH_ROOT/sub:/bar"
test_fail second_of_three


GIT_CEILING_DIRECTORIES="$TRASH_ROOT/sub"
GIT_DIR=../../.git
export GIT_DIR
test_prefix git_dir_specified ""
unset GIT_DIR


cd ../.. || exit 1
mkdir -p s/d || exit 1
cd s/d || exit 1

unset GIT_CEILING_DIRECTORIES
test_prefix sd_no_ceil "s/d/"

export GIT_CEILING_DIRECTORIES

GIT_CEILING_DIRECTORIES=""
test_prefix sd_ceil_empty "s/d/"

GIT_CEILING_DIRECTORIES="$TRASH_ROOT"
test_fail sd_ceil_at_trash

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/"
test_fail sd_ceil_at_trash_slash

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/s"
test_fail sd_ceil_at_s

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/s/"
test_fail sd_ceil_at_s_slash

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/s/d"
test_prefix sd_ceil_at_sd "s/d/"

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/s/d/"
test_prefix sd_ceil_at_sd_slash "s/d/"


GIT_CEILING_DIRECTORIES="$TRASH_ROOT/su"
test_prefix sd_ceil_at_su "s/d/"

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/su/"
test_prefix sd_ceil_at_su_slash "s/d/"

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/s/di"
test_prefix sd_ceil_at_s_di "s/d/"

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/s/di"
test_prefix sd_ceil_at_s_di_slash "s/d/"

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/sdi"
test_prefix sd_ceil_at_sdi "s/d/"

GIT_CEILING_DIRECTORIES="$TRASH_ROOT/sdi"
test_prefix sd_ceil_at_sdi_slash "s/d/"


test_done
