#!/bin/sh

test_description='git am running'

. ./test-lib.sh

test_expect_success 'setup: messages' '
	cat >msg <<-\EOF &&
	second

	Lorem ipsum dolor sit amet, consectetuer sadipscing elitr, sed diam nonumy
	eirmod tempor invidunt ut labore et dolore magna aliquyam erat, sed diam
	voluptua. At vero eos et accusam et justo duo dolores et ea rebum. Stet clita
	kasd gubergren, no sea takimata sanctus est Lorem ipsum dolor sit amet. Lorem
	ipsum dolor sit amet, consetetur sadipscing elitr, sed diam nonumy eirmod
	tempor invidunt ut labore et dolore magna aliquyam erat, sed diam voluptua. At
	vero eos et accusam et justo duo dolores et ea rebum.

	EOF
	q_to_tab <<-\EOF >>msg &&
	QDuis autem vel eum iriure dolor in hendrerit in vulputate velit
	Qesse molestie consequat, vel illum dolore eu feugiat nulla facilisis
	Qat vero eros et accumsan et iusto odio dignissim qui blandit
	Qpraesent luptatum zzril delenit augue duis dolore te feugait nulla
	Qfacilisi.
	EOF
	cat >>msg <<-\EOF &&

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

	cat >failmail <<-\EOF &&
	From foo@example.com Fri May 23 10:43:49 2008
	From:	foo@example.com
	To:	bar@example.com
	Subject: Re: [RFC/PATCH] git-foo.sh
	Date:	Fri, 23 May 2008 05:23:42 +0200

	Sometimes we have to find out that there'\''s nothing left.

	EOF

	cat >pine <<-\EOF &&
	From MAILER-DAEMON Fri May 23 10:43:49 2008
	Date: 23 May 2008 05:23:42 +0200
	From: Mail System Internal Data <MAILER-DAEMON@example.com>
	Subject: DON'\''T DELETE THIS MESSAGE -- FOLDER INTERNAL DATA
	Message-ID: <foo-0001@example.com>

	This text is part of the internal format of your mail folder, and is not
	a real message.  It is created automatically by the mail system software.
	If deleted, important folder data will be lost, and it will be re-created
	with the data reset to initial values.

	EOF

	signoff="Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>"
'

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

	git format-patch --stdout master >lorem-move.patch &&

	git checkout -b rename &&
	git mv file renamed &&
	git commit -m "renamed a file" &&

	git format-patch -M --stdout lorem >rename.patch &&

	git reset --soft lorem^ &&
	git commit -m "renamed a file and added another" &&

	git format-patch -M --stdout lorem^ >rename-add.patch &&

	# reset time
	unset test_tick &&
	test_tick
'

test_expect_success 'am applies patch correctly' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	test_tick &&
	git am <patch1 &&
	! test -d .git/rebase-apply &&
	git diff --exit-code second &&
	test "$(git rev-parse second)" = "$(git rev-parse HEAD)" &&
	test "$(git rev-parse second^)" = "$(git rev-parse HEAD^)"
'

test_expect_success 'am applies patch e-mail not in a mbox' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	git am patch1.eml &&
	! test -d .git/rebase-apply &&
	git diff --exit-code second &&
	test "$(git rev-parse second)" = "$(git rev-parse HEAD)" &&
	test "$(git rev-parse second^)" = "$(git rev-parse HEAD^)"
'

test_expect_success 'am applies patch e-mail not in a mbox with CRLF' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	git am patch1-crlf.eml &&
	! test -d .git/rebase-apply &&
	git diff --exit-code second &&
	test "$(git rev-parse second)" = "$(git rev-parse HEAD)" &&
	test "$(git rev-parse second^)" = "$(git rev-parse HEAD^)"
'

test_expect_success 'setup: new author and committer' '
	GIT_AUTHOR_NAME="Another Thor" &&
	GIT_AUTHOR_EMAIL="a.thor@example.com" &&
	GIT_COMMITTER_NAME="Co M Miter" &&
	GIT_COMMITTER_EMAIL="c.miter@example.com" &&
	export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL
'

compare () {
	a=$(git cat-file commit "$2" | grep "^$1 ") &&
	b=$(git cat-file commit "$3" | grep "^$1 ") &&
	test "$a" = "$b"
}

test_expect_success 'am changes committer and keeps author' '
	test_tick &&
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	git am patch2 &&
	! test -d .git/rebase-apply &&
	test "$(git rev-parse master^^)" = "$(git rev-parse HEAD^^)" &&
	git diff --exit-code master..HEAD &&
	git diff --exit-code master^..HEAD^ &&
	compare author master HEAD &&
	compare author master^ HEAD^ &&
	test "$GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>" = \
	     "$(git log -1 --pretty=format:"%cn <%ce>" HEAD)"
'

test_expect_success 'am --signoff adds Signed-off-by: line' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout -b master2 first &&
	git am --signoff <patch2 &&
	printf "%s\n" "$signoff" >expected &&
	echo "Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>" >>expected &&
	git cat-file commit HEAD^ | grep "Signed-off-by:" >actual &&
	test_cmp expected actual &&
	echo "Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>" >expected &&
	git cat-file commit HEAD | grep "Signed-off-by:" >actual &&
	test_cmp expected actual
'

test_expect_success 'am stays in branch' '
	echo refs/heads/master2 >expected &&
	git symbolic-ref HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'am --signoff does not add Signed-off-by: line if already there' '
	git format-patch --stdout HEAD^ >patch3 &&
	sed -e "/^Subject/ s,\[PATCH,Re: Re: Re: & 1/5 v2," patch3 >patch4
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout HEAD^ &&
	git am --signoff patch4 &&
	git cat-file commit HEAD >actual &&
	test $(grep -c "^Signed-off-by:" actual) -eq 1
'

test_expect_success 'am without --keep removes Re: and [PATCH] stuff' '
	git rev-parse HEAD >expected &&
	git rev-parse master2 >actual &&
	test_cmp expected actual
'

test_expect_success 'am --keep really keeps the subject' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout HEAD^ &&
	git am --keep patch4 &&
	! test -d .git/rebase-apply &&
	git cat-file commit HEAD >actual &&
	grep "Re: Re: Re: \[PATCH 1/5 v2\] third" actual
'

test_expect_success 'am -3 falls back to 3-way merge' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout -b lorem2 master2 &&
	sed -n -e "3,\$p" msg >file &&
	head -n 9 msg >>file &&
	git add file &&
	test_tick &&
	git commit -m "copied stuff" &&
	git am -3 lorem-move.patch &&
	! test -d .git/rebase-apply &&
	git diff --exit-code lorem
'

test_expect_success 'am can rename a file' '
	grep "^rename from" rename.patch &&
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout lorem^0 &&
	git am rename.patch &&
	! test -d .git/rebase-apply &&
	git update-index --refresh &&
	git diff --exit-code rename
'

test_expect_success 'am -3 can rename a file' '
	grep "^rename from" rename.patch &&
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout lorem^0 &&
	git am -3 rename.patch &&
	! test -d .git/rebase-apply &&
	git update-index --refresh &&
	git diff --exit-code rename
'

test_expect_success 'am -3 can rename a file after falling back to 3-way merge' '
	grep "^rename from" rename-add.patch &&
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout lorem^0 &&
	git am -3 rename-add.patch &&
	! test -d .git/rebase-apply &&
	git update-index --refresh &&
	git diff --exit-code rename
'

test_expect_success 'am -3 -q is quiet' '
	rm -fr .git/rebase-apply &&
	git checkout -f lorem2 &&
	git reset master2 --hard &&
	sed -n -e "3,\$p" msg >file &&
	head -n 9 msg >>file &&
	git add file &&
	test_tick &&
	git commit -m "copied stuff" &&
	git am -3 -q lorem-move.patch >output.out 2>&1 &&
	! test -s output.out
'

test_expect_success 'am pauses on conflict' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout lorem2^^ &&
	test_must_fail git am lorem-move.patch &&
	test -d .git/rebase-apply
'

test_expect_success 'am --skip works' '
	echo goodbye >expected &&
	git am --skip &&
	! test -d .git/rebase-apply &&
	git diff --exit-code lorem2^^ -- file &&
	test_cmp expected another
'

test_expect_success 'am --resolved works' '
	echo goodbye >expected &&
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout lorem2^^ &&
	test_must_fail git am lorem-move.patch &&
	test -d .git/rebase-apply &&
	echo resolved >>file &&
	git add file &&
	git am --resolved &&
	! test -d .git/rebase-apply &&
	test_cmp expected another
'

test_expect_success 'am takes patches from a Pine mailbox' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	cat pine patch1 | git am &&
	! test -d .git/rebase-apply &&
	git diff --exit-code master^..HEAD
'

test_expect_success 'am fails on mail without patch' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	test_must_fail git am <failmail &&
	git am --abort &&
	! test -d .git/rebase-apply
'

test_expect_success 'am fails on empty patch' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	echo "---" >>failmail &&
	test_must_fail git am <failmail &&
	git am --skip &&
	! test -d .git/rebase-apply
'

test_expect_success 'am works from stdin in subdirectory' '
	rm -fr subdir &&
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	(
		mkdir -p subdir &&
		cd subdir &&
		git am <../patch1
	) &&
	git diff --exit-code second
'

test_expect_success 'am works from file (relative path given) in subdirectory' '
	rm -fr subdir &&
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	(
		mkdir -p subdir &&
		cd subdir &&
		git am ../patch1
	) &&
	git diff --exit-code second
'

test_expect_success 'am works from file (absolute path given) in subdirectory' '
	rm -fr subdir &&
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	P=$(pwd) &&
	(
		mkdir -p subdir &&
		cd subdir &&
		git am "$P/patch1"
	) &&
	git diff --exit-code second
'

test_expect_success 'am --committer-date-is-author-date' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	test_tick &&
	git am --committer-date-is-author-date patch1 &&
	git cat-file commit HEAD | sed -e "/^\$/q" >head1 &&
	sed -ne "/^author /s/.*> //p" head1 >at &&
	sed -ne "/^committer /s/.*> //p" head1 >ct &&
	test_cmp at ct
'

test_expect_success 'am without --committer-date-is-author-date' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	test_tick &&
	git am patch1 &&
	git cat-file commit HEAD | sed -e "/^\$/q" >head1 &&
	sed -ne "/^author /s/.*> //p" head1 >at &&
	sed -ne "/^committer /s/.*> //p" head1 >ct &&
	! test_cmp at ct
'

# This checks for +0000 because TZ is set to UTC and that should
# show up when the current time is used. The date in message is set
# by test_tick that uses -0700 timezone; if this feature does not
# work, we will see that instead of +0000.
test_expect_success 'am --ignore-date' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	test_tick &&
	git am --ignore-date patch1 &&
	git cat-file commit HEAD | sed -e "/^\$/q" >head1 &&
	sed -ne "/^author /s/.*> //p" head1 >at &&
	grep "+0000" at
'

test_expect_success 'am into an unborn branch' '
	git rev-parse first^{tree} >expected &&
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	rm -fr subdir &&
	mkdir subdir &&
	git format-patch --numbered-files -o subdir -1 first &&
	(
		cd subdir &&
		git init &&
		git am 1
	) &&
	(
		cd subdir &&
		git rev-parse HEAD^{tree} >../actual
	) &&
	test_cmp expected actual
'

test_expect_success 'am newline in subject' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	test_tick &&
	sed -e "s/second/second \\\n foo/" patch1 >patchnl &&
	git am <patchnl >output.out 2>&1 &&
	grep "^Applying: second \\\n foo$" output.out
'

test_expect_success 'am -q is quiet' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	test_tick &&
	git am -q <patch1 >output.out 2>&1 &&
	! test -s output.out
'

test_done
