#!/bin/sh

test_description='test BUT_CEILING_DIRECTORIES'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_prefix() {
	test_expect_success "$1" \
	"test '$2' = \"\$(but rev-parse --show-prefix)\""
}

test_fail() {
	test_expect_success "$1: prefix" '
		test_expect_code 128 but rev-parse --show-prefix
	'
}

TRASH_ROOT="$PWD"
ROOT_PARENT=$(dirname "$TRASH_ROOT")


unset BUT_CEILING_DIRECTORIES
test_prefix no_ceil ""

export BUT_CEILING_DIRECTORIES

BUT_CEILING_DIRECTORIES=""
test_prefix ceil_empty ""

BUT_CEILING_DIRECTORIES="$ROOT_PARENT"
test_prefix ceil_at_parent ""

BUT_CEILING_DIRECTORIES="$ROOT_PARENT/"
test_prefix ceil_at_parent_slash ""

BUT_CEILING_DIRECTORIES="$TRASH_ROOT"
test_prefix ceil_at_trash ""

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/"
test_prefix ceil_at_trash_slash ""

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/sub"
test_prefix ceil_at_sub ""

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/sub/"
test_prefix ceil_at_sub_slash ""

if test_have_prereq SYMLINKS
then
	ln -s sub top
fi

mkdir -p sub/dir || exit 1
cd sub/dir || exit 1

unset BUT_CEILING_DIRECTORIES
test_prefix subdir_no_ceil "sub/dir/"

export BUT_CEILING_DIRECTORIES

BUT_CEILING_DIRECTORIES=""
test_prefix subdir_ceil_empty "sub/dir/"

BUT_CEILING_DIRECTORIES="$TRASH_ROOT"
test_fail subdir_ceil_at_trash

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/"
test_fail subdir_ceil_at_trash_slash

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/sub"
test_fail subdir_ceil_at_sub

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/sub/"
test_fail subdir_ceil_at_sub_slash

if test_have_prereq SYMLINKS
then
	BUT_CEILING_DIRECTORIES="$TRASH_ROOT/top"
	test_fail subdir_ceil_at_top
	BUT_CEILING_DIRECTORIES="$TRASH_ROOT/top/"
	test_fail subdir_ceil_at_top_slash

	BUT_CEILING_DIRECTORIES=":$TRASH_ROOT/top"
	test_prefix subdir_ceil_at_top_no_resolve "sub/dir/"
	BUT_CEILING_DIRECTORIES=":$TRASH_ROOT/top/"
	test_prefix subdir_ceil_at_top_slash_no_resolve "sub/dir/"
fi

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/sub/dir"
test_prefix subdir_ceil_at_subdir "sub/dir/"

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/sub/dir/"
test_prefix subdir_ceil_at_subdir_slash "sub/dir/"


BUT_CEILING_DIRECTORIES="$TRASH_ROOT/su"
test_prefix subdir_ceil_at_su "sub/dir/"

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/su/"
test_prefix subdir_ceil_at_su_slash "sub/dir/"

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/sub/di"
test_prefix subdir_ceil_at_sub_di "sub/dir/"

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/sub/di"
test_prefix subdir_ceil_at_sub_di_slash "sub/dir/"

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/subdi"
test_prefix subdir_ceil_at_subdi "sub/dir/"

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/subdi"
test_prefix subdir_ceil_at_subdi_slash "sub/dir/"


BUT_CEILING_DIRECTORIES="/foo:$TRASH_ROOT/sub"
test_fail second_of_two

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/sub:/bar"
test_fail first_of_two

BUT_CEILING_DIRECTORIES="/foo:$TRASH_ROOT/sub:/bar"
test_fail second_of_three


BUT_CEILING_DIRECTORIES="$TRASH_ROOT/sub"
BUT_DIR=../../.but
export BUT_DIR
test_prefix but_dir_specified ""
unset BUT_DIR


cd ../.. || exit 1
mkdir -p s/d || exit 1
cd s/d || exit 1

unset BUT_CEILING_DIRECTORIES
test_prefix sd_no_ceil "s/d/"

export BUT_CEILING_DIRECTORIES

BUT_CEILING_DIRECTORIES=""
test_prefix sd_ceil_empty "s/d/"

BUT_CEILING_DIRECTORIES="$TRASH_ROOT"
test_fail sd_ceil_at_trash

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/"
test_fail sd_ceil_at_trash_slash

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/s"
test_fail sd_ceil_at_s

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/s/"
test_fail sd_ceil_at_s_slash

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/s/d"
test_prefix sd_ceil_at_sd "s/d/"

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/s/d/"
test_prefix sd_ceil_at_sd_slash "s/d/"


BUT_CEILING_DIRECTORIES="$TRASH_ROOT/su"
test_prefix sd_ceil_at_su "s/d/"

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/su/"
test_prefix sd_ceil_at_su_slash "s/d/"

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/s/di"
test_prefix sd_ceil_at_s_di "s/d/"

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/s/di"
test_prefix sd_ceil_at_s_di_slash "s/d/"

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/sdi"
test_prefix sd_ceil_at_sdi "s/d/"

BUT_CEILING_DIRECTORIES="$TRASH_ROOT/sdi"
test_prefix sd_ceil_at_sdi_slash "s/d/"


test_done
