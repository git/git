#!/bin/sh

test_description='format-patch -s should force MIME encoding as needed'

. ./test-lib.sh

test_expect_success setup '

	>F &&
	git add F &&
	git commit -m initial &&
	echo new line >F &&

	test_tick &&
	git commit -m "This adds some lines to F" F

'

test_expect_success 'format normally' '

	git format-patch --stdout -1 >output &&
	! grep Content-Type output

'

test_expect_success 'format with signoff without funny signer name' '

	git format-patch -s --stdout -1 >output &&
	! grep Content-Type output

'

test_expect_success 'format with non ASCII signer name' '

	GIT_COMMITTER_NAME="はまの ふにおう" \
	git format-patch -s --stdout -1 >output &&
	grep Content-Type output

'

test_expect_success 'attach and signoff do not duplicate mime headers' '

	GIT_COMMITTER_NAME="はまの ふにおう" \
	git format-patch -s --stdout -1 --attach >output &&
	test `grep -ci ^MIME-Version: output` = 1

'

test_done

