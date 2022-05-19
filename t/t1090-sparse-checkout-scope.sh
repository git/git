#!/bin/sh

test_description='sparse checkout scope tests'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	echo "initial" >a &&
	echo "initial" >b &&
	echo "initial" >c &&
	but add a b c &&
	but cummit -m "initial cummit"
'

test_expect_success 'create feature branch' '
	but checkout -b feature &&
	echo "modified" >b &&
	echo "modified" >c &&
	but add b c &&
	but cummit -m "modification"
'

test_expect_success 'perform sparse checkout of main' '
	but config --local --bool core.sparsecheckout true &&
	echo "!/*" >.but/info/sparse-checkout &&
	echo "/a" >>.but/info/sparse-checkout &&
	echo "/c" >>.but/info/sparse-checkout &&
	but checkout main &&
	test_path_is_file a &&
	test_path_is_missing b &&
	test_path_is_file c
'

test_expect_success 'merge feature branch into sparse checkout of main' '
	but merge feature &&
	test_path_is_file a &&
	test_path_is_missing b &&
	test_path_is_file c &&
	test "$(cat c)" = "modified"
'

test_expect_success 'return to full checkout of main' '
	but checkout feature &&
	echo "/*" >.but/info/sparse-checkout &&
	but checkout main &&
	test_path_is_file a &&
	test_path_is_file b &&
	test_path_is_file c &&
	test "$(cat b)" = "modified"
'

test_expect_success 'skip-worktree on files outside sparse patterns' '
	but sparse-checkout disable &&
	but sparse-checkout set --no-cone "a*" &&
	but checkout-index --all --ignore-skip-worktree-bits &&

	but ls-files -t >output &&
	! grep ^S output >actual &&
	test_must_be_empty actual &&

	test_config sparse.expectFilesOutsideOfPatterns true &&
	cat <<-\EOF >expect &&
	S b
	S c
	EOF
	but ls-files -t >output &&
	grep ^S output >actual &&
	test_cmp expect actual
'

test_expect_success 'in partial clone, sparse checkout only fetches needed blobs' '
	test_create_repo server &&
	but clone "file://$(pwd)/server" client &&

	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&
	echo a >server/a &&
	echo bb >server/b &&
	mkdir server/c &&
	echo ccc >server/c/c &&
	but -C server add a b c/c &&
	but -C server cummit -m message &&

	test_config -C client core.sparsecheckout 1 &&
	echo "!/*" >client/.but/info/sparse-checkout &&
	echo "/a" >>client/.but/info/sparse-checkout &&
	but -C client fetch --filter=blob:none origin &&
	but -C client checkout FETCH_HEAD &&

	but -C client rev-list HEAD \
		--quiet --objects --missing=print >unsorted_actual &&
	(
		printf "?" &&
		but hash-object server/b &&
		printf "?" &&
		but hash-object server/c/c
	) >unsorted_expect &&
	sort unsorted_actual >actual &&
	sort unsorted_expect >expect &&
	test_cmp expect actual
'

test_done
