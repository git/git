#!/bin/sh

test_description='directory traversal handling, especially with common prefixes'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit hello &&

	>empty &&
	mkdir untracked_dir &&
	>untracked_dir/empty &&
	but init untracked_repo &&
	>untracked_repo/empty &&

	cat <<-EOF >.butignore &&
	ignored
	an_ignored_dir/
	EOF
	mkdir an_ignored_dir &&
	mkdir an_untracked_dir &&
	>an_ignored_dir/ignored &&
	>an_ignored_dir/untracked &&
	>an_untracked_dir/ignored &&
	>an_untracked_dir/untracked
'

test_expect_success 'but ls-files -o shows the right entries' '
	cat <<-EOF >expect &&
	.butignore
	actual
	an_ignored_dir/ignored
	an_ignored_dir/untracked
	an_untracked_dir/ignored
	an_untracked_dir/untracked
	empty
	expect
	untracked_dir/empty
	untracked_repo/
	EOF
	but ls-files -o >actual &&
	test_cmp expect actual
'

test_expect_success 'but ls-files -o --exclude-standard shows the right entries' '
	cat <<-EOF >expect &&
	.butignore
	actual
	an_untracked_dir/untracked
	empty
	expect
	untracked_dir/empty
	untracked_repo/
	EOF
	but ls-files -o --exclude-standard >actual &&
	test_cmp expect actual
'

test_expect_success 'but ls-files -o untracked_dir recurses' '
	echo untracked_dir/empty >expect &&
	but ls-files -o untracked_dir >actual &&
	test_cmp expect actual
'

test_expect_success 'but ls-files -o untracked_dir/ recurses' '
	echo untracked_dir/empty >expect &&
	but ls-files -o untracked_dir/ >actual &&
	test_cmp expect actual
'

test_expect_success 'but ls-files -o --directory untracked_dir does not recurse' '
	echo untracked_dir/ >expect &&
	but ls-files -o --directory untracked_dir >actual &&
	test_cmp expect actual
'

test_expect_success 'but ls-files -o --directory untracked_dir/ does not recurse' '
	echo untracked_dir/ >expect &&
	but ls-files -o --directory untracked_dir/ >actual &&
	test_cmp expect actual
'

test_expect_success 'but ls-files -o untracked_repo does not recurse' '
	echo untracked_repo/ >expect &&
	but ls-files -o untracked_repo >actual &&
	test_cmp expect actual
'

test_expect_success 'but ls-files -o untracked_repo/ does not recurse' '
	echo untracked_repo/ >expect &&
	but ls-files -o untracked_repo/ >actual &&
	test_cmp expect actual
'

test_expect_success 'but ls-files -o untracked_dir untracked_repo recurses into untracked_dir only' '
	cat <<-EOF >expect &&
	untracked_dir/empty
	untracked_repo/
	EOF
	but ls-files -o untracked_dir untracked_repo >actual &&
	test_cmp expect actual
'

test_expect_success 'but ls-files -o untracked_dir/ untracked_repo/ recurses into untracked_dir only' '
	cat <<-EOF >expect &&
	untracked_dir/empty
	untracked_repo/
	EOF
	but ls-files -o untracked_dir/ untracked_repo/ >actual &&
	test_cmp expect actual
'

test_expect_success 'but ls-files -o --directory untracked_dir untracked_repo does not recurse' '
	cat <<-EOF >expect &&
	untracked_dir/
	untracked_repo/
	EOF
	but ls-files -o --directory untracked_dir untracked_repo >actual &&
	test_cmp expect actual
'

test_expect_success 'but ls-files -o --directory untracked_dir/ untracked_repo/ does not recurse' '
	cat <<-EOF >expect &&
	untracked_dir/
	untracked_repo/
	EOF
	but ls-files -o --directory untracked_dir/ untracked_repo/ >actual &&
	test_cmp expect actual
'

test_expect_success 'but ls-files -o .but shows nothing' '
	but ls-files -o .but >actual &&
	test_must_be_empty actual
'

test_expect_success 'but ls-files -o .but/ shows nothing' '
	but ls-files -o .but/ >actual &&
	test_must_be_empty actual
'

test_expect_success FUNNYNAMES 'but ls-files -o untracked_* recurses appropriately' '
	mkdir "untracked_*" &&
	>"untracked_*/empty" &&

	cat <<-EOF >expect &&
	untracked_*/empty
	untracked_dir/empty
	untracked_repo/
	EOF
	but ls-files -o "untracked_*" >actual &&
	test_cmp expect actual
'

# It turns out fill_directory returns the right paths, but ls-files' post-call
# filtering in show_dir_entry() via calling dir_path_match() which ends up
# in but_fnmatch() has logic for PATHSPEC_ONESTAR that assumes the pathspec
# must match the full path; it doesn't check it for matching a leading
# directory.
test_expect_failure FUNNYNAMES 'but ls-files -o untracked_*/ recurses appropriately' '
	cat <<-EOF >expect &&
	untracked_*/empty
	untracked_dir/empty
	untracked_repo/
	EOF
	but ls-files -o "untracked_*/" >actual &&
	test_cmp expect actual
'

test_expect_success FUNNYNAMES 'but ls-files -o --directory untracked_* does not recurse' '
	cat <<-EOF >expect &&
	untracked_*/
	untracked_dir/
	untracked_repo/
	EOF
	but ls-files -o --directory "untracked_*" >actual &&
	test_cmp expect actual
'

test_expect_success FUNNYNAMES 'but ls-files -o --directory untracked_*/ does not recurse' '
	cat <<-EOF >expect &&
	untracked_*/
	untracked_dir/
	untracked_repo/
	EOF
	but ls-files -o --directory "untracked_*/" >actual &&
	test_cmp expect actual
'

test_expect_success 'but ls-files -o consistent between one or two dirs' '
	but ls-files -o --exclude-standard an_ignored_dir/ an_untracked_dir/ >tmp &&
	! grep ^an_ignored_dir/ tmp >expect &&
	but ls-files -o --exclude-standard an_ignored_dir/ >actual &&
	test_cmp expect actual
'

# ls-files doesn't have a way to request showing both untracked and ignored
# files at the same time, so use `but status --ignored`
test_expect_success 'but status --ignored shows same files under dir with or without pathspec' '
	cat <<-EOF >expect &&
	?? an_untracked_dir/
	!! an_untracked_dir/ignored
	EOF
	but status --porcelain --ignored >output &&
	grep an_untracked_dir output >expect &&
	but status --porcelain --ignored an_untracked_dir/ >actual &&
	test_cmp expect actual
'

test_done
