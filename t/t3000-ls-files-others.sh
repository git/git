#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='basic tests for ls-files --others

This test runs but ls-files --others with the following on the
filesystem.

    path0       - a file
    path1	- a symlink
    path2/file2 - a file in a directory
    path3-junk  - a file to confuse things
    path3/file3 - a file in a directory
    path4       - an empty directory
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup ' '
	date >path0 &&
	if test_have_prereq SYMLINKS
	then
		ln -s xyzzy path1
	else
		date >path1
	fi &&
	mkdir path2 path3 path4 &&
	date >path2/file2 &&
	date >path2-junk &&
	date >path3/file3 &&
	date >path3-junk &&
	but update-index --add path3-junk path3/file3
'

test_expect_success 'setup: expected output' '
	cat >expected1 <<-\EOF &&
	expected1
	expected2
	expected3
	output
	path0
	path1
	path2-junk
	path2/file2
	EOF

	sed -e "s|path2/file2|path2/|" <expected1 >expected2 &&
	cp expected2 expected3 &&
	echo path4/ >>expected2
'

test_expect_success 'ls-files --others' '
	but ls-files --others >output &&
	test_cmp expected1 output
'

test_expect_success 'ls-files --others --directory' '
	but ls-files --others --directory >output &&
	test_cmp expected2 output
'

test_expect_success '--no-empty-directory hides empty directory' '
	but ls-files --others --directory --no-empty-directory >output &&
	test_cmp expected3 output
'

test_expect_success 'ls-files --others handles non-submodule .but' '
	mkdir not-a-submodule &&
	echo foo >not-a-submodule/.but &&
	but ls-files -o >output &&
	test_cmp expected1 output
'

test_expect_success SYMLINKS 'ls-files --others with symlinked submodule' '
	but init super &&
	but init sub &&
	(
		cd sub &&
		>a &&
		but add a &&
		but cummit -m sub &&
		but pack-refs --all
	) &&
	(
		cd super &&
		"$SHELL_PATH" "$TEST_DIRECTORY/../contrib/workdir/but-new-workdir" ../sub sub &&
		but ls-files --others --exclude-standard >../actual
	) &&
	echo sub/ >expect &&
	test_cmp expect actual
'

test_expect_success 'setup nested pathspec search' '
	test_create_repo nested &&
	(
		cd nested &&

		mkdir -p partially_tracked/untracked_dir &&
		> partially_tracked/content &&
		> partially_tracked/untracked_dir/file &&

		mkdir -p untracked/deep &&
		> untracked/deep/path &&
		> untracked/deep/foo.c &&

		but add partially_tracked/content
	)
'

test_expect_success 'ls-files -o --directory with single deep dir pathspec' '
	(
		cd nested &&

		but ls-files -o --directory untracked/deep/ >actual &&

		cat <<-EOF >expect &&
		untracked/deep/
		EOF

		test_cmp expect actual
	)
'

test_expect_success 'ls-files -o --directory with multiple dir pathspecs' '
	(
		cd nested &&

		but ls-files -o --directory partially_tracked/ untracked/ >actual &&

		cat <<-EOF >expect &&
		partially_tracked/untracked_dir/
		untracked/
		EOF

		test_cmp expect actual
	)
'

test_expect_success 'ls-files -o --directory with mix dir/file pathspecs' '
	(
		cd nested &&

		but ls-files -o --directory partially_tracked/ untracked/deep/path >actual &&

		cat <<-EOF >expect &&
		partially_tracked/untracked_dir/
		untracked/deep/path
		EOF

		test_cmp expect actual
	)
'

test_expect_success 'ls-files -o --directory with glob filetype match' '
	(
		cd nested &&

		# globs kinda defeat --directory, but only for that pathspec
		but ls-files --others --directory partially_tracked "untracked/*.c" >actual &&

		cat <<-EOF >expect &&
		partially_tracked/untracked_dir/
		untracked/deep/foo.c
		EOF

		test_cmp expect actual
	)
'

test_expect_success 'ls-files -o --directory with mix of tracked states' '
	(
		cd nested &&

		# globs kinda defeat --directory, but only for that pathspec
		but ls-files --others --directory partially_tracked/ "untracked/?*" >actual &&

		cat <<-EOF >expect &&
		partially_tracked/untracked_dir/
		untracked/deep/
		EOF

		test_cmp expect actual
	)
'

test_expect_success 'ls-files -o --directory with glob filetype match only' '
	(
		cd nested &&

		but ls-files --others --directory "untracked/*.c" >actual &&

		cat <<-EOF >expect &&
		untracked/deep/foo.c
		EOF

		test_cmp expect actual
	)
'

test_expect_success 'ls-files -o --directory to get immediate paths under one dir only' '
	(
		cd nested &&

		but ls-files --others --directory "untracked/?*" >actual &&

		cat <<-EOF >expect &&
		untracked/deep/
		EOF

		test_cmp expect actual
	)
'

test_expect_success 'ls-files -o avoids listing untracked non-matching butdir' '
	test_when_finished "rm -rf nested/untracked/deep/empty" &&
	(
		cd nested &&

		but init untracked/deep/empty &&
		but ls-files --others "untracked/*.c" >actual &&

		cat <<-EOF >expect &&
		untracked/deep/foo.c
		EOF

		test_cmp expect actual
	)
'

test_done
