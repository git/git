#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='git mailinfo and git mailsplit test'

. ./test-lib.sh

DATA="$TEST_DIRECTORY/t5100"

test_expect_success 'split sample box' \
	'git mailsplit -o. "$DATA/sample.mbox" >last &&
	last=$(cat last) &&
	echo total is $last &&
	test $(cat last) = 18'

check_mailinfo () {
	mail=$1 opt=$2
	mo="$mail$opt"
	git mailinfo -u $opt "msg$mo" "patch$mo" <"$mail" >"info$mo" &&
	test_cmp "$DATA/msg$mo" "msg$mo" &&
	test_cmp "$DATA/patch$mo" "patch$mo" &&
	test_cmp "$DATA/info$mo" "info$mo"
}


for mail in 00*
do
	test_expect_success "mailinfo $mail" '
		check_mailinfo "$mail" "" &&
		if test -f "$DATA/msg$mail--scissors"
		then
			check_mailinfo "$mail" --scissors
		fi &&
		if test -f "$DATA/msg$mail--no-inbody-headers"
		then
			check_mailinfo "$mail" --no-inbody-headers
		fi &&
		if test -f "$DATA/msg$mail--message-id"
		then
			check_mailinfo "$mail" --message-id
		fi
	'
done


test_expect_success 'split box with rfc2047 samples' \
	'mkdir rfc2047 &&
	git mailsplit -orfc2047 "$DATA/rfc2047-samples.mbox" \
	  >rfc2047/last &&
	last=$(cat rfc2047/last) &&
	echo total is $last &&
	test $(cat rfc2047/last) = 11'

for mail in rfc2047/00*
do
	test_expect_success "mailinfo $mail" '
		git mailinfo -u "$mail-msg" "$mail-patch" <"$mail" >"$mail-info" &&
		echo msg &&
		test_cmp "$DATA/empty" "$mail-msg" &&
		echo patch &&
		test_cmp "$DATA/empty" "$mail-patch" &&
		echo info &&
		test_cmp "$DATA/rfc2047-info-$(basename $mail)" "$mail-info"
	'
done

test_expect_success 'respect NULs' '

	git mailsplit -d3 -o. "$DATA/nul-plain" &&
	test_cmp "$DATA/nul-plain" 001 &&
	(cat 001 | git mailinfo msg patch) &&
	test_line_count = 4 patch

'

test_expect_success 'Preserve NULs out of MIME encoded message' '

	git mailsplit -d5 -o. "$DATA/nul-b64.in" &&
	test_cmp "$DATA/nul-b64.in" 00001 &&
	git mailinfo msg patch <00001 &&
	test_cmp "$DATA/nul-b64.expect" patch

'

test_expect_success 'mailinfo on from header without name works' '

	mkdir info-from &&
	git mailsplit -oinfo-from "$DATA/info-from.in" &&
	test_cmp "$DATA/info-from.in" info-from/0001 &&
	git mailinfo info-from/msg info-from/patch \
	  <info-from/0001 >info-from/out &&
	test_cmp "$DATA/info-from.expect" info-from/out

'

test_expect_success 'mailinfo finds headers after embedded From line' '
	mkdir embed-from &&
	git mailsplit -oembed-from "$DATA/embed-from.in" &&
	test_cmp "$DATA/embed-from.in" embed-from/0001 &&
	git mailinfo embed-from/msg embed-from/patch \
	  <embed-from/0001 >embed-from/out &&
	test_cmp "$DATA/embed-from.expect" embed-from/out
'

test_expect_success 'mailinfo on message with quoted >From' '
	mkdir quoted-from &&
	git mailsplit -oquoted-from "$DATA/quoted-from.in" &&
	test_cmp "$DATA/quoted-from.in" quoted-from/0001 &&
	git mailinfo quoted-from/msg quoted-from/patch \
	  <quoted-from/0001 >quoted-from/out &&
	test_cmp "$DATA/quoted-from.expect" quoted-from/msg
'

test_expect_success 'mailinfo unescapes with --mboxrd' '
	mkdir mboxrd &&
	git mailsplit -omboxrd --mboxrd \
		"$DATA/sample.mboxrd" >last &&
	test x"$(cat last)" = x2 &&
	for i in 0001 0002
	do
		git mailinfo mboxrd/msg mboxrd/patch \
		  <mboxrd/$i >mboxrd/out &&
		test_cmp "$DATA/${i}mboxrd" mboxrd/msg
	done &&
	sp=" " &&
	echo "From " >expect &&
	echo "From " >>expect &&
	echo >> expect &&
	cat >sp <<-INPUT_END &&
	From mboxrd Mon Sep 17 00:00:00 2001
	From: trailing spacer <sp@example.com>
	Subject: [PATCH] a commit with trailing space

	From$sp
	>From$sp

	INPUT_END

	git mailsplit -f2 -omboxrd --mboxrd <sp >last &&
	test x"$(cat last)" = x1 &&
	git mailinfo mboxrd/msg mboxrd/patch <mboxrd/0003 &&
	test_cmp expect mboxrd/msg
'

test_expect_success 'mailinfo handles rfc2822 quoted-string' '
	mkdir quoted-string &&
	git mailinfo /dev/null /dev/null <"$DATA/quoted-string.in" \
		>quoted-string/info &&
	test_cmp "$DATA/quoted-string.expect" quoted-string/info
'

test_expect_success 'mailinfo handles rfc2822 comment' '
	mkdir comment &&
	git mailinfo /dev/null /dev/null <"$DATA/comment.in" \
		>comment/info &&
	test_cmp "$DATA/comment.expect" comment/info
'

test_expect_success 'mailinfo with mailinfo.scissors config' '
	test_config mailinfo.scissors true &&
	(
		mkdir sub &&
		cd sub &&
		git mailinfo ../msg0014.sc ../patch0014.sc <../0014 >../info0014.sc
	) &&
	test_cmp "$DATA/msg0014--scissors" msg0014.sc &&
	test_cmp "$DATA/patch0014--scissors" patch0014.sc &&
	test_cmp "$DATA/info0014--scissors" info0014.sc
'


test_expect_success 'mailinfo no options' '
	subj="$(echo "Subject: [PATCH] [other] [PATCH] message" |
		git mailinfo /dev/null /dev/null)" &&
	test z"$subj" = z"Subject: message"
'

test_expect_success 'mailinfo -k' '
	subj="$(echo "Subject: [PATCH] [other] [PATCH] message" |
		git mailinfo -k /dev/null /dev/null)" &&
	test z"$subj" = z"Subject: [PATCH] [other] [PATCH] message"
'

test_expect_success 'mailinfo -b no [PATCH]' '
	subj="$(echo "Subject: [other] message" |
		git mailinfo -b /dev/null /dev/null)" &&
	test z"$subj" = z"Subject: [other] message"
'

test_expect_success 'mailinfo -b leading [PATCH]' '
	subj="$(echo "Subject: [PATCH] [other] message" |
		git mailinfo -b /dev/null /dev/null)" &&
	test z"$subj" = z"Subject: [other] message"
'

test_expect_success 'mailinfo -b double [PATCH]' '
	subj="$(echo "Subject: [PATCH] [PATCH] message" |
		git mailinfo -b /dev/null /dev/null)" &&
	test z"$subj" = z"Subject: message"
'

test_expect_failure 'mailinfo -b trailing [PATCH]' '
	subj="$(echo "Subject: [other] [PATCH] message" |
		git mailinfo -b /dev/null /dev/null)" &&
	test z"$subj" = z"Subject: [other] message"
'

test_expect_failure 'mailinfo -b separated double [PATCH]' '
	subj="$(echo "Subject: [PATCH] [other] [PATCH] message" |
		git mailinfo -b /dev/null /dev/null)" &&
	test z"$subj" = z"Subject: [other] message"
'

test_done
