#!/bin/sh

test_description='but am running'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

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

	  but
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
	Subject: Re: [RFC/PATCH] but-foo.sh
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
	Test that but-am --scissors cuts at the scissors line

	This line should be included in the cummit message.
	EOF

	printf "Subject: " >subject-prefix &&

	cat - subject-prefix msg-without-scissors-line >msg-with-scissors-line <<-\EOF
	This line should not be included in the cummit message with --scissors enabled.

	 - - >8 - - remove everything above this line - - >8 - -

	EOF
'

test_expect_success setup '
	echo hello >file &&
	but add file &&
	test_tick &&
	but cummit -m first &&
	but tag first &&

	echo world >>file &&
	but add file &&
	test_tick &&
	but cummit -F msg &&
	but tag second &&

	but format-patch --stdout first >patch1 &&
	{
		echo "Message-Id: <1226501681-24923-1-but-send-email-bda@mnsspb.ru>" &&
		echo "X-Fake-Field: Line One" &&
		echo "X-Fake-Field: Line Two" &&
		echo "X-Fake-Field: Line Three" &&
		but format-patch --stdout first | sed -e "1d"
	} > patch1.eml &&
	{
		echo "X-Fake-Field: Line One" &&
		echo "X-Fake-Field: Line Two" &&
		echo "X-Fake-Field: Line Three" &&
		but format-patch --stdout first | sed -e "1d"
	} | append_cr >patch1-crlf.eml &&
	{
		printf "%255s\\n" "" &&
		echo "X-Fake-Field: Line One" &&
		echo "X-Fake-Field: Line Two" &&
		echo "X-Fake-Field: Line Three" &&
		but format-patch --stdout first | sed -e "1d"
	} > patch1-ws.eml &&
	{
		sed -ne "1p" msg &&
		echo &&
		echo "From: $BUT_AUTHOR_NAME <$BUT_AUTHOR_EMAIL>" &&
		echo "Date: $BUT_AUTHOR_DATE" &&
		echo &&
		sed -e "1,2d" msg &&
		echo "---" &&
		but diff-tree --no-cummit-id --stat -p second
	} >patch1-stbut.eml &&
	mkdir stbut-series &&
	cp patch1-stbut.eml stbut-series/patch &&
	{
		echo "# This series applies on BUT cummit $(but rev-parse first)" &&
		echo "patch"
	} >stbut-series/series &&
	{
		echo "# HG changeset patch" &&
		echo "# User $BUT_AUTHOR_NAME <$BUT_AUTHOR_EMAIL>" &&
		echo "# Date $test_tick 25200" &&
		echo "#      $(but show --pretty="%aD" -s second)" &&
		echo "# Node ID $ZERO_OID" &&
		echo "# Parent  $ZERO_OID" &&
		cat msg &&
		echo &&
		but diff-tree --no-cummit-id -p second
	} >patch1-hg.eml &&


	echo file >file &&
	but add file &&
	but cummit -F msg-without-scissors-line &&
	but tag expected-for-scissors &&
	but reset --hard HEAD^ &&

	echo file >file &&
	but add file &&
	but cummit -F msg-with-scissors-line &&
	but tag expected-for-no-scissors &&
	but format-patch --stdout expected-for-no-scissors^ >patch-with-scissors-line.eml &&
	but reset --hard HEAD^ &&

	sed -n -e "3,\$p" msg >file &&
	but add file &&
	test_tick &&
	but cummit -m third &&

	but format-patch --stdout first >patch2 &&

	but checkout -b lorem &&
	sed -n -e "11,\$p" msg >file &&
	head -n 9 msg >>file &&
	test_tick &&
	but cummit -a -m "moved stuff" &&

	echo goodbye >another &&
	but add another &&
	test_tick &&
	but cummit -m "added another file" &&

	but format-patch --stdout main >lorem-move.patch &&
	but format-patch --no-prefix --stdout main >lorem-zero.patch &&

	but checkout -b rename &&
	but mv file renamed &&
	but cummit -m "renamed a file" &&

	but format-patch -M --stdout lorem >rename.patch &&

	but reset --soft lorem^ &&
	but cummit -m "renamed a file and added another" &&

	but format-patch -M --stdout lorem^ >rename-add.patch &&

	but checkout -b empty-cummit &&
	but cummit -m "empty cummit" --allow-empty &&

	: >empty.patch &&
	but format-patch --always --stdout empty-cummit^ >empty-cummit.patch &&

	# reset time
	sane_unset test_tick &&
	test_tick
'

test_expect_success 'am applies patch correctly' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	test_tick &&
	but am <patch1 &&
	test_path_is_missing .but/rebase-apply &&
	but diff --exit-code second &&
	test "$(but rev-parse second)" = "$(but rev-parse HEAD)" &&
	test "$(but rev-parse second^)" = "$(but rev-parse HEAD^)"
'

test_expect_success 'am fails if index is dirty' '
	test_when_finished "rm -f dirtyfile" &&
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	echo dirtyfile >dirtyfile &&
	but add dirtyfile &&
	test_must_fail but am patch1 &&
	test_path_is_dir .but/rebase-apply &&
	test_cmp_rev first HEAD
'

test_expect_success 'am applies patch e-mail not in a mbox' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	but am patch1.eml &&
	test_path_is_missing .but/rebase-apply &&
	but diff --exit-code second &&
	test "$(but rev-parse second)" = "$(but rev-parse HEAD)" &&
	test "$(but rev-parse second^)" = "$(but rev-parse HEAD^)"
'

test_expect_success 'am applies patch e-mail not in a mbox with CRLF' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	but am patch1-crlf.eml &&
	test_path_is_missing .but/rebase-apply &&
	but diff --exit-code second &&
	test "$(but rev-parse second)" = "$(but rev-parse HEAD)" &&
	test "$(but rev-parse second^)" = "$(but rev-parse HEAD^)"
'

test_expect_success 'am applies patch e-mail with preceding whitespace' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	but am patch1-ws.eml &&
	test_path_is_missing .but/rebase-apply &&
	but diff --exit-code second &&
	test "$(but rev-parse second)" = "$(but rev-parse HEAD)" &&
	test "$(but rev-parse second^)" = "$(but rev-parse HEAD^)"
'

test_expect_success 'am applies stbut patch' '
	rm -fr .but/rebase-apply &&
	but checkout -f first &&
	but am patch1-stbut.eml &&
	test_path_is_missing .but/rebase-apply &&
	but diff --exit-code second &&
	test_cmp_rev second HEAD &&
	test_cmp_rev second^ HEAD^
'

test_expect_success 'am --patch-format=stbut applies stbut patch' '
	rm -fr .but/rebase-apply &&
	but checkout -f first &&
	but am --patch-format=stbut <patch1-stbut.eml &&
	test_path_is_missing .but/rebase-apply &&
	but diff --exit-code second &&
	test_cmp_rev second HEAD &&
	test_cmp_rev second^ HEAD^
'

test_expect_success 'am applies stbut series' '
	rm -fr .but/rebase-apply &&
	but checkout -f first &&
	but am stbut-series/series &&
	test_path_is_missing .but/rebase-apply &&
	but diff --exit-code second &&
	test_cmp_rev second HEAD &&
	test_cmp_rev second^ HEAD^
'

test_expect_success 'am applies hg patch' '
	rm -fr .but/rebase-apply &&
	but checkout -f first &&
	but am patch1-hg.eml &&
	test_path_is_missing .but/rebase-apply &&
	but diff --exit-code second &&
	test_cmp_rev second HEAD &&
	test_cmp_rev second^ HEAD^
'

test_expect_success 'am --patch-format=hg applies hg patch' '
	rm -fr .but/rebase-apply &&
	but checkout -f first &&
	but am --patch-format=hg <patch1-hg.eml &&
	test_path_is_missing .but/rebase-apply &&
	but diff --exit-code second &&
	test_cmp_rev second HEAD &&
	test_cmp_rev second^ HEAD^
'

test_expect_success 'am with applypatch-msg hook' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	test_hook applypatch-msg <<-\EOF &&
	cat "$1" >actual-msg &&
	echo hook-message >"$1"
	EOF
	but am patch1 &&
	test_path_is_missing .but/rebase-apply &&
	but diff --exit-code second &&
	echo hook-message >expected &&
	but log -1 --format=format:%B >actual &&
	test_cmp expected actual &&
	but log -1 --format=format:%B second >expected &&
	test_cmp expected actual-msg
'

test_expect_success 'am with failing applypatch-msg hook' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	test_hook applypatch-msg <<-\EOF &&
	exit 1
	EOF
	test_must_fail but am patch1 &&
	test_path_is_dir .but/rebase-apply &&
	but diff --exit-code first &&
	test_cmp_rev first HEAD
'

test_expect_success 'am with pre-applypatch hook' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	test_hook pre-applypatch <<-\EOF &&
	but diff first >diff.actual
	exit 0
	EOF
	but am patch1 &&
	test_path_is_missing .but/rebase-apply &&
	but diff --exit-code second &&
	test_cmp_rev second HEAD &&
	but diff first..second >diff.expected &&
	test_cmp diff.expected diff.actual
'

test_expect_success 'am with failing pre-applypatch hook' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	test_hook pre-applypatch <<-\EOF &&
	exit 1
	EOF
	test_must_fail but am patch1 &&
	test_path_is_dir .but/rebase-apply &&
	but diff --exit-code second &&
	test_cmp_rev first HEAD
'

test_expect_success 'am with post-applypatch hook' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	test_hook post-applypatch <<-\EOF &&
	but rev-parse HEAD >head.actual
	but diff second >diff.actual
	exit 0
	EOF
	but am patch1 &&
	test_path_is_missing .but/rebase-apply &&
	test_cmp_rev second HEAD &&
	but rev-parse second >head.expected &&
	test_cmp head.expected head.actual &&
	but diff second >diff.expected &&
	test_cmp diff.expected diff.actual
'

test_expect_success 'am with failing post-applypatch hook' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	test_hook post-applypatch <<-\EOF &&
	but rev-parse HEAD >head.actual
	exit 1
	EOF
	but am patch1 &&
	test_path_is_missing .but/rebase-apply &&
	but diff --exit-code second &&
	test_cmp_rev second HEAD &&
	but rev-parse second >head.expected &&
	test_cmp head.expected head.actual
'

test_expect_success 'am --scissors cuts the message at the scissors line' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout second &&
	but am --scissors patch-with-scissors-line.eml &&
	test_path_is_missing .but/rebase-apply &&
	but diff --exit-code expected-for-scissors &&
	test_cmp_rev expected-for-scissors HEAD
'

test_expect_success 'am --no-scissors overrides mailinfo.scissors' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout second &&
	test_config mailinfo.scissors true &&
	but am --no-scissors patch-with-scissors-line.eml &&
	test_path_is_missing .but/rebase-apply &&
	but diff --exit-code expected-for-no-scissors &&
	test_cmp_rev expected-for-no-scissors HEAD
'

test_expect_success 'setup: new author and cummitter' '
	BUT_AUTHOR_NAME="Another Thor" &&
	BUT_AUTHOR_EMAIL="a.thor@example.com" &&
	BUT_CUMMITTER_NAME="Co M Miter" &&
	BUT_CUMMITTER_EMAIL="c.miter@example.com" &&
	export BUT_AUTHOR_NAME BUT_AUTHOR_EMAIL BUT_CUMMITTER_NAME BUT_CUMMITTER_EMAIL
'

compare () {
	a=$(but cat-file cummit "$2" | grep "^$1 ") &&
	b=$(but cat-file cummit "$3" | grep "^$1 ") &&
	test "$a" = "$b"
}

test_expect_success 'am changes cummitter and keeps author' '
	test_tick &&
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	but am patch2 &&
	test_path_is_missing .but/rebase-apply &&
	test "$(but rev-parse main^^)" = "$(but rev-parse HEAD^^)" &&
	but diff --exit-code main..HEAD &&
	but diff --exit-code main^..HEAD^ &&
	compare author main HEAD &&
	compare author main^ HEAD^ &&
	test "$BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL>" = \
	     "$(but log -1 --pretty=format:"%cn <%ce>" HEAD)"
'

test_expect_success 'am --signoff adds Signed-off-by: line' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout -b topic_2 first &&
	but am --signoff <patch2 &&
	{
		printf "third\n\nSigned-off-by: %s <%s>\n\n" \
			"$BUT_CUMMITTER_NAME" "$BUT_CUMMITTER_EMAIL" &&
		cat msg &&
		printf "Signed-off-by: %s <%s>\n\n" \
			"$BUT_CUMMITTER_NAME" "$BUT_CUMMITTER_EMAIL"
	} >expected-log &&
	but log --pretty=%B -2 HEAD >actual &&
	test_cmp expected-log actual
'

test_expect_success 'am stays in branch' '
	echo refs/heads/topic_2 >expected &&
	but symbolic-ref HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'am --signoff does not add Signed-off-by: line if already there' '
	but format-patch --stdout first >patch3 &&
	but reset --hard first &&
	but am --signoff <patch3 &&
	but log --pretty=%B -2 HEAD >actual &&
	test_cmp expected-log actual
'

test_expect_success 'am --signoff adds Signed-off-by: if another author is preset' '
	NAME="A N Other" &&
	EMAIL="a.n.other@example.com" &&
	{
		printf "third\n\nSigned-off-by: %s <%s>\nSigned-off-by: %s <%s>\n\n" \
			"$BUT_CUMMITTER_NAME" "$BUT_CUMMITTER_EMAIL" \
			"$NAME" "$EMAIL" &&
		cat msg &&
		printf "Signed-off-by: %s <%s>\nSigned-off-by: %s <%s>\n\n" \
			"$BUT_CUMMITTER_NAME" "$BUT_CUMMITTER_EMAIL" \
			"$NAME" "$EMAIL"
	} >expected-log &&
	but reset --hard first &&
	BUT_CUMMITTER_NAME="$NAME" BUT_CUMMITTER_EMAIL="$EMAIL" \
		but am --signoff <patch3 &&
	but log --pretty=%B -2 HEAD >actual &&
	test_cmp expected-log actual
'

test_expect_success 'am --signoff duplicates Signed-off-by: if it is not the last one' '
	NAME="A N Other" &&
	EMAIL="a.n.other@example.com" &&
	{
		printf "third\n\nSigned-off-by: %s <%s>\n\
Signed-off-by: %s <%s>\nSigned-off-by: %s <%s>\n\n" \
			"$BUT_CUMMITTER_NAME" "$BUT_CUMMITTER_EMAIL" \
			"$NAME" "$EMAIL" \
			"$BUT_CUMMITTER_NAME" "$BUT_CUMMITTER_EMAIL" &&
		cat msg &&
		printf "Signed-off-by: %s <%s>\nSigned-off-by: %s <%s>\n\
Signed-off-by: %s <%s>\n\n" \
			"$BUT_CUMMITTER_NAME" "$BUT_CUMMITTER_EMAIL" \
			"$NAME" "$EMAIL" \
			"$BUT_CUMMITTER_NAME" "$BUT_CUMMITTER_EMAIL"
	} >expected-log &&
	but format-patch --stdout first >patch3 &&
	but reset --hard first &&
	but am --signoff <patch3 &&
	but log --pretty=%B -2 HEAD >actual &&
	test_cmp expected-log actual
'

test_expect_success 'am without --keep removes Re: and [PATCH] stuff' '
	but format-patch --stdout HEAD^ >tmp &&
	sed -e "/^Subject/ s,\[PATCH,Re: Re: Re: & 1/5 v2] [foo," tmp >patch4 &&
	but reset --hard HEAD^ &&
	but am <patch4 &&
	but rev-parse HEAD >expected &&
	but rev-parse topic_2 >actual &&
	test_cmp expected actual
'

test_expect_success 'am --keep really keeps the subject' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout HEAD^ &&
	but am --keep patch4 &&
	test_path_is_missing .but/rebase-apply &&
	but cat-file commit HEAD >actual &&
	grep "Re: Re: Re: \[PATCH 1/5 v2\] \[foo\] third" actual
'

test_expect_success 'am --keep-non-patch really keeps the non-patch part' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout HEAD^ &&
	but am --keep-non-patch patch4 &&
	test_path_is_missing .but/rebase-apply &&
	but cat-file commit HEAD >actual &&
	grep "^\[foo\] third" actual
'

test_expect_success 'setup am -3' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout -b base3way topic_2 &&
	sed -n -e "3,\$p" msg >file &&
	head -n 9 msg >>file &&
	but add file &&
	test_tick &&
	but cummit -m "copied stuff"
'

test_expect_success 'am -3 falls back to 3-way merge' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout -b lorem2 base3way &&
	but am -3 lorem-move.patch &&
	test_path_is_missing .but/rebase-apply &&
	but diff --exit-code lorem
'

test_expect_success 'am -3 -p0 can read --no-prefix patch' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout -b lorem3 base3way &&
	but am -3 -p0 lorem-zero.patch &&
	test_path_is_missing .but/rebase-apply &&
	but diff --exit-code lorem
'

test_expect_success 'am with config am.threeWay falls back to 3-way merge' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout -b lorem4 base3way &&
	test_config am.threeWay 1 &&
	but am lorem-move.patch &&
	test_path_is_missing .but/rebase-apply &&
	but diff --exit-code lorem
'

test_expect_success 'am with config am.threeWay overridden by --no-3way' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout -b lorem5 base3way &&
	test_config am.threeWay 1 &&
	test_must_fail but am --no-3way lorem-move.patch &&
	test_path_is_dir .but/rebase-apply
'

test_expect_success 'am can rename a file' '
	grep "^rename from" rename.patch &&
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout lorem^0 &&
	but am rename.patch &&
	test_path_is_missing .but/rebase-apply &&
	but update-index --refresh &&
	but diff --exit-code rename
'

test_expect_success 'am -3 can rename a file' '
	grep "^rename from" rename.patch &&
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout lorem^0 &&
	but am -3 rename.patch &&
	test_path_is_missing .but/rebase-apply &&
	but update-index --refresh &&
	but diff --exit-code rename
'

test_expect_success 'am -3 can rename a file after falling back to 3-way merge' '
	grep "^rename from" rename-add.patch &&
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout lorem^0 &&
	but am -3 rename-add.patch &&
	test_path_is_missing .but/rebase-apply &&
	but update-index --refresh &&
	but diff --exit-code rename
'

test_expect_success 'am -3 -q is quiet' '
	rm -fr .but/rebase-apply &&
	but checkout -f lorem2 &&
	but reset base3way --hard &&
	but am -3 -q lorem-move.patch >output.out 2>&1 &&
	test_must_be_empty output.out
'

test_expect_success 'am pauses on conflict' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout lorem2^^ &&
	test_must_fail but am lorem-move.patch &&
	test -d .but/rebase-apply
'

test_expect_success 'am --show-current-patch' '
	but am --show-current-patch >actual.patch &&
	test_cmp .but/rebase-apply/0001 actual.patch
'

test_expect_success 'am --show-current-patch=raw' '
	but am --show-current-patch=raw >actual.patch &&
	test_cmp .but/rebase-apply/0001 actual.patch
'

test_expect_success 'am --show-current-patch=diff' '
	but am --show-current-patch=diff >actual.patch &&
	test_cmp .but/rebase-apply/patch actual.patch
'

test_expect_success 'am accepts repeated --show-current-patch' '
	but am --show-current-patch --show-current-patch=raw >actual.patch &&
	test_cmp .but/rebase-apply/0001 actual.patch
'

test_expect_success 'am detects incompatible --show-current-patch' '
	test_must_fail but am --show-current-patch=raw --show-current-patch=diff &&
	test_must_fail but am --show-current-patch --show-current-patch=diff
'

test_expect_success 'am --skip works' '
	echo goodbye >expected &&
	but am --skip &&
	test_path_is_missing .but/rebase-apply &&
	but diff --exit-code lorem2^^ -- file &&
	test_cmp expected another
'

test_expect_success 'am --abort removes a stray directory' '
	mkdir .but/rebase-apply &&
	but am --abort &&
	test_path_is_missing .but/rebase-apply
'

test_expect_success 'am refuses patches when paused' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout lorem2^^ &&

	test_must_fail but am lorem-move.patch &&
	test_path_is_dir .but/rebase-apply &&
	test_cmp_rev lorem2^^ HEAD &&

	test_must_fail but am <lorem-move.patch &&
	test_path_is_dir .but/rebase-apply &&
	test_cmp_rev lorem2^^ HEAD
'

test_expect_success 'am --resolved works' '
	echo goodbye >expected &&
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout lorem2^^ &&
	test_must_fail but am lorem-move.patch &&
	test -d .but/rebase-apply &&
	echo resolved >>file &&
	but add file &&
	but am --resolved &&
	test_path_is_missing .but/rebase-apply &&
	test_cmp expected another
'

test_expect_success 'am --resolved fails if index has no changes' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout lorem2^^ &&
	test_must_fail but am lorem-move.patch &&
	test_path_is_dir .but/rebase-apply &&
	test_cmp_rev lorem2^^ HEAD &&
	test_must_fail but am --resolved &&
	test_path_is_dir .but/rebase-apply &&
	test_cmp_rev lorem2^^ HEAD
'

test_expect_success 'am --resolved fails if index has unmerged entries' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout second &&
	test_must_fail but am -3 lorem-move.patch &&
	test_path_is_dir .but/rebase-apply &&
	test_cmp_rev second HEAD &&
	test_must_fail but am --resolved >err &&
	test_path_is_dir .but/rebase-apply &&
	test_cmp_rev second HEAD &&
	test_i18ngrep "still have unmerged paths" err
'

test_expect_success 'am takes patches from a Pine mailbox' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	cat pine patch1 | but am &&
	test_path_is_missing .but/rebase-apply &&
	but diff --exit-code main^..HEAD
'

test_expect_success 'am fails on mail without patch' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	test_must_fail but am <failmail &&
	but am --abort &&
	test_path_is_missing .but/rebase-apply
'

test_expect_success 'am fails on empty patch' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	echo "---" >>failmail &&
	test_must_fail but am <failmail &&
	but am --skip &&
	test_path_is_missing .but/rebase-apply
'

test_expect_success 'am works from stdin in subdirectory' '
	rm -fr subdir &&
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	(
		mkdir -p subdir &&
		cd subdir &&
		but am <../patch1
	) &&
	but diff --exit-code second
'

test_expect_success 'am works from file (relative path given) in subdirectory' '
	rm -fr subdir &&
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	(
		mkdir -p subdir &&
		cd subdir &&
		but am ../patch1
	) &&
	but diff --exit-code second
'

test_expect_success 'am works from file (absolute path given) in subdirectory' '
	rm -fr subdir &&
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	P=$(pwd) &&
	(
		mkdir -p subdir &&
		cd subdir &&
		but am "$P/patch1"
	) &&
	but diff --exit-code second
'

test_expect_success 'am --cummitter-date-is-author-date' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	test_tick &&
	but am --cummitter-date-is-author-date patch1 &&
	but cat-file commit HEAD | sed -e "/^\$/q" >head1 &&
	sed -ne "/^author /s/.*> //p" head1 >at &&
	sed -ne "/^cummitter /s/.*> //p" head1 >ct &&
	test_cmp at ct
'

test_expect_success 'am without --cummitter-date-is-author-date' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	test_tick &&
	but am patch1 &&
	but cat-file commit HEAD | sed -e "/^\$/q" >head1 &&
	sed -ne "/^author /s/.*> //p" head1 >at &&
	sed -ne "/^cummitter /s/.*> //p" head1 >ct &&
	! test_cmp at ct
'

# This checks for +0000 because TZ is set to UTC and that should
# show up when the current time is used. The date in message is set
# by test_tick that uses -0700 timezone; if this feature does not
# work, we will see that instead of +0000.
test_expect_success 'am --ignore-date' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	test_tick &&
	but am --ignore-date patch1 &&
	but cat-file commit HEAD | sed -e "/^\$/q" >head1 &&
	sed -ne "/^author /s/.*> //p" head1 >at &&
	grep "+0000" at
'

test_expect_success 'am into an unborn branch' '
	but rev-parse first^{tree} >expected &&
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	rm -fr subdir &&
	mkdir subdir &&
	but format-patch --numbered-files -o subdir -1 first &&
	(
		cd subdir &&
		but init &&
		but am 1
	) &&
	(
		cd subdir &&
		but rev-parse HEAD^{tree} >../actual
	) &&
	test_cmp expected actual
'

test_expect_success 'am newline in subject' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	test_tick &&
	sed -e "s/second/second \\\n foo/" patch1 >patchnl &&
	but am <patchnl >output.out 2>&1 &&
	test_i18ngrep "^Applying: second \\\n foo$" output.out
'

test_expect_success 'am -q is quiet' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout first &&
	test_tick &&
	but am -q <patch1 >output.out 2>&1 &&
	test_must_be_empty output.out
'

test_expect_success 'am empty-file does not infloop' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	touch empty-file &&
	test_tick &&
	test_must_fail but am empty-file 2>actual &&
	echo Patch format detection failed. >expected &&
	test_cmp expected actual
'

test_expect_success 'am --message-id really adds the message id' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout HEAD^ &&
	but am --message-id patch1.eml &&
	test_path_is_missing .but/rebase-apply &&
	but cat-file commit HEAD | tail -n1 >actual &&
	grep Message-Id patch1.eml >expected &&
	test_cmp expected actual
'

test_expect_success 'am.messageid really adds the message id' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout HEAD^ &&
	test_config am.messageid true &&
	but am patch1.eml &&
	test_path_is_missing .but/rebase-apply &&
	but cat-file commit HEAD | tail -n1 >actual &&
	grep Message-Id patch1.eml >expected &&
	test_cmp expected actual
'

test_expect_success 'am --message-id -s signs off after the message id' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	but checkout HEAD^ &&
	but am -s --message-id patch1.eml &&
	test_path_is_missing .but/rebase-apply &&
	but cat-file commit HEAD | tail -n2 | head -n1 >actual &&
	grep Message-Id patch1.eml >expected &&
	test_cmp expected actual
'

test_expect_success 'am -3 works with rerere' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&

	# make patches one->two and two->three...
	test_cummit one file &&
	test_cummit two file &&
	test_cummit three file &&
	but format-patch -2 --stdout >seq.patch &&

	# and create a situation that conflicts...
	but reset --hard one &&
	test_cummit other file &&

	# enable rerere...
	test_config rerere.enabled true &&
	test_when_finished "rm -rf .but/rr-cache" &&

	# ...and apply. Our resolution is to skip the first
	# patch, and the rerere the second one.
	test_must_fail but am -3 seq.patch &&
	test_must_fail but am --skip &&
	echo resolved >file &&
	but add file &&
	but am --resolved &&

	# now apply again, and confirm that rerere engaged (we still
	# expect failure from am because rerere does not auto-cummit
	# for us).
	but reset --hard other &&
	test_must_fail but am -3 seq.patch &&
	test_must_fail but am --skip &&
	echo resolved >expect &&
	test_cmp expect file
'

test_expect_success 'am -s unexpected trailer block' '
	rm -fr .but/rebase-apply &&
	but reset --hard &&
	echo signed >file &&
	but add file &&
	cat >msg <<-EOF &&
	subject here

	Signed-off-by: $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL>
	[jc: tweaked log message]
	Signed-off-by: J C H <j@c.h>
	EOF
	but cummit -F msg &&
	but cat-file commit HEAD | sed -e "1,/^$/d" >original &&
	but format-patch --stdout -1 >patch &&

	but reset --hard HEAD^ &&
	but am -s patch &&
	(
		cat original &&
		echo "Signed-off-by: $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL>"
	) >expect &&
	but cat-file commit HEAD | sed -e "1,/^$/d" >actual &&
	test_cmp expect actual &&

	cat >msg <<-\EOF &&
	subject here

	We make sure that there is a blank line between the log
	message proper and Signed-off-by: line added.
	EOF
	but reset HEAD^ &&
	but cummit -F msg file &&
	but cat-file commit HEAD | sed -e "1,/^$/d" >original &&
	but format-patch --stdout -1 >patch &&

	but reset --hard HEAD^ &&
	but am -s patch &&

	(
		cat original &&
		echo &&
		echo "Signed-off-by: $BUT_CUMMITTER_NAME <$BUT_CUMMITTER_EMAIL>"
	) >expect &&
	but cat-file commit HEAD | sed -e "1,/^$/d" >actual &&
	test_cmp expect actual
'

test_expect_success 'am --patch-format=mboxrd handles mboxrd' '
	rm -fr .but/rebase-apply &&
	but checkout -f first &&
	echo mboxrd >>file &&
	but add file &&
	cat >msg <<-\INPUT_END &&
	mboxrd should escape the body

	From could trip up a loose mbox parser
	>From extra escape for reversibility
	INPUT_END
	but cummit -F msg &&
	but format-patch --pretty=mboxrd --stdout -1 >mboxrd1 &&
	grep "^>From could trip up a loose mbox parser" mboxrd1 &&
	but checkout -f first &&
	but am --patch-format=mboxrd mboxrd1 &&
	but cat-file commit HEAD | tail -n4 >out &&
	test_cmp msg out
'

test_expect_success 'am works with multi-line in-body headers' '
	FORTY="String that has a length of more than forty characters" &&
	LONG="$FORTY $FORTY" &&
	rm -fr .but/rebase-apply &&
	but checkout -f first &&
	echo one >> file &&
	but cummit -am "$LONG

    Body test" --author="$LONG <long@example.com>" &&
	but format-patch --stdout -1 >patch &&
	# bump from, date, and subject down to in-body header
	perl -lpe "
		if (/^From:/) {
			print \"From: x <x\@example.com>\";
			print \"Date: Sat, 1 Jan 2000 00:00:00 +0000\";
			print \"Subject: x\n\";
		}
	" patch >msg &&
	but checkout HEAD^ &&
	but am msg &&
	# Ensure that the author and full message are present
	but cat-file commit HEAD | grep "^author.*long@example.com" &&
	but cat-file commit HEAD | grep "^$LONG$"
'

test_expect_success 'am --quit keeps HEAD where it is' '
	mkdir .but/rebase-apply &&
	>.but/rebase-apply/last &&
	>.but/rebase-apply/next &&
	but rev-parse HEAD^ >.but/ORIG_HEAD &&
	but rev-parse HEAD >expected &&
	but am --quit &&
	test_path_is_missing .but/rebase-apply &&
	but rev-parse HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'am and .butattibutes' '
	test_create_repo attributes &&
	(
		cd attributes &&
		test_cummit init &&
		but config filter.test.clean "sed -e '\''s/smudged/clean/g'\''" &&
		but config filter.test.smudge "sed -e '\''s/clean/smudged/g'\''" &&

		test_cummit second &&
		but checkout -b test HEAD^ &&

		echo "*.txt filter=test conflict-marker-size=10" >.butattributes &&
		but add .butattributes &&
		test_cummit third &&

		echo "This text is smudged." >a.txt &&
		but add a.txt &&
		test_cummit fourth &&

		but checkout -b removal HEAD^ &&
		but rm .butattributes &&
		but add -u &&
		test_cummit fifth &&
		but cherry-pick test &&

		but checkout -b conflict third &&
		echo "This text is different." >a.txt &&
		but add a.txt &&
		test_cummit sixth &&

		but checkout test &&
		but format-patch --stdout main..HEAD >patches &&
		but reset --hard main &&
		but am patches &&
		grep "smudged" a.txt &&

		but checkout removal &&
		but reset --hard &&
		but format-patch --stdout main..HEAD >patches &&
		but reset --hard main &&
		but am patches &&
		grep "clean" a.txt &&

		but checkout conflict &&
		but reset --hard &&
		but format-patch --stdout main..HEAD >patches &&
		but reset --hard fourth &&
		test_must_fail but am -3 patches &&
		grep "<<<<<<<<<<" a.txt
	)
'

test_expect_success 'apply binary blob in partial clone' '
	printf "\\000" >binary &&
	but add binary &&
	but cummit -m "binary blob" &&
	but format-patch --stdout -m HEAD^ >patch &&

	test_create_repo server &&
	test_config -C server uploadpack.allowfilter 1 &&
	test_config -C server uploadpack.allowanysha1inwant 1 &&
	but clone --filter=blob:none "file://$(pwd)/server" client &&
	test_when_finished "rm -rf client" &&

	# Exercise to make sure that it works
	but -C client am ../patch
'

test_expect_success 'an empty input file is error regardless of --empty option' '
	test_when_finished "but am --abort || :" &&
	test_must_fail but am --empty=drop empty.patch 2>actual &&
	echo "Patch format detection failed." >expected &&
	test_cmp expected actual
'

test_expect_success 'invalid when passing the --empty option alone' '
	test_when_finished "but am --abort || :" &&
	but checkout empty-cummit^ &&
	test_must_fail but am --empty empty-cummit.patch 2>err &&
	echo "error: invalid value for '\''--empty'\'': '\''empty-cummit.patch'\''" >expected &&
	test_cmp expected err
'

test_expect_success 'a message without a patch is an error (default)' '
	test_when_finished "but am --abort || :" &&
	test_must_fail but am empty-cummit.patch >err &&
	grep "Patch is empty" err
'

test_expect_success 'a message without a patch is an error where an explicit "--empty=stop" is given' '
	test_when_finished "but am --abort || :" &&
	test_must_fail but am --empty=stop empty-cummit.patch >err &&
	grep "Patch is empty." err
'

test_expect_success 'a message without a patch will be skipped when "--empty=drop" is given' '
	but am --empty=drop empty-cummit.patch >output &&
	but rev-parse empty-cummit^ >expected &&
	but rev-parse HEAD >actual &&
	test_cmp expected actual &&
	grep "Skipping: empty cummit" output
'

test_expect_success 'record as an empty cummit when meeting e-mail message that lacks a patch' '
	but am --empty=keep empty-cummit.patch >output &&
	test_path_is_missing .but/rebase-apply &&
	but show empty-cummit --format="%B" >expected &&
	but show HEAD --format="%B" >actual &&
	grep -f actual expected &&
	grep "Creating an empty cummit: empty cummit" output
'

test_expect_success 'skip an empty patch in the middle of an am session' '
	but checkout empty-cummit^ &&
	test_must_fail but am empty-cummit.patch >err &&
	grep "Patch is empty." err &&
	grep "To record the empty patch as an empty cummit, run \"but am --allow-empty\"." err &&
	but am --skip &&
	test_path_is_missing .but/rebase-apply &&
	but rev-parse empty-cummit^ >expected &&
	but rev-parse HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'record an empty patch as an empty cummit in the middle of an am session' '
	but checkout empty-cummit^ &&
	test_must_fail but am empty-cummit.patch >err &&
	grep "Patch is empty." err &&
	grep "To record the empty patch as an empty cummit, run \"but am --allow-empty\"." err &&
	but am --allow-empty >output &&
	grep "No changes - recorded it as an empty cummit." output &&
	test_path_is_missing .but/rebase-apply &&
	but show empty-cummit --format="%B" >expected &&
	but show HEAD --format="%B" >actual &&
	grep -f actual expected
'

test_expect_success 'create an non-empty cummit when the index IS changed though "--allow-empty" is given' '
	but checkout empty-cummit^ &&
	test_must_fail but am empty-cummit.patch >err &&
	: >empty-file &&
	but add empty-file &&
	but am --allow-empty &&
	but show empty-cummit --format="%B" >expected &&
	but show HEAD --format="%B" >actual &&
	grep -f actual expected &&
	but diff HEAD^..HEAD --name-only
'

test_expect_success 'cannot create empty cummits when there is a clean index due to merge conflicts' '
	test_when_finished "but am --abort || :" &&
	but rev-parse HEAD >expected &&
	test_must_fail but am seq.patch &&
	test_must_fail but am --allow-empty >err &&
	! grep "To record the empty patch as an empty cummit, run \"but am --allow-empty\"." err &&
	but rev-parse HEAD >actual &&
	test_cmp actual expected
'

test_expect_success 'cannot create empty cummits when there is unmerged index due to merge conflicts' '
	test_when_finished "but am --abort || :" &&
	but rev-parse HEAD >expected &&
	test_must_fail but am -3 seq.patch &&
	test_must_fail but am --allow-empty >err &&
	! grep "To record the empty patch as an empty cummit, run \"but am --allow-empty\"." err &&
	but rev-parse HEAD >actual &&
	test_cmp actual expected
'

test_done
