#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git mailinfo and git mailsplit test'

. ./test-lib.sh

test_expect_success 'split sample box' \
	'git mailsplit -o. "$TEST_DIRECTORY"/t5100/sample.mbox >last &&
	last=`cat last` &&
	echo total is $last &&
	test `cat last` = 13'

for mail in `echo 00*`
do
	test_expect_success "mailinfo $mail" '
		git mailinfo -u msg$mail patch$mail <$mail >info$mail &&
		echo msg &&
		test_cmp "$TEST_DIRECTORY"/t5100/msg$mail msg$mail &&
		echo patch &&
		test_cmp "$TEST_DIRECTORY"/t5100/patch$mail patch$mail &&
		echo info &&
		test_cmp "$TEST_DIRECTORY"/t5100/info$mail info$mail
	'
done


test_expect_success 'split box with rfc2047 samples' \
	'mkdir rfc2047 &&
	git mailsplit -orfc2047 "$TEST_DIRECTORY"/t5100/rfc2047-samples.mbox \
	  >rfc2047/last &&
	last=`cat rfc2047/last` &&
	echo total is $last &&
	test `cat rfc2047/last` = 11'

for mail in `echo rfc2047/00*`
do
	test_expect_success "mailinfo $mail" '
		git mailinfo -u $mail-msg $mail-patch <$mail >$mail-info &&
		echo msg &&
		test_cmp "$TEST_DIRECTORY"/t5100/empty $mail-msg &&
		echo patch &&
		test_cmp "$TEST_DIRECTORY"/t5100/empty $mail-patch &&
		echo info &&
		test_cmp "$TEST_DIRECTORY"/t5100/rfc2047-info-$(basename $mail) $mail-info
	'
done

test_expect_success 'respect NULs' '

	git mailsplit -d3 -o. "$TEST_DIRECTORY"/t5100/nul-plain &&
	test_cmp "$TEST_DIRECTORY"/t5100/nul-plain 001 &&
	(cat 001 | git mailinfo msg patch) &&
	test 4 = $(wc -l < patch)

'

test_expect_success 'Preserve NULs out of MIME encoded message' '

	git mailsplit -d5 -o. "$TEST_DIRECTORY"/t5100/nul-b64.in &&
	test_cmp "$TEST_DIRECTORY"/t5100/nul-b64.in 00001 &&
	git mailinfo msg patch <00001 &&
	test_cmp "$TEST_DIRECTORY"/t5100/nul-b64.expect patch

'

test_expect_success 'mailinfo on from header without name works' '

	mkdir info-from &&
	git mailsplit -oinfo-from "$TEST_DIRECTORY"/t5100/info-from.in &&
	test_cmp "$TEST_DIRECTORY"/t5100/info-from.in info-from/0001 &&
	git mailinfo info-from/msg info-from/patch \
	  <info-from/0001 >info-from/out &&
	test_cmp "$TEST_DIRECTORY"/t5100/info-from.expect info-from/out

'

test_done
