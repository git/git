#!/bin/sh

test_description='git send-email'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# May be altered later in the test
PREREQ="PERL"

replace_variable_fields () {
	sed	-e "s/^\(Date:\).*/\1 DATE-STRING/" \
		-e "s/^\(Message-ID:\).*/\1 MESSAGE-ID-STRING/" \
		-e "s/^\(X-Mailer:\).*/\1 X-MAILER-STRING/"
}

test_expect_success $PREREQ 'prepare reference tree' '
	echo "1A quick brown fox jumps over the" >file &&
	echo "lazy dog" >>file &&
	git add file &&
	GIT_AUTHOR_NAME="A" git commit -a -m "Initial."
'

test_expect_success $PREREQ 'Setup helper tool' '
	write_script fake.sendmail <<-\EOF &&
	shift
	output=1
	while test -f commandline$output
	do
		output=$(($output+1))
	done
	for a
	do
		echo "!$a!"
	done >commandline$output
	cat >"msgtxt$output"
	EOF
	git add fake.sendmail &&
	GIT_AUTHOR_NAME="A" git commit -a -m "Second."
'

clean_fake_sendmail () {
	rm -f commandline* msgtxt*
}

test_expect_success $PREREQ 'Extract patches' '
	patches=$(git format-patch -s --cc="One <one@example.com>" --cc=two@example.com -n HEAD^1) &&
	threaded_patches=$(git format-patch -o threaded --thread=shallow -s --in-reply-to="format" HEAD^1)
'

# Test no confirm early to ensure remaining tests will not hang
test_no_confirm () {
	rm -f no_confirm_okay
	echo n | \
		GIT_SEND_EMAIL_NOTTY=1 \
		git send-email \
		--from="Example <from@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		$@ \
		$patches >stdout &&
	! grep "Send this email" stdout &&
	>no_confirm_okay
}

# Exit immediately to prevent hang if a no-confirm test fails
check_no_confirm () {
	if ! test -f no_confirm_okay
	then
		say 'confirm test failed; skipping remaining tests to prevent hanging'
		PREREQ="$PREREQ,CHECK_NO_CONFIRM"
	fi
	return 0
}

test_expect_success $PREREQ 'No confirm with --suppress-cc' '
	test_no_confirm --suppress-cc=sob &&
	check_no_confirm
'


test_expect_success $PREREQ 'No confirm with --confirm=never' '
	test_no_confirm --confirm=never &&
	check_no_confirm
'

# leave sendemail.confirm set to never after this so that none of the
# remaining tests prompt unintentionally.
test_expect_success $PREREQ 'No confirm with sendemail.confirm=never' '
	git config sendemail.confirm never &&
	test_no_confirm --compose --subject=foo &&
	check_no_confirm
'

test_expect_success $PREREQ 'Send patches' '
	git send-email --suppress-cc=sob --from="Example <nobody@example.com>" --to=nobody@example.com --smtp-server="$(pwd)/fake.sendmail" $patches 2>errors
'

test_expect_success $PREREQ 'setup expect' '
	cat >expected <<-\EOF
	!nobody@example.com!
	!author@example.com!
	!one@example.com!
	!two@example.com!
	EOF
'

test_expect_success $PREREQ 'Verify commandline' '
	test_cmp expected commandline1
'

test_expect_success $PREREQ 'Send patches with --envelope-sender' '
	clean_fake_sendmail &&
	git send-email --envelope-sender="Patch Contributor <patch@example.com>" --suppress-cc=sob --from="Example <nobody@example.com>" --to=nobody@example.com --smtp-server="$(pwd)/fake.sendmail" $patches 2>errors
'

test_expect_success $PREREQ 'setup expect' '
	cat >expected <<-\EOF
	!patch@example.com!
	!-i!
	!nobody@example.com!
	!author@example.com!
	!one@example.com!
	!two@example.com!
	EOF
'

test_expect_success $PREREQ 'Verify commandline' '
	test_cmp expected commandline1
'

test_expect_success $PREREQ 'Send patches with --envelope-sender=auto' '
	clean_fake_sendmail &&
	git send-email --envelope-sender=auto --suppress-cc=sob --from="Example <nobody@example.com>" --to=nobody@example.com --smtp-server="$(pwd)/fake.sendmail" $patches 2>errors
'

test_expect_success $PREREQ 'setup expect' '
	cat >expected <<-\EOF
	!nobody@example.com!
	!-i!
	!nobody@example.com!
	!author@example.com!
	!one@example.com!
	!two@example.com!
	EOF
'

test_expect_success $PREREQ 'Verify commandline' '
	test_cmp expected commandline1
'

test_expect_success $PREREQ 'setup expect for cc trailer' "
cat >expected-cc <<\EOF
!recipient@example.com!
!author@example.com!
!one@example.com!
!two@example.com!
!three@example.com!
!four@example.com!
!five@example.com!
!six@example.com!
EOF
"

test_expect_success $PREREQ 'cc trailer with various syntax' '
	test_commit cc-trailer &&
	test_when_finished "git reset --hard HEAD^" &&
	git commit --amend -F - <<-EOF &&
	Test Cc: trailers.

	Cc: one@example.com
	Cc: <two@example.com> # trailing comments are ignored
	Cc: <three@example.com>, <not.four@example.com> one address per line
	Cc: "Some # Body" <four@example.com> [ <also.a.comment> ]
	Cc: five@example.com # not.six@example.com
	Cc: six@example.com, not.seven@example.com
	EOF
	clean_fake_sendmail &&
	git send-email -1 --to=recipient@example.com \
		--smtp-server="$(pwd)/fake.sendmail" &&
	test_cmp expected-cc commandline1
'

test_expect_success $PREREQ 'setup fake get_maintainer.pl script for cc trailer' "
	write_script expected-cc-script.sh <<-EOF
	echo 'One Person <one@example.com> (supporter:THIS (FOO/bar))'
	echo 'Two Person <two@example.com> (maintainer:THIS THING)'
	echo 'Third List <three@example.com> (moderated list:THIS THING (FOO/bar))'
	echo '<four@example.com> (moderated list:FOR THING)'
	echo 'five@example.com (open list:FOR THING (FOO/bar))'
	echo 'six@example.com (open list)'
	EOF
"

test_expect_success $PREREQ 'cc trailer with get_maintainer.pl output' '
	clean_fake_sendmail &&
	git send-email -1 --to=recipient@example.com \
		--cc-cmd=./expected-cc-script.sh \
		--smtp-server="$(pwd)/fake.sendmail" &&
	test_cmp expected-cc commandline1
'

test_expect_success $PREREQ 'setup expect' "
cat >expected-show-all-headers <<\EOF
0001-Second.patch
(mbox) Adding cc: A <author@example.com> from line 'From: A <author@example.com>'
(mbox) Adding cc: One <one@example.com> from line 'Cc: One <one@example.com>, two@example.com'
(mbox) Adding cc: two@example.com from line 'Cc: One <one@example.com>, two@example.com'
Dry-OK. Log says:
Server: relay.example.com
MAIL FROM:<from@example.com>
RCPT TO:<to@example.com>
RCPT TO:<cc@example.com>
RCPT TO:<author@example.com>
RCPT TO:<one@example.com>
RCPT TO:<two@example.com>
RCPT TO:<bcc@example.com>
From: Example <from@example.com>
To: to@example.com
Cc: cc@example.com,
	A <author@example.com>,
	One <one@example.com>,
	two@example.com
Subject: [PATCH 1/1] Second.
Date: DATE-STRING
Message-ID: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING
In-Reply-To: <unique-message-id@example.com>
References: <unique-message-id@example.com>
Reply-To: Reply <reply@example.com>
MIME-Version: 1.0
Content-Transfer-Encoding: 8bit

Result: OK
EOF
"

test_suppress_self () {
	test_commit $3 &&
	test_when_finished "git reset --hard HEAD^" &&

	write_script cccmd-sed <<-EOF &&
		sed -n -e s/^cccmd--//p "\$1"
	EOF

	git commit --amend --author="$1 <$2>" -F - &&
	clean_fake_sendmail &&
	git format-patch --stdout -1 >"suppress-self-$3.patch" &&

	git send-email --from="$1 <$2>" \
		--to=nobody@example.com \
		--cc-cmd=./cccmd-sed \
		--suppress-cc=self \
		--smtp-server="$(pwd)/fake.sendmail" \
		suppress-self-$3.patch &&

	mv msgtxt1 msgtxt1-$3 &&
	sed -e '/^$/q' msgtxt1-$3 >"msghdr1-$3" &&

	(grep '^Cc:' msghdr1-$3 >"actual-no-cc-$3";
	 test_must_be_empty actual-no-cc-$3)
}

test_suppress_self_unquoted () {
	test_suppress_self "$1" "$2" "unquoted-$3" <<-EOF
		test suppress-cc.self unquoted-$3 with name $1 email $2

		unquoted-$3

		cccmd--$1 <$2>

		Cc: $1 <$2>
		Signed-off-by: $1 <$2>
	EOF
}

test_suppress_self_quoted () {
	test_suppress_self "$1" "$2" "quoted-$3" <<-EOF
		test suppress-cc.self quoted-$3 with name $1 email $2

		quoted-$3

		cccmd--"$1" <$2>

		Cc: $1 <$2>
		Cc: "$1" <$2>
		Signed-off-by: $1 <$2>
		Signed-off-by: "$1" <$2>
	EOF
}

test_expect_success $PREREQ 'self name is suppressed' "
	test_suppress_self_unquoted 'A U Thor' 'author@example.com' \
		'self_name_suppressed'
"

test_expect_success $PREREQ 'self name with dot is suppressed' "
	test_suppress_self_quoted 'A U. Thor' 'author@example.com' \
		'self_name_dot_suppressed'
"

test_expect_success $PREREQ 'non-ascii self name is suppressed' "
	test_suppress_self_quoted 'Füñný Nâmé' 'odd_?=mail@example.com' \
		'non_ascii_self_suppressed'
"

# This name is long enough to force format-patch to split it into multiple
# encoded-words, assuming it uses UTF-8 with the "Q" encoding.
test_expect_success $PREREQ 'long non-ascii self name is suppressed' "
	test_suppress_self_quoted 'Ƒüñníęř €. Nâṁé' 'odd_?=mail@example.com' \
		'long_non_ascii_self_suppressed'
"

test_expect_success $PREREQ 'sanitized self name is suppressed' "
	test_suppress_self_unquoted '\"A U. Thor\"' 'author@example.com' \
		'self_name_sanitized_suppressed'
"

test_expect_success $PREREQ 'Show all headers' '
	git send-email \
		--dry-run \
		--suppress-cc=sob \
		--from="Example <from@example.com>" \
		--reply-to="Reply <reply@example.com>" \
		--to=to@example.com \
		--cc=cc@example.com \
		--bcc=bcc@example.com \
		--in-reply-to="<unique-message-id@example.com>" \
		--smtp-server relay.example.com \
		$patches | replace_variable_fields \
		>actual-show-all-headers &&
	test_cmp expected-show-all-headers actual-show-all-headers
'

test_expect_success $PREREQ 'Prompting works' '
	clean_fake_sendmail &&
	(echo "to@example.com" &&
	 echo "my-message-id@example.com"
	) | GIT_SEND_EMAIL_NOTTY=1 git send-email \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches \
		2>errors &&
		grep "^From: A U Thor <author@example.com>\$" msgtxt1 &&
		grep "^To: to@example.com\$" msgtxt1 &&
		grep "^In-Reply-To: <my-message-id@example.com>" msgtxt1
'

test_expect_success $PREREQ,AUTOIDENT 'implicit ident is allowed' '
	clean_fake_sendmail &&
	(sane_unset GIT_AUTHOR_NAME &&
	sane_unset GIT_AUTHOR_EMAIL &&
	sane_unset GIT_COMMITTER_NAME &&
	sane_unset GIT_COMMITTER_EMAIL &&
	GIT_SEND_EMAIL_NOTTY=1 git send-email \
		--smtp-server="$(pwd)/fake.sendmail" \
		--to=to@example.com \
		$patches </dev/null 2>errors
	)
'

test_expect_success $PREREQ,!AUTOIDENT 'broken implicit ident aborts send-email' '
	clean_fake_sendmail &&
	(sane_unset GIT_AUTHOR_NAME &&
	sane_unset GIT_AUTHOR_EMAIL &&
	sane_unset GIT_COMMITTER_NAME &&
	sane_unset GIT_COMMITTER_EMAIL &&
	GIT_SEND_EMAIL_NOTTY=1 && export GIT_SEND_EMAIL_NOTTY &&
	test_must_fail git send-email \
		--smtp-server="$(pwd)/fake.sendmail" \
		--to=to@example.com \
		$patches </dev/null 2>errors &&
	test_grep "tell me who you are" errors
	)
'

test_expect_success $PREREQ 'setup cmd scripts' '
	write_script tocmd-sed <<-\EOF &&
	sed -n -e "s/^tocmd--//p" "$1"
	EOF
	write_script cccmd-sed <<-\EOF &&
	sed -n -e "s/^cccmd--//p" "$1"
	EOF
	write_script headercmd-sed <<-\EOF
	sed -n -e "s/^headercmd--//p" "$1"
	EOF
'

test_expect_success $PREREQ 'tocmd works' '
	clean_fake_sendmail &&
	cp $patches tocmd.patch &&
	echo tocmd--tocmd@example.com >>tocmd.patch &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to-cmd=./tocmd-sed \
		--smtp-server="$(pwd)/fake.sendmail" \
		tocmd.patch \
		&&
	grep "^To: tocmd@example.com" msgtxt1
'

test_expect_success $PREREQ 'cccmd works' '
	clean_fake_sendmail &&
	cp $patches cccmd.patch &&
	echo "cccmd--  cccmd@example.com" >>cccmd.patch &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--cc-cmd=./cccmd-sed \
		--smtp-server="$(pwd)/fake.sendmail" \
		cccmd.patch \
		&&
	grep "^	cccmd@example.com" msgtxt1
'

test_expect_success $PREREQ 'headercmd works' '
	clean_fake_sendmail &&
	cp $patches headercmd.patch &&
	echo "headercmd--X-Debbugs-CC: dummy@example.com" >>headercmd.patch &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--header-cmd=./headercmd-sed \
		--smtp-server="$(pwd)/fake.sendmail" \
		headercmd.patch \
		&&
	grep "^X-Debbugs-CC: dummy@example.com" msgtxt1
'

test_expect_success $PREREQ '--no-header-cmd works' '
	clean_fake_sendmail &&
	cp $patches headercmd.patch &&
	echo "headercmd--X-Debbugs-CC: dummy@example.com" >>headercmd.patch &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--header-cmd=./headercmd-sed \
		--no-header-cmd \
		--smtp-server="$(pwd)/fake.sendmail" \
		headercmd.patch \
		&&
	! grep "^X-Debbugs-CC: dummy@example.com" msgtxt1
'

test_expect_success $PREREQ 'multiline fields are correctly unfolded' '
	clean_fake_sendmail &&
	cp $patches headercmd.patch &&
	write_script headercmd-multiline <<-\EOF &&
	echo "X-Debbugs-CC: someone@example.com
FoldedField: This is a tale
 best told using
 multiple lines."
	EOF
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--header-cmd=./headercmd-multiline \
		--smtp-server="$(pwd)/fake.sendmail" \
		headercmd.patch &&
	grep "^FoldedField: This is a tale best told using multiple lines.$" msgtxt1
'

# Blank lines in the middle of the output of a command are invalid.
test_expect_success $PREREQ 'malform output reported on blank lines in command output' '
	clean_fake_sendmail &&
	cp $patches headercmd.patch &&
	write_script headercmd-malformed-output <<-\EOF &&
	echo "X-Debbugs-CC: someone@example.com

SomeOtherField: someone-else@example.com"
	EOF
	! git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--header-cmd=./headercmd-malformed-output \
		--smtp-server="$(pwd)/fake.sendmail" \
		headercmd.patch
'

test_expect_success $PREREQ 'reject long lines' '
	z8=zzzzzzzz &&
	z64=$z8$z8$z8$z8$z8$z8$z8$z8 &&
	z512=$z64$z64$z64$z64$z64$z64$z64$z64 &&
	clean_fake_sendmail &&
	cp $patches longline.patch &&
	cat >>longline.patch <<-EOF &&
	$z512$z512
	not a long line
	$z512$z512
	EOF
	test_must_fail git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		--transfer-encoding=8bit \
		$patches longline.patch \
		2>actual &&
	cat >expect <<-\EOF &&
	fatal: longline.patch:35 is longer than 998 characters
	warning: no patches were sent
	EOF
	test_cmp expect actual
'

test_expect_success $PREREQ 'no patch was sent' '
	! test -e commandline1
'

test_expect_success $PREREQ 'Author From: in message body' '
	clean_fake_sendmail &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches &&
	sed "1,/^\$/d" <msgtxt1 >msgbody1 &&
	grep "From: A <author@example.com>" msgbody1
'

test_expect_success $PREREQ 'Author From: not in message body' '
	clean_fake_sendmail &&
	git send-email \
		--from="A <author@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches &&
	sed "1,/^\$/d" <msgtxt1 >msgbody1 &&
	! grep "From: A <author@example.com>" msgbody1
'

test_expect_success $PREREQ 'allow long lines with --no-validate' '
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		--no-validate \
		$patches longline.patch \
		2>errors
'

test_expect_success $PREREQ 'short lines with auto encoding are 8bit' '
	clean_fake_sendmail &&
	git send-email \
		--from="A <author@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		--transfer-encoding=auto \
		$patches &&
	grep "Content-Transfer-Encoding: 8bit" msgtxt1
'

test_expect_success $PREREQ 'long lines with auto encoding are quoted-printable' '
	clean_fake_sendmail &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		--transfer-encoding=auto \
		--no-validate \
		longline.patch &&
	grep "Content-Transfer-Encoding: quoted-printable" msgtxt1
'

test_expect_success $PREREQ 'carriage returns with auto encoding are quoted-printable' '
	clean_fake_sendmail &&
	cp $patches cr.patch &&
	printf "this is a line\r\n" >>cr.patch &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		--transfer-encoding=auto \
		--no-validate \
		cr.patch &&
	grep "Content-Transfer-Encoding: quoted-printable" msgtxt1
'

for enc in auto quoted-printable base64
do
	test_expect_success $PREREQ "--validate passes with encoding $enc" '
		git send-email \
			--from="Example <nobody@example.com>" \
			--to=nobody@example.com \
			--smtp-server="$(pwd)/fake.sendmail" \
			--transfer-encoding=$enc \
			--validate \
			$patches longline.patch
	'

done

test_expect_success $PREREQ "--validate respects relative core.hooksPath path" '
	clean_fake_sendmail &&
	mkdir my-hooks &&
	test_when_finished "rm my-hooks.ran" &&
	write_script my-hooks/sendemail-validate <<-\EOF &&
	>my-hooks.ran
	exit 1
	EOF
	test_config core.hooksPath "my-hooks" &&
	test_must_fail git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		--validate \
		longline.patch 2>actual &&
	test_path_is_file my-hooks.ran &&
	cat >expect <<-EOF &&
	fatal: longline.patch: rejected by sendemail-validate hook
	fatal: command '"'"'git hook run --ignore-missing sendemail-validate -- <patch> <header>'"'"' died with exit code 1
	warning: no patches were sent
	EOF
	test_cmp expect actual
'

test_expect_success $PREREQ "--validate respects absolute core.hooksPath path" '
	hooks_path="$(pwd)/my-hooks" &&
	test_config core.hooksPath "$hooks_path" &&
	test_when_finished "rm my-hooks.ran" &&
	test_must_fail git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		--validate \
		longline.patch 2>actual &&
	test_path_is_file my-hooks.ran &&
	cat >expect <<-EOF &&
	fatal: longline.patch: rejected by sendemail-validate hook
	fatal: command '"'"'git hook run --ignore-missing sendemail-validate -- <patch> <header>'"'"' died with exit code 1
	warning: no patches were sent
	EOF
	test_cmp expect actual
'

test_expect_success $PREREQ "--validate hook supports multiple addresses in arguments" '
	hooks_path="$(pwd)/my-hooks" &&
	test_config core.hooksPath "$hooks_path" &&
	test_when_finished "rm my-hooks.ran" &&
	test_must_fail git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com,abc@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		--validate \
		longline.patch 2>actual &&
	test_path_is_file my-hooks.ran &&
	cat >expect <<-EOF &&
	fatal: longline.patch: rejected by sendemail-validate hook
	fatal: command '"'"'git hook run --ignore-missing sendemail-validate -- <patch> <header>'"'"' died with exit code 1
	warning: no patches were sent
	EOF
	test_cmp expect actual
'

test_expect_success $PREREQ "--validate hook supports header argument" '
	write_script my-hooks/sendemail-validate <<-\EOF &&
	if test "$#" -ge 2
	then
		grep "X-test-header: v1.0" "$2"
	else
		echo "No header arg passed"
		exit 1
	fi
	EOF
	test_config core.hooksPath "my-hooks" &&
	rm -fr outdir &&
	git format-patch \
		--add-header="X-test-header: v1.0" \
		-n HEAD^1 -o outdir &&
	git send-email \
		--dry-run \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		--validate \
		outdir/000?-*.patch
'

test_expect_success $PREREQ 'clear message-id before parsing a new message' '
	clean_fake_sendmail &&
	echo true | write_script my-hooks/sendemail-validate &&
	test_config core.hooksPath my-hooks &&
	git send-email --validate --to=recipient@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches $threaded_patches &&
	id0=$(grep "^Message-ID: " $threaded_patches) &&
	id1=$(grep "^Message-ID: " msgtxt1) &&
	id2=$(grep "^Message-ID: " msgtxt2) &&
	test "z$id0" = "z$id2" &&
	test "z$id1" != "z$id2"
'

for enc in 7bit 8bit quoted-printable base64
do
	test_expect_success $PREREQ "--transfer-encoding=$enc produces correct header" '
		clean_fake_sendmail &&
		git send-email \
			--from="Example <nobody@example.com>" \
			--to=nobody@example.com \
			--smtp-server="$(pwd)/fake.sendmail" \
			--transfer-encoding=$enc \
			$patches &&
		grep "Content-Transfer-Encoding: $enc" msgtxt1
	'
done

test_expect_success $PREREQ 'Invalid In-Reply-To' '
	clean_fake_sendmail &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--in-reply-to=" " \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches \
		2>errors &&
	! grep "^In-Reply-To: < *>" msgtxt1
'

test_expect_success $PREREQ 'Valid In-Reply-To when prompting' '
	clean_fake_sendmail &&
	(echo "From Example <from@example.com>" &&
	 echo "To Example <to@example.com>" &&
	 echo ""
	) | GIT_SEND_EMAIL_NOTTY=1 git send-email \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches 2>errors &&
	! grep "^In-Reply-To: < *>" msgtxt1
'

test_expect_success $PREREQ 'In-Reply-To without --chain-reply-to' '
	clean_fake_sendmail &&
	echo "<unique-message-id@example.com>" >expect &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--no-chain-reply-to \
		--in-reply-to="$(cat expect)" \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches $patches $patches \
		2>errors &&
	# The first message is a reply to --in-reply-to
	sed -n -e "s/^In-Reply-To: *\(.*\)/\1/p" msgtxt1 >actual &&
	test_cmp expect actual &&
	# Second and subsequent messages are replies to the first one
	sed -n -e "s/^Message-ID: *\(.*\)/\1/p" msgtxt1 >expect &&
	sed -n -e "s/^In-Reply-To: *\(.*\)/\1/p" msgtxt2 >actual &&
	test_cmp expect actual &&
	sed -n -e "s/^In-Reply-To: *\(.*\)/\1/p" msgtxt3 >actual &&
	test_cmp expect actual
'

test_expect_success $PREREQ 'In-Reply-To with --chain-reply-to' '
	clean_fake_sendmail &&
	echo "<unique-message-id@example.com>" >expect &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--chain-reply-to \
		--in-reply-to="$(cat expect)" \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches $patches $patches \
		2>errors &&
	sed -n -e "s/^In-Reply-To: *\(.*\)/\1/p" msgtxt1 >actual &&
	test_cmp expect actual &&
	sed -n -e "s/^Message-ID: *\(.*\)/\1/p" msgtxt1 >expect &&
	sed -n -e "s/^In-Reply-To: *\(.*\)/\1/p" msgtxt2 >actual &&
	test_cmp expect actual &&
	sed -n -e "s/^Message-ID: *\(.*\)/\1/p" msgtxt2 >expect &&
	sed -n -e "s/^In-Reply-To: *\(.*\)/\1/p" msgtxt3 >actual &&
	test_cmp expect actual
'

test_set_editor "$(pwd)/fake-editor"

test_expect_success $PREREQ 'setup erroring fake editor' '
	write_script fake-editor <<-\EOF
	echo >&2 "I am about to error"
	exit 1
	EOF
'

test_expect_success $PREREQ 'fake editor dies with error' '
	clean_fake_sendmail &&
	test_must_fail git send-email \
		--compose --subject foo \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches 2>err &&
	grep "I am about to error" err &&
	grep "the editor exited uncleanly, aborting everything" err
'

test_expect_success $PREREQ 'setup fake editor' '
	write_script fake-editor <<-\EOF
	echo fake edit >>"$1"
	EOF
'

test_expect_success $PREREQ '--compose works' '
	clean_fake_sendmail &&
	git send-email \
	--compose --subject foo \
	--from="Example <nobody@example.com>" \
	--to=nobody@example.com \
	--smtp-server="$(pwd)/fake.sendmail" \
	$patches \
	2>errors
'

test_expect_success $PREREQ 'first message is compose text' '
	grep "^fake edit" msgtxt1
'

test_expect_success $PREREQ 'second message is patch' '
	grep "Subject:.*Second" msgtxt2
'

test_expect_success $PREREQ 'setup expect' "
cat >expected-suppress-sob <<\EOF
0001-Second.patch
(mbox) Adding cc: A <author@example.com> from line 'From: A <author@example.com>'
(mbox) Adding cc: One <one@example.com> from line 'Cc: One <one@example.com>, two@example.com'
(mbox) Adding cc: two@example.com from line 'Cc: One <one@example.com>, two@example.com'
Dry-OK. Log says:
Server: relay.example.com
MAIL FROM:<from@example.com>
RCPT TO:<to@example.com>
RCPT TO:<cc@example.com>
RCPT TO:<author@example.com>
RCPT TO:<one@example.com>
RCPT TO:<two@example.com>
From: Example <from@example.com>
To: to@example.com
Cc: cc@example.com,
	A <author@example.com>,
	One <one@example.com>,
	two@example.com
Subject: [PATCH 1/1] Second.
Date: DATE-STRING
Message-ID: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING
MIME-Version: 1.0
Content-Transfer-Encoding: 8bit

Result: OK
EOF
"

test_suppression () {
	git send-email \
		--dry-run \
		--suppress-cc=$1 ${2+"--suppress-cc=$2"} \
		--from="Example <from@example.com>" \
		--to=to@example.com \
		--smtp-server relay.example.com \
		$patches | replace_variable_fields \
		>actual-suppress-$1${2+"-$2"} &&
	test_cmp expected-suppress-$1${2+"-$2"} actual-suppress-$1${2+"-$2"}
}

test_expect_success $PREREQ 'sendemail.cc set' '
	git config sendemail.cc cc@example.com &&
	test_suppression sob
'

test_expect_success $PREREQ 'setup expect' "
cat >expected-suppress-sob <<\EOF
0001-Second.patch
(mbox) Adding cc: A <author@example.com> from line 'From: A <author@example.com>'
(mbox) Adding cc: One <one@example.com> from line 'Cc: One <one@example.com>, two@example.com'
(mbox) Adding cc: two@example.com from line 'Cc: One <one@example.com>, two@example.com'
Dry-OK. Log says:
Server: relay.example.com
MAIL FROM:<from@example.com>
RCPT TO:<to@example.com>
RCPT TO:<author@example.com>
RCPT TO:<one@example.com>
RCPT TO:<two@example.com>
From: Example <from@example.com>
To: to@example.com
Cc: A <author@example.com>,
	One <one@example.com>,
	two@example.com
Subject: [PATCH 1/1] Second.
Date: DATE-STRING
Message-ID: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING
MIME-Version: 1.0
Content-Transfer-Encoding: 8bit

Result: OK
EOF
"

test_expect_success $PREREQ 'sendemail.cc unset' '
	git config --unset sendemail.cc &&
	test_suppression sob
'

test_expect_success $PREREQ 'setup expect' "
cat >expected-suppress-cccmd <<\EOF
0001-Second.patch
(mbox) Adding cc: A <author@example.com> from line 'From: A <author@example.com>'
(mbox) Adding cc: One <one@example.com> from line 'Cc: One <one@example.com>, two@example.com'
(mbox) Adding cc: two@example.com from line 'Cc: One <one@example.com>, two@example.com'
(body) Adding cc: C O Mitter <committer@example.com> from line 'Signed-off-by: C O Mitter <committer@example.com>'
Dry-OK. Log says:
Server: relay.example.com
MAIL FROM:<from@example.com>
RCPT TO:<to@example.com>
RCPT TO:<author@example.com>
RCPT TO:<one@example.com>
RCPT TO:<two@example.com>
RCPT TO:<committer@example.com>
From: Example <from@example.com>
To: to@example.com
Cc: A <author@example.com>,
	One <one@example.com>,
	two@example.com,
	C O Mitter <committer@example.com>
Subject: [PATCH 1/1] Second.
Date: DATE-STRING
Message-ID: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING
MIME-Version: 1.0
Content-Transfer-Encoding: 8bit

Result: OK
EOF
"

test_expect_success $PREREQ 'sendemail.cccmd' '
	write_script cccmd <<-\EOF &&
	echo cc-cmd@example.com
	EOF
	git config sendemail.cccmd ./cccmd &&
	test_suppression cccmd
'

test_expect_success $PREREQ 'setup expect' '
cat >expected-suppress-all <<\EOF
0001-Second.patch
Dry-OK. Log says:
Server: relay.example.com
MAIL FROM:<from@example.com>
RCPT TO:<to@example.com>
From: Example <from@example.com>
To: to@example.com
Subject: [PATCH 1/1] Second.
Date: DATE-STRING
Message-ID: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING
MIME-Version: 1.0
Content-Transfer-Encoding: 8bit

Result: OK
EOF
'

test_expect_success $PREREQ '--suppress-cc=all' '
	test_suppression all
'

test_expect_success $PREREQ 'setup expect' "
cat >expected-suppress-body <<\EOF
0001-Second.patch
(mbox) Adding cc: A <author@example.com> from line 'From: A <author@example.com>'
(mbox) Adding cc: One <one@example.com> from line 'Cc: One <one@example.com>, two@example.com'
(mbox) Adding cc: two@example.com from line 'Cc: One <one@example.com>, two@example.com'
(cc-cmd) Adding cc: cc-cmd@example.com from: './cccmd'
Dry-OK. Log says:
Server: relay.example.com
MAIL FROM:<from@example.com>
RCPT TO:<to@example.com>
RCPT TO:<author@example.com>
RCPT TO:<one@example.com>
RCPT TO:<two@example.com>
RCPT TO:<cc-cmd@example.com>
From: Example <from@example.com>
To: to@example.com
Cc: A <author@example.com>,
	One <one@example.com>,
	two@example.com,
	cc-cmd@example.com
Subject: [PATCH 1/1] Second.
Date: DATE-STRING
Message-ID: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING
MIME-Version: 1.0
Content-Transfer-Encoding: 8bit

Result: OK
EOF
"

test_expect_success $PREREQ '--suppress-cc=body' '
	test_suppression body
'

test_expect_success $PREREQ 'setup expect' "
cat >expected-suppress-body-cccmd <<\EOF
0001-Second.patch
(mbox) Adding cc: A <author@example.com> from line 'From: A <author@example.com>'
(mbox) Adding cc: One <one@example.com> from line 'Cc: One <one@example.com>, two@example.com'
(mbox) Adding cc: two@example.com from line 'Cc: One <one@example.com>, two@example.com'
Dry-OK. Log says:
Server: relay.example.com
MAIL FROM:<from@example.com>
RCPT TO:<to@example.com>
RCPT TO:<author@example.com>
RCPT TO:<one@example.com>
RCPT TO:<two@example.com>
From: Example <from@example.com>
To: to@example.com
Cc: A <author@example.com>,
	One <one@example.com>,
	two@example.com
Subject: [PATCH 1/1] Second.
Date: DATE-STRING
Message-ID: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING
MIME-Version: 1.0
Content-Transfer-Encoding: 8bit

Result: OK
EOF
"

test_expect_success $PREREQ '--suppress-cc=body --suppress-cc=cccmd' '
	test_suppression body cccmd
'

test_expect_success $PREREQ 'setup expect' "
cat >expected-suppress-sob <<\EOF
0001-Second.patch
(mbox) Adding cc: A <author@example.com> from line 'From: A <author@example.com>'
(mbox) Adding cc: One <one@example.com> from line 'Cc: One <one@example.com>, two@example.com'
(mbox) Adding cc: two@example.com from line 'Cc: One <one@example.com>, two@example.com'
Dry-OK. Log says:
Server: relay.example.com
MAIL FROM:<from@example.com>
RCPT TO:<to@example.com>
RCPT TO:<author@example.com>
RCPT TO:<one@example.com>
RCPT TO:<two@example.com>
From: Example <from@example.com>
To: to@example.com
Cc: A <author@example.com>,
	One <one@example.com>,
	two@example.com
Subject: [PATCH 1/1] Second.
Date: DATE-STRING
Message-ID: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING
MIME-Version: 1.0
Content-Transfer-Encoding: 8bit

Result: OK
EOF
"

test_expect_success $PREREQ '--suppress-cc=sob' '
	test_might_fail git config --unset sendemail.cccmd &&
	test_suppression sob
'

test_expect_success $PREREQ 'setup expect' "
cat >expected-suppress-bodycc <<\EOF
0001-Second.patch
(mbox) Adding cc: A <author@example.com> from line 'From: A <author@example.com>'
(mbox) Adding cc: One <one@example.com> from line 'Cc: One <one@example.com>, two@example.com'
(mbox) Adding cc: two@example.com from line 'Cc: One <one@example.com>, two@example.com'
(body) Adding cc: C O Mitter <committer@example.com> from line 'Signed-off-by: C O Mitter <committer@example.com>'
Dry-OK. Log says:
Server: relay.example.com
MAIL FROM:<from@example.com>
RCPT TO:<to@example.com>
RCPT TO:<author@example.com>
RCPT TO:<one@example.com>
RCPT TO:<two@example.com>
RCPT TO:<committer@example.com>
From: Example <from@example.com>
To: to@example.com
Cc: A <author@example.com>,
	One <one@example.com>,
	two@example.com,
	C O Mitter <committer@example.com>
Subject: [PATCH 1/1] Second.
Date: DATE-STRING
Message-ID: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING
MIME-Version: 1.0
Content-Transfer-Encoding: 8bit

Result: OK
EOF
"

test_expect_success $PREREQ '--suppress-cc=bodycc' '
	test_suppression bodycc
'

test_expect_success $PREREQ 'setup expect' "
cat >expected-suppress-cc <<\EOF
0001-Second.patch
(mbox) Adding cc: A <author@example.com> from line 'From: A <author@example.com>'
(body) Adding cc: C O Mitter <committer@example.com> from line 'Signed-off-by: C O Mitter <committer@example.com>'
Dry-OK. Log says:
Server: relay.example.com
MAIL FROM:<from@example.com>
RCPT TO:<to@example.com>
RCPT TO:<author@example.com>
RCPT TO:<committer@example.com>
From: Example <from@example.com>
To: to@example.com
Cc: A <author@example.com>,
	C O Mitter <committer@example.com>
Subject: [PATCH 1/1] Second.
Date: DATE-STRING
Message-ID: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING
MIME-Version: 1.0
Content-Transfer-Encoding: 8bit

Result: OK
EOF
"

test_expect_success $PREREQ '--suppress-cc=cc' '
	test_suppression cc
'

test_confirm () {
	echo y | \
		GIT_SEND_EMAIL_NOTTY=1 \
		git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		$@ $patches >stdout &&
	grep "Send this email" stdout
}

test_expect_success $PREREQ '--confirm=always' '
	test_confirm --confirm=always --suppress-cc=all
'

test_expect_success $PREREQ '--confirm=auto' '
	test_confirm --confirm=auto
'

test_expect_success $PREREQ '--confirm=cc' '
	test_confirm --confirm=cc
'

test_expect_success $PREREQ '--confirm=compose' '
	test_confirm --confirm=compose --compose
'

test_expect_success $PREREQ 'confirm by default (due to cc)' '
	test_when_finished git config sendemail.confirm never &&
	git config --unset sendemail.confirm &&
	test_confirm
'

test_expect_success $PREREQ 'confirm by default (due to --compose)' '
	test_when_finished git config sendemail.confirm never &&
	git config --unset sendemail.confirm &&
	test_confirm --suppress-cc=all --compose
'

test_expect_success $PREREQ 'confirm detects EOF (inform assumes y)' '
	test_when_finished git config sendemail.confirm never &&
	git config --unset sendemail.confirm &&
	rm -fr outdir &&
	git format-patch -2 -o outdir &&
	GIT_SEND_EMAIL_NOTTY=1 \
		git send-email \
			--from="Example <nobody@example.com>" \
			--to=nobody@example.com \
			--smtp-server="$(pwd)/fake.sendmail" \
			outdir/*.patch </dev/null
'

test_expect_success $PREREQ 'confirm detects EOF (auto causes failure)' '
	test_when_finished git config sendemail.confirm never &&
	git config sendemail.confirm auto &&
	GIT_SEND_EMAIL_NOTTY=1 &&
	export GIT_SEND_EMAIL_NOTTY &&
		test_must_fail git send-email \
			--from="Example <nobody@example.com>" \
			--to=nobody@example.com \
			--smtp-server="$(pwd)/fake.sendmail" \
			$patches </dev/null
'

test_expect_success $PREREQ 'confirm does not loop forever' '
	test_when_finished git config sendemail.confirm never &&
	git config sendemail.confirm auto &&
	GIT_SEND_EMAIL_NOTTY=1 &&
	export GIT_SEND_EMAIL_NOTTY &&
		yes "bogus" | test_must_fail git send-email \
			--from="Example <nobody@example.com>" \
			--to=nobody@example.com \
			--smtp-server="$(pwd)/fake.sendmail" \
			$patches
'

test_expect_success $PREREQ 'utf8 Cc is rfc2047 encoded' '
	clean_fake_sendmail &&
	rm -fr outdir &&
	git format-patch -1 -o outdir --cc="àéìöú <utf8@example.com>" &&
	git send-email \
	--from="Example <nobody@example.com>" \
	--to=nobody@example.com \
	--smtp-server="$(pwd)/fake.sendmail" \
	outdir/*.patch &&
	grep "^	" msgtxt1 |
	grep "=?UTF-8?q?=C3=A0=C3=A9=C3=AC=C3=B6=C3=BA?= <utf8@example.com>"
'

test_expect_success $PREREQ '--compose adds MIME for utf8 body' '
	clean_fake_sendmail &&
	write_script fake-editor-utf8 <<-\EOF &&
	echo "utf8 body: àéìöú" >>"$1"
	EOF
	GIT_EDITOR="\"$(pwd)/fake-editor-utf8\"" \
	git send-email \
		--compose --subject foo \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches &&
	grep "^utf8 body" msgtxt1 &&
	grep "^Content-Type: text/plain; charset=UTF-8" msgtxt1
'

test_expect_success $PREREQ '--compose respects user mime type' '
	clean_fake_sendmail &&
	write_script fake-editor-utf8-mime <<-\EOF &&
	cat >"$1" <<-\EOM
	MIME-Version: 1.0
	Content-Type: text/plain; charset=iso-8859-1
	Content-Transfer-Encoding: 8bit
	Subject: foo

	utf8 body: àéìöú
	EOM
	EOF
	GIT_EDITOR="\"$(pwd)/fake-editor-utf8-mime\"" \
	git send-email \
		--compose --subject foo \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches &&
	grep "^utf8 body" msgtxt1 &&
	grep "^Content-Type: text/plain; charset=iso-8859-1" msgtxt1 &&
	! grep "^Content-Type: text/plain; charset=UTF-8" msgtxt1
'

test_expect_success $PREREQ '--compose adds MIME for utf8 subject' '
	clean_fake_sendmail &&
	GIT_EDITOR="\"$(pwd)/fake-editor\"" \
	git send-email \
		--compose --subject utf8-sübjëct \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches &&
	grep "^fake edit" msgtxt1 &&
	grep "^Subject: =?UTF-8?q?utf8-s=C3=BCbj=C3=ABct?=" msgtxt1
'

test_expect_success $PREREQ 'utf8 author is correctly passed on' '
	clean_fake_sendmail &&
	test_commit weird_author &&
	test_when_finished "git reset --hard HEAD^" &&
	git commit --amend --author "Füñný Nâmé <odd_?=mail@example.com>" &&
	git format-patch --stdout -1 >funny_name.patch &&
	git send-email --from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		funny_name.patch &&
	grep "^From: Füñný Nâmé <odd_?=mail@example.com>" msgtxt1
'

test_expect_success $PREREQ 'utf8 sender is not duplicated' '
	clean_fake_sendmail &&
	test_commit weird_sender &&
	test_when_finished "git reset --hard HEAD^" &&
	git commit --amend --author "Füñný Nâmé <odd_?=mail@example.com>" &&
	git format-patch --stdout -1 >funny_name.patch &&
	git send-email --from="Füñný Nâmé <odd_?=mail@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		funny_name.patch &&
	grep "^From: " msgtxt1 >msgfrom &&
	test_line_count = 1 msgfrom
'

test_expect_success $PREREQ 'sendemail.composeencoding works' '
	clean_fake_sendmail &&
	git config sendemail.composeencoding iso-8859-1 &&
	write_script fake-editor-utf8 <<-\EOF &&
	echo "utf8 body: àéìöú" >>"$1"
	EOF
	GIT_EDITOR="\"$(pwd)/fake-editor-utf8\"" \
	git send-email \
		--compose --subject foo \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches &&
	grep "^utf8 body" msgtxt1 &&
	grep "^Content-Type: text/plain; charset=iso-8859-1" msgtxt1
'

test_expect_success $PREREQ '--compose-encoding works' '
	clean_fake_sendmail &&
	write_script fake-editor-utf8 <<-\EOF &&
	echo "utf8 body: àéìöú" >>"$1"
	EOF
	GIT_EDITOR="\"$(pwd)/fake-editor-utf8\"" \
	git send-email \
		--compose-encoding iso-8859-1 \
		--compose --subject foo \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches &&
	grep "^utf8 body" msgtxt1 &&
	grep "^Content-Type: text/plain; charset=iso-8859-1" msgtxt1
'

test_expect_success $PREREQ '--compose-encoding overrides sendemail.composeencoding' '
	clean_fake_sendmail &&
	git config sendemail.composeencoding iso-8859-1 &&
	write_script fake-editor-utf8 <<-\EOF &&
	echo "utf8 body: àéìöú" >>"$1"
	EOF
	GIT_EDITOR="\"$(pwd)/fake-editor-utf8\"" \
	git send-email \
		--compose-encoding iso-8859-2 \
		--compose --subject foo \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches &&
	grep "^utf8 body" msgtxt1 &&
	grep "^Content-Type: text/plain; charset=iso-8859-2" msgtxt1
'

test_expect_success $PREREQ '--compose-encoding adds correct MIME for subject' '
	clean_fake_sendmail &&
	GIT_EDITOR="\"$(pwd)/fake-editor\"" \
	git send-email \
		--compose-encoding iso-8859-2 \
		--compose --subject utf8-sübjëct \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches &&
	grep "^fake edit" msgtxt1 &&
	grep "^Subject: =?iso-8859-2?q?utf8-s=C3=BCbj=C3=ABct?=" msgtxt1
'

test_expect_success $PREREQ 'detects ambiguous reference/file conflict' '
	echo main >main &&
	git add main &&
	git commit -m"add main" &&
	test_must_fail git send-email --dry-run main 2>errors &&
	grep disambiguate errors
'

test_expect_success $PREREQ 'feed two files' '
	rm -fr outdir &&
	git format-patch -2 -o outdir &&
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		outdir/000?-*.patch 2>errors >out &&
	grep "^Subject: " out >subjects &&
	test "z$(sed -n -e 1p subjects)" = "zSubject: [PATCH 1/2] Second." &&
	test "z$(sed -n -e 2p subjects)" = "zSubject: [PATCH 2/2] add main"
'

test_expect_success $PREREQ 'in-reply-to but no threading' '
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--in-reply-to="<in-reply-id@example.com>" \
		--no-thread \
		$patches >out &&
	grep "In-Reply-To: <in-reply-id@example.com>" out
'

test_expect_success $PREREQ 'no in-reply-to and no threading' '
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--no-thread \
		$patches >stdout &&
	! grep "In-Reply-To: " stdout
'

test_expect_success $PREREQ 'threading but no chain-reply-to' '
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--thread \
		--no-chain-reply-to \
		$patches $patches >stdout &&
	grep "In-Reply-To: " stdout
'

test_expect_success $PREREQ 'override in-reply-to if no threading' '
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--no-thread \
		--in-reply-to="override" \
		$threaded_patches >stdout &&
	grep "In-Reply-To: <override>" stdout
'

test_expect_success $PREREQ 'sendemail.to works' '
	git config --replace-all sendemail.to "Somebody <somebody@ex.com>" &&
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		$patches >stdout &&
	grep "To: Somebody <somebody@ex.com>" stdout
'

test_expect_success $PREREQ 'setup sendemail.identity' '
	git config --replace-all sendemail.to "default@example.com" &&
	git config --replace-all sendemail.isp.to "isp@example.com" &&
	git config --replace-all sendemail.cloud.to "cloud@example.com"
'

test_expect_success $PREREQ 'sendemail.identity: reads the correct identity config' '
	git -c sendemail.identity=cloud send-email \
		--dry-run \
		--from="nobody@example.com" \
		$patches >stdout &&
	grep "To: cloud@example.com" stdout
'

test_expect_success $PREREQ 'sendemail.identity: identity overrides sendemail.identity' '
	git -c sendemail.identity=cloud send-email \
		--identity=isp \
		--dry-run \
		--from="nobody@example.com" \
		$patches >stdout &&
	grep "To: isp@example.com" stdout
'

test_expect_success $PREREQ 'sendemail.identity: --no-identity clears previous identity' '
	git -c sendemail.identity=cloud send-email \
		--no-identity \
		--dry-run \
		--from="nobody@example.com" \
		$patches >stdout &&
	grep "To: default@example.com" stdout
'

test_expect_success $PREREQ 'sendemail.identity: bool identity variable existence overrides' '
	git -c sendemail.identity=cloud \
		-c sendemail.xmailer=true \
		-c sendemail.cloud.xmailer=false \
		send-email \
		--dry-run \
		--from="nobody@example.com" \
		$patches >stdout &&
	grep "To: cloud@example.com" stdout &&
	! grep "X-Mailer" stdout
'

test_expect_success $PREREQ 'sendemail.identity: bool variable fallback' '
	git -c sendemail.identity=cloud \
		-c sendemail.xmailer=false \
		send-email \
		--dry-run \
		--from="nobody@example.com" \
		$patches >stdout &&
	grep "To: cloud@example.com" stdout &&
	! grep "X-Mailer" stdout
'

test_expect_success $PREREQ 'sendemail.identity: bool variable without a value' '
	git -c sendemail.xmailer \
		send-email \
		--dry-run \
		--from="nobody@example.com" \
		$patches >stdout &&
	grep "To: default@example.com" stdout &&
	grep "X-Mailer" stdout
'

test_expect_success $PREREQ '--no-to overrides sendemail.to' '
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--no-to \
		--to=nobody@example.com \
		$patches >stdout &&
	grep "To: nobody@example.com" stdout &&
	! grep "To: Somebody <somebody@ex.com>" stdout
'

test_expect_success $PREREQ 'sendemail.cc works' '
	git config --replace-all sendemail.cc "Somebody <somebody@ex.com>" &&
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		$patches >stdout &&
	grep "Cc: Somebody <somebody@ex.com>" stdout
'

test_expect_success $PREREQ '--no-cc overrides sendemail.cc' '
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--no-cc \
		--cc=bodies@example.com \
		--to=nobody@example.com \
		$patches >stdout &&
	grep "Cc: bodies@example.com" stdout &&
	! grep "Cc: Somebody <somebody@ex.com>" stdout
'

test_expect_success $PREREQ 'sendemail.bcc works' '
	git config --replace-all sendemail.bcc "Other <other@ex.com>" &&
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server relay.example.com \
		$patches >stdout &&
	grep "RCPT TO:<other@ex.com>" stdout
'

test_expect_success $PREREQ '--no-bcc overrides sendemail.bcc' '
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--no-bcc \
		--bcc=bodies@example.com \
		--to=nobody@example.com \
		--smtp-server relay.example.com \
		$patches >stdout &&
	grep "RCPT TO:<bodies@example.com>" stdout &&
	! grep "RCPT TO:<other@ex.com>" stdout
'

test_expect_success $PREREQ 'patches To headers are used by default' '
	patch=$(git format-patch -1 --to="bodies@example.com") &&
	test_when_finished "rm $patch" &&
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--smtp-server relay.example.com \
		$patch >stdout &&
	grep "RCPT TO:<bodies@example.com>" stdout
'

test_expect_success $PREREQ 'patches To headers are appended to' '
	patch=$(git format-patch -1 --to="bodies@example.com") &&
	test_when_finished "rm $patch" &&
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server relay.example.com \
		$patch >stdout &&
	grep "RCPT TO:<bodies@example.com>" stdout &&
	grep "RCPT TO:<nobody@example.com>" stdout
'

test_expect_success $PREREQ 'To headers from files reset each patch' '
	patch1=$(git format-patch -1 --to="bodies@example.com") &&
	patch2=$(git format-patch -1 --to="other@example.com" HEAD~) &&
	test_when_finished "rm $patch1 && rm $patch2" &&
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--to="nobody@example.com" \
		--smtp-server relay.example.com \
		$patch1 $patch2 >stdout &&
	test $(grep -c "RCPT TO:<bodies@example.com>" stdout) = 1 &&
	test $(grep -c "RCPT TO:<nobody@example.com>" stdout) = 2 &&
	test $(grep -c "RCPT TO:<other@example.com>" stdout) = 1
'

test_expect_success $PREREQ 'setup expect' '
cat >email-using-8bit <<\EOF
From fe6ecc66ece37198fe5db91fa2fc41d9f4fe5cc4 Mon Sep 17 00:00:00 2001
Message-ID: <bogus-message-id@example.com>
From: author@example.com
Date: Sat, 12 Jun 2010 15:53:58 +0200
Subject: subject goes here

Dieser deutsche Text enthält einen Umlaut!
EOF
'

test_expect_success $PREREQ 'setup expect' '
	echo "Subject: subject goes here" >expected
'

test_expect_success $PREREQ 'ASCII subject is not RFC2047 quoted' '
	clean_fake_sendmail &&
	echo bogus |
	git send-email --from=author@example.com --to=nobody@example.com \
			--smtp-server="$(pwd)/fake.sendmail" \
			--8bit-encoding=UTF-8 \
			email-using-8bit >stdout &&
	grep "Subject" msgtxt1 >actual &&
	test_cmp expected actual
'

test_expect_success $PREREQ 'setup expect' '
	cat >content-type-decl <<-\EOF
	MIME-Version: 1.0
	Content-Type: text/plain; charset=UTF-8
	Content-Transfer-Encoding: 8bit
	EOF
'

test_expect_success $PREREQ 'asks about and fixes 8bit encodings' '
	clean_fake_sendmail &&
	echo |
	git send-email --from=author@example.com --to=nobody@example.com \
			--smtp-server="$(pwd)/fake.sendmail" \
			email-using-8bit >stdout &&
	grep "do not declare a Content-Transfer-Encoding" stdout &&
	grep email-using-8bit stdout &&
	grep "Which 8bit encoding" stdout &&
	grep -E "Content|MIME" msgtxt1 >actual &&
	test_cmp content-type-decl actual
'

test_expect_success $PREREQ 'sendemail.8bitEncoding works' '
	clean_fake_sendmail &&
	git config sendemail.assume8bitEncoding UTF-8 &&
	echo bogus |
	git send-email --from=author@example.com --to=nobody@example.com \
			--smtp-server="$(pwd)/fake.sendmail" \
			email-using-8bit >stdout &&
	grep -E "Content|MIME" msgtxt1 >actual &&
	test_cmp content-type-decl actual
'

test_expect_success $PREREQ 'sendemail.8bitEncoding in .git/config overrides --global .gitconfig' '
	clean_fake_sendmail &&
	git config sendemail.assume8bitEncoding UTF-8 &&
	test_when_finished "rm -rf home" &&
	mkdir home &&
	git config -f home/.gitconfig sendemail.assume8bitEncoding "bogus too" &&
	echo bogus |
	env HOME="$(pwd)/home" DEBUG=1 \
	git send-email --from=author@example.com --to=nobody@example.com \
			--smtp-server="$(pwd)/fake.sendmail" \
			email-using-8bit >stdout &&
	grep -E "Content|MIME" msgtxt1 >actual &&
	test_cmp content-type-decl actual
'

test_expect_success $PREREQ '--8bit-encoding overrides sendemail.8bitEncoding' '
	clean_fake_sendmail &&
	git config sendemail.assume8bitEncoding "bogus too" &&
	echo bogus |
	git send-email --from=author@example.com --to=nobody@example.com \
			--smtp-server="$(pwd)/fake.sendmail" \
			--8bit-encoding=UTF-8 \
			email-using-8bit >stdout &&
	grep -E "Content|MIME" msgtxt1 >actual &&
	test_cmp content-type-decl actual
'

test_expect_success $PREREQ 'setup expect' '
	cat >email-using-8bit <<-\EOF
	From fe6ecc66ece37198fe5db91fa2fc41d9f4fe5cc4 Mon Sep 17 00:00:00 2001
	Message-ID: <bogus-message-id@example.com>
	From: author@example.com
	Date: Sat, 12 Jun 2010 15:53:58 +0200
	Subject: Dieser Betreff enthält auch einen Umlaut!

	Nothing to see here.
	EOF
'

test_expect_success $PREREQ 'setup expect' '
	cat >expected <<-\EOF
	Subject: =?UTF-8?q?Dieser=20Betreff=20enth=C3=A4lt=20auch=20einen=20Umlaut!?=
	EOF
'

test_expect_success $PREREQ '--8bit-encoding also treats subject' '
	clean_fake_sendmail &&
	echo bogus |
	git send-email --from=author@example.com --to=nobody@example.com \
			--smtp-server="$(pwd)/fake.sendmail" \
			--8bit-encoding=UTF-8 \
			email-using-8bit >stdout &&
	grep "Subject" msgtxt1 >actual &&
	test_cmp expected actual
'

test_expect_success $PREREQ 'setup expect' '
	cat >email-using-8bit <<-\EOF
	From fe6ecc66ece37198fe5db91fa2fc41d9f4fe5cc4 Mon Sep 17 00:00:00 2001
	Message-ID: <bogus-message-id@example.com>
	From: A U Thor <author@example.com>
	Date: Sat, 12 Jun 2010 15:53:58 +0200
	Content-Type: text/plain; charset=UTF-8
	Subject: Nothing to see here.

	Dieser Betreff enthält auch einen Umlaut!
	EOF
'

test_expect_success $PREREQ '--transfer-encoding overrides sendemail.transferEncoding' '
	clean_fake_sendmail &&
	test_must_fail git -c sendemail.transferEncoding=8bit \
		send-email \
		--transfer-encoding=7bit \
		--smtp-server="$(pwd)/fake.sendmail" \
		email-using-8bit \
		2>errors >out &&
	grep "cannot send message as 7bit" errors &&
	test -z "$(ls msgtxt*)"
'

test_expect_success $PREREQ 'sendemail.transferEncoding via config' '
	clean_fake_sendmail &&
	test_must_fail git -c sendemail.transferEncoding=7bit \
		send-email \
		--smtp-server="$(pwd)/fake.sendmail" \
		email-using-8bit \
		2>errors >out &&
	grep "cannot send message as 7bit" errors &&
	test -z "$(ls msgtxt*)"
'

test_expect_success $PREREQ 'sendemail.transferEncoding via cli' '
	clean_fake_sendmail &&
	test_must_fail git send-email \
		--transfer-encoding=7bit \
		--smtp-server="$(pwd)/fake.sendmail" \
		email-using-8bit \
		2>errors >out &&
	grep "cannot send message as 7bit" errors &&
	test -z "$(ls msgtxt*)"
'

test_expect_success $PREREQ 'setup expect' '
	cat >expected <<-\EOF
	Dieser Betreff enth=C3=A4lt auch einen Umlaut!
	EOF
'

test_expect_success $PREREQ '8-bit and sendemail.transferencoding=quoted-printable' '
	clean_fake_sendmail &&
	git send-email \
		--transfer-encoding=quoted-printable \
		--smtp-server="$(pwd)/fake.sendmail" \
		email-using-8bit \
		2>errors >out &&
	sed "1,/^$/d" msgtxt1 >actual &&
	test_cmp expected actual
'

test_expect_success $PREREQ 'setup expect' '
	cat >expected <<-\EOF
	RGllc2VyIEJldHJlZmYgZW50aMOkbHQgYXVjaCBlaW5lbiBVbWxhdXQhCg==
	EOF
'

test_expect_success $PREREQ '8-bit and sendemail.transferencoding=base64' '
	clean_fake_sendmail &&
	git send-email \
		--transfer-encoding=base64 \
		--smtp-server="$(pwd)/fake.sendmail" \
		email-using-8bit \
		2>errors >out &&
	sed "1,/^$/d" msgtxt1 >actual &&
	test_cmp expected actual
'

test_expect_success $PREREQ 'setup expect' '
	cat >email-using-qp <<-\EOF
	From fe6ecc66ece37198fe5db91fa2fc41d9f4fe5cc4 Mon Sep 17 00:00:00 2001
	Message-ID: <bogus-message-id@example.com>
	From: A U Thor <author@example.com>
	Date: Sat, 12 Jun 2010 15:53:58 +0200
	MIME-Version: 1.0
	Content-Transfer-Encoding: quoted-printable
	Content-Type: text/plain; charset=UTF-8
	Subject: Nothing to see here.

	Dieser Betreff enth=C3=A4lt auch einen Umlaut!
	EOF
'

test_expect_success $PREREQ 'convert from quoted-printable to base64' '
	clean_fake_sendmail &&
	git send-email \
		--transfer-encoding=base64 \
		--smtp-server="$(pwd)/fake.sendmail" \
		email-using-qp \
		2>errors >out &&
	sed "1,/^$/d" msgtxt1 >actual &&
	test_cmp expected actual
'

test_expect_success $PREREQ 'setup expect' "
tr -d '\\015' | tr '%' '\\015' >email-using-crlf <<EOF
From fe6ecc66ece37198fe5db91fa2fc41d9f4fe5cc4 Mon Sep 17 00:00:00 2001
Message-ID: <bogus-message-id@example.com>
From: A U Thor <author@example.com>
Date: Sat, 12 Jun 2010 15:53:58 +0200
Content-Type: text/plain; charset=UTF-8
Subject: Nothing to see here.

Look, I have a CRLF and an = sign!%
EOF
"

test_expect_success $PREREQ 'setup expect' '
	cat >expected <<-\EOF
	Look, I have a CRLF and an =3D sign!=0D
	EOF
'

test_expect_success $PREREQ 'CRLF and sendemail.transferencoding=quoted-printable' '
	clean_fake_sendmail &&
	git send-email \
		--transfer-encoding=quoted-printable \
		--smtp-server="$(pwd)/fake.sendmail" \
		email-using-crlf \
		2>errors >out &&
	sed "1,/^$/d" msgtxt1 >actual &&
	test_cmp expected actual
'

test_expect_success $PREREQ 'setup expect' '
	cat >expected <<-\EOF
	TG9vaywgSSBoYXZlIGEgQ1JMRiBhbmQgYW4gPSBzaWduIQ0K
	EOF
'

test_expect_success $PREREQ 'CRLF and sendemail.transferencoding=base64' '
	clean_fake_sendmail &&
	git send-email \
		--transfer-encoding=base64 \
		--smtp-server="$(pwd)/fake.sendmail" \
		email-using-crlf \
		2>errors >out &&
	sed "1,/^$/d" msgtxt1 >actual &&
	test_cmp expected actual
'


# Note that the patches in this test are deliberately out of order; we
# want to make sure it works even if the cover-letter is not in the
# first mail.
test_expect_success $PREREQ 'refusing to send cover letter template' '
	clean_fake_sendmail &&
	rm -fr outdir &&
	git format-patch --cover-letter -2 -o outdir &&
	test_must_fail git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		outdir/0002-*.patch \
		outdir/0000-*.patch \
		outdir/0001-*.patch \
		2>errors >out &&
	grep "SUBJECT HERE" errors &&
	test -z "$(ls msgtxt*)"
'

test_expect_success $PREREQ '--force sends cover letter template anyway' '
	clean_fake_sendmail &&
	rm -fr outdir &&
	git format-patch --cover-letter -2 -o outdir &&
	git send-email \
		--force \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		outdir/0002-*.patch \
		outdir/0000-*.patch \
		outdir/0001-*.patch \
		2>errors >out &&
	! grep "SUBJECT HERE" errors &&
	test -n "$(ls msgtxt*)"
'

test_cover_addresses () {
	header="$1"
	shift
	clean_fake_sendmail &&
	rm -fr outdir &&
	git format-patch --cover-letter -2 -o outdir &&
	cover=$(echo outdir/0000-*.patch) &&
	mv $cover cover-to-edit.patch &&
	perl -pe "s/^From:/$header: extra\@address.com\nFrom:/" cover-to-edit.patch >"$cover" &&
	git send-email \
		--force \
		--from="Example <nobody@example.com>" \
		--no-to --no-cc \
		"$@" \
		--smtp-server="$(pwd)/fake.sendmail" \
		outdir/0000-*.patch \
		outdir/0001-*.patch \
		outdir/0002-*.patch \
		2>errors >out &&
	grep "^$header: extra@address.com" msgtxt1 >to1 &&
	grep "^$header: extra@address.com" msgtxt2 >to2 &&
	grep "^$header: extra@address.com" msgtxt3 >to3 &&
	test_line_count = 1 to1 &&
	test_line_count = 1 to2 &&
	test_line_count = 1 to3
}

test_expect_success $PREREQ 'to-cover adds To to all mail' '
	test_cover_addresses "To" --to-cover
'

test_expect_success $PREREQ 'cc-cover adds Cc to all mail' '
	test_cover_addresses "Cc" --cc-cover
'

test_expect_success $PREREQ 'tocover adds To to all mail' '
	test_config sendemail.tocover true &&
	test_cover_addresses "To"
'

test_expect_success $PREREQ 'cccover adds Cc to all mail' '
	test_config sendemail.cccover true &&
	test_cover_addresses "Cc"
'

test_expect_success $PREREQ 'escaped quotes in sendemail.aliasfiletype=mutt' '
	clean_fake_sendmail &&
	echo "alias sbd \\\"Dot U. Sir\\\" <somebody@example.org>" >.mutt &&
	git config --replace-all sendemail.aliasesfile "$(pwd)/.mutt" &&
	git config sendemail.aliasfiletype mutt &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=sbd \
		--smtp-server="$(pwd)/fake.sendmail" \
		outdir/0001-*.patch \
		2>errors >out &&
	grep "^!somebody@example\.org!$" commandline1 &&
	grep -F "To: \"Dot U. Sir\" <somebody@example.org>" out
'

test_expect_success $PREREQ 'sendemail.aliasfiletype=mailrc' '
	clean_fake_sendmail &&
	echo "alias sbd  somebody@example.org" >.mailrc &&
	git config --replace-all sendemail.aliasesfile "$(pwd)/.mailrc" &&
	git config sendemail.aliasfiletype mailrc &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=sbd \
		--smtp-server="$(pwd)/fake.sendmail" \
		outdir/0001-*.patch \
		2>errors >out &&
	grep "^!somebody@example\.org!$" commandline1
'

test_expect_success $PREREQ 'sendemail.aliasesfile=~/.mailrc' '
	clean_fake_sendmail &&
	echo "alias sbd  someone@example.org" >"$HOME/.mailrc" &&
	git config --replace-all sendemail.aliasesfile "~/.mailrc" &&
	git config sendemail.aliasfiletype mailrc &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=sbd \
		--smtp-server="$(pwd)/fake.sendmail" \
		outdir/0001-*.patch \
		2>errors >out &&
	grep "^!someone@example\.org!$" commandline1
'

test_dump_aliases () {
	msg="$1" && shift &&
	filetype="$1" && shift &&
	printf '%s\n' "$@" >expect &&
	cat >.tmp-email-aliases &&

	test_expect_success $PREREQ "$msg" '
		clean_fake_sendmail && rm -fr outdir &&
		git config --replace-all sendemail.aliasesfile \
			"$(pwd)/.tmp-email-aliases" &&
		git config sendemail.aliasfiletype "$filetype" &&
		git send-email --dump-aliases 2>errors >actual &&
		test_cmp expect actual
	'
}

test_dump_aliases '--dump-aliases sendmail format' \
	'sendmail' \
	'abgroup' \
	'alice' \
	'bcgrp' \
	'bob' \
	'chloe' <<-\EOF
	alice: Alice W Land <awol@example.com>
	bob: Robert Bobbyton <bob@example.com>
	chloe: chloe@example.com
	abgroup: alice, bob
	bcgrp: bob, chloe, Other <o@example.com>
	EOF

test_dump_aliases '--dump-aliases mutt format' \
	'mutt' \
	'alice' \
	'bob' \
	'chloe' \
	'donald' <<-\EOF
	alias alice Alice W Land <awol@example.com>
	alias donald Donald C Carlton <donc@example.com>
	alias bob Robert Bobbyton <bob@example.com>
	alias chloe chloe@example.com
	EOF

test_dump_aliases '--dump-aliases mailrc format' \
	'mailrc' \
	'alice' \
	'bob' \
	'chloe' \
	'eve' <<-\EOF
	alias alice   Alice W Land <awol@example.com>
	alias eve     Eve <eve@example.com>
	alias bob     Robert Bobbyton <bob@example.com>
	alias chloe   chloe@example.com
	EOF

test_dump_aliases '--dump-aliases pine format' \
	'pine' \
	'alice' \
	'bob' \
	'chloe' \
	'eve' <<-\EOF
	alice	Alice W Land	<awol@example.com>
	eve	Eve	<eve@example.com>
	bob	Robert	Bobbyton <bob@example.com>
	chloe		chloe@example.com
	EOF

test_dump_aliases '--dump-aliases gnus format' \
	'gnus' \
	'alice' \
	'bob' \
	'chloe' \
	'eve' <<-\EOF
	(define-mail-alias "alice" "awol@example.com")
	(define-mail-alias "eve" "eve@example.com")
	(define-mail-alias "bob" "bob@example.com")
	(define-mail-alias "chloe" "chloe@example.com")
	EOF

test_expect_success '--dump-aliases must be used alone' '
	test_must_fail git send-email --dump-aliases --to=janice@example.com -1 refs/heads/accounting
'

test_expect_success $PREREQ 'aliases and sendemail.identity' '
	test_must_fail git \
		-c sendemail.identity=cloud \
		-c sendemail.aliasesfile=default-aliases \
		-c sendemail.cloud.aliasesfile=cloud-aliases \
		send-email -1 2>stderr &&
	test_grep "cloud-aliases" stderr
'

test_sendmail_aliases () {
	msg="$1" && shift &&
	expect="$@" &&
	cat >.tmp-email-aliases &&

	test_expect_success $PREREQ "$msg" '
		clean_fake_sendmail && rm -fr outdir &&
		git format-patch -1 -o outdir &&
		git config --replace-all sendemail.aliasesfile \
			"$(pwd)/.tmp-email-aliases" &&
		git config sendemail.aliasfiletype sendmail &&
		git send-email \
			--from="Example <nobody@example.com>" \
			--to=alice --to=bcgrp \
			--smtp-server="$(pwd)/fake.sendmail" \
			outdir/0001-*.patch \
			2>errors >out &&
		for i in $expect
		do
			grep "^!$i!$" commandline1 || return 1
		done
	'
}

test_sendmail_aliases 'sendemail.aliasfiletype=sendmail' \
	'awol@example\.com' \
	'bob@example\.com' \
	'chloe@example\.com' \
	'o@example\.com' <<-\EOF
	alice: Alice W Land <awol@example.com>
	bob: Robert Bobbyton <bob@example.com>
	# this is a comment
	   # this is also a comment
	chloe: chloe@example.com
	abgroup: alice, bob
	bcgrp: bob, chloe, Other <o@example.com>
	EOF

test_sendmail_aliases 'sendmail aliases line folding' \
	alice1 \
	bob1 bob2 \
	chuck1 chuck2 \
	darla1 darla2 darla3 \
	elton1 elton2 elton3 \
	fred1 fred2 \
	greg1 <<-\EOF
	alice: alice1
	bob: bob1,\
	bob2
	chuck: chuck1,
	    chuck2
	darla: darla1,\
	darla2,
	    darla3
	elton: elton1,
	    elton2,\
	elton3
	fred: fred1,\
	    fred2
	greg: greg1
	bcgrp: bob, chuck, darla, elton, fred, greg
	EOF

test_sendmail_aliases 'sendmail aliases tolerate bogus line folding' \
	alice1 bob1 <<-\EOF
	    alice: alice1
	bcgrp: bob1\
	EOF

test_sendmail_aliases 'sendmail aliases empty' alice bcgrp <<-\EOF
	EOF

test_expect_success $PREREQ 'alias support in To header' '
	clean_fake_sendmail &&
	echo "alias sbd  someone@example.org" >.mailrc &&
	test_config sendemail.aliasesfile ".mailrc" &&
	test_config sendemail.aliasfiletype mailrc &&
	git format-patch --stdout -1 --to=sbd >aliased.patch &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--smtp-server="$(pwd)/fake.sendmail" \
		aliased.patch \
		2>errors >out &&
	grep "^!someone@example\.org!$" commandline1
'

test_expect_success $PREREQ 'alias support in Cc header' '
	clean_fake_sendmail &&
	echo "alias sbd  someone@example.org" >.mailrc &&
	test_config sendemail.aliasesfile ".mailrc" &&
	test_config sendemail.aliasfiletype mailrc &&
	git format-patch --stdout -1 --cc=sbd >aliased.patch &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--smtp-server="$(pwd)/fake.sendmail" \
		aliased.patch \
		2>errors >out &&
	grep "^!someone@example\.org!$" commandline1
'

test_expect_success $PREREQ 'tocmd works with aliases' '
	clean_fake_sendmail &&
	echo "alias sbd  someone@example.org" >.mailrc &&
	test_config sendemail.aliasesfile ".mailrc" &&
	test_config sendemail.aliasfiletype mailrc &&
	git format-patch --stdout -1 >tocmd.patch &&
	echo tocmd--sbd >>tocmd.patch &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to-cmd=./tocmd-sed \
		--smtp-server="$(pwd)/fake.sendmail" \
		tocmd.patch \
		2>errors >out &&
	grep "^!someone@example\.org!$" commandline1
'

test_expect_success $PREREQ 'cccmd works with aliases' '
	clean_fake_sendmail &&
	echo "alias sbd  someone@example.org" >.mailrc &&
	test_config sendemail.aliasesfile ".mailrc" &&
	test_config sendemail.aliasfiletype mailrc &&
	git format-patch --stdout -1 >cccmd.patch &&
	echo cccmd--sbd >>cccmd.patch &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--cc-cmd=./cccmd-sed \
		--smtp-server="$(pwd)/fake.sendmail" \
		cccmd.patch \
		2>errors >out &&
	grep "^!someone@example\.org!$" commandline1
'

do_xmailer_test () {
	expected=$1 params=$2 &&
	git format-patch -1 &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=someone@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		$params \
		0001-*.patch \
		2>errors >out &&
	{ grep '^X-Mailer:' out || :; } >mailer &&
	test_line_count = $expected mailer
}

test_expect_success $PREREQ '--[no-]xmailer without any configuration' '
	do_xmailer_test 1 "--xmailer" &&
	do_xmailer_test 0 "--no-xmailer"
'

test_expect_success $PREREQ '--[no-]xmailer with sendemail.xmailer=true' '
	test_config sendemail.xmailer true &&
	do_xmailer_test 1 "" &&
	do_xmailer_test 0 "--no-xmailer" &&
	do_xmailer_test 1 "--xmailer"
'

test_expect_success $PREREQ '--[no-]xmailer with sendemail.xmailer' '
	test_when_finished "test_unconfig sendemail.xmailer" &&
	cat >>.git/config <<-\EOF &&
	[sendemail]
		xmailer
	EOF
	test_config sendemail.xmailer true &&
	do_xmailer_test 1 "" &&
	do_xmailer_test 0 "--no-xmailer" &&
	do_xmailer_test 1 "--xmailer"
'

test_expect_success $PREREQ '--[no-]xmailer with sendemail.xmailer=false' '
	test_config sendemail.xmailer false &&
	do_xmailer_test 0 "" &&
	do_xmailer_test 0 "--no-xmailer" &&
	do_xmailer_test 1 "--xmailer"
'

test_expect_success $PREREQ '--[no-]xmailer with sendemail.xmailer=' '
	test_config sendemail.xmailer "" &&
	do_xmailer_test 0 "" &&
	do_xmailer_test 0 "--no-xmailer" &&
	do_xmailer_test 1 "--xmailer"
'

test_expect_success $PREREQ 'setup expected-list' '
	git send-email \
	--dry-run \
	--from="Example <from@example.com>" \
	--to="To 1 <to1@example.com>" \
	--to="to2@example.com" \
	--to="to3@example.com" \
	--cc="Cc 1 <cc1@example.com>" \
	--cc="Cc2 <cc2@example.com>" \
	--bcc="bcc1@example.com" \
	--bcc="bcc2@example.com" \
	0001-add-main.patch | replace_variable_fields \
	>expected-list
'

test_expect_success $PREREQ 'use email list in --cc --to and --bcc' '
	git send-email \
	--dry-run \
	--from="Example <from@example.com>" \
	--to="To 1 <to1@example.com>, to2@example.com" \
	--to="to3@example.com" \
	--cc="Cc 1 <cc1@example.com>, Cc2 <cc2@example.com>" \
	--bcc="bcc1@example.com, bcc2@example.com" \
	0001-add-main.patch | replace_variable_fields \
	>actual-list &&
	test_cmp expected-list actual-list
'

test_expect_success $PREREQ 'aliases work with email list' '
	echo "alias to2 to2@example.com" >.mutt &&
	echo "alias cc1 Cc 1 <cc1@example.com>" >>.mutt &&
	test_config sendemail.aliasesfile ".mutt" &&
	test_config sendemail.aliasfiletype mutt &&
	git send-email \
	--dry-run \
	--from="Example <from@example.com>" \
	--to="To 1 <to1@example.com>, to2, to3@example.com" \
	--cc="cc1, Cc2 <cc2@example.com>" \
	--bcc="bcc1@example.com, bcc2@example.com" \
	0001-add-main.patch | replace_variable_fields \
	>actual-list &&
	test_cmp expected-list actual-list
'

test_expect_success $PREREQ 'leading and trailing whitespaces are removed' '
	echo "alias to2 to2@example.com" >.mutt &&
	echo "alias cc1 Cc 1 <cc1@example.com>" >>.mutt &&
	test_config sendemail.aliasesfile ".mutt" &&
	test_config sendemail.aliasfiletype mutt &&
	TO1=$(echo "QTo 1 <to1@example.com>" | q_to_tab) &&
	TO2=$(echo "QZto2" | qz_to_tab_space) &&
	CC1=$(echo "cc1" | append_cr) &&
	BCC1=$(echo " bcc1@example.com Q" | q_to_nul) &&
	git send-email \
	--dry-run \
	--from="	Example <from@example.com>" \
	--to="$TO1" \
	--to="$TO2" \
	--to="  to3@example.com   " \
	--cc="$CC1" \
	--cc="Cc2 <cc2@example.com>" \
	--bcc="$BCC1" \
	--bcc="bcc2@example.com" \
	0001-add-main.patch | replace_variable_fields \
	>actual-list &&
	test_cmp expected-list actual-list
'

test_expect_success $PREREQ 'test using command name with --sendmail-cmd' '
	clean_fake_sendmail &&
	PATH="$PWD:$PATH" \
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--sendmail-cmd="fake.sendmail" \
		HEAD^ &&
	test_path_is_file commandline1
'

test_expect_success $PREREQ 'test using arguments with --sendmail-cmd' '
	clean_fake_sendmail &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--sendmail-cmd='\''"$(pwd)/fake.sendmail" -f nobody@example.com'\'' \
		HEAD^ &&
	test_path_is_file commandline1
'

test_expect_success $PREREQ 'test shell expression with --sendmail-cmd' '
	clean_fake_sendmail &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--sendmail-cmd='\''f() { "$(pwd)/fake.sendmail" "$@"; };f'\'' \
		HEAD^ &&
	test_path_is_file commandline1
'

test_expect_success $PREREQ 'set up in-reply-to/references patches' '
	cat >has-reply.patch <<-\EOF &&
	From: A U Thor <author@example.com>
	Subject: patch with in-reply-to
	Message-ID: <patch.with.in.reply.to@example.com>
	In-Reply-To: <replied.to@example.com>
	References: <replied.to@example.com>

	This is the body.
	EOF
	cat >no-reply.patch <<-\EOF
	From: A U Thor <author@example.com>
	Subject: patch without in-reply-to
	Message-ID: <patch.without.in.reply.to@example.com>

	This is the body.
	EOF
'

test_expect_success $PREREQ 'patch reply headers correct with --no-thread' '
	clean_fake_sendmail &&
	git send-email \
		--no-thread \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		has-reply.patch no-reply.patch &&
	grep "In-Reply-To: <replied.to@example.com>" msgtxt1 &&
	grep "References: <replied.to@example.com>" msgtxt1 &&
	! grep replied.to@example.com msgtxt2
'

test_expect_success $PREREQ 'cmdline in-reply-to used with --no-thread' '
	clean_fake_sendmail &&
	git send-email \
		--no-thread \
		--in-reply-to="<cmdline.reply@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		has-reply.patch no-reply.patch &&
	grep "In-Reply-To: <cmdline.reply@example.com>" msgtxt1 &&
	grep "References: <cmdline.reply@example.com>" msgtxt1 &&
	grep "In-Reply-To: <cmdline.reply@example.com>" msgtxt2 &&
	grep "References: <cmdline.reply@example.com>" msgtxt2
'

test_expect_success $PREREQ 'invoke hook' '
	test_hook sendemail-validate <<-\EOF &&
	# test that we have the correct environment variable, pwd, and
	# argument
	case "$GIT_DIR" in
	*.git)
		true
		;;
	*)
		false
		;;
	esac &&
	test -f 0001-add-main.patch &&
	grep "add main" "$1"
	EOF

	mkdir subdir &&
	(
		# Test that it works even if we are not at the root of the
		# working tree
		cd subdir &&
		git send-email \
			--from="Example <nobody@example.com>" \
			--to=nobody@example.com \
			--smtp-server="$(pwd)/../fake.sendmail" \
			../0001-add-main.patch &&

		# Verify error message when a patch is rejected by the hook
		sed -e "s/add main/x/" ../0001-add-main.patch >../another.patch &&
		test_must_fail git send-email \
			--from="Example <nobody@example.com>" \
			--to=nobody@example.com \
			--smtp-server="$(pwd)/../fake.sendmail" \
			../another.patch 2>err &&
		test_grep "rejected by sendemail-validate hook" err
	)
'

expected_file_counter_output () {
	total=$1
	count=0
	while test $count -ne $total
	do
		count=$((count + 1)) &&
		echo "$count/$total" || return
	done
}

test_expect_success $PREREQ '--validate hook allows counting of messages' '
	test_when_finished "rm -rf my-hooks.log" &&
	test_config core.hooksPath "my-hooks" &&
	mkdir -p my-hooks &&

	write_script my-hooks/sendemail-validate <<-\EOF &&
		num=$GIT_SENDEMAIL_FILE_COUNTER &&
		tot=$GIT_SENDEMAIL_FILE_TOTAL &&
		echo "$num/$tot" >>my-hooks.log || exit 1
	EOF

	>my-hooks.log &&
	expected_file_counter_output 4 >expect &&
	git send-email \
		--from="Example <from@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		--validate -3 --cover-letter --force &&
	test_cmp expect my-hooks.log
'

test_expect_success $PREREQ 'test that send-email works outside a repo' '
	nongit git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		"$(pwd)/0001-add-main.patch"
'

test_expect_success $PREREQ 'send-email relays -v 3 to format-patch' '
	test_when_finished "rm -f out" &&
	git send-email --dry-run -v 3 -1 >out &&
	grep "PATCH v3" out
'

test_expect_success $PREREQ 'test that sendmail config is rejected' '
	test_config sendmail.program sendmail &&
	test_must_fail git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		HEAD^ 2>err &&
	test_grep "found configuration options for '"'"sendmail"'"'" err
'

test_expect_success $PREREQ 'test that sendmail config rejection is specific' '
	test_config resendmail.program sendmail &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		HEAD^
'

test_expect_success $PREREQ 'test forbidSendmailVariables behavior override' '
	test_config sendmail.program sendmail &&
	test_config sendemail.forbidSendmailVariables false &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		HEAD^
'

test_expect_success $PREREQ '--compose handles lowercase headers' '
	write_script fake-editor <<-\EOF &&
	sed "s/^[Ff][Rr][Oo][Mm]:.*/from: edited-from@example.com/" "$1" >"$1.tmp" &&
	mv "$1.tmp" "$1"
	EOF
	clean_fake_sendmail &&
	git send-email \
		--compose \
		--from="Example <from@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		HEAD^ &&
	grep "From: edited-from@example.com" msgtxt1
'

test_expect_success $PREREQ '--compose handles to headers' '
	write_script fake-editor <<-\EOF &&
	sed "s/^To: .*/&, edited-to@example.com/" <"$1" >"$1.tmp" &&
	echo this is the body >>"$1.tmp" &&
	mv "$1.tmp" "$1"
	EOF
	clean_fake_sendmail &&
	GIT_SEND_EMAIL_NOTTY=1 \
	git send-email \
		--compose \
		--from="Example <from@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		HEAD^ &&
	# Check both that the cover letter used our modified "to" line,
	# but also that it was picked up for the patch.
	q_to_tab >expect <<-\EOF &&
	To: nobody@example.com,
	Qedited-to@example.com
	EOF
	grep -A1 "^To:" msgtxt1 >msgtxt1.to &&
	test_cmp expect msgtxt1.to &&
	grep -A1 "^To:" msgtxt2 >msgtxt2.to &&
	test_cmp expect msgtxt2.to
'

test_done
