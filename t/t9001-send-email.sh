#!/bin/sh

test_description='git send-email'
. ./test-lib.sh

# May be altered later in the test
PREREQ="PERL"

test_expect_success $PREREQ \
    'prepare reference tree' \
    'echo "1A quick brown fox jumps over the" >file &&
     echo "lazy dog" >>file &&
     git add file &&
     GIT_AUTHOR_NAME="A" git commit -a -m "Initial."'

test_expect_success $PREREQ \
    'Setup helper tool' \
    '(echo "#!$SHELL_PATH"
      echo shift
      echo output=1
      echo "while test -f commandline\$output; do output=\$((\$output+1)); done"
      echo for a
      echo do
      echo "  echo \"!\$a!\""
      echo "done >commandline\$output"
      test_have_prereq MINGW && echo "dos2unix commandline\$output"
      echo "cat > msgtxt\$output"
      ) >fake.sendmail &&
     chmod +x ./fake.sendmail &&
     git add fake.sendmail &&
     GIT_AUTHOR_NAME="A" git commit -a -m "Second."'

clean_fake_sendmail() {
	rm -f commandline* msgtxt*
}

test_expect_success $PREREQ 'Extract patches' '
    patches=`git format-patch -s --cc="One <one@example.com>" --cc=two@example.com -n HEAD^1`
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
		$patches > stdout &&
		test_must_fail grep "Send this email" stdout &&
		> no_confirm_okay
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
cat >expected <<\EOF
!nobody@example.com!
!author@example.com!
!one@example.com!
!two@example.com!
EOF
'

test_expect_success $PREREQ \
    'Verify commandline' \
    'test_cmp expected commandline1'

test_expect_success $PREREQ 'Send patches with --envelope-sender' '
    clean_fake_sendmail &&
     git send-email --envelope-sender="Patch Contributer <patch@example.com>" --suppress-cc=sob --from="Example <nobody@example.com>" --to=nobody@example.com --smtp-server="$(pwd)/fake.sendmail" $patches 2>errors
'

test_expect_success $PREREQ 'setup expect' '
cat >expected <<\EOF
!patch@example.com!
!-i!
!nobody@example.com!
!author@example.com!
!one@example.com!
!two@example.com!
EOF
'

test_expect_success $PREREQ \
    'Verify commandline' \
    'test_cmp expected commandline1'

test_expect_success $PREREQ 'Send patches with --envelope-sender=auto' '
    clean_fake_sendmail &&
     git send-email --envelope-sender=auto --suppress-cc=sob --from="Example <nobody@example.com>" --to=nobody@example.com --smtp-server="$(pwd)/fake.sendmail" $patches 2>errors
'

test_expect_success $PREREQ 'setup expect' '
cat >expected <<\EOF
!nobody@example.com!
!-i!
!nobody@example.com!
!author@example.com!
!one@example.com!
!two@example.com!
EOF
'

test_expect_success $PREREQ \
    'Verify commandline' \
    'test_cmp expected commandline1'

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
Message-Id: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING
In-Reply-To: <unique-message-id@example.com>
References: <unique-message-id@example.com>

Result: OK
EOF
"

test_expect_success $PREREQ 'Show all headers' '
	git send-email \
		--dry-run \
		--suppress-cc=sob \
		--from="Example <from@example.com>" \
		--to=to@example.com \
		--cc=cc@example.com \
		--bcc=bcc@example.com \
		--in-reply-to="<unique-message-id@example.com>" \
		--smtp-server relay.example.com \
		$patches |
	sed	-e "s/^\(Date:\).*/\1 DATE-STRING/" \
		-e "s/^\(Message-Id:\).*/\1 MESSAGE-ID-STRING/" \
		-e "s/^\(X-Mailer:\).*/\1 X-MAILER-STRING/" \
		>actual-show-all-headers &&
	test_cmp expected-show-all-headers actual-show-all-headers
'

test_expect_success $PREREQ 'Prompting works' '
	clean_fake_sendmail &&
	(echo "Example <from@example.com>"
	 echo "to@example.com"
	 echo ""
	) | GIT_SEND_EMAIL_NOTTY=1 git send-email \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches \
		2>errors &&
		grep "^From: Example <from@example.com>\$" msgtxt1 &&
		grep "^To: to@example.com\$" msgtxt1
'

test_expect_success $PREREQ 'tocmd works' '
	clean_fake_sendmail &&
	cp $patches tocmd.patch &&
	echo tocmd--tocmd@example.com >>tocmd.patch &&
	{
	  echo "#!$SHELL_PATH"
	  echo sed -n -e s/^tocmd--//p \"\$1\"
	} > tocmd-sed &&
	chmod +x tocmd-sed &&
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
	{
	  echo "#!$SHELL_PATH"
	  echo sed -n -e s/^cccmd--//p \"\$1\"
	} > cccmd-sed &&
	chmod +x cccmd-sed &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--cc-cmd=./cccmd-sed \
		--smtp-server="$(pwd)/fake.sendmail" \
		cccmd.patch \
		&&
	grep "^	cccmd@example.com" msgtxt1
'

test_expect_success $PREREQ 'reject long lines' '
	z8=zzzzzzzz &&
	z64=$z8$z8$z8$z8$z8$z8$z8$z8 &&
	z512=$z64$z64$z64$z64$z64$z64$z64$z64 &&
	clean_fake_sendmail &&
	cp $patches longline.patch &&
	echo $z512$z512 >>longline.patch &&
	test_must_fail git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches longline.patch \
		2>errors &&
	grep longline.patch errors
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
	sed "1,/^\$/d" < msgtxt1 > msgbody1 &&
	grep "From: A <author@example.com>" msgbody1
'

test_expect_success $PREREQ 'Author From: not in message body' '
	clean_fake_sendmail &&
	git send-email \
		--from="A <author@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches &&
	sed "1,/^\$/d" < msgtxt1 > msgbody1 &&
	! grep "From: A <author@example.com>" msgbody1
'

test_expect_success $PREREQ 'allow long lines with --no-validate' '
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		--novalidate \
		$patches longline.patch \
		2>errors
'

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
	(echo "From Example <from@example.com>"
	 echo "To Example <to@example.com>"
	 echo ""
	) | env GIT_SEND_EMAIL_NOTTY=1 git send-email \
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
		--nochain-reply-to \
		--in-reply-to="$(cat expect)" \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches $patches $patches \
		2>errors &&
	# The first message is a reply to --in-reply-to
	sed -n -e "s/^In-Reply-To: *\(.*\)/\1/p" msgtxt1 >actual &&
	test_cmp expect actual &&
	# Second and subsequent messages are replies to the first one
	sed -n -e "s/^Message-Id: *\(.*\)/\1/p" msgtxt1 >expect &&
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
	sed -n -e "s/^Message-Id: *\(.*\)/\1/p" msgtxt1 >expect &&
	sed -n -e "s/^In-Reply-To: *\(.*\)/\1/p" msgtxt2 >actual &&
	test_cmp expect actual &&
	sed -n -e "s/^Message-Id: *\(.*\)/\1/p" msgtxt2 >expect &&
	sed -n -e "s/^In-Reply-To: *\(.*\)/\1/p" msgtxt3 >actual &&
	test_cmp expect actual
'

test_expect_success $PREREQ 'setup fake editor' '
	(echo "#!$SHELL_PATH" &&
	 echo "echo fake edit >>\"\$1\""
	) >fake-editor &&
	chmod +x fake-editor
'

test_set_editor "$(pwd)/fake-editor"

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
Message-Id: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING

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
		$patches |
	sed	-e "s/^\(Date:\).*/\1 DATE-STRING/" \
		-e "s/^\(Message-Id:\).*/\1 MESSAGE-ID-STRING/" \
		-e "s/^\(X-Mailer:\).*/\1 X-MAILER-STRING/" \
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
Message-Id: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING

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
Message-Id: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING

Result: OK
EOF
"

test_expect_success $PREREQ 'sendemail.cccmd' '
	echo echo cc-cmd@example.com > cccmd &&
	chmod +x cccmd &&
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
Message-Id: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING

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
Message-Id: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING

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
Message-Id: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING

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
Message-Id: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING

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
Message-Id: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING

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
Message-Id: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING

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
		$@ $patches > stdout &&
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
	CONFIRM=$(git config --get sendemail.confirm) &&
	git config --unset sendemail.confirm &&
	test_confirm
	ret="$?"
	git config sendemail.confirm ${CONFIRM:-never}
	test $ret = "0"
'

test_expect_success $PREREQ 'confirm by default (due to --compose)' '
	CONFIRM=$(git config --get sendemail.confirm) &&
	git config --unset sendemail.confirm &&
	test_confirm --suppress-cc=all --compose
	ret="$?"
	git config sendemail.confirm ${CONFIRM:-never}
	test $ret = "0"
'

test_expect_success $PREREQ 'confirm detects EOF (inform assumes y)' '
	CONFIRM=$(git config --get sendemail.confirm) &&
	git config --unset sendemail.confirm &&
	rm -fr outdir &&
	git format-patch -2 -o outdir &&
	GIT_SEND_EMAIL_NOTTY=1 \
		git send-email \
			--from="Example <nobody@example.com>" \
			--to=nobody@example.com \
			--smtp-server="$(pwd)/fake.sendmail" \
			outdir/*.patch < /dev/null
	ret="$?"
	git config sendemail.confirm ${CONFIRM:-never}
	test $ret = "0"
'

test_expect_success $PREREQ 'confirm detects EOF (auto causes failure)' '
	CONFIRM=$(git config --get sendemail.confirm) &&
	git config sendemail.confirm auto &&
	GIT_SEND_EMAIL_NOTTY=1 &&
	export GIT_SEND_EMAIL_NOTTY &&
		test_must_fail git send-email \
			--from="Example <nobody@example.com>" \
			--to=nobody@example.com \
			--smtp-server="$(pwd)/fake.sendmail" \
			$patches < /dev/null
	ret="$?"
	git config sendemail.confirm ${CONFIRM:-never}
	test $ret = "0"
'

test_expect_success $PREREQ 'confirm doesnt loop forever' '
	CONFIRM=$(git config --get sendemail.confirm) &&
	git config sendemail.confirm auto &&
	GIT_SEND_EMAIL_NOTTY=1 &&
	export GIT_SEND_EMAIL_NOTTY &&
		yes "bogus" | test_must_fail git send-email \
			--from="Example <nobody@example.com>" \
			--to=nobody@example.com \
			--smtp-server="$(pwd)/fake.sendmail" \
			$patches
	ret="$?"
	git config sendemail.confirm ${CONFIRM:-never}
	test $ret = "0"
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
	(echo "#!$SHELL_PATH" &&
	 echo "echo utf8 body: àéìöú >>\"\$1\""
	) >fake-editor-utf8 &&
	chmod +x fake-editor-utf8 &&
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
	(echo "#!$SHELL_PATH" &&
	 echo "(echo MIME-Version: 1.0"
	 echo " echo Content-Type: text/plain\\; charset=iso-8859-1"
	 echo " echo Content-Transfer-Encoding: 8bit"
	 echo " echo Subject: foo"
	 echo " echo "
	 echo " echo utf8 body: àéìöú) >\"\$1\""
	) >fake-editor-utf8-mime &&
	chmod +x fake-editor-utf8-mime &&
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

test_expect_success $PREREQ 'detects ambiguous reference/file conflict' '
	echo master > master &&
	git add master &&
	git commit -m"add master" &&
	test_must_fail git send-email --dry-run master 2>errors &&
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
	test "z$(sed -n -e 2p subjects)" = "zSubject: [PATCH 2/2] add master"
'

test_expect_success $PREREQ 'in-reply-to but no threading' '
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--in-reply-to="<in-reply-id@example.com>" \
		--nothread \
		$patches |
	grep "In-Reply-To: <in-reply-id@example.com>"
'

test_expect_success $PREREQ 'no in-reply-to and no threading' '
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--nothread \
		$patches $patches >stdout &&
	! grep "In-Reply-To: " stdout
'

test_expect_success $PREREQ 'threading but no chain-reply-to' '
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--thread \
		--nochain-reply-to \
		$patches $patches >stdout &&
	grep "In-Reply-To: " stdout
'

test_expect_success $PREREQ 'warning with an implicit --chain-reply-to' '
	git send-email \
	--dry-run \
	--from="Example <nobody@example.com>" \
	--to=nobody@example.com \
	outdir/000?-*.patch 2>errors >out &&
	grep "no-chain-reply-to" errors
'

test_expect_success $PREREQ 'no warning with an explicit --chain-reply-to' '
	git send-email \
	--dry-run \
	--from="Example <nobody@example.com>" \
	--to=nobody@example.com \
	--chain-reply-to \
	outdir/000?-*.patch 2>errors >out &&
	! grep "no-chain-reply-to" errors
'

test_expect_success $PREREQ 'no warning with an explicit --no-chain-reply-to' '
	git send-email \
	--dry-run \
	--from="Example <nobody@example.com>" \
	--to=nobody@example.com \
	--nochain-reply-to \
	outdir/000?-*.patch 2>errors >out &&
	! grep "no-chain-reply-to" errors
'

test_expect_success $PREREQ 'no warning with sendemail.chainreplyto = false' '
	git config sendemail.chainreplyto false &&
	git send-email \
	--dry-run \
	--from="Example <nobody@example.com>" \
	--to=nobody@example.com \
	outdir/000?-*.patch 2>errors >out &&
	! grep "no-chain-reply-to" errors
'

test_expect_success $PREREQ 'no warning with sendemail.chainreplyto = true' '
	git config sendemail.chainreplyto true &&
	git send-email \
	--dry-run \
	--from="Example <nobody@example.com>" \
	--to=nobody@example.com \
	outdir/000?-*.patch 2>errors >out &&
	! grep "no-chain-reply-to" errors
'

test_expect_success $PREREQ 'sendemail.to works' '
	git config --replace-all sendemail.to "Somebody <somebody@ex.com>" &&
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		$patches $patches >stdout &&
	grep "To: Somebody <somebody@ex.com>" stdout
'

test_expect_success $PREREQ '--no-to overrides sendemail.to' '
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--no-to \
		--to=nobody@example.com \
		$patches $patches >stdout &&
	grep "To: nobody@example.com" stdout &&
	! grep "To: Somebody <somebody@ex.com>" stdout
'

test_expect_success $PREREQ 'sendemail.cc works' '
	git config --replace-all sendemail.cc "Somebody <somebody@ex.com>" &&
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		$patches $patches >stdout &&
	grep "Cc: Somebody <somebody@ex.com>" stdout
'

test_expect_success $PREREQ '--no-cc overrides sendemail.cc' '
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--no-cc \
		--cc=bodies@example.com \
		--to=nobody@example.com \
		$patches $patches >stdout &&
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
		$patches $patches >stdout &&
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
		$patches $patches >stdout &&
	grep "RCPT TO:<bodies@example.com>" stdout &&
	! grep "RCPT TO:<other@ex.com>" stdout
'

test_expect_success $PREREQ 'patches To headers are used by default' '
	patch=`git format-patch -1 --to="bodies@example.com"` &&
	test_when_finished "rm $patch" &&
	git send-email \
		--dry-run \
		--from="Example <nobody@example.com>" \
		--smtp-server relay.example.com \
		$patch >stdout &&
	grep "RCPT TO:<bodies@example.com>" stdout
'

test_expect_success $PREREQ 'patches To headers are appended to' '
	patch=`git format-patch -1 --to="bodies@example.com"` &&
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
	patch1=`git format-patch -1 --to="bodies@example.com"` &&
	patch2=`git format-patch -1 --to="other@example.com" HEAD~` &&
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
cat >email-using-8bit <<EOF
From fe6ecc66ece37198fe5db91fa2fc41d9f4fe5cc4 Mon Sep 17 00:00:00 2001
Message-Id: <bogus-message-id@example.com>
From: author@example.com
Date: Sat, 12 Jun 2010 15:53:58 +0200
Subject: subject goes here

Dieser deutsche Text enthält einen Umlaut!
EOF
'

test_expect_success $PREREQ 'setup expect' '
cat >content-type-decl <<EOF
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
	egrep "Content|MIME" msgtxt1 >actual &&
	test_cmp actual content-type-decl
'

test_expect_success $PREREQ 'sendemail.8bitEncoding works' '
	clean_fake_sendmail &&
	git config sendemail.assume8bitEncoding UTF-8 &&
	echo bogus |
	git send-email --from=author@example.com --to=nobody@example.com \
			--smtp-server="$(pwd)/fake.sendmail" \
			email-using-8bit >stdout &&
	egrep "Content|MIME" msgtxt1 >actual &&
	test_cmp actual content-type-decl
'

test_expect_success $PREREQ '--8bit-encoding overrides sendemail.8bitEncoding' '
	clean_fake_sendmail &&
	git config sendemail.assume8bitEncoding "bogus too" &&
	echo bogus |
	git send-email --from=author@example.com --to=nobody@example.com \
			--smtp-server="$(pwd)/fake.sendmail" \
			--8bit-encoding=UTF-8 \
			email-using-8bit >stdout &&
	egrep "Content|MIME" msgtxt1 >actual &&
	test_cmp actual content-type-decl
'

test_expect_success $PREREQ 'setup expect' '
cat >email-using-8bit <<EOF
From fe6ecc66ece37198fe5db91fa2fc41d9f4fe5cc4 Mon Sep 17 00:00:00 2001
Message-Id: <bogus-message-id@example.com>
From: author@example.com
Date: Sat, 12 Jun 2010 15:53:58 +0200
Subject: Dieser Betreff enthält auch einen Umlaut!

Nothing to see here.
EOF
'

test_expect_success $PREREQ 'setup expect' '
cat >expected <<EOF
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

test_expect_success $PREREQ 'sendemail.aliasfile=~/.mailrc' '
	clean_fake_sendmail &&
	echo "alias sbd  someone@example.org" >~/.mailrc &&
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

test_done
