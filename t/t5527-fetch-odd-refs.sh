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

test_done
