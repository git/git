#!/bin/sh

test_description='git-send-email'
. ./test-lib.sh

PROG='git send-email'
test_expect_success \
    'prepare reference tree' \
    'echo "1A quick brown fox jumps over the" >file &&
     echo "lazy dog" >>file &&
     git add file &&
     GIT_AUTHOR_NAME="A" git commit -a -m "Initial."'

test_expect_success \
    'Setup helper tool' \
    '(echo "#!/bin/sh"
      echo shift
      echo output=1
      echo "while test -f commandline\$output; do output=\$((\$output+1)); done"
      echo for a
      echo do
      echo "  echo \"!\$a!\""
      echo "done >commandline\$output"
      echo "cat > msgtxt\$output"
      ) >fake.sendmail &&
     chmod +x ./fake.sendmail &&
     git add fake.sendmail &&
     GIT_AUTHOR_NAME="A" git commit -a -m "Second."'

clean_fake_sendmail() {
	rm -f commandline* msgtxt*
}

test_expect_success 'Extract patches' '
    patches=`git format-patch -n HEAD^1`
'

test_expect_success 'Send patches' '
     git send-email --from="Example <nobody@example.com>" --to=nobody@example.com --smtp-server="$(pwd)/fake.sendmail" $patches 2>errors
'

cat >expected <<\EOF
!nobody@example.com!
!author@example.com!
EOF
test_expect_success \
    'Verify commandline' \
    'diff commandline1 expected'

cat >expected-show-all-headers <<\EOF
0001-Second.patch
(mbox) Adding cc: A <author@example.com> from line 'From: A <author@example.com>'
Dry-OK. Log says:
Server: relay.example.com
MAIL FROM:<from@example.com>
RCPT TO:<to@example.com>,<cc@example.com>,<author@example.com>,<bcc@example.com>
From: Example <from@example.com>
To: to@example.com
Cc: cc@example.com, A <author@example.com>
Subject: [PATCH 1/1] Second.
Date: DATE-STRING
Message-Id: MESSAGE-ID-STRING
X-Mailer: X-MAILER-STRING
In-Reply-To: <unique-message-id@example.com>
References: <unique-message-id@example.com>

Result: OK
EOF

test_expect_success 'Show all headers' '
	git send-email \
		--dry-run \
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
	diff -u expected-show-all-headers actual-show-all-headers
'

z8=zzzzzzzz
z64=$z8$z8$z8$z8$z8$z8$z8$z8
z512=$z64$z64$z64$z64$z64$z64$z64$z64
test_expect_success 'reject long lines' '
	clean_fake_sendmail &&
	cp $patches longline.patch &&
	echo $z512$z512 >>longline.patch &&
	! git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches longline.patch \
		2>errors &&
	grep longline.patch errors
'

test_expect_success 'no patch was sent' '
	! test -e commandline1
'

test_expect_success 'allow long lines with --no-validate' '
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		--no-validate \
		$patches longline.patch \
		2>errors
'

test_expect_success 'Invalid In-Reply-To' '
	clean_fake_sendmail &&
	git send-email \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--in-reply-to=" " \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches
		2>errors
	! grep "^In-Reply-To: < *>" msgtxt1
'

test_expect_success 'Valid In-Reply-To when prompting' '
	clean_fake_sendmail &&
	(echo "From Example <from@example.com>"
	 echo "To Example <to@example.com>"
	 echo ""
	) | env GIT_SEND_EMAIL_NOTTY=1 git send-email \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches 2>errors &&
	! grep "^In-Reply-To: < *>" msgtxt1
'

test_expect_success 'setup fake editor' '
	(echo "#!/bin/sh" &&
	 echo "echo fake edit >>\$1"
	) >fake-editor &&
	chmod +x fake-editor
'

test_expect_success '--compose works' '
	clean_fake_sendmail &&
	echo y | \
		GIT_EDITOR=$(pwd)/fake-editor \
		GIT_SEND_EMAIL_NOTTY=1 \
		git send-email \
		--compose --subject foo \
		--from="Example <nobody@example.com>" \
		--to=nobody@example.com \
		--smtp-server="$(pwd)/fake.sendmail" \
		$patches \
		2>errors
'

test_expect_success 'first message is compose text' '
	grep "^fake edit" msgtxt1
'

test_expect_success 'second message is patch' '
	grep "Subject:.*Second" msgtxt2
'

test_done
