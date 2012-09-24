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
	test `cat last` = 17'

check_mailinfo () {
	mail=$1 opt=$2
	mo="$mail$opt"
	git mailinfo -u $opt msg$mo patch$mo <$mail >info$mo &&
	test_cmp "$TEST_DIRECTORY"/t5100/msg$mo msg$mo &&
	test_cmp "$TEST_DIRECTORY"/t5100/patch$mo patch$mo &&
	test_cmp "$TEST_DIRECTORY"/t5100/info$mo info$mo
}


for mail in `echo 00*`
do
	test_expect_success "mailinfo $mail" '
		check_mailinfo $mail "" &&
		if test -f "$TEST_DIRECTORY"/t5100/msg$mail--scissors
		then
			check_mailinfo $mail --scissors
		fi &&
		if test -f "$TEST_DIRECTORY"/t5100/msg$mail--no-inbody-headers
		then
			check_mailinfo $mail --no-inbody-headers
		fi
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
	test_line_count = 4 patch

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
