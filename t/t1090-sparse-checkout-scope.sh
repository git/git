#!/bin/sh

test_description='sparse checkout scope tests'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_CREATE_REPO_NO_TEMPLATE=1
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

test_expect_success 'perform sparse checkout of main' '
	git config --local --bool core.sparsecheckout true &&
	mkdir .git/info &&
	echo "!/*" >.git/info/sparse-checkout &&
	echo "/a" >>.git/info/sparse-checkout &&
	echo "/c" >>.git/info/sparse-checkout &&
	git checkout main &&
	test_path_is_file a &&
	test_path_is_missing b &&
	test_path_is_file c
'

test_expect_success 'merge feature branch into sparse checkout of main' '
	git merge feature &&
	test_path_is_file a &&
	test_path_is_missing b &&
	test_path_is_file c &&
	test "$(cat c)" = "modified"
'

test_expect_success 'return to full checkout of main' '
	git checkout feature &&
	echo "/*" >.git/info/sparse-checkout &&
	git checkout main &&
	test_path_is_file a &&
	test_path_is_file b &&
	test_path_is_file c &&
	test "$(cat b)" = "modified"
'

test_expect_success 'skip-worktree on files outside sparse patterns' '
	git sparse-checkout disable &&
	git sparse-checkout set --no-cone "a*" &&
	git checkout-index --all --ignore-skip-worktree-bits &&

	git ls-files -t >output &&
	! grep ^S output >actual &&
	test_must_be_empty actual &&

	test_config sparse.expectFilesOutsideOfPatterns true &&
	cat <<-\EOF >expect &&
	S b
	S c
	EOF
	git ls-files -t >output &&
	grep ^S output >actual &&
	test_cmp expect actual
'

test_expect_success 'in partial clone, sparse checkout only fetches needed blobs' '
	test_create_repo server &&
	git clone --template= "file://$(pwd)/server" client &&

	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&
	echo a >server/a &&
	echo bb >server/b &&
	mkdir server/c &&
	echo ccc >server/c/c &&
	git -C server add a b c/c &&
	git -C server commit -m message &&

	test_config -C client core.sparsecheckout 1 &&
	mkdir client/.git/info &&
	echo "!/*" >client/.git/info/sparse-checkout &&
	echo "/a" >>client/.git/info/sparse-checkout &&
	git -C client fetch --filter=blob:none origin &&
	git -C client checkout FETCH_HEAD &&

	git -C client rev-list HEAD \
		--quiet --objects --missing=print >unsorted_actual &&
	(
		printf "?" &&
		git hash-object server/b &&
		printf "?" &&
		git hash-object server/c/c
	) >unsorted_expect &&
	sort unsorted_actual >actual &&
	sort unsorted_expect >expect &&
	test_cmp expect actual
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
				--cacheinfo 100644,$sha1,excluded/$f || exit 1
		done &&
		test_tick &&
		git commit -m excluded &&
		GIT_TRACE_FSCACHE=1 git status >out 2>err &&
		grep excluded err >grep.out &&
		test_line_count = 1 grep.out
	)
'

test_done
