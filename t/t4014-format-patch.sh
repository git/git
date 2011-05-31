#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='various format-patch tests'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

test_expect_success setup '

	for i in 1 2 3 4 5 6 7 8 9 10; do echo "$i"; done >file &&
	cat file >elif &&
	git add file elif &&
	test_tick &&
	git commit -m Initial &&
	git checkout -b side &&

	for i in 1 2 5 6 A B C 7 8 9 10; do echo "$i"; done >file &&
	test_chmod +x elif &&
	test_tick &&
	git commit -m "Side changes #1" &&

	for i in D E F; do echo "$i"; done >>file &&
	git update-index file &&
	test_tick &&
	git commit -m "Side changes #2" &&
	git tag C2 &&

	for i in 5 6 1 2 3 A 4 B C 7 8 9 10 D E F; do echo "$i"; done >file &&
	git update-index file &&
	test_tick &&
	git commit -m "Side changes #3 with \\n backslash-n in it." &&

	git checkout master &&
	git diff-tree -p C2 | git apply --index &&
	test_tick &&
	git commit -m "Master accepts moral equivalent of #2"

'

test_expect_success "format-patch --ignore-if-in-upstream" '

	git format-patch --stdout master..side >patch0 &&
	cnt=`grep "^From " patch0 | wc -l` &&
	test $cnt = 3

'

test_expect_success "format-patch --ignore-if-in-upstream" '

	git format-patch --stdout \
		--ignore-if-in-upstream master..side >patch1 &&
	cnt=`grep "^From " patch1 | wc -l` &&
	test $cnt = 2

'

test_expect_success "format-patch doesn't consider merge commits" '

	git checkout -b slave master &&
	echo "Another line" >>file &&
	test_tick &&
	git commit -am "Slave change #1" &&
	echo "Yet another line" >>file &&
	test_tick &&
	git commit -am "Slave change #2" &&
	git checkout -b merger master &&
	test_tick &&
	git merge --no-ff slave &&
	cnt=`git format-patch -3 --stdout | grep "^From " | wc -l` &&
	test $cnt = 3
'

test_expect_success "format-patch result applies" '

	git checkout -b rebuild-0 master &&
	git am -3 patch0 &&
	cnt=`git rev-list master.. | wc -l` &&
	test $cnt = 2
'

test_expect_success "format-patch --ignore-if-in-upstream result applies" '

	git checkout -b rebuild-1 master &&
	git am -3 patch1 &&
	cnt=`git rev-list master.. | wc -l` &&
	test $cnt = 2
'

test_expect_success 'commit did not screw up the log message' '

	git cat-file commit side | grep "^Side .* with .* backslash-n"

'

test_expect_success 'format-patch did not screw up the log message' '

	grep "^Subject: .*Side changes #3 with .* backslash-n" patch0 &&
	grep "^Subject: .*Side changes #3 with .* backslash-n" patch1

'

test_expect_success 'replay did not screw up the log message' '

	git cat-file commit rebuild-1 | grep "^Side .* with .* backslash-n"

'

test_expect_success 'extra headers' '

	git config format.headers "To: R. E. Cipient <rcipient@example.com>
" &&
	git config --add format.headers "Cc: S. E. Cipient <scipient@example.com>
" &&
	git format-patch --stdout master..side > patch2 &&
	sed -e "/^\$/q" patch2 > hdrs2 &&
	grep "^To: R. E. Cipient <rcipient@example.com>\$" hdrs2 &&
	grep "^Cc: S. E. Cipient <scipient@example.com>\$" hdrs2

'

test_expect_success 'extra headers without newlines' '

	git config --replace-all format.headers "To: R. E. Cipient <rcipient@example.com>" &&
	git config --add format.headers "Cc: S. E. Cipient <scipient@example.com>" &&
	git format-patch --stdout master..side >patch3 &&
	sed -e "/^\$/q" patch3 > hdrs3 &&
	grep "^To: R. E. Cipient <rcipient@example.com>\$" hdrs3 &&
	grep "^Cc: S. E. Cipient <scipient@example.com>\$" hdrs3

'

test_expect_success 'extra headers with multiple To:s' '

	git config --replace-all format.headers "To: R. E. Cipient <rcipient@example.com>" &&
	git config --add format.headers "To: S. E. Cipient <scipient@example.com>" &&
	git format-patch --stdout master..side > patch4 &&
	sed -e "/^\$/q" patch4 > hdrs4 &&
	grep "^To: R. E. Cipient <rcipient@example.com>,\$" hdrs4 &&
	grep "^ *S. E. Cipient <scipient@example.com>\$" hdrs4
'

test_expect_success 'additional command line cc' '

	git config --replace-all format.headers "Cc: R. E. Cipient <rcipient@example.com>" &&
	git format-patch --cc="S. E. Cipient <scipient@example.com>" --stdout master..side | sed -e "/^\$/q" >patch5 &&
	grep "^Cc: R. E. Cipient <rcipient@example.com>,\$" patch5 &&
	grep "^ *S. E. Cipient <scipient@example.com>\$" patch5
'

test_expect_success 'command line headers' '

	git config --unset-all format.headers &&
	git format-patch --add-header="Cc: R. E. Cipient <rcipient@example.com>" --stdout master..side | sed -e "/^\$/q" >patch6 &&
	grep "^Cc: R. E. Cipient <rcipient@example.com>\$" patch6
'

test_expect_success 'configuration headers and command line headers' '

	git config --replace-all format.headers "Cc: R. E. Cipient <rcipient@example.com>" &&
	git format-patch --add-header="Cc: S. E. Cipient <scipient@example.com>" --stdout master..side | sed -e "/^\$/q" >patch7 &&
	grep "^Cc: R. E. Cipient <rcipient@example.com>,\$" patch7 &&
	grep "^ *S. E. Cipient <scipient@example.com>\$" patch7
'

test_expect_success 'command line To: header' '

	git config --unset-all format.headers &&
	git format-patch --to="R. E. Cipient <rcipient@example.com>" --stdout master..side | sed -e "/^\$/q" >patch8 &&
	grep "^To: R. E. Cipient <rcipient@example.com>\$" patch8
'

test_expect_success 'configuration To: header' '

	git config format.to "R. E. Cipient <rcipient@example.com>" &&
	git format-patch --stdout master..side | sed -e "/^\$/q" >patch9 &&
	grep "^To: R. E. Cipient <rcipient@example.com>\$" patch9
'

test_expect_success '--no-to overrides config.to' '

	git config --replace-all format.to \
		"R. E. Cipient <rcipient@example.com>" &&
	git format-patch --no-to --stdout master..side |
	sed -e "/^\$/q" >patch10 &&
	! grep "^To: R. E. Cipient <rcipient@example.com>\$" patch10
'

test_expect_success '--no-to and --to replaces config.to' '

	git config --replace-all format.to \
		"Someone <someone@out.there>" &&
	git format-patch --no-to --to="Someone Else <else@out.there>" \
		--stdout master..side |
	sed -e "/^\$/q" >patch11 &&
	! grep "^To: Someone <someone@out.there>\$" patch11 &&
	grep "^To: Someone Else <else@out.there>\$" patch11
'

test_expect_success '--no-cc overrides config.cc' '

	git config --replace-all format.cc \
		"C. E. Cipient <rcipient@example.com>" &&
	git format-patch --no-cc --stdout master..side |
	sed -e "/^\$/q" >patch12 &&
	! grep "^Cc: C. E. Cipient <rcipient@example.com>\$" patch12
'

test_expect_success '--no-add-headers overrides config.headers' '

	git config --replace-all format.headers \
		"Header1: B. E. Cipient <rcipient@example.com>" &&
	git format-patch --no-add-headers --stdout master..side |
	sed -e "/^\$/q" >patch13 &&
	! grep "^Header1: B. E. Cipient <rcipient@example.com>\$" patch13
'

test_expect_success 'multiple files' '

	rm -rf patches/ &&
	git checkout side &&
	git format-patch -o patches/ master &&
	ls patches/0001-Side-changes-1.patch patches/0002-Side-changes-2.patch patches/0003-Side-changes-3-with-n-backslash-n-in-it.patch
'

check_threading () {
	expect="$1" &&
	shift &&
	(git format-patch --stdout "$@"; echo $? > status.out) |
	# Prints everything between the Message-ID and In-Reply-To,
	# and replaces all Message-ID-lookalikes by a sequence number
	perl -ne '
		if (/^(message-id|references|in-reply-to)/i) {
			$printing = 1;
		} elsif (/^\S/) {
			$printing = 0;
		}
		if ($printing) {
			$h{$1}=$i++ if (/<([^>]+)>/ and !exists $h{$1});
			for $k (keys %h) {s/$k/$h{$k}/};
			print;
		}
		print "---\n" if /^From /i;
	' > actual &&
	test 0 = "$(cat status.out)" &&
	test_cmp "$expect" actual
}

cat >> expect.no-threading <<EOF
---
---
---
EOF

test_expect_success 'no threading' '
	git checkout side &&
	check_threading expect.no-threading master
'

cat > expect.thread <<EOF
---
Message-Id: <0>
---
Message-Id: <1>
In-Reply-To: <0>
References: <0>
---
Message-Id: <2>
In-Reply-To: <0>
References: <0>
EOF

test_expect_success 'thread' '
	check_threading expect.thread --thread master
'

cat > expect.in-reply-to <<EOF
---
Message-Id: <0>
In-Reply-To: <1>
References: <1>
---
Message-Id: <2>
In-Reply-To: <1>
References: <1>
---
Message-Id: <3>
In-Reply-To: <1>
References: <1>
EOF

test_expect_success 'thread in-reply-to' '
	check_threading expect.in-reply-to --in-reply-to="<test.message>" \
		--thread master
'

cat > expect.cover-letter <<EOF
---
Message-Id: <0>
---
Message-Id: <1>
In-Reply-To: <0>
References: <0>
---
Message-Id: <2>
In-Reply-To: <0>
References: <0>
---
Message-Id: <3>
In-Reply-To: <0>
References: <0>
EOF

test_expect_success 'thread cover-letter' '
	check_threading expect.cover-letter --cover-letter --thread master
'

cat > expect.cl-irt <<EOF
---
Message-Id: <0>
In-Reply-To: <1>
References: <1>
---
Message-Id: <2>
In-Reply-To: <0>
References: <1>
	<0>
---
Message-Id: <3>
In-Reply-To: <0>
References: <1>
	<0>
---
Message-Id: <4>
In-Reply-To: <0>
References: <1>
	<0>
EOF

test_expect_success 'thread cover-letter in-reply-to' '
	check_threading expect.cl-irt --cover-letter \
		--in-reply-to="<test.message>" --thread master
'

test_expect_success 'thread explicit shallow' '
	check_threading expect.cl-irt --cover-letter \
		--in-reply-to="<test.message>" --thread=shallow master
'

cat > expect.deep <<EOF
---
Message-Id: <0>
---
Message-Id: <1>
In-Reply-To: <0>
References: <0>
---
Message-Id: <2>
In-Reply-To: <1>
References: <0>
	<1>
EOF

test_expect_success 'thread deep' '
	check_threading expect.deep --thread=deep master
'

cat > expect.deep-irt <<EOF
---
Message-Id: <0>
In-Reply-To: <1>
References: <1>
---
Message-Id: <2>
In-Reply-To: <0>
References: <1>
	<0>
---
Message-Id: <3>
In-Reply-To: <2>
References: <1>
	<0>
	<2>
EOF

test_expect_success 'thread deep in-reply-to' '
	check_threading expect.deep-irt  --thread=deep \
		--in-reply-to="<test.message>" master
'

cat > expect.deep-cl <<EOF
---
Message-Id: <0>
---
Message-Id: <1>
In-Reply-To: <0>
References: <0>
---
Message-Id: <2>
In-Reply-To: <1>
References: <0>
	<1>
---
Message-Id: <3>
In-Reply-To: <2>
References: <0>
	<1>
	<2>
EOF

test_expect_success 'thread deep cover-letter' '
	check_threading expect.deep-cl --cover-letter --thread=deep master
'

cat > expect.deep-cl-irt <<EOF
---
Message-Id: <0>
In-Reply-To: <1>
References: <1>
---
Message-Id: <2>
In-Reply-To: <0>
References: <1>
	<0>
---
Message-Id: <3>
In-Reply-To: <2>
References: <1>
	<0>
	<2>
---
Message-Id: <4>
In-Reply-To: <3>
References: <1>
	<0>
	<2>
	<3>
EOF

test_expect_success 'thread deep cover-letter in-reply-to' '
	check_threading expect.deep-cl-irt --cover-letter \
		--in-reply-to="<test.message>" --thread=deep master
'

test_expect_success 'thread via config' '
	git config format.thread true &&
	check_threading expect.thread master
'

test_expect_success 'thread deep via config' '
	git config format.thread deep &&
	check_threading expect.deep master
'

test_expect_success 'thread config + override' '
	git config format.thread deep &&
	check_threading expect.thread --thread master
'

test_expect_success 'thread config + --no-thread' '
	git config format.thread deep &&
	check_threading expect.no-threading --no-thread master
'

test_expect_success 'excessive subject' '

	rm -rf patches/ &&
	git checkout side &&
	for i in 5 6 1 2 3 A 4 B C 7 8 9 10 D E F; do echo "$i"; done >>file &&
	git update-index file &&
	git commit -m "This is an excessively long subject line for a message due to the habit some projects have of not having a short, one-line subject at the start of the commit message, but rather sticking a whole paragraph right at the start as the only thing in the commit message. It had better not become the filename for the patch." &&
	git format-patch -o patches/ master..side &&
	ls patches/0004-This-is-an-excessively-long-subject-line-for-a-messa.patch
'

test_expect_success 'cover-letter inherits diff options' '

	git mv file foo &&
	git commit -m foo &&
	git format-patch --cover-letter -1 &&
	! grep "file => foo .* 0 *\$" 0000-cover-letter.patch &&
	git format-patch --cover-letter -1 -M &&
	grep "file => foo .* 0 *\$" 0000-cover-letter.patch

'

cat > expect << EOF
  This is an excessively long subject line for a message due to the
    habit some projects have of not having a short, one-line subject at
    the start of the commit message, but rather sticking a whole
    paragraph right at the start as the only thing in the commit
    message. It had better not become the filename for the patch.
  foo

EOF

test_expect_success 'shortlog of cover-letter wraps overly-long onelines' '

	git format-patch --cover-letter -2 &&
	sed -e "1,/A U Thor/d" -e "/^\$/q" < 0000-cover-letter.patch > output &&
	test_cmp expect output

'

cat > expect << EOF
---
 file |   16 ++++++++++++++++
 1 files changed, 16 insertions(+), 0 deletions(-)

diff --git a/file b/file
index 40f36c6..2dc5c23 100644
--- a/file
+++ b/file
@@ -13,4 +13,20 @@ C
 10
 D
 E
 F
+5
EOF

test_expect_success 'format-patch respects -U' '

	git format-patch -U4 -2 &&
	sed -e "1,/^\$/d" -e "/^+5/q" < 0001-This-is-an-excessively-long-subject-line-for-a-messa.patch > output &&
	test_cmp expect output

'

cat > expect << EOF

diff --git a/file b/file
index 40f36c6..2dc5c23 100644
--- a/file
+++ b/file
@@ -14,3 +14,19 @@ C
 D
 E
 F
+5
EOF

test_expect_success 'format-patch -p suppresses stat' '

	git format-patch -p -2 &&
	sed -e "1,/^\$/d" -e "/^+5/q" < 0001-This-is-an-excessively-long-subject-line-for-a-messa.patch > output &&
	test_cmp expect output

'

test_expect_success 'format-patch from a subdirectory (1)' '
	filename=$(
		rm -rf sub &&
		mkdir -p sub/dir &&
		cd sub/dir &&
		git format-patch -1
	) &&
	case "$filename" in
	0*)
		;; # ok
	*)
		echo "Oops? $filename"
		false
		;;
	esac &&
	test -f "$filename"
'

test_expect_success 'format-patch from a subdirectory (2)' '
	filename=$(
		rm -rf sub &&
		mkdir -p sub/dir &&
		cd sub/dir &&
		git format-patch -1 -o ..
	) &&
	case "$filename" in
	../0*)
		;; # ok
	*)
		echo "Oops? $filename"
		false
		;;
	esac &&
	basename=$(expr "$filename" : ".*/\(.*\)") &&
	test -f "sub/$basename"
'

test_expect_success 'format-patch from a subdirectory (3)' '
	rm -f 0* &&
	filename=$(
		rm -rf sub &&
		mkdir -p sub/dir &&
		cd sub/dir &&
		git format-patch -1 -o "$TRASH_DIRECTORY"
	) &&
	basename=$(expr "$filename" : ".*/\(.*\)") &&
	test -f "$basename"
'

test_expect_success 'format-patch --in-reply-to' '
	git format-patch -1 --stdout --in-reply-to "baz@foo.bar" > patch8 &&
	grep "^In-Reply-To: <baz@foo.bar>" patch8 &&
	grep "^References: <baz@foo.bar>" patch8
'

test_expect_success 'format-patch --signoff' '
	git format-patch -1 --signoff --stdout |
	grep "^Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>"
'

echo "fatal: --name-only does not make sense" > expect.name-only
echo "fatal: --name-status does not make sense" > expect.name-status
echo "fatal: --check does not make sense" > expect.check

test_expect_success 'options no longer allowed for format-patch' '
	test_must_fail git format-patch --name-only 2> output &&
	test_i18ncmp expect.name-only output &&
	test_must_fail git format-patch --name-status 2> output &&
	test_i18ncmp expect.name-status output &&
	test_must_fail git format-patch --check 2> output &&
	test_i18ncmp expect.check output'

test_expect_success 'format-patch --numstat should produce a patch' '
	git format-patch --numstat --stdout master..side > output &&
	test 6 = $(grep "^diff --git a/" output | wc -l)'

test_expect_success 'format-patch -- <path>' '
	git format-patch master..side -- file 2>error &&
	! grep "Use .--" error
'

test_expect_success 'format-patch --ignore-if-in-upstream HEAD' '
	git format-patch --ignore-if-in-upstream HEAD
'

test_expect_success 'format-patch --signature' '
	git format-patch --stdout --signature="my sig" -1 >output &&
	grep "my sig" output
'

test_expect_success 'format-patch with format.signature config' '
	git config format.signature "config sig" &&
	git format-patch --stdout -1 >output &&
	grep "config sig" output
'

test_expect_success 'format-patch --signature overrides format.signature' '
	git config format.signature "config sig" &&
	git format-patch --stdout --signature="overrides" -1 >output &&
	! grep "config sig" output &&
	grep "overrides" output
'

test_expect_success 'format-patch --no-signature ignores format.signature' '
	git config format.signature "config sig" &&
	git format-patch --stdout --signature="my sig" --no-signature \
		-1 >output &&
	! grep "config sig" output &&
	! grep "my sig" output &&
	! grep "^-- \$" output
'

test_expect_success 'format-patch --signature --cover-letter' '
	git config --unset-all format.signature &&
	git format-patch --stdout --signature="my sig" --cover-letter \
		-1 >output &&
	grep "my sig" output &&
	test 2 = $(grep "my sig" output | wc -l)
'

test_expect_success 'format.signature="" supresses signatures' '
	git config format.signature "" &&
	git format-patch --stdout -1 >output &&
	! grep "^-- \$" output
'

test_expect_success 'format-patch --no-signature supresses signatures' '
	git config --unset-all format.signature &&
	git format-patch --stdout --no-signature -1 >output &&
	! grep "^-- \$" output
'

test_expect_success 'format-patch --signature="" supresses signatures' '
	git format-patch --signature="" -1 >output &&
	! grep "^-- \$" output
'

test_expect_success TTY 'format-patch --stdout paginates' '
	rm -f pager_used &&
	(
		GIT_PAGER="wc >pager_used" &&
		export GIT_PAGER &&
		test_terminal git format-patch --stdout --all
	) &&
	test_path_is_file pager_used
'

 test_expect_success TTY 'format-patch --stdout pagination can be disabled' '
	rm -f pager_used &&
	(
		GIT_PAGER="wc >pager_used" &&
		export GIT_PAGER &&
		test_terminal git --no-pager format-patch --stdout --all &&
		test_terminal git -c "pager.format-patch=false" format-patch --stdout --all
	) &&
	test_path_is_missing pager_used &&
	test_path_is_missing .git/pager_used
'

test_expect_success 'format-patch handles multi-line subjects' '
	rm -rf patches/ &&
	echo content >>file &&
	for i in one two three; do echo $i; done >msg &&
	git add file &&
	git commit -F msg &&
	git format-patch -o patches -1 &&
	grep ^Subject: patches/0001-one.patch >actual &&
	echo "Subject: [PATCH] one two three" >expect &&
	test_cmp expect actual
'

test_expect_success 'format-patch handles multi-line encoded subjects' '
	rm -rf patches/ &&
	echo content >>file &&
	for i in en två tre; do echo $i; done >msg &&
	git add file &&
	git commit -F msg &&
	git format-patch -o patches -1 &&
	grep ^Subject: patches/0001-en.patch >actual &&
	echo "Subject: [PATCH] =?UTF-8?q?en=20tv=C3=A5=20tre?=" >expect &&
	test_cmp expect actual
'

M8="foo bar "
M64=$M8$M8$M8$M8$M8$M8$M8$M8
M512=$M64$M64$M64$M64$M64$M64$M64$M64
cat >expect <<'EOF'
Subject: [PATCH] foo bar foo bar foo bar foo bar foo bar foo bar foo bar foo
 bar foo bar foo bar foo bar foo bar foo bar foo bar foo bar
 foo bar foo bar foo bar foo bar foo bar foo bar foo bar foo
 bar foo bar foo bar foo bar foo bar foo bar foo bar foo bar
 foo bar foo bar foo bar foo bar foo bar foo bar foo bar foo
 bar foo bar foo bar foo bar foo bar foo bar foo bar foo bar
 foo bar foo bar foo bar foo bar foo bar foo bar foo bar foo
 bar foo bar foo bar foo bar foo bar foo bar foo bar foo bar
 foo bar foo bar foo bar foo bar
EOF
test_expect_success 'format-patch wraps extremely long headers (ascii)' '
	echo content >>file &&
	git add file &&
	git commit -m "$M512" &&
	git format-patch --stdout -1 >patch &&
	sed -n "/^Subject/p; /^ /p; /^$/q" <patch >subject &&
	test_cmp expect subject
'

M8="föö bar "
M64=$M8$M8$M8$M8$M8$M8$M8$M8
M512=$M64$M64$M64$M64$M64$M64$M64$M64
cat >expect <<'EOF'
Subject: [PATCH] =?UTF-8?q?f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar?=
EOF
test_expect_success 'format-patch wraps extremely long headers (rfc2047)' '
	rm -rf patches/ &&
	echo content >>file &&
	git add file &&
	git commit -m "$M512" &&
	git format-patch --stdout -1 >patch &&
	sed -n "/^Subject/p; /^ /p; /^$/q" <patch >subject &&
	test_cmp expect subject
'

M8="foo_bar_"
M64=$M8$M8$M8$M8$M8$M8$M8$M8
cat >expect <<EOF
From: $M64
 <foobar@foo.bar>
EOF
test_expect_success 'format-patch wraps non-quotable headers' '
	rm -rf patches/ &&
	echo content >>file &&
	git add file &&
	git commit -mfoo --author "$M64 <foobar@foo.bar>" &&
	git format-patch --stdout -1 >patch &&
	sed -n "/^From: /p; /^ /p; /^$/q" <patch >from &&
	test_cmp expect from
'

check_author() {
	echo content >>file &&
	git add file &&
	GIT_AUTHOR_NAME=$1 git commit -m author-check &&
	git format-patch --stdout -1 >patch &&
	grep ^From: patch >actual &&
	test_cmp expect actual
}

cat >expect <<'EOF'
From: "Foo B. Bar" <author@example.com>
EOF
test_expect_success 'format-patch quotes dot in headers' '
	check_author "Foo B. Bar"
'

cat >expect <<'EOF'
From: "Foo \"The Baz\" Bar" <author@example.com>
EOF
test_expect_success 'format-patch quotes double-quote in headers' '
	check_author "Foo \"The Baz\" Bar"
'

cat >expect <<'EOF'
From: =?UTF-8?q?"F=C3=B6o=20B.=20Bar"?= <author@example.com>
EOF
test_expect_success 'rfc2047-encoded headers also double-quote 822 specials' '
	check_author "Föo B. Bar"
'

cat >expect <<'EOF'
Subject: header with . in it
EOF
test_expect_success 'subject lines do not have 822 atom-quoting' '
	echo content >>file &&
	git add file &&
	git commit -m "header with . in it" &&
	git format-patch -k -1 --stdout >patch &&
	grep ^Subject: patch >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
Subject: [PREFIX 1/1] header with . in it
EOF
test_expect_success 'subject prefixes have space prepended' '
	git format-patch -n -1 --stdout --subject-prefix=PREFIX >patch &&
	grep ^Subject: patch >actual &&
	test_cmp expect actual
'

cat >expect <<'EOF'
Subject: [1/1] header with . in it
EOF
test_expect_success 'empty subject prefix does not have extra space' '
	git format-patch -n -1 --stdout --subject-prefix= >patch &&
	grep ^Subject: patch >actual &&
	test_cmp expect actual
'

test_done
