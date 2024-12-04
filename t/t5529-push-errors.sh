#!/bin/sh

test_description='detect some push errors early (before contacting remote)'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup commits' '
	test_commit one
'

test_expect_success 'setup remote' '
	git init --bare remote.git &&
	git remote add origin remote.git
'

test_expect_success 'setup fake receive-pack' '
	FAKE_RP_ROOT=$(pwd) &&
	export FAKE_RP_ROOT &&
	write_script fake-rp <<-\EOF &&
	echo yes >"$FAKE_RP_ROOT"/rp-ran
	exit 1
	EOF
	git config remote.origin.receivepack "\"\$FAKE_RP_ROOT/fake-rp\""
'

test_expect_success 'detect missing branches early' '
	echo no >rp-ran &&
	echo no >expect &&
	test_must_fail git push origin missing &&
	test_cmp expect rp-ran
'

test_expect_success 'detect missing sha1 expressions early' '
	echo no >rp-ran &&
	echo no >expect &&
	test_must_fail git push origin main~2:main &&
	test_cmp expect rp-ran
'

# We use an existing local_ref, since it follows a different flow in
# 'builtin/push.c:set_refspecs()' and we want to test that regression.
test_expect_success 'detect empty remote with existing local ref' '
	test_must_fail git push "" main 2> stderr &&
	grep "fatal: bad repository ${SQ}${SQ}" stderr
'

# While similar to the previous test, here we want to ensure that
# even targeted refspecs are handled.
test_expect_success 'detect empty remote with targeted refspec' '
	test_must_fail git push "" HEAD:refs/heads/main 2> stderr &&
	grep "fatal: bad repository ${SQ}${SQ}" stderr
'

test_expect_success 'detect ambiguous refs early' '
	git branch foo &&
	git tag foo &&
	echo no >rp-ran &&
	echo no >expect &&
	test_must_fail git push origin foo &&
	test_cmp expect rp-ran
'

test_done
