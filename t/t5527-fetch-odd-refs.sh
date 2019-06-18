#!/bin/sh

test_description='test fetching of oddly-named refs'
. ./test-lib.sh

# afterwards we will have:
#  HEAD - two
#  refs/for/refs/heads/master - one
#  refs/heads/master - three
test_expect_success 'setup repo with odd suffix ref' '
	echo content >file &&
	git add . &&
	git commit -m one &&
	git update-ref refs/for/refs/heads/master HEAD &&
	echo content >>file &&
	git commit -a -m two &&
	echo content >>file &&
	git commit -a -m three &&
	git checkout HEAD^
'

test_expect_success 'suffix ref is ignored during fetch' '
	git clone --bare file://"$PWD" suffix &&
	echo three >expect &&
	git --git-dir=suffix log -1 --format=%s refs/heads/master >actual &&
	test_cmp expect actual
'

test_expect_success 'try to create repo with absurdly long refname' '
	ref240=$ZERO_OID/$ZERO_OID/$ZERO_OID/$ZERO_OID/$ZERO_OID/$ZERO_OID &&
	ref1440=$ref240/$ref240/$ref240/$ref240/$ref240/$ref240 &&
	git init long &&
	(
		cd long &&
		test_commit long &&
		test_commit master
	) &&
	if git -C long update-ref refs/heads/$ref1440 long; then
		test_set_prereq LONG_REF
	else
		echo >&2 "long refs not supported"
	fi
'

test_expect_success LONG_REF 'fetch handles extremely long refname' '
	git fetch long refs/heads/*:refs/remotes/long/* &&
	cat >expect <<-\EOF &&
	long
	master
	EOF
	git for-each-ref --format="%(subject)" refs/remotes/long >actual &&
	test_cmp expect actual
'

test_expect_success LONG_REF 'push handles extremely long refname' '
	git push long :refs/heads/$ref1440 &&
	git -C long for-each-ref --format="%(subject)" refs/heads >actual &&
	echo master >expect &&
	test_cmp expect actual
'

test_done
