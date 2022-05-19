#!/bin/sh

test_description='check pre-push hooks'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	test_hook pre-push <<-\EOF &&
	cat >actual
	EOF

	but config push.default upstream &&
	but init --bare repo1 &&
	but remote add parent1 repo1 &&
	test_cummit one &&
	cat >expect <<-EOF &&
	HEAD $(but rev-parse HEAD) refs/heads/foreign $(test_oid zero)
	EOF

	test_when_finished "rm actual" &&
	but push parent1 HEAD:foreign &&
	test_cmp expect actual
'

cummit1="$(but rev-parse HEAD)"
export cummit1

test_expect_success 'push with failing hook' '
	test_hook pre-push <<-\EOF &&
	cat >actual &&
	exit 1
	EOF

	test_cummit two &&
	cat >expect <<-EOF &&
	HEAD $(but rev-parse HEAD) refs/heads/main $(test_oid zero)
	EOF

	test_when_finished "rm actual" &&
	test_must_fail but push parent1 HEAD &&
	test_cmp expect actual
'

test_expect_success '--no-verify bypasses hook' '
	but push --no-verify parent1 HEAD &&
	test_path_is_missing actual
'

cummit2="$(but rev-parse HEAD)"
export cummit2

test_expect_success 'push with hook' '
	test_hook --setup pre-push <<-\EOF &&
	echo "$1" >actual
	echo "$2" >>actual
	cat >>actual
	EOF

	cat >expect <<-EOF &&
	parent1
	repo1
	refs/heads/main $cummit2 refs/heads/foreign $cummit1
	EOF

	but push parent1 main:foreign &&
	test_cmp expect actual
'

test_expect_success 'add a branch' '
	but checkout -b other parent1/foreign &&
	test_cummit three
'

cummit3="$(but rev-parse HEAD)"
export cummit3

test_expect_success 'push to default' '
	cat >expect <<-EOF &&
	parent1
	repo1
	refs/heads/other $cummit3 refs/heads/foreign $cummit2
	EOF
	but push &&
	test_cmp expect actual
'

test_expect_success 'push non-branches' '
	cat >expect <<-EOF &&
	parent1
	repo1
	refs/tags/one $cummit1 refs/tags/tag1 $ZERO_OID
	HEAD~ $cummit2 refs/heads/prev $ZERO_OID
	EOF

	but push parent1 one:tag1 HEAD~:refs/heads/prev &&
	test_cmp expect actual
'

test_expect_success 'push delete' '
	cat >expect <<-EOF &&
	parent1
	repo1
	(delete) $ZERO_OID refs/heads/prev $cummit2
	EOF

	but push parent1 :prev &&
	test_cmp expect actual
'

test_expect_success 'push to URL' '
	cat >expect <<-EOF &&
	repo1
	repo1
	HEAD $cummit3 refs/heads/other $ZERO_OID
	EOF

	but push repo1 HEAD &&
	test_cmp expect actual
'

test_expect_success 'set up many-ref tests' '
	{
		nr=1000 &&
		while test $nr -lt 2000
		do
			nr=$(( $nr + 1 )) &&
			echo "create refs/heads/b/$nr $cummit3" || return 1
		done
	} | but update-ref --stdin
'

test_expect_success 'sigpipe does not cause pre-push hook failure' '
	test_hook --clobber pre-push <<-\EOF &&
	exit 0
	EOF
	but push parent1 "refs/heads/b/*:refs/heads/b/*"
'

test_done
