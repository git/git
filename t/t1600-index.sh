#!/bin/sh

test_description='index file specific tests'

. ./test-lib.sh

sane_unset GIT_TEST_SPLIT_INDEX

test_expect_success 'setup' '
	echo 1 >a
'

test_expect_success 'bogus GIT_INDEX_VERSION issues warning' '
	(
		rm -f .git/index &&
		GIT_INDEX_VERSION=2bogus &&
		export GIT_INDEX_VERSION &&
		git add a 2>err &&
		sed "s/[0-9]//" err >actual.err &&
		sed -e "s/ Z$/ /" <<-\EOF >expect.err &&
			warning: GIT_INDEX_VERSION set, but the value is invalid.
			Using version Z
		EOF
		test_cmp expect.err actual.err
	)
'

test_expect_success 'out of bounds GIT_INDEX_VERSION issues warning' '
	(
		rm -f .git/index &&
		GIT_INDEX_VERSION=1 &&
		export GIT_INDEX_VERSION &&
		git add a 2>err &&
		sed "s/[0-9]//" err >actual.err &&
		sed -e "s/ Z$/ /" <<-\EOF >expect.err &&
			warning: GIT_INDEX_VERSION set, but the value is invalid.
			Using version Z
		EOF
		test_cmp expect.err actual.err
	)
'

test_expect_success 'no warning with bogus GIT_INDEX_VERSION and existing index' '
	(
		GIT_INDEX_VERSION=1 &&
		export GIT_INDEX_VERSION &&
		git add a 2>actual.err &&
		test_must_be_empty actual.err
	)
'

test_expect_success 'out of bounds index.version issues warning' '
	(
		sane_unset GIT_INDEX_VERSION &&
		rm -f .git/index &&
		git config --add index.version 1 &&
		git add a 2>err &&
		sed "s/[0-9]//" err >actual.err &&
		sed -e "s/ Z$/ /" <<-\EOF >expect.err &&
			warning: index.version set, but the value is invalid.
			Using version Z
		EOF
		test_cmp expect.err actual.err
	)
'

test_expect_success 'index.skipHash config option' '
	rm -f .git/index &&
	git -c index.skipHash=true add a &&
	test_trailing_hash .git/index >hash &&
	echo $(test_oid zero) >expect &&
	test_cmp expect hash &&
	git fsck &&

	rm -f .git/index &&
	git -c feature.manyFiles=true add a &&
	test_trailing_hash .git/index >hash &&
	cmp expect hash &&

	rm -f .git/index &&
	git -c feature.manyFiles=true \
	    -c index.skipHash=false add a &&
	test_trailing_hash .git/index >hash &&
	! cmp expect hash &&

	test_commit start &&
	git -c protocol.file.allow=always submodule add ./ sub &&
	git config index.skipHash false &&
	git -C sub config index.skipHash true &&
	rm -f .git/modules/sub/index &&
	>sub/file &&
	git -C sub add a &&
	test_trailing_hash .git/modules/sub/index >hash &&
	test_cmp expect hash &&
	git -C sub fsck
'

test_index_version () {
	INDEX_VERSION_CONFIG=$1 &&
	FEATURE_MANY_FILES=$2 &&
	ENV_VAR_VERSION=$3
	EXPECTED_OUTPUT_VERSION=$4 &&
	(
		rm -f .git/index &&
		rm -f .git/config &&
		if test "$INDEX_VERSION_CONFIG" -ne 0
		then
			git config --add index.version $INDEX_VERSION_CONFIG
		fi &&
		git config --add feature.manyFiles $FEATURE_MANY_FILES
		if test "$ENV_VAR_VERSION" -ne 0
		then
			GIT_INDEX_VERSION=$ENV_VAR_VERSION &&
			export GIT_INDEX_VERSION
		else
			unset GIT_INDEX_VERSION
		fi &&
		git add a &&
		echo $EXPECTED_OUTPUT_VERSION >expect &&
		git update-index --show-index-version >actual &&
		test_cmp expect actual
	)
}

test_expect_success 'cache entry with a representable name length is accepted' '
	test-tool cache-entry 100 >actual &&
	test_grep "^ok: allocated cache entry for namelen 100" actual
'

test_expect_success SIZE_T_IS_64BIT \
	'over-long cache entry name length is rejected rather than truncated' '
	# 4294967296 == 2^32
	test_must_fail test-tool cache-entry 4294967296 2>err &&
	test_grep "index entry path is too long" err
'

test_expect_success 'path length enforced from filesystem to index' '
	name=a-somewhat-long-path-name &&
	echo foo >"$name" &&
	test_must_fail git -c index.maxPathLen=10 add "$name" 2>err &&
	test_grep "invalid path" err
'

test_expect_success 'path length enforced from tree to index' '
	name=a-somewhat-long-path-name &&
	echo foo >"$name" &&
	git add "$name" &&
	git commit -m long-path &&
	test_when_finished "rm -rf long-clone.git" &&
	test_must_fail git -c index.maxPathLen=10 clone . long-clone.git 2>err &&
	test_grep "invalid path" err
'

test_expect_success 'index.maxPathLen rejects out-of-range values' '
	test_must_fail git -c index.maxPathLen=-1 status 2>err &&
	test_grep "bad numeric config value" err &&
	test_must_fail git -c index.maxPathLen=2147483648 status 2>err &&
	test_grep "exceeds maximum" err
'

make_large_tree () {
	{
		printf "100644 " &&
		test-tool genbytes a "$1" &&
		printf "\0" &&
		test-tool genzeros "$(test_oid rawsz)"
	} >"tree-$1" &&
	git hash-object --literally -w -t tree "tree-$1"
}

check_overlong_tree () {
	test_must_fail git -C overlong "$@" >actual 2>err &&
	test_cmp expect-overlong err &&
	test_must_be_empty actual
}

test_expect_success EXPENSIVE,SIZE_T_IS_64BIT,LONG_IS_64BIT \
		"set up a tree path whose length wraps to a small int" '
	test_create_repo overlong &&
	(
		cd overlong &&
		subtree=$(make_large_tree 4294967294) &&
		printf "040000 tree %s\tabc\n" "$subtree" |
		git mktree >tree
	) &&
	overlong_tree=$(cat overlong/tree) &&
	echo "fatal: index entry path is too long" \
		"(4294967298 bytes)" >expect-overlong
'

test_expect_success EXPENSIVE,SIZE_T_IS_64BIT,LONG_IS_64BIT \
		"checkout rejects a path before its length is truncated" '
	check_overlong_tree checkout "$overlong_tree" -- :/
'

test_expect_success EXPENSIVE,SIZE_T_IS_64BIT,LONG_IS_64BIT \
		"restore rejects a path before its length is truncated" '
	check_overlong_tree restore --source="$overlong_tree" --staged -- :/
'

test_expect_success EXPENSIVE,SIZE_T_IS_64BIT,LONG_IS_64BIT \
		"ls-files rejects a path before its length is truncated" '
	check_overlong_tree ls-files --with-tree="$overlong_tree"
'

test_expect_success EXPENSIVE,SIZE_T_IS_64BIT,LONG_IS_64BIT \
		"clean up the large tree" '
	rm -rf overlong
'

test_expect_success EXPENSIVE,SIZE_T_IS_64BIT \
		"ls-files includes the terminator without signed overflow" '
	test_create_repo boundary &&
	test_when_finished "rm -rf boundary" &&
	(
		cd boundary &&
		make_large_tree 2147483647 >tree
	) &&
	echo 100644 >expect &&
	git -C boundary ls-files --with-tree="$(cat boundary/tree)" \
		--format="%(objectmode)" >actual &&
	test_cmp expect actual
'

# This must be the last test case because it deletes .git/config
test_expect_success 'index version config precedence' '
	test_index_version 0 false 0 2 &&
	test_index_version 2 false 0 2 &&
	test_index_version 3 false 0 2 &&
	test_index_version 4 false 0 4 &&
	test_index_version 2 false 4 4 &&
	test_index_version 2 true 0 2 &&
	test_index_version 0 true 0 4 &&
	test_index_version 0 true 2 2
'

test_done
