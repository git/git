#!/bin/sh

test_description='index file specific tests'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

sane_unset BUT_TEST_SPLIT_INDEX

test_expect_success 'setup' '
	echo 1 >a
'

test_expect_success 'bogus BUT_INDEX_VERSION issues warning' '
	(
		rm -f .but/index &&
		BUT_INDEX_VERSION=2bogus &&
		export BUT_INDEX_VERSION &&
		but add a 2>err &&
		sed "s/[0-9]//" err >actual.err &&
		sed -e "s/ Z$/ /" <<-\EOF >expect.err &&
			warning: BUT_INDEX_VERSION set, but the value is invalid.
			Using version Z
		EOF
		test_cmp expect.err actual.err
	)
'

test_expect_success 'out of bounds BUT_INDEX_VERSION issues warning' '
	(
		rm -f .but/index &&
		BUT_INDEX_VERSION=1 &&
		export BUT_INDEX_VERSION &&
		but add a 2>err &&
		sed "s/[0-9]//" err >actual.err &&
		sed -e "s/ Z$/ /" <<-\EOF >expect.err &&
			warning: BUT_INDEX_VERSION set, but the value is invalid.
			Using version Z
		EOF
		test_cmp expect.err actual.err
	)
'

test_expect_success 'no warning with bogus BUT_INDEX_VERSION and existing index' '
	(
		BUT_INDEX_VERSION=1 &&
		export BUT_INDEX_VERSION &&
		but add a 2>actual.err &&
		test_must_be_empty actual.err
	)
'

test_expect_success 'out of bounds index.version issues warning' '
	(
		sane_unset BUT_INDEX_VERSION &&
		rm -f .but/index &&
		but config --add index.version 1 &&
		but add a 2>err &&
		sed "s/[0-9]//" err >actual.err &&
		sed -e "s/ Z$/ /" <<-\EOF >expect.err &&
			warning: index.version set, but the value is invalid.
			Using version Z
		EOF
		test_cmp expect.err actual.err
	)
'

test_index_version () {
	INDEX_VERSION_CONFIG=$1 &&
	FEATURE_MANY_FILES=$2 &&
	ENV_VAR_VERSION=$3
	EXPECTED_OUTPUT_VERSION=$4 &&
	(
		rm -f .but/index &&
		rm -f .but/config &&
		if test "$INDEX_VERSION_CONFIG" -ne 0
		then
			but config --add index.version $INDEX_VERSION_CONFIG
		fi &&
		but config --add feature.manyFiles $FEATURE_MANY_FILES
		if test "$ENV_VAR_VERSION" -ne 0
		then
			BUT_INDEX_VERSION=$ENV_VAR_VERSION &&
			export BUT_INDEX_VERSION
		else
			unset BUT_INDEX_VERSION
		fi &&
		but add a &&
		echo $EXPECTED_OUTPUT_VERSION >expect &&
		test-tool index-version <.but/index >actual &&
		test_cmp expect actual
	)
}

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
