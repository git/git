#!/bin/sh
#
# Copyright (c) 2016 Thurston Stone
#


test_description='check-git-ignore-cmd'

. ./test-lib.sh

test_expect_success 'setup' '
	echo a >a &&
	git add a &&
	git commit -m"adding initial files"
'

test_expect_success 'ignore at root' '
	echo a >ignoreme.txt &&
	git ignore ignoreme.txt &&
	echo "ignoreme.txt"  >expect &&
	cat .gitignore >actual &&
	test_cmp expect actual
'

test_expect_success 'ignore in subdir' '
	rm .gitignore &&
	mkdir -p "sub/dir with space" &&
	echo a >"sub/dir with space/ignoreme.txt" &&
	(
		cd "sub/dir with space" &&
		git ignore -v ignoreme.txt
	) &&
	echo "sub/dir with space/ignoreme.txt"	>expect &&
	cat .gitignore >actual &&
	test_cmp expect actual
'

test_expect_success 'ignore extentions at root' '
	rm .gitignore &&
	echo a >ignoreme.txt &&
	git ignore -v -e ignoreme.txt &&
	echo "*.txt" >expect &&
	cat .gitignore >actual &&
	test_cmp expect actual
'

test_expect_success 'ignore extentions in subdir' '
	rm .gitignore &&
	mkdir -p "sub/dir with space" &&
	echo a >"sub/dir with space/ignoreme.txt" &&
	(
		cd "sub/dir with space" &&
		git ignore -v -e ignoreme.txt
	) &&
	echo "sub/dir with space/*.txt" >expect &&
	cat .gitignore >actual &&
	test_cmp expect actual
'

test_expect_success 'ignore extentions anywhere' '
	rm .gitignore &&
	mkdir -p "sub/dir with space" &&
	echo a >"sub/dir with space/ignoreme.txt" &&
	(
		cd "sub/dir with space" &&
		git ignore -v -E ignoreme.txt
	) &&
	echo "**/*.txt" >expect &&
	cat .gitignore >actual &&
	test_cmp expect actual
'

test_expect_success 'ignore directory' '
	rm .gitignore &&
	mkdir -p "sub/dir with space" &&
	echo a >"sub/dir with space/ignoreme.txt" &&
	(
		cd "sub/dir with space" &&
		git ignore -v -d ignoreme.txt
	) &&
	echo "sub/dir with space/*" >expect &&
	cat .gitignore >actual &&
	test_cmp expect actual
'

test_expect_success 'ignore filename anywhere' '
	rm .gitignore &&
	mkdir -p "sub/dir with space" &&
	echo a >"sub/dir with space/ignoreme.txt" &&
	(
		cd "sub/dir with space" &&
		git ignore -v -a ignoreme.txt
	) &&
	echo "**/ignoreme.txt" >expect &&
	cat .gitignore >actual &&
	test_cmp expect actual
'

test_expect_success 'dry run does not write anything' '
	rm .gitignore &&
	echo a >ignoreme.txt &&
	git ignore -v -n ignoreme.txt >output &&
	grep "^DRY-RUN!" <output &&
	test_path_is_missing .gitignore
'

test_expect_success 'parent-level set to current dir' '
	mkdir -p "sub/dir with space" &&
	echo a >"sub/dir with space/ignoreme.txt" &&
	(
		cd "sub/dir with space" &&
		git ignore -v -p 0 ignoreme.txt
	) &&
	echo "ignoreme.txt" >expect &&
	cat "sub/dir with space/.gitignore" >actual &&
	test_cmp expect actual
'

test_expect_success 'parent-level set to dir outside of repo top-level' '
	mkdir -p "sub/dir with space" &&
	echo a >"sub/dir with space/ignoreme.txt" &&
	(
		cd "sub/dir with space" &&
		git ignore -v -p 2 ignoreme.txt >output
	) &&
	grep "^WARNING" <"sub/dir with space/output" &&
	echo "sub/dir with space/ignoreme.txt" >expect &&
	cat .gitignore >actual &&
	test_cmp expect actual
'

test_expect_success 'parent-level set to mutliple gitignores' '
	mkdir -p "sub/dir1 with space/test" &&
	echo a >"sub/dir1 with space/test/ignoreme.txt" &&
	mkdir -p "sub/dir2 with space/test" &&
	echo a >"sub/dir2 with space/test/ignoreme.txt" &&
	git ignore -v -p 1 "sub/dir1 with space/test/ignoreme.txt" "sub/dir2 with space/test/ignoreme.txt" &&
	echo "test/ignoreme.txt" >expect &&
	cat "sub/dir1 with space/.gitignore" >actual &&
	test_cmp expect actual &&
	cat "sub/dir2 with space/.gitignore" >actual &&
	test_cmp expect actual
'

setup_fake_editor () {
	write_script fake-editor <<-\EOF
set -x
file=$1
printf "edited the file like a boss">"$1"
EOF
}

test_set_editor "$(pwd)/fake-editor"

test_expect_success 'edit root gitignore' '
	setup_fake_editor &&
	mkdir -p "sub/dir with space" &&
	(
		cd "sub/dir with space" &&
		git ignore -v --edit
	) &&
	printf "edited the file like a boss" >expect &&
	cat .gitignore >actual &&
	test_cmp expect actual
'

test_expect_success 'edit root gitignore using --parent-level' '
	setup_fake_editor &&
	mkdir -p "sub/dir with space/test" &&
	(
		cd "sub/dir with space/test" &&
		git ignore -p 2 --edit
	) &&
	printf "edited the file like a boss" >expect &&
	cat sub/.gitignore >actual &&
	test_cmp expect actual
'

test_done
