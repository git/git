#!/bin/sh

test_description='git am running'

. ./test-lib.sh

cat >msg <<EOF
second

Lorem ipsum dolor sit amet, consectetuer sadipscing elitr, sed diam nonumy
eirmod tempor invidunt ut labore et dolore magna aliquyam erat, sed diam
voluptua. At vero eos et accusam et justo duo dolores et ea rebum. Stet clita
kasd gubergren, no sea takimata sanctus est Lorem ipsum dolor sit amet. Lorem
ipsum dolor sit amet, consetetur sadipscing elitr, sed diam nonumy eirmod
tempor invidunt ut labore et dolore magna aliquyam erat, sed diam voluptua. At
vero eos et accusam et justo duo dolores et ea rebum.

	Duis autem vel eum iriure dolor in hendrerit in vulputate velit
	esse molestie consequat, vel illum dolore eu feugiat nulla facilisis
	at vero eros et accumsan et iusto odio dignissim qui blandit
	praesent luptatum zzril delenit augue duis dolore te feugait nulla
	facilisi.


Lorem ipsum dolor sit amet,
consectetuer adipiscing elit, sed diam nonummy nibh euismod tincidunt ut
laoreet dolore magna aliquam erat volutpat.

  git
  ---
  +++

Ut wisi enim ad minim veniam, quis nostrud exerci tation ullamcorper suscipit
lobortis nisl ut aliquip ex ea commodo consequat. Duis autem vel eum iriure
dolor in hendrerit in vulputate velit esse molestie consequat, vel illum
dolore eu feugiat nulla facilisis at vero eros et accumsan et iusto odio
dignissim qui blandit praesent luptatum zzril delenit augue duis dolore te
feugait nulla facilisi.
EOF

cat >failmail <<EOF
From foo@example.com Fri May 23 10:43:49 2008
From:	foo@example.com
To:	bar@example.com
Subject: Re: [RFC/PATCH] git-foo.sh
Date:	Fri, 23 May 2008 05:23:42 +0200

Sometimes we have to find out that there's nothing left.

EOF

cat >pine <<EOF
From MAILER-DAEMON Fri May 23 10:43:49 2008
Date: 23 May 2008 05:23:42 +0200
From: Mail System Internal Data <MAILER-DAEMON@example.com>
Subject: DON'T DELETE THIS MESSAGE -- FOLDER INTERNAL DATA
Message-ID: <foo-0001@example.com>

This text is part of the internal format of your mail folder, and is not
a real message.  It is created automatically by the mail system software.
If deleted, important folder data will be lost, and it will be re-created
with the data reset to initial values.

EOF

echo "Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>" >expected

test_expect_success setup '
	echo hello >file &&
	git add file &&
	test_tick &&
	git commit -m first &&
	git tag first &&
	echo world >>file &&
	git add file &&
	test_tick &&
	git commit -s -F msg &&
	git tag second &&
	git format-patch --stdout first >patch1 &&
	{
		echo "X-Fake-Field: Line One" &&
		echo "X-Fake-Field: Line Two" &&
		echo "X-Fake-Field: Line Three" &&
		git format-patch --stdout first | sed -e "1d"
	} > patch1.eml &&
	{
		echo "X-Fake-Field: Line One" &&
		echo "X-Fake-Field: Line Two" &&
		echo "X-Fake-Field: Line Three" &&
		git format-patch --stdout first | sed -e "1d"
	} | append_cr >patch1-crlf.eml &&
	sed -n -e "3,\$p" msg >file &&
	git add file &&
	test_tick &&
	git commit -m third &&
	git format-patch --stdout first >patch2	&&
	git checkout -b lorem &&
	sed -n -e "11,\$p" msg >file &&
	head -n 9 msg >>file &&
	test_tick &&
	git commit -a -m "moved stuff" &&
	echo goodbye >another &&
	git add another &&
	test_tick &&
	git commit -m "added another file" &&
	git format-patch --stdout master >lorem-move.patch
'

# reset time
unset test_tick
test_tick

test_expect_success 'am applies patch correctly' '
	git checkout first &&
	test_tick &&
	git am <patch1 &&
	! test -d .git/rebase-apply &&
	test -z "$(git diff second)" &&
	test "$(git rev-parse second)" = "$(git rev-parse HEAD)" &&
	test "$(git rev-parse second^)" = "$(git rev-parse HEAD^)"
'

test_expect_success 'am applies patch e-mail not in a mbox' '
	git checkout first &&
	git am patch1.eml &&
	! test -d .git/rebase-apply &&
	test -z "$(git diff second)" &&
	test "$(git rev-parse second)" = "$(git rev-parse HEAD)" &&
	test "$(git rev-parse second^)" = "$(git rev-parse HEAD^)"
'

test_expect_success 'am applies patch e-mail not in a mbox with CRLF' '
	git checkout first &&
	git am patch1-crlf.eml &&
	! test -d .git/rebase-apply &&
	test -z "$(git diff second)" &&
	test "$(git rev-parse second)" = "$(git rev-parse HEAD)" &&
	test "$(git rev-parse second^)" = "$(git rev-parse HEAD^)"
'

GIT_AUTHOR_NAME="Another Thor"
GIT_AUTHOR_EMAIL="a.thor@example.com"
GIT_COMMITTER_NAME="Co M Miter"
GIT_COMMITTER_EMAIL="c.miter@example.com"
export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL

compare () {
	test "$(git cat-file commit "$2" | grep "^$1 ")" = \
	     "$(git cat-file commit "$3" | grep "^$1 ")"
}

test_expect_success 'am changes committer and keeps author' '
	test_tick &&
	git checkout first &&
	git am patch2 &&
	! test -d .git/rebase-apply &&
	test "$(git rev-parse master^^)" = "$(git rev-parse HEAD^^)" &&
	test -z "$(git diff master..HEAD)" &&
	test -z "$(git diff master^..HEAD^)" &&
	compare author master HEAD &&
	compare author master^ HEAD^ &&
	test "$GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>" = \
	     "$(git log -1 --pretty=format:"%cn <%ce>" HEAD)"
'

test_expect_success 'am --signoff adds Signed-off-by: line' '
	git checkout -b master2 first &&
	git am --signoff <patch2 &&
	echo "Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>" >>expected &&
	git cat-file commit HEAD^ | grep "Signed-off-by:" >actual &&
	test_cmp actual expected &&
	echo "Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>" >expected &&
	git cat-file commit HEAD | grep "Signed-off-by:" >actual &&
	test_cmp actual expected
'

test_expect_success 'am stays in branch' '
	test "refs/heads/master2" = "$(git symbolic-ref HEAD)"
'

test_expect_success 'am --signoff does not add Signed-off-by: line if already there' '
	git format-patch --stdout HEAD^ >patch3 &&
	sed -e "/^Subject/ s,\[PATCH,Re: Re: Re: & 1/5 v2," patch3 >patch4
	git checkout HEAD^ &&
	git am --signoff patch4 &&
	test "$(git cat-file commit HEAD | grep -c "^Signed-off-by:")" -eq 1
'

test_expect_success 'am without --keep removes Re: and [PATCH] stuff' '
	test "$(git rev-parse HEAD)" = "$(git rev-parse master2)"
'

test_expect_success 'am --keep really keeps the subject' '
	git checkout HEAD^ &&
	git am --keep patch4 &&
	! test -d .git/rebase-apply &&
	git cat-file commit HEAD |
		fgrep "Re: Re: Re: [PATCH 1/5 v2] third"
'

test_expect_success 'am -3 falls back to 3-way merge' '
	git checkout -b lorem2 master2 &&
	sed -n -e "3,\$p" msg >file &&
	head -n 9 msg >>file &&
	git add file &&
	test_tick &&
	git commit -m "copied stuff" &&
	git am -3 lorem-move.patch &&
	! test -d .git/rebase-apply &&
	test -z "$(git diff lorem)"
'

test_expect_success 'am -3 -q is quiet' '
	git reset master2 --hard &&
	sed -n -e "3,\$p" msg >file &&
	head -n 9 msg >>file &&
	git add file &&
	test_tick &&
	git commit -m "copied stuff" &&
	git am -3 -q lorem-move.patch > output.out 2>&1 &&
	! test -s output.out
'

test_expect_success 'am pauses on conflict' '
	git checkout lorem2^^ &&
	test_must_fail git am lorem-move.patch &&
	test -d .git/rebase-apply
'

test_expect_success 'am --skip works' '
	git am --skip &&
	! test -d .git/rebase-apply &&
	test -z "$(git diff lorem2^^ -- file)" &&
	test goodbye = "$(cat another)"
'

test_expect_success 'am --resolved works' '
	git checkout lorem2^^ &&
	test_must_fail git am lorem-move.patch &&
	test -d .git/rebase-apply &&
	echo resolved >>file &&
	git add file &&
	git am --resolved &&
	! test -d .git/rebase-apply &&
	test goodbye = "$(cat another)"
'

test_expect_success 'am takes patches from a Pine mailbox' '
	git checkout first &&
	cat pine patch1 | git am &&
	! test -d .git/rebase-apply &&
	test -z "$(git diff master^..HEAD)"
'

test_expect_success 'am fails on mail without patch' '
	test_must_fail git am <failmail &&
	rm -r .git/rebase-apply/
'

test_expect_success 'am fails on empty patch' '
	echo "---" >>failmail &&
	test_must_fail git am <failmail &&
	git am --skip &&
	! test -d .git/rebase-apply
'

test_expect_success 'am works from stdin in subdirectory' '
	rm -fr subdir &&
	git checkout first &&
	(
		mkdir -p subdir &&
		cd subdir &&
		git am <../patch1
	) &&
	test -z "$(git diff second)"
'

test_expect_success 'am works from file (relative path given) in subdirectory' '
	rm -fr subdir &&
	git checkout first &&
	(
		mkdir -p subdir &&
		cd subdir &&
		git am ../patch1
	) &&
	test -z "$(git diff second)"
'

test_expect_success 'am works from file (absolute path given) in subdirectory' '
	rm -fr subdir &&
	git checkout first &&
	P=$(pwd) &&
	(
		mkdir -p subdir &&
		cd subdir &&
		git am "$P/patch1"
	) &&
	test -z "$(git diff second)"
'

test_expect_success 'am --committer-date-is-author-date' '
	git checkout first &&
	test_tick &&
	git am --committer-date-is-author-date patch1 &&
	git cat-file commit HEAD | sed -e "/^\$/q" >head1 &&
	at=$(sed -ne "/^author /s/.*> //p" head1) &&
	ct=$(sed -ne "/^committer /s/.*> //p" head1) &&
	test "$at" = "$ct"
'

test_expect_success 'am without --committer-date-is-author-date' '
	git checkout first &&
	test_tick &&
	git am patch1 &&
	git cat-file commit HEAD | sed -e "/^\$/q" >head1 &&
	at=$(sed -ne "/^author /s/.*> //p" head1) &&
	ct=$(sed -ne "/^committer /s/.*> //p" head1) &&
	test "$at" != "$ct"
'

# This checks for +0000 because TZ is set to UTC and that should
# show up when the current time is used. The date in message is set
# by test_tick that uses -0700 timezone; if this feature does not
# work, we will see that instead of +0000.
test_expect_success 'am --ignore-date' '
	git checkout first &&
	test_tick &&
	git am --ignore-date patch1 &&
	git cat-file commit HEAD | sed -e "/^\$/q" >head1 &&
	at=$(sed -ne "/^author /s/.*> //p" head1) &&
	echo "$at" | grep "+0000"
'

test_expect_success 'am into an unborn branch' '
	rm -fr subdir &&
	mkdir -p subdir &&
	git format-patch --numbered-files -o subdir -1 first &&
	(
		cd subdir &&
		git init &&
		git am 1
	) &&
	result=$(
		cd subdir && git rev-parse HEAD^{tree}
	) &&
	test "z$result" = "z$(git rev-parse first^{tree})"
'

test_expect_success 'am newline in subject' '
	git checkout first &&
	test_tick &&
	sed -e "s/second/second \\\n foo/" patch1 > patchnl &&
	git am < patchnl > output.out 2>&1 &&
	grep "^Applying: second \\\n foo$" output.out
'

test_expect_success 'am -q is quiet' '
	git checkout first &&
	test_tick &&
	git am -q < patch1 > output.out 2>&1 &&
	! test -s output.out
'

test_done
