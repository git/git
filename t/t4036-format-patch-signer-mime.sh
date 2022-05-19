#!/bin/sh

test_description='format-patch -s should force MIME encoding as needed'

. ./test-lib.sh

test_expect_success setup '

	>F &&
	but add F &&
	but cummit -m initial &&
	echo new line >F &&

	test_tick &&
	but cummit -m "This adds some lines to F" F

'

test_expect_success 'format normally' '

	but format-patch --stdout -1 >output &&
	! grep Content-Type output

'

test_expect_success 'format with signoff without funny signer name' '

	but format-patch -s --stdout -1 >output &&
	! grep Content-Type output

'

test_expect_success 'format with non ASCII signer name' '

	GIT_CUMMITTER_NAME="はまの ふにおう" \
	but format-patch -s --stdout -1 >output &&
	grep Content-Type output

'

test_expect_success 'attach and signoff do not duplicate mime headers' '

	GIT_CUMMITTER_NAME="はまの ふにおう" \
	but format-patch -s --stdout -1 --attach >output &&
	test $(grep -ci ^MIME-Version: output) = 1

'

test_done

