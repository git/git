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
	qz_to_tab_space <<-\EOF >>msg &&
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

	Reported-by: A N Other <a.n.other@example.com>
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

	cat >msg-without-scissors-line <<-\EOF &&
	Test that git-am --scissors cuts at the scissors line

	This line should be included in the commit message.
	EOF

	printf "Subject: " >subject-prefix &&

	cat - subject-prefix msg-without-scissors-line >msg-with-scissors-line <<-\EOF
	This line should not be included in the commit message with --scissors enabled.

	 - - >8 - - remove everything above this line - - >8 - -

	EOF
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
	git commit -F msg &&
	git tag second &&

	git format-patch --stdout first >patch1 &&
	{
		echo "Message-Id: <1226501681-24923-1-git-send-email-bda@mnsspb.ru>" &&
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
	{
		printf "%255s\\n" ""
		echo "X-Fake-Field: Line One" &&
		echo "X-Fake-Field: Line Two" &&
		echo "X-Fake-Field: Line Three" &&
		git format-patch --stdout first | sed -e "1d"
	} > patch1-ws.eml &&
	{
		sed -ne "1p" msg &&
		echo &&
		echo "From: $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>" &&
		echo "Date: $GIT_AUTHOR_DATE" &&
		echo &&
		sed -e "1,2d" msg &&
		echo "---" &&
		git diff-tree --no-commit-id --stat -p second
	} >patch1-stgit.eml &&
	mkdir stgit-series &&
	cp patch1-stgit.eml stgit-series/patch &&
	{
		echo "# This series applies on GIT commit $(git rev-parse first)" &&
		echo "patch"
	} >stgit-series/series &&
	{
		echo "# HG changeset patch" &&
		echo "# User $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL>" &&
		echo "# Date $test_tick 25200" &&
		echo "#      $(git show --pretty="%aD" -s second)" &&
		echo "# Node ID $ZERO_OID" &&
		echo "# Parent  $ZERO_OID" &&
		cat msg &&
		echo &&
		git diff-tree --no-commit-id -p second
	} >patch1-hg.eml &&


	echo file >file &&
	git add file &&
	git commit -F msg-without-scissors-line &&
	git tag expected-for-scissors &&
	git reset --hard HEAD^ &&

	echo file >file &&
	git add file &&
	git commit -F msg-with-scissors-line &&
	git tag expected-for-no-scissors &&
	git format-patch --stdout expected-for-no-scissors^ >patch-with-scissors-line.eml &&
	git reset --hard HEAD^ &&

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
	git format-patch --no-prefix --stdout master >lorem-zero.patch &&

	git checkout -b rename &&
	git mv file renamed &&
	git commit -m "renamed a file" &&

	git format-patch -M --stdout lorem >rename.patch &&

	git reset --soft lorem^ &&
	git commit -m "renamed a file and added another" &&

	git format-patch -M --stdout lorem^ >rename-add.patch &&

	# reset time
	sane_unset test_tick &&
	test_tick
'

test_expect_success 'am applies patch correctly' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	test_tick &&
	git am <patch1 &&
	test_path_is_missing .git/rebase-apply &&
	git diff --exit-code second &&
	test "$(git rev-parse second)" = "$(git rev-parse HEAD)" &&
	test "$(git rev-parse second^)" = "$(git rev-parse HEAD^)"
'

test_expect_success 'am fails if index is dirty' '
	test_when_finished "rm -f dirtyfile" &&
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	echo dirtyfile >dirtyfile &&
	git add dirtyfile &&
	test_must_fail git am patch1 &&
	test_path_is_dir .git/rebase-apply &&
	test_cmp_rev first HEAD
'

test_expect_success 'am applies patch e-mail not in a mbox' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	git am patch1.eml &&
	test_path_is_missing .git/rebase-apply &&
	git diff --exit-code second &&
	test "$(git rev-parse second)" = "$(git rev-parse HEAD)" &&
	test "$(git rev-parse second^)" = "$(git rev-parse HEAD^)"
'

test_expect_success 'am applies patch e-mail not in a mbox with CRLF' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	git am patch1-crlf.eml &&
	test_path_is_missing .git/rebase-apply &&
	git diff --exit-code second &&
	test "$(git rev-parse second)" = "$(git rev-parse HEAD)" &&
	test "$(git rev-parse second^)" = "$(git rev-parse HEAD^)"
'

test_expect_success 'am applies patch e-mail with preceding whitespace' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	git am patch1-ws.eml &&
	test_path_is_missing .git/rebase-apply &&
	git diff --exit-code second &&
	test "$(git rev-parse second)" = "$(git rev-parse HEAD)" &&
	test "$(git rev-parse second^)" = "$(git rev-parse HEAD^)"
'

test_expect_success 'am applies stgit patch' '
	rm -fr .git/rebase-apply &&
	git checkout -f first &&
	git am patch1-stgit.eml &&
	test_path_is_missing .git/rebase-apply &&
	git diff --exit-code second &&
	test_cmp_rev second HEAD &&
	test_cmp_rev second^ HEAD^
'

test_expect_success 'am --patch-format=stgit applies stgit patch' '
	rm -fr .git/rebase-apply &&
	git checkout -f first &&
	git am --patch-format=stgit <patch1-stgit.eml &&
	test_path_is_missing .git/rebase-apply &&
	git diff --exit-code second &&
	test_cmp_rev second HEAD &&
	test_cmp_rev second^ HEAD^
'

test_expect_success 'am applies stgit series' '
	rm -fr .git/rebase-apply &&
	git checkout -f first &&
	git am stgit-series/series &&
	test_path_is_missing .git/rebase-apply &&
	git diff --exit-code second &&
	test_cmp_rev second HEAD &&
	test_cmp_rev second^ HEAD^
'

test_expect_success 'am applies hg patch' '
	rm -fr .git/rebase-apply &&
	git checkout -f first &&
	git am patch1-hg.eml &&
	test_path_is_missing .git/rebase-apply &&
	git diff --exit-code second &&
	test_cmp_rev second HEAD &&
	test_cmp_rev second^ HEAD^
'

test_expect_success 'am --patch-format=hg applies hg patch' '
	rm -fr .git/rebase-apply &&
	git checkout -f first &&
	git am --patch-format=hg <patch1-hg.eml &&
	test_path_is_missing .git/rebase-apply &&
	git diff --exit-code second &&
	test_cmp_rev second HEAD &&
	test_cmp_rev second^ HEAD^
'

test_expect_success 'am with applypatch-msg hook' '
	test_when_finished "rm -f .git/hooks/applypatch-msg" &&
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	mkdir -p .git/hooks &&
	write_script .git/hooks/applypatch-msg <<-\EOF &&
	cat "$1" >actual-msg &&
	echo hook-message >"$1"
	EOF
	git am patch1 &&
	test_path_is_missing .git/rebase-apply &&
	git diff --exit-code second &&
	echo hook-message >expected &&
	git log -1 --format=format:%B >actual &&
	test_cmp expected actual &&
	git log -1 --format=format:%B second >expected &&
	test_cmp expected actual-msg
'

test_expect_success 'am with failing applypatch-msg hook' '
	test_when_finished "rm -f .git/hooks/applypatch-msg" &&
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	mkdir -p .git/hooks &&
	write_script .git/hooks/applypatch-msg <<-\EOF &&
	exit 1
	EOF
	test_must_fail git am patch1 &&
	test_path_is_dir .git/rebase-apply &&
	git diff --exit-code first &&
	test_cmp_rev first HEAD
'

test_expect_success 'am with pre-applypatch hook' '
	test_when_finished "rm -f .git/hooks/pre-applypatch" &&
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	mkdir -p .git/hooks &&
	write_script .git/hooks/pre-applypatch <<-\EOF &&
	git diff first >diff.actual
	exit 0
	EOF
	git am patch1 &&
	test_path_is_missing .git/rebase-apply &&
	git diff --exit-code second &&
	test_cmp_rev second HEAD &&
	git diff first..second >diff.expected &&
	test_cmp diff.expected diff.actual
'

test_expect_success 'am with failing pre-applypatch hook' '
	test_when_finished "rm -f .git/hooks/pre-applypatch" &&
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	mkdir -p .git/hooks &&
	write_script .git/hooks/pre-applypatch <<-\EOF &&
	exit 1
	EOF
	test_must_fail git am patch1 &&
	test_path_is_dir .git/rebase-apply &&
	git diff --exit-code second &&
	test_cmp_rev first HEAD
'

test_expect_success 'am with post-applypatch hook' '
	test_when_finished "rm -f .git/hooks/post-applypatch" &&
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	mkdir -p .git/hooks &&
	write_script .git/hooks/post-applypatch <<-\EOF &&
	git rev-parse HEAD >head.actual
	git diff second >diff.actual
	exit 0
	EOF
	git am patch1 &&
	test_path_is_missing .git/rebase-apply &&
	test_cmp_rev second HEAD &&
	git rev-parse second >head.expected &&
	test_cmp head.expected head.actual &&
	git diff second >diff.expected &&
	test_cmp diff.expected diff.actual
'

test_expect_success 'am with failing post-applypatch hook' '
	test_when_finished "rm -f .git/hooks/post-applypatch" &&
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	mkdir -p .git/hooks &&
	write_script .git/hooks/post-applypatch <<-\EOF &&
	git rev-parse HEAD >head.actual
	exit 1
	EOF
	git am patch1 &&
	test_path_is_missing .git/rebase-apply &&
	git diff --exit-code second &&
	test_cmp_rev second HEAD &&
	git rev-parse second >head.expected &&
	test_cmp head.expected head.actual
'

test_expect_success 'am --scissors cuts the message at the scissors line' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout second &&
	git am --scissors patch-with-scissors-line.eml &&
	test_path_is_missing .git/rebase-apply &&
	git diff --exit-code expected-for-scissors &&
	test_cmp_rev expected-for-scissors HEAD
'

test_expect_success 'am --no-scissors overrides mailinfo.scissors' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout second &&
	test_config mailinfo.scissors true &&
	git am --no-scissors patch-with-scissors-line.eml &&
	test_path_is_missing .git/rebase-apply &&
	git diff --exit-code expected-for-no-scissors &&
	test_cmp_rev expected-for-no-scissors HEAD
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
	test_path_is_missing .git/rebase-apply &&
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
	{
		printf "third\n\nSigned-off-by: %s <%s>\n\n" \
			"$GIT_COMMITTER_NAME" "$GIT_COMMITTER_EMAIL" &&
		cat msg &&
		printf "Signed-off-by: %s <%s>\n\n" \
			"$GIT_COMMITTER_NAME" "$GIT_COMMITTER_EMAIL"
	} >expected-log &&
	git log --pretty=%B -2 HEAD >actual &&
	test_cmp expected-log actual
'

test_expect_success 'am stays in branch' '
	echo refs/heads/master2 >expected &&
	git symbolic-ref HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'am --signoff does not add Signed-off-by: line if already there' '
	git format-patch --stdout first >patch3 &&
	git reset --hard first &&
	git am --signoff <patch3 &&
	git log --pretty=%B -2 HEAD >actual &&
	test_cmp expected-log actual
'

test_expect_success 'am --signoff adds Signed-off-by: if another author is preset' '
	NAME="A N Other" &&
	EMAIL="a.n.other@example.com" &&
	{
		printf "third\n\nSigned-off-by: %s <%s>\nSigned-off-by: %s <%s>\n\n" \
			"$GIT_COMMITTER_NAME" "$GIT_COMMITTER_EMAIL" \
			"$NAME" "$EMAIL" &&
		cat msg &&
		printf "Signed-off-by: %s <%s>\nSigned-off-by: %s <%s>\n\n" \
			"$GIT_COMMITTER_NAME" "$GIT_COMMITTER_EMAIL" \
			"$NAME" "$EMAIL"
	} >expected-log &&
	git reset --hard first &&
	GIT_COMMITTER_NAME="$NAME" GIT_COMMITTER_EMAIL="$EMAIL" \
		git am --signoff <patch3 &&
	git log --pretty=%B -2 HEAD >actual &&
	test_cmp expected-log actual
'

test_expect_success 'am --signoff duplicates Signed-off-by: if it is not the last one' '
	NAME="A N Other" &&
	EMAIL="a.n.other@example.com" &&
	{
		printf "third\n\nSigned-off-by: %s <%s>\n\
Signed-off-by: %s <%s>\nSigned-off-by: %s <%s>\n\n" \
			"$GIT_COMMITTER_NAME" "$GIT_COMMITTER_EMAIL" \
			"$NAME" "$EMAIL" \
			"$GIT_COMMITTER_NAME" "$GIT_COMMITTER_EMAIL" &&
		cat msg &&
		printf "Signed-off-by: %s <%s>\nSigned-off-by: %s <%s>\n\
Signed-off-by: %s <%s>\n\n" \
			"$GIT_COMMITTER_NAME" "$GIT_COMMITTER_EMAIL" \
			"$NAME" "$EMAIL" \
			"$GIT_COMMITTER_NAME" "$GIT_COMMITTER_EMAIL"
	} >expected-log &&
	git format-patch --stdout first >patch3 &&
	git reset --hard first &&
	git am --signoff <patch3 &&
	git log --pretty=%B -2 HEAD >actual &&
	test_cmp expected-log actual
'

test_expect_success 'am without --keep removes Re: and [PATCH] stuff' '
	git format-patch --stdout HEAD^ >tmp &&
	sed -e "/^Subject/ s,\[PATCH,Re: Re: Re: & 1/5 v2] [foo," tmp >patch4 &&
	git reset --hard HEAD^ &&
	git am <patch4 &&
	git rev-parse HEAD >expected &&
	git rev-parse master2 >actual &&
	test_cmp expected actual
'

test_expect_success 'am --keep really keeps the subject' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout HEAD^ &&
	git am --keep patch4 &&
	test_path_is_missing .git/rebase-apply &&
	git cat-file commit HEAD >actual &&
	grep "Re: Re: Re: \[PATCH 1/5 v2\] \[foo\] third" actual
'

test_expect_success 'am --keep-non-patch really keeps the non-patch part' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout HEAD^ &&
	git am --keep-non-patch patch4 &&
	test_path_is_missing .git/rebase-apply &&
	git cat-file commit HEAD >actual &&
	grep "^\[foo\] third" actual
'

test_expect_success 'setup am -3' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout -b base3way master2 &&
	sed -n -e "3,\$p" msg >file &&
	head -n 9 msg >>file &&
	git add file &&
	test_tick &&
	git commit -m "copied stuff"
'

test_expect_success 'am -3 falls back to 3-way merge' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout -b lorem2 base3way &&
	git am -3 lorem-move.patch &&
	test_path_is_missing .git/rebase-apply &&
	git diff --exit-code lorem
'

test_expect_success 'am -3 -p0 can read --no-prefix patch' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout -b lorem3 base3way &&
	git am -3 -p0 lorem-zero.patch &&
	test_path_is_missing .git/rebase-apply &&
	git diff --exit-code lorem
'

test_expect_success 'am with config am.threeWay falls back to 3-way merge' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout -b lorem4 base3way &&
	test_config am.threeWay 1 &&
	git am lorem-move.patch &&
	test_path_is_missing .git/rebase-apply &&
	git diff --exit-code lorem
'

test_expect_success 'am with config am.threeWay overridden by --no-3way' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout -b lorem5 base3way &&
	test_config am.threeWay 1 &&
	test_must_fail git am --no-3way lorem-move.patch &&
	test_path_is_dir .git/rebase-apply
'

test_expect_success 'am can rename a file' '
	grep "^rename from" rename.patch &&
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout lorem^0 &&
	git am rename.patch &&
	test_path_is_missing .git/rebase-apply &&
	git update-index --refresh &&
	git diff --exit-code rename
'

test_expect_success 'am -3 can rename a file' '
	grep "^rename from" rename.patch &&
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout lorem^0 &&
	git am -3 rename.patch &&
	test_path_is_missing .git/rebase-apply &&
	git update-index --refresh &&
	git diff --exit-code rename
'

test_expect_success 'am -3 can rename a file after falling back to 3-way merge' '
	grep "^rename from" rename-add.patch &&
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout lorem^0 &&
	git am -3 rename-add.patch &&
	test_path_is_missing .git/rebase-apply &&
	git update-index --refresh &&
	git diff --exit-code rename
'

test_expect_success 'am -3 -q is quiet' '
	rm -fr .git/rebase-apply &&
	git checkout -f lorem2 &&
	git reset base3way --hard &&
	git am -3 -q lorem-move.patch >output.out 2>&1 &&
	test_must_be_empty output.out
'

test_expect_success 'am pauses on conflict' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout lorem2^^ &&
	test_must_fail git am lorem-move.patch &&
	test -d .git/rebase-apply
'

test_expect_success 'am --show-current-patch' '
	git am --show-current-patch >actual.patch &&
	test_cmp .git/rebase-apply/0001 actual.patch
'

test_expect_success 'am --skip works' '
	echo goodbye >expected &&
	git am --skip &&
	test_path_is_missing .git/rebase-apply &&
	git diff --exit-code lorem2^^ -- file &&
	test_cmp expected another
'

test_expect_success 'am --abort removes a stray directory' '
	mkdir .git/rebase-apply &&
	git am --abort &&
	test_path_is_missing .git/rebase-apply
'

test_expect_success 'am refuses patches when paused' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout lorem2^^ &&

	test_must_fail git am lorem-move.patch &&
	test_path_is_dir .git/rebase-apply &&
	test_cmp_rev lorem2^^ HEAD &&

	test_must_fail git am <lorem-move.patch &&
	test_path_is_dir .git/rebase-apply &&
	test_cmp_rev lorem2^^ HEAD
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
	test_path_is_missing .git/rebase-apply &&
	test_cmp expected another
'

test_expect_success 'am --resolved fails if index has no changes' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout lorem2^^ &&
	test_must_fail git am lorem-move.patch &&
	test_path_is_dir .git/rebase-apply &&
	test_cmp_rev lorem2^^ HEAD &&
	test_must_fail git am --resolved &&
	test_path_is_dir .git/rebase-apply &&
	test_cmp_rev lorem2^^ HEAD
'

test_expect_success 'am --resolved fails if index has unmerged entries' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout second &&
	test_must_fail git am -3 lorem-move.patch &&
	test_path_is_dir .git/rebase-apply &&
	test_cmp_rev second HEAD &&
	test_must_fail git am --resolved >err &&
	test_path_is_dir .git/rebase-apply &&
	test_cmp_rev second HEAD &&
	test_i18ngrep "still have unmerged paths" err
'

test_expect_success 'am takes patches from a Pine mailbox' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	cat pine patch1 | git am &&
	test_path_is_missing .git/rebase-apply &&
	git diff --exit-code master^..HEAD
'

test_expect_success 'am fails on mail without patch' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	test_must_fail git am <failmail &&
	git am --abort &&
	test_path_is_missing .git/rebase-apply
'

test_expect_success 'am fails on empty patch' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	echo "---" >>failmail &&
	test_must_fail git am <failmail &&
	git am --skip &&
	test_path_is_missing .git/rebase-apply
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
	test_i18ngrep "^Applying: second \\\n foo$" output.out
'

test_expect_success 'am -q is quiet' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout first &&
	test_tick &&
	git am -q <patch1 >output.out 2>&1 &&
	test_must_be_empty output.out
'

test_expect_success 'am empty-file does not infloop' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	touch empty-file &&
	test_tick &&
	test_must_fail git am empty-file 2>actual &&
	echo Patch format detection failed. >expected &&
	test_i18ncmp expected actual
'

test_expect_success 'am --message-id really adds the message id' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout HEAD^ &&
	git am --message-id patch1.eml &&
	test_path_is_missing .git/rebase-apply &&
	git cat-file commit HEAD | tail -n1 >actual &&
	grep Message-Id patch1.eml >expected &&
	test_cmp expected actual
'

test_expect_success 'am.messageid really adds the message id' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout HEAD^ &&
	test_config am.messageid true &&
	git am patch1.eml &&
	test_path_is_missing .git/rebase-apply &&
	git cat-file commit HEAD | tail -n1 >actual &&
	grep Message-Id patch1.eml >expected &&
	test_cmp expected actual
'

test_expect_success 'am --message-id -s signs off after the message id' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	git checkout HEAD^ &&
	git am -s --message-id patch1.eml &&
	test_path_is_missing .git/rebase-apply &&
	git cat-file commit HEAD | tail -n2 | head -n1 >actual &&
	grep Message-Id patch1.eml >expected &&
	test_cmp expected actual
'

test_expect_success 'am -3 works with rerere' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&

	# make patches one->two and two->three...
	test_commit one file &&
	test_commit two file &&
	test_commit three file &&
	git format-patch -2 --stdout >seq.patch &&

	# and create a situation that conflicts...
	git reset --hard one &&
	test_commit other file &&

	# enable rerere...
	test_config rerere.enabled true &&
	test_when_finished "rm -rf .git/rr-cache" &&

	# ...and apply. Our resolution is to skip the first
	# patch, and the rerere the second one.
	test_must_fail git am -3 seq.patch &&
	test_must_fail git am --skip &&
	echo resolved >file &&
	git add file &&
	git am --resolved &&

	# now apply again, and confirm that rerere engaged (we still
	# expect failure from am because rerere does not auto-commit
	# for us).
	git reset --hard other &&
	test_must_fail git am -3 seq.patch &&
	test_must_fail git am --skip &&
	echo resolved >expect &&
	test_cmp expect file
'

test_expect_success 'am -s unexpected trailer block' '
	rm -fr .git/rebase-apply &&
	git reset --hard &&
	echo signed >file &&
	git add file &&
	cat >msg <<-EOF &&
	subject here

	Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>
	[jc: tweaked log message]
	Signed-off-by: J C H <j@c.h>
	EOF
	git commit -F msg &&
	git cat-file commit HEAD | sed -e '1,/^$/d' >original &&
	git format-patch --stdout -1 >patch &&

	git reset --hard HEAD^ &&
	git am -s patch &&
	(
		cat original &&
		echo "Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>"
	) >expect &&
	git cat-file commit HEAD | sed -e '1,/^$/d' >actual &&
	test_cmp expect actual &&

	cat >msg <<-\EOF &&
	subject here

	We make sure that there is a blank line between the log
	message proper and Signed-off-by: line added.
	EOF
	git reset HEAD^ &&
	git commit -F msg file &&
	git cat-file commit HEAD | sed -e '1,/^$/d' >original &&
	git format-patch --stdout -1 >patch &&

	git reset --hard HEAD^ &&
	git am -s patch &&

	(
		cat original &&
		echo &&
		echo "Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>"
	) >expect &&
	git cat-file commit HEAD | sed -e '1,/^$/d' >actual &&
	test_cmp expect actual
'

test_expect_success 'am --patch-format=mboxrd handles mboxrd' '
	rm -fr .git/rebase-apply &&
	git checkout -f first &&
	echo mboxrd >>file &&
	git add file &&
	cat >msg <<-\INPUT_END &&
	mboxrd should escape the body

	From could trip up a loose mbox parser
	>From extra escape for reversibility
	INPUT_END
	git commit -F msg &&
	git format-patch --pretty=mboxrd --stdout -1 >mboxrd1 &&
	grep "^>From could trip up a loose mbox parser" mboxrd1 &&
	git checkout -f first &&
	git am --patch-format=mboxrd mboxrd1 &&
	git cat-file commit HEAD | tail -n4 >out &&
	test_cmp msg out
'

test_expect_success 'am works with multi-line in-body headers' '
	FORTY="String that has a length of more than forty characters" &&
	LONG="$FORTY $FORTY" &&
	rm -fr .git/rebase-apply &&
	git checkout -f first &&
	echo one >> file &&
	git commit -am "$LONG

    Body test" --author="$LONG <long@example.com>" &&
	git format-patch --stdout -1 >patch &&
	# bump from, date, and subject down to in-body header
	perl -lpe "
		if (/^From:/) {
			print \"From: x <x\@example.com>\";
			print \"Date: Sat, 1 Jan 2000 00:00:00 +0000\";
			print \"Subject: x\n\";
		}
	" patch >msg &&
	git checkout HEAD^ &&
	git am msg &&
	# Ensure that the author and full message are present
	git cat-file commit HEAD | grep "^author.*long@example.com" &&
	git cat-file commit HEAD | grep "^$LONG$"
'

test_expect_success 'am --quit keeps HEAD where it is' '
	mkdir .git/rebase-apply &&
	>.git/rebase-apply/last &&
	>.git/rebase-apply/next &&
	git rev-parse HEAD^ >.git/ORIG_HEAD &&
	git rev-parse HEAD >expected &&
	git am --quit &&
	test_path_is_missing .git/rebase-apply &&
	git rev-parse HEAD >actual &&
	test_cmp expected actual
'

test_done
