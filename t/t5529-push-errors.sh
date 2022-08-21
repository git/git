#!/bin/sh

test_description='detect some push errors early (before contacting remote)'
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

test_expect_success 'detect ambiguous refs early' '
	git branch foo &&
	git tag foo &&
	echo no >rp-ran &&
	echo no >expect &&
	test_must_fail git push origin foo &&
	test_cmp expect rp-ran
'

test_done
