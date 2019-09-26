#!/bin/sh

test_description='index file specific tests'

. ./test-lib.sh

test_expect_success 'setup' '
	echo 1 >a
'

test_expect_success 'bogus GIT_INDEX_VERSION issues warning' '
	(
		rm -f .git/index &&
		GIT_INDEX_VERSION=2bogus &&
		export GIT_INDEX_VERSION &&
		git add a 2>&1 | sed "s/[0-9]//" >actual.err &&
		sed -e "s/ Z$/ /" <<-\EOF >expect.err &&
			warning: GIT_INDEX_VERSION set, but the value is invalid.
			Using version Z
		EOF
		test_i18ncmp expect.err actual.err
	)
'

test_expect_success 'out of bounds GIT_INDEX_VERSION issues warning' '
	(
		rm -f .git/index &&
		GIT_INDEX_VERSION=1 &&
		export GIT_INDEX_VERSION &&
		git add a 2>&1 | sed "s/[0-9]//" >actual.err &&
		sed -e "s/ Z$/ /" <<-\EOF >expect.err &&
			warning: GIT_INDEX_VERSION set, but the value is invalid.
			Using version Z
		EOF
		test_i18ncmp expect.err actual.err
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
		git add a 2>&1 | sed "s/[0-9]//" >actual.err &&
		sed -e "s/ Z$/ /" <<-\EOF >expect.err &&
			warning: index.version set, but the value is invalid.
			Using version Z
		EOF
		test_i18ncmp expect.err actual.err
	)
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
		git add a 2>&1 &&
		echo $EXPECTED_OUTPUT_VERSION >expect &&
		test-tool index-version <.git/index >actual &&
		test_cmp expect actual
	)
}

test_expect_success 'index version config precedence' '
	test_index_version 2 false 4 4 &&
	test_index_version 2 true 0 2 &&
	test_index_version 0 true 0 4 &&
	test_index_version 0 true 2 2
'

test_done
