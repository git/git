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

test_expect_success 'checkout -b checkout.optimizeNewBranch interaction' '
	cp .git/info/sparse-checkout .git/info/sparse-checkout.bak &&
	test_when_finished "
		mv -f .git/info/sparse-checkout.bak .git/info/sparse-checkout
		git checkout master
	" &&
	echo "/b" >>.git/info/sparse-checkout &&
	test "$(git ls-files -t b)" = "S b" &&
	git -c checkout.optimizeNewBranch=true checkout -b fast &&
	test "$(git ls-files -t b)" = "S b" &&
	git checkout -b slow &&
	test "$(git ls-files -t b)" = "H b"
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

test_expect_success 'in partial clone, sparse checkout only fetches needed blobs' '
	test_create_repo server &&
	git clone "file://$(pwd)/server" client &&

	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&
	echo a >server/a &&
	echo bb >server/b &&
	mkdir server/c &&
	echo ccc >server/c/c &&
	git -C server add a b c/c &&
	git -C server commit -m message &&

	test_config -C client core.sparsecheckout 1 &&
	test_config -C client extensions.partialclone origin &&
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

test_done
