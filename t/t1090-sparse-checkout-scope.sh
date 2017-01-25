#!/bin/sh

test_description='sparse checkout scope tests'

. ./test-lib.sh

test_expect_success 'setup' '
	echo "initial" >a &&
	echo "initial" >b &&
	echo "initial" >c &&
	git add a b c &&
	git commit -m "initial commit"
'

test_expect_success 'create feature branch' '
	git checkout -b feature &&
	echo "modified" >b &&
	echo "modified" >c &&
	git add b c &&
	git commit -m "modification"
'

test_expect_success 'perform sparse checkout of master' '
	git config --local --bool core.sparsecheckout true &&
	echo "!/*" >.git/info/sparse-checkout &&
	echo "/a" >>.git/info/sparse-checkout &&
	echo "/c" >>.git/info/sparse-checkout &&
	git checkout master &&
	test_path_is_file a &&
	test_path_is_missing b &&
	test_path_is_file c
'

test_expect_success 'merge feature branch into sparse checkout of master' '
	git merge feature &&
	test_path_is_file a &&
	test_path_is_missing b &&
	test_path_is_file c &&
	test "$(cat c)" = "modified"
'

test_expect_success 'return to full checkout of master' '
	git checkout feature &&
	echo "/*" >.git/info/sparse-checkout &&
	git checkout master &&
	test_path_is_file a &&
	test_path_is_file b &&
	test_path_is_file c &&
	test "$(cat b)" = "modified"
'

test_expect_success MINGW 'no unnecessary opendir() with fscache' '
	git clone . fscache-test &&
	(
		cd fscache-test &&
		git config core.fscache 1 &&
		echo "/excluded/*" >.git/info/sparse-checkout &&
		for f in $(test_seq 10)
		do
			sha1=$(echo $f | git hash-object -w --stdin) &&
			git update-index --add \
				--cacheinfo 100644,$sha1,excluded/$f || break
		done &&
		test_tick &&
		git commit -m excluded &&
		GIT_TRACE_FSCACHE=1 git status >out 2>err &&
		grep excluded err >grep.out &&
		test_line_count = 1 grep.out
	)
'

test_done
