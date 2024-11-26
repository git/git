#!/bin/sh

test_description='ls-tree output'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-t3100.sh

test_expect_success 'ls-tree --format usage' '
	test_expect_code 129 git ls-tree --format=fmt -l HEAD &&
	test_expect_code 129 git ls-tree --format=fmt --name-only HEAD &&
	test_expect_code 129 git ls-tree --format=fmt --name-status HEAD
'

test_expect_success 'setup' '
	setup_basic_ls_tree_data
'

test_ls_tree_format_mode_output () {
	local opts="$1" &&
	shift &&
	cat >expect &&

	while test $# -gt 0
	do
		local mode="$1" &&
		shift &&

		test_expect_success "'ls-tree $opts${mode:+ $mode}' output" '
			git ls-tree ${mode:+$mode }$opts HEAD >actual &&
			test_cmp expect actual
		'

		case "$opts" in
		--full-tree)
			test_expect_success "'ls-tree $opts${mode:+ $mode}' output (via subdir, fails)" '
				test_must_fail git -C dir ls-tree --full-name ${mode:+$mode }$opts HEAD -- ../
			'
			;;
		*)
			test_expect_success "'ls-tree $opts${mode:+ $mode}' output (via subdir)" '
				git -C dir ls-tree --full-name ${mode:+$mode }$opts HEAD -- ../ >actual &&
				test_cmp expect actual
			'
			;;
		esac
	done
}

# test exact output of option (none, --long, ...) and mode (none and
# -d, -r -t) and combinations
test_expect_success 'setup: HEAD_* variables' '
	HEAD_gitmodules=$(git rev-parse HEAD:.gitmodules) &&
	HEAD_dir=$(git rev-parse HEAD:dir) &&
	HEAD_top_file=$(git rev-parse HEAD:top-file.t) &&
	HEAD_submodule=$(git rev-parse HEAD:submodule) &&
	HEAD_dir_sub_file=$(git rev-parse HEAD:dir/sub-file.t)
'
## opt =
test_ls_tree_format_mode_output "" "" "-t" <<-EOF
	100644 blob $HEAD_gitmodules	.gitmodules
	040000 tree $HEAD_dir	dir
	160000 commit $HEAD_submodule	submodule
	100644 blob $HEAD_top_file	top-file.t
	EOF
test_ls_tree_format_mode_output "" "-d" <<-EOF
	040000 tree $HEAD_dir	dir
	160000 commit $HEAD_submodule	submodule
	EOF
test_ls_tree_format_mode_output "" "-r" <<-EOF
	100644 blob $HEAD_gitmodules	.gitmodules
	100644 blob $HEAD_dir_sub_file	dir/sub-file.t
	160000 commit $HEAD_submodule	submodule
	100644 blob $HEAD_top_file	top-file.t
	EOF
## opt = --long
test_ls_tree_format_mode_output "--long" "" "-t" <<-EOF
	100644 blob $HEAD_gitmodules      61	.gitmodules
	040000 tree $HEAD_dir       -	dir
	160000 commit $HEAD_submodule       -	submodule
	100644 blob $HEAD_top_file       9	top-file.t
	EOF
test_ls_tree_format_mode_output "--long" "-d" <<-EOF
	040000 tree $HEAD_dir       -	dir
	160000 commit $HEAD_submodule       -	submodule
	EOF
test_ls_tree_format_mode_output "--long" "-r" <<-EOF
	100644 blob $HEAD_gitmodules      61	.gitmodules
	100644 blob $HEAD_dir_sub_file      13	dir/sub-file.t
	160000 commit $HEAD_submodule       -	submodule
	100644 blob $HEAD_top_file       9	top-file.t
	EOF
## opt = --name-only
test_ls_tree_format_mode_output "--name-only" "" "-t" <<-EOF
	.gitmodules
	dir
	submodule
	top-file.t
	EOF
test_ls_tree_format_mode_output "--name-only" "-d" <<-EOF
	dir
	submodule
	EOF
test_ls_tree_format_mode_output "--name-only" "-r" <<-EOF
	.gitmodules
	dir/sub-file.t
	submodule
	top-file.t
	EOF
## opt = --object-only
test_ls_tree_format_mode_output "--object-only" "" "-t" <<-EOF
	$HEAD_gitmodules
	$HEAD_dir
	$HEAD_submodule
	$HEAD_top_file
	EOF
test_ls_tree_format_mode_output "--object-only" "-d" <<-EOF
	$HEAD_dir
	$HEAD_submodule
	EOF
test_ls_tree_format_mode_output "--object-only" "-r" <<-EOF
	$HEAD_gitmodules
	$HEAD_dir_sub_file
	$HEAD_submodule
	$HEAD_top_file
	EOF
## opt = --object-only --abbrev
test_expect_success 'setup: HEAD_short_* variables' '
	HEAD_short_gitmodules=$(git rev-parse --short HEAD:.gitmodules) &&
	HEAD_short_dir=$(git rev-parse --short HEAD:dir) &&
	HEAD_short_top_file=$(git rev-parse --short HEAD:top-file.t) &&
	HEAD_short_submodule=$(git rev-parse --short HEAD:submodule) &&
	HEAD_short_dir_sub_file=$(git rev-parse --short HEAD:dir/sub-file.t)
'
test_ls_tree_format_mode_output "--object-only --abbrev" "" "-t" <<-EOF
	$HEAD_short_gitmodules
	$HEAD_short_dir
	$HEAD_short_submodule
	$HEAD_short_top_file
	EOF
test_ls_tree_format_mode_output "--object-only --abbrev" "-d" <<-EOF
	$HEAD_short_dir
	$HEAD_short_submodule
	EOF
test_ls_tree_format_mode_output "--object-only --abbrev" "-r" <<-EOF
	$HEAD_short_gitmodules
	$HEAD_short_dir_sub_file
	$HEAD_short_submodule
	$HEAD_short_top_file
	EOF
## opt = --full-name
test_ls_tree_format_mode_output "--full-name" "" <<-EOF
	100644 blob $HEAD_gitmodules	.gitmodules
	040000 tree $HEAD_dir	dir
	160000 commit $HEAD_submodule	submodule
	100644 blob $HEAD_top_file	top-file.t
	EOF
test_ls_tree_format_mode_output "--full-name" "-d" <<-EOF
	040000 tree $HEAD_dir	dir
	160000 commit $HEAD_submodule	submodule
	EOF
test_ls_tree_format_mode_output "--full-name" "-r" <<-EOF
	100644 blob $HEAD_gitmodules	.gitmodules
	100644 blob $HEAD_dir_sub_file	dir/sub-file.t
	160000 commit $HEAD_submodule	submodule
	100644 blob $HEAD_top_file	top-file.t
	EOF
test_ls_tree_format_mode_output "--full-name" "-t" <<-EOF
	100644 blob $HEAD_gitmodules	.gitmodules
	040000 tree $HEAD_dir	dir
	160000 commit $HEAD_submodule	submodule
	100644 blob $HEAD_top_file	top-file.t
	EOF
## opt = --full-tree
test_ls_tree_format_mode_output "--full-tree" "" "-t" <<-EOF
	100644 blob $HEAD_gitmodules	.gitmodules
	040000 tree $HEAD_dir	dir
	160000 commit $HEAD_submodule	submodule
	100644 blob $HEAD_top_file	top-file.t
	EOF
test_ls_tree_format_mode_output "--full-tree" "-d" <<-EOF
	040000 tree $HEAD_dir	dir
	160000 commit $HEAD_submodule	submodule
	EOF
test_ls_tree_format_mode_output "--full-tree" "-r" <<-EOF
	100644 blob $HEAD_gitmodules	.gitmodules
	100644 blob $HEAD_dir_sub_file	dir/sub-file.t
	160000 commit $HEAD_submodule	submodule
	100644 blob $HEAD_top_file	top-file.t
	EOF

test_done
