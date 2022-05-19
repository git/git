#!/bin/sh

test_description='test fetching of oddly-named refs'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# afterwards we will have:
#  HEAD - two
#  refs/for/refs/heads/main - one
#  refs/heads/main - three
test_expect_success 'setup repo with odd suffix ref' '
	echo content >file &&
	but add . &&
	but cummit -m one &&
	but update-ref refs/for/refs/heads/main HEAD &&
	echo content >>file &&
	but cummit -a -m two &&
	echo content >>file &&
	but cummit -a -m three &&
	but checkout HEAD^
'

test_expect_success 'suffix ref is ignored during fetch' '
	but clone --bare file://"$PWD" suffix &&
	echo three >expect &&
	but --but-dir=suffix log -1 --format=%s refs/heads/main >actual &&
	test_cmp expect actual
'

test_expect_success 'try to create repo with absurdly long refname' '
	ref240=$ZERO_OID/$ZERO_OID/$ZERO_OID/$ZERO_OID/$ZERO_OID/$ZERO_OID &&
	ref1440=$ref240/$ref240/$ref240/$ref240/$ref240/$ref240 &&
	but init long &&
	(
		cd long &&
		test_cummit long &&
		test_cummit main
	) &&
	if but -C long update-ref refs/heads/$ref1440 long; then
		test_set_prereq LONG_REF
	else
		echo >&2 "long refs not supported"
	fi
'

test_expect_success LONG_REF 'fetch handles extremely long refname' '
	but fetch long refs/heads/*:refs/remotes/long/* &&
	cat >expect <<-\EOF &&
	long
	main
	EOF
	but for-each-ref --format="%(subject)" refs/remotes/long >actual &&
	test_cmp expect actual
'

test_expect_success LONG_REF 'push handles extremely long refname' '
	but push long :refs/heads/$ref1440 &&
	but -C long for-each-ref --format="%(subject)" refs/heads >actual &&
	echo main >expect &&
	test_cmp expect actual
'

test_done
