#!/bin/sh

test_description='detect some push errors early (before contacting remote)'
. ./test-lib.sh

test_expect_success 'setup cummits' '
	test_cummit one
'

test_expect_success 'setup remote' '
	but init --bare remote.but &&
	but remote add origin remote.but
'

test_expect_success 'setup fake receive-pack' '
	FAKE_RP_ROOT=$(pwd) &&
	export FAKE_RP_ROOT &&
	write_script fake-rp <<-\EOF &&
	echo yes >"$FAKE_RP_ROOT"/rp-ran
	exit 1
	EOF
	but config remote.origin.receivepack "\"\$FAKE_RP_ROOT/fake-rp\""
'

test_expect_success 'detect missing branches early' '
	echo no >rp-ran &&
	echo no >expect &&
	test_must_fail but push origin missing &&
	test_cmp expect rp-ran
'

test_expect_success 'detect missing sha1 expressions early' '
	echo no >rp-ran &&
	echo no >expect &&
	test_must_fail but push origin main~2:main &&
	test_cmp expect rp-ran
'

test_expect_success 'detect ambiguous refs early' '
	but branch foo &&
	but tag foo &&
	echo no >rp-ran &&
	echo no >expect &&
	test_must_fail but push origin foo &&
	test_cmp expect rp-ran
'

test_done
