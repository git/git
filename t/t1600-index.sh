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
		>expect.err &&
		test_i18ncmp expect.err actual.err
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

test_expect_success 'GIT_INDEX_VERSION takes precedence over config' '
	(
		rm -f .git/index &&
		GIT_INDEX_VERSION=4 &&
		export GIT_INDEX_VERSION &&
		git config --add index.version 2 &&
		git add a 2>&1 &&
		echo 4 >expect &&
		test-index-version <.git/index >actual &&
		test_cmp expect actual
	)
'

test_done
