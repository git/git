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
	git commit -m "Master accepts moral equivalent of #2" &&

	git checkout side &&
	git checkout -b patchid &&
	for i in 5 6 1 2 3 A 4 B C 7 8 9 10 D E F; do echo "$i"; done >file2 &&
	for i in 1 2 3 A 4 B C 7 8 9 10 D E F 5 6; do echo "$i"; done >file3 &&
	for i in 8 9 10; do echo "$i"; done >file &&
	git add file file2 file3 &&
	test_tick &&
	git commit -m "patchid 1" &&
	for i in 4 A B 7 8 9 10; do echo "$i"; done >file2 &&
	for i in 8 9 10 5 6; do echo "$i"; done >file3 &&
	git add file2 file3 &&
	test_tick &&
	git commit -m "patchid 2" &&
	for i in 10 5 6; do echo "$i"; done >file &&
	git add file &&
	test_tick &&
	git commit -m "patchid 3" &&

	git checkout master
'

test_expect_success "format-patch --ignore-if-in-upstream" '

	git format-patch --stdout master..side >patch0 &&
	cnt=$(grep "^From " patch0 | wc -l) &&
	test $cnt = 3

'

test_expect_success "format-patch --ignore-if-in-upstream" '

	git format-patch --stdout \
		--ignore-if-in-upstream master..side >patch1 &&
	cnt=$(grep "^From " patch1 | wc -l) &&
	test $cnt = 2

'

test_expect_success "format-patch --ignore-if-in-upstream handles tags" '
	git tag -a v1 -m tag side &&
	git tag -a v2 -m tag master &&
	git format-patch --stdout --ignore-if-in-upstream v2..v1 >patch1 &&
	cnt=$(grep "^From " patch1 | wc -l) &&
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
	cnt=$(git format-patch -3 --stdout | grep "^From " | wc -l) &&
	test $cnt = 3
'

test_expect_success "format-patch result applies" '

	git checkout -b rebuild-0 master &&
	git am -3 patch0 &&
	cnt=$(git rev-list master.. | wc -l) &&
	test $cnt = 2
'

test_expect_success "format-patch --ignore-if-in-upstream result applies" '

	git checkout -b rebuild-1 master &&
	git am -3 patch1 &&
	cnt=$(git rev-list master.. | wc -l) &&
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

	git config format.headers "To: R E Cipient <rcipient@example.com>
" &&
	git config --add format.headers "Cc: S E Cipient <scipient@example.com>
" &&
	git format-patch --stdout master..side > patch2 &&
	sed -e "/^\$/q" patch2 > hdrs2 &&
	grep "^To: R E Cipient <rcipient@example.com>\$" hdrs2 &&
	grep "^Cc: S E Cipient <scipient@example.com>\$" hdrs2

'

test_expect_success 'extra headers without newlines' '

	git config --replace-all format.headers "To: R E Cipient <rcipient@example.com>" &&
	git config --add format.headers "Cc: S E Cipient <scipient@example.com>" &&
	git format-patch --stdout master..side >patch3 &&
	sed -e "/^\$/q" patch3 > hdrs3 &&
	grep "^To: R E Cipient <rcipient@example.com>\$" hdrs3 &&
	grep "^Cc: S E Cipient <scipient@example.com>\$" hdrs3

'

test_expect_success 'extra headers with multiple To:s' '

	git config --replace-all format.headers "To: R E Cipient <rcipient@example.com>" &&
	git config --add format.headers "To: S E Cipient <scipient@example.com>" &&
	git format-patch --stdout master..side > patch4 &&
	sed -e "/^\$/q" patch4 > hdrs4 &&
	grep "^To: R E Cipient <rcipient@example.com>,\$" hdrs4 &&
	grep "^ *S E Cipient <scipient@example.com>\$" hdrs4
'

test_expect_success 'additional command line cc (ascii)' '

	git config --replace-all format.headers "Cc: R E Cipient <rcipient@example.com>" &&
	git format-patch --cc="S E Cipient <scipient@example.com>" --stdout master..side | sed -e "/^\$/q" >patch5 &&
	grep "^Cc: R E Cipient <rcipient@example.com>,\$" patch5 &&
	grep "^ *S E Cipient <scipient@example.com>\$" patch5
'

test_expect_failure 'additional command line cc (rfc822)' '

	git config --replace-all format.headers "Cc: R E Cipient <rcipient@example.com>" &&
	git format-patch --cc="S. E. Cipient <scipient@example.com>" --stdout master..side | sed -e "/^\$/q" >patch5 &&
	grep "^Cc: R E Cipient <rcipient@example.com>,\$" patch5 &&
	grep "^ *\"S. E. Cipient\" <scipient@example.com>\$" patch5
'

test_expect_success 'command line headers' '

	git config --unset-all format.headers &&
	git format-patch --add-header="Cc: R E Cipient <rcipient@example.com>" --stdout master..side | sed -e "/^\$/q" >patch6 &&
	grep "^Cc: R E Cipient <rcipient@example.com>\$" patch6
'

test_expect_success 'configuration headers and command line headers' '

	git config --replace-all format.headers "Cc: R E Cipient <rcipient@example.com>" &&
	git format-patch --add-header="Cc: S E Cipient <scipient@example.com>" --stdout master..side | sed -e "/^\$/q" >patch7 &&
	grep "^Cc: R E Cipient <rcipient@example.com>,\$" patch7 &&
	grep "^ *S E Cipient <scipient@example.com>\$" patch7
'

test_expect_success 'command line To: header (ascii)' '

	git config --unset-all format.headers &&
	git format-patch --to="R E Cipient <rcipient@example.com>" --stdout master..side | sed -e "/^\$/q" >patch8 &&
	grep "^To: R E Cipient <rcipient@example.com>\$" patch8
'

test_expect_failure 'command line To: header (rfc822)' '

	git format-patch --to="R. E. Cipient <rcipient@example.com>" --stdout master..side | sed -e "/^\$/q" >patch8 &&
	grep "^To: \"R. E. Cipient\" <rcipient@example.com>\$" patch8
'

test_expect_failure 'command line To: header (rfc2047)' '

	git format-patch --to="R Ä Cipient <rcipient@example.com>" --stdout master..side | sed -e "/^\$/q" >patch8 &&
	grep "^To: =?UTF-8?q?R=20=C3=84=20Cipient?= <rcipient@example.com>\$" patch8
'

test_expect_success 'configuration To: header (ascii)' '

	git config format.to "R E Cipient <rcipient@example.com>" &&
	git format-patch --stdout master..side | sed -e "/^\$/q" >patch9 &&
	grep "^To: R E Cipient <rcipient@example.com>\$" patch9
'

test_expect_failure 'configuration To: header (rfc822)' '

	git config format.to "R. E. Cipient <rcipient@example.com>" &&
	git format-patch --stdout master..side | sed -e "/^\$/q" >patch9 &&
	grep "^To: \"R. E. Cipient\" <rcipient@example.com>\$" patch9
'

test_expect_failure 'configuration To: header (rfc2047)' '

	git config format.to "R Ä Cipient <rcipient@example.com>" &&
	git format-patch --stdout master..side | sed -e "/^\$/q" >patch9 &&
	grep "^To: =?UTF-8?q?R=20=C3=84=20Cipient?= <rcipient@example.com>\$" patch9
'

# check_patch <patch>: Verify that <patch> looks like a half-sane
# patch email to avoid a false positive with !grep
check_patch () {
	grep -e "^From:" "$1" &&
	grep -e "^Date:" "$1" &&
	grep -e "^Subject:" "$1"
}

test_expect_success 'format.from=false' '

	git -c format.from=false format-patch --stdout master..side |
	sed -e "/^\$/q" >patch &&
	check_patch patch &&
	! grep "^From: C O Mitter <committer@example.com>\$" patch
'

test_expect_success 'format.from=true' '

	git -c format.from=true format-patch --stdout master..side |
	sed -e "/^\$/q" >patch &&
	check_patch patch &&
	grep "^From: C O Mitter <committer@example.com>\$" patch
'

test_expect_success 'format.from with address' '

	git -c format.from="F R Om <from@example.com>" format-patch --stdout master..side |
	sed -e "/^\$/q" >patch &&
	check_patch patch &&
	grep "^From: F R Om <from@example.com>\$" patch
'

test_expect_success '--no-from overrides format.from' '

	git -c format.from="F R Om <from@example.com>" format-patch --no-from --stdout master..side |
	sed -e "/^\$/q" >patch &&
	check_patch patch &&
	! grep "^From: F R Om <from@example.com>\$" patch
'

test_expect_success '--from overrides format.from' '

	git -c format.from="F R Om <from@example.com>" format-patch --from --stdout master..side |
	sed -e "/^\$/q" >patch &&
	check_patch patch &&
	! grep "^From: F R Om <from@example.com>\$" patch
'

test_expect_success '--no-to overrides config.to' '

	git config --replace-all format.to \
		"R E Cipient <rcipient@example.com>" &&
	git format-patch --no-to --stdout master..side |
	sed -e "/^\$/q" >patch10 &&
	check_patch patch10 &&
	! grep "^To: R E Cipient <rcipient@example.com>\$" patch10
'

test_expect_success '--no-to and --to replaces config.to' '

	git config --replace-all format.to \
		"Someone <someone@out.there>" &&
	git format-patch --no-to --to="Someone Else <else@out.there>" \
		--stdout master..side |
	sed -e "/^\$/q" >patch11 &&
	check_patch patch11 &&
	! grep "^To: Someone <someone@out.there>\$" patch11 &&
	grep "^To: Someone Else <else@out.there>\$" patch11
'

test_expect_success '--no-cc overrides config.cc' '

	git config --replace-all format.cc \
		"C E Cipient <rcipient@example.com>" &&
	git format-patch --no-cc --stdout master..side |
	sed -e "/^\$/q" >patch12 &&
	check_patch patch12 &&
	! grep "^Cc: C E Cipient <rcipient@example.com>\$" patch12
'

test_expect_success '--no-add-header overrides config.headers' '

	git config --replace-all format.headers \
		"Header1: B E Cipient <rcipient@example.com>" &&
	git format-patch --no-add-header --stdout master..side |
	sed -e "/^\$/q" >patch13 &&
	check_patch patch13 &&
	! grep "^Header1: B E Cipient <rcipient@example.com>\$" patch13
'

test_expect_success 'multiple files' '

	rm -rf patches/ &&
	git checkout side &&
	git format-patch -o patches/ master &&
	ls patches/0001-Side-changes-1.patch patches/0002-Side-changes-2.patch patches/0003-Side-changes-3-with-n-backslash-n-in-it.patch
'

test_expect_success 'reroll count' '
	rm -fr patches &&
	git format-patch -o patches --cover-letter --reroll-count 4 master..side >list &&
	! grep -v "^patches/v4-000[0-3]-" list &&
	sed -n -e "/^Subject: /p" $(cat list) >subjects &&
	! grep -v "^Subject: \[PATCH v4 [0-3]/3\] " subjects
'

test_expect_success 'reroll count (-v)' '
	rm -fr patches &&
	git format-patch -o patches --cover-letter -v4 master..side >list &&
	! grep -v "^patches/v4-000[0-3]-" list &&
	sed -n -e "/^Subject: /p" $(cat list) >subjects &&
	! grep -v "^Subject: \[PATCH v4 [0-3]/3\] " subjects
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
	test_config format.thread true &&
	check_threading expect.thread master
'

test_expect_success 'thread deep via config' '
	test_config format.thread deep &&
	check_threading expect.deep master
'

test_expect_success 'thread config + override' '
	test_config format.thread deep &&
	check_threading expect.thread --thread master
'

test_expect_success 'thread config + --no-thread' '
	test_config format.thread deep &&
	check_threading expect.no-threading --no-thread master
'

test_expect_success 'excessive subject' '

	rm -rf patches/ &&
	git checkout side &&
	before=$(git hash-object file) &&
	before=$(git rev-parse --short $before) &&
	for i in 5 6 1 2 3 A 4 B C 7 8 9 10 D E F; do echo "$i"; done >>file &&
	after=$(git hash-object file) &&
	after=$(git rev-parse --short $after) &&
	git update-index file &&
	git commit -m "This is an excessively long subject line for a message due to the habit some projects have of not having a short, one-line subject at the start of the commit message, but rather sticking a whole paragraph right at the start as the only thing in the commit message. It had better not become the filename for the patch." &&
	git format-patch -o patches/ master..side &&
	ls patches/0004-This-is-an-excessively-long-subject-line-for-a-messa.patch
'

test_expect_success 'failure to write cover-letter aborts gracefully' '
	test_when_finished "rmdir 0000-cover-letter.patch" &&
	mkdir 0000-cover-letter.patch &&
	test_must_fail git format-patch --no-renames --cover-letter -1
'

test_expect_success 'cover-letter inherits diff options' '
	git mv file foo &&
	git commit -m foo &&
	git format-patch --no-renames --cover-letter -1 &&
	check_patch 0000-cover-letter.patch &&
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
index $before..$after 100644
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
	sed -e "1,/^diff/d" -e "/^+5/q" \
		<0001-This-is-an-excessively-long-subject-line-for-a-messa.patch \
		>output &&
	test_cmp expect output

'

cat > expect << EOF

diff --git a/file b/file
index $before..$after 100644
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
	git format-patch -1 --signoff --stdout >out &&
	grep "^Signed-off-by: $GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL>" out
'

test_expect_success 'format-patch --notes --signoff' '
	git notes --ref test add -m "test message" HEAD &&
	git format-patch -1 --signoff --stdout --notes=test >out &&
	# Three dashes must come after S-o-b
	! sed "/^Signed-off-by: /q" out | grep "test message" &&
	sed "1,/^Signed-off-by: /d" out | grep "test message" &&
	# Notes message must come after three dashes
	! sed "/^---$/q" out | grep "test message" &&
	sed "1,/^---$/d" out | grep "test message"
'

test_expect_success 'format-patch notes output control' '
	git notes add -m "notes config message" HEAD &&
	test_when_finished git notes remove HEAD &&

	git format-patch -1 --stdout >out &&
	! grep "notes config message" out &&
	git format-patch -1 --stdout --notes >out &&
	grep "notes config message" out &&
	git format-patch -1 --stdout --no-notes >out &&
	! grep "notes config message" out &&
	git format-patch -1 --stdout --notes --no-notes >out &&
	! grep "notes config message" out &&
	git format-patch -1 --stdout --no-notes --notes >out &&
	grep "notes config message" out &&

	test_config format.notes true &&
	git format-patch -1 --stdout >out &&
	grep "notes config message" out &&
	git format-patch -1 --stdout --notes >out &&
	grep "notes config message" out &&
	git format-patch -1 --stdout --no-notes >out &&
	! grep "notes config message" out &&
	git format-patch -1 --stdout --notes --no-notes >out &&
	! grep "notes config message" out &&
	git format-patch -1 --stdout --no-notes --notes >out &&
	grep "notes config message" out
'

test_expect_success 'format-patch with multiple notes refs' '
	git notes --ref note1 add -m "this is note 1" HEAD &&
	test_when_finished git notes --ref note1 remove HEAD &&
	git notes --ref note2 add -m "this is note 2" HEAD &&
	test_when_finished git notes --ref note2 remove HEAD &&

	git format-patch -1 --stdout >out &&
	! grep "this is note 1" out &&
	! grep "this is note 2" out &&
	git format-patch -1 --stdout --notes=note1 >out &&
	grep "this is note 1" out &&
	! grep "this is note 2" out &&
	git format-patch -1 --stdout --notes=note2 >out &&
	! grep "this is note 1" out &&
	grep "this is note 2" out &&
	git format-patch -1 --stdout --notes=note1 --notes=note2 >out &&
	grep "this is note 1" out &&
	grep "this is note 2" out &&

	test_config format.notes note1 &&
	git format-patch -1 --stdout >out &&
	grep "this is note 1" out &&
	! grep "this is note 2" out &&
	git format-patch -1 --stdout --no-notes >out &&
	! grep "this is note 1" out &&
	! grep "this is note 2" out &&
	git format-patch -1 --stdout --notes=note2 >out &&
	grep "this is note 1" out &&
	grep "this is note 2" out &&
	git format-patch -1 --stdout --no-notes --notes=note2 >out &&
	! grep "this is note 1" out &&
	grep "this is note 2" out &&

	git config --add format.notes note2 &&
	git format-patch -1 --stdout >out &&
	grep "this is note 1" out &&
	grep "this is note 2" out &&
	git format-patch -1 --stdout --no-notes >out &&
	! grep "this is note 1" out &&
	! grep "this is note 2" out
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
	test 5 = $(grep "^diff --git a/" output | wc -l)'

test_expect_success 'format-patch -- <path>' '
	git format-patch master..side -- file 2>error &&
	! grep "Use .--" error
'

test_expect_success 'format-patch --ignore-if-in-upstream HEAD' '
	git format-patch --ignore-if-in-upstream HEAD
'

git_version="$(git --version | sed "s/.* //")"

signature() {
	printf "%s\n%s\n\n" "-- " "${1:-$git_version}"
}

test_expect_success 'format-patch default signature' '
	git format-patch --stdout -1 | tail -n 3 >output &&
	signature >expect &&
	test_cmp expect output
'

test_expect_success 'format-patch --signature' '
	git format-patch --stdout --signature="my sig" -1 | tail -n 3 >output &&
	signature "my sig" >expect &&
	test_cmp expect output
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
	check_patch output &&
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

test_expect_success 'format.signature="" suppresses signatures' '
	git config format.signature "" &&
	git format-patch --stdout -1 >output &&
	check_patch output &&
	! grep "^-- \$" output
'

test_expect_success 'format-patch --no-signature suppresses signatures' '
	git config --unset-all format.signature &&
	git format-patch --stdout --no-signature -1 >output &&
	check_patch output &&
	! grep "^-- \$" output
'

test_expect_success 'format-patch --signature="" suppresses signatures' '
	git format-patch --stdout --signature="" -1 >output &&
	check_patch output &&
	! grep "^-- \$" output
'

test_expect_success 'prepare mail-signature input' '
	cat >mail-signature <<-\EOF

	Test User <test.email@kernel.org>
	http://git.kernel.org/cgit/git/git.git

	git.kernel.org/?p=git/git.git;a=summary

	EOF
'

test_expect_success '--signature-file=file works' '
	git format-patch --stdout --signature-file=mail-signature -1 >output &&
	check_patch output &&
	sed -e "1,/^-- \$/d" <output >actual &&
	{
		cat mail-signature && echo
	} >expect &&
	test_cmp expect actual
'

test_expect_success 'format.signaturefile works' '
	test_config format.signaturefile mail-signature &&
	git format-patch --stdout -1 >output &&
	check_patch output &&
	sed -e "1,/^-- \$/d" <output >actual &&
	{
		cat mail-signature && echo
	} >expect &&
	test_cmp expect actual
'

test_expect_success '--no-signature suppresses format.signaturefile ' '
	test_config format.signaturefile mail-signature &&
	git format-patch --stdout --no-signature -1 >output &&
	check_patch output &&
	! grep "^-- \$" output
'

test_expect_success '--signature-file overrides format.signaturefile' '
	cat >other-mail-signature <<-\EOF &&
	Use this other signature instead of mail-signature.
	EOF
	test_config format.signaturefile mail-signature &&
	git format-patch --stdout \
			--signature-file=other-mail-signature -1 >output &&
	check_patch output &&
	sed -e "1,/^-- \$/d" <output >actual &&
	{
		cat other-mail-signature && echo
	} >expect &&
	test_cmp expect actual
'

test_expect_success '--signature overrides format.signaturefile' '
	test_config format.signaturefile mail-signature &&
	git format-patch --stdout --signature="my sig" -1 >output &&
	check_patch output &&
	grep "my sig" output
'

test_expect_success TTY 'format-patch --stdout paginates' '
	rm -f pager_used &&
	test_terminal env GIT_PAGER="wc >pager_used" git format-patch --stdout --all &&
	test_path_is_file pager_used
'

 test_expect_success TTY 'format-patch --stdout pagination can be disabled' '
	rm -f pager_used &&
	test_terminal env GIT_PAGER="wc >pager_used" git --no-pager format-patch --stdout --all &&
	test_terminal env GIT_PAGER="wc >pager_used" git -c "pager.format-patch=false" format-patch --stdout --all &&
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
 bar foo bar foo bar foo bar foo bar foo bar foo bar foo bar foo bar foo bar
 foo bar foo bar foo bar foo bar foo bar foo bar foo bar foo bar foo bar foo
 bar foo bar foo bar foo bar foo bar foo bar foo bar foo bar foo bar foo bar
 foo bar foo bar foo bar foo bar foo bar foo bar foo bar foo bar foo bar foo
 bar foo bar foo bar foo bar foo bar foo bar foo bar foo bar foo bar foo bar
 foo bar foo bar foo bar foo bar foo bar foo bar foo bar foo bar foo bar
EOF
test_expect_success 'format-patch wraps extremely long subject (ascii)' '
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
Subject: [PATCH] =?UTF-8?q?f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f?=
 =?UTF-8?q?=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar?=
 =?UTF-8?q?=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20?=
 =?UTF-8?q?bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6?=
 =?UTF-8?q?=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f?=
 =?UTF-8?q?=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar?=
 =?UTF-8?q?=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20?=
 =?UTF-8?q?bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6?=
 =?UTF-8?q?=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f?=
 =?UTF-8?q?=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar?=
 =?UTF-8?q?=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20?=
 =?UTF-8?q?bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6?=
 =?UTF-8?q?=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f?=
 =?UTF-8?q?=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar?=
 =?UTF-8?q?=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20?=
 =?UTF-8?q?bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6?=
 =?UTF-8?q?=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6?=
 =?UTF-8?q?=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f?=
 =?UTF-8?q?=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar?=
 =?UTF-8?q?=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20bar=20f=C3=B6=C3=B6=20?=
 =?UTF-8?q?bar?=
EOF
test_expect_success 'format-patch wraps extremely long subject (rfc2047)' '
	rm -rf patches/ &&
	echo content >>file &&
	git add file &&
	git commit -m "$M512" &&
	git format-patch --stdout -1 >patch &&
	sed -n "/^Subject/p; /^ /p; /^$/q" <patch >subject &&
	test_cmp expect subject
'

check_author() {
	echo content >>file &&
	git add file &&
	GIT_AUTHOR_NAME=$1 git commit -m author-check &&
	git format-patch --stdout -1 >patch &&
	sed -n "/^From: /p; /^ /p; /^$/q" <patch >actual &&
	test_cmp expect actual
}

cat >expect <<'EOF'
From: "Foo B. Bar" <author@example.com>
EOF
test_expect_success 'format-patch quotes dot in from-headers' '
	check_author "Foo B. Bar"
'

cat >expect <<'EOF'
From: "Foo \"The Baz\" Bar" <author@example.com>
EOF
test_expect_success 'format-patch quotes double-quote in from-headers' '
	check_author "Foo \"The Baz\" Bar"
'

cat >expect <<'EOF'
From: =?UTF-8?q?F=C3=B6o=20Bar?= <author@example.com>
EOF
test_expect_success 'format-patch uses rfc2047-encoded from-headers when necessary' '
	check_author "Föo Bar"
'

cat >expect <<'EOF'
From: =?UTF-8?q?F=C3=B6o=20B=2E=20Bar?= <author@example.com>
EOF
test_expect_success 'rfc2047-encoded from-headers leave no rfc822 specials' '
	check_author "Föo B. Bar"
'

cat >expect <<EOF
From: foo_bar_foo_bar_foo_bar_foo_bar_foo_bar_foo_bar_foo_bar_foo_bar_
 <author@example.com>
EOF
test_expect_success 'format-patch wraps moderately long from-header (ascii)' '
	check_author "foo_bar_foo_bar_foo_bar_foo_bar_foo_bar_foo_bar_foo_bar_foo_bar_"
'

cat >expect <<'EOF'
From: Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar
 Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo
 Bar Foo Bar Foo Bar Foo Bar <author@example.com>
EOF
test_expect_success 'format-patch wraps extremely long from-header (ascii)' '
	check_author "Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar"
'

cat >expect <<'EOF'
From: "Foo.Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar
 Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo
 Bar Foo Bar Foo Bar Foo Bar" <author@example.com>
EOF
test_expect_success 'format-patch wraps extremely long from-header (rfc822)' '
	check_author "Foo.Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar"
'

cat >expect <<'EOF'
From: =?UTF-8?q?Fo=C3=B6=20Bar=20Foo=20Bar=20Foo=20Bar=20Foo=20Bar=20Foo?=
 =?UTF-8?q?=20Bar=20Foo=20Bar=20Foo=20Bar=20Foo=20Bar=20Foo=20Bar=20Foo=20?=
 =?UTF-8?q?Bar=20Foo=20Bar=20Foo=20Bar=20Foo=20Bar=20Foo=20Bar=20Foo=20Bar?=
 =?UTF-8?q?=20Foo=20Bar=20Foo=20Bar=20Foo=20Bar=20Foo=20Bar=20Foo=20Bar=20?=
 =?UTF-8?q?Foo=20Bar=20Foo=20Bar?= <author@example.com>
EOF
test_expect_success 'format-patch wraps extremely long from-header (rfc2047)' '
	check_author "Foö Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar Foo Bar"
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

test_expect_success '--rfc' '
	cat >expect <<-\EOF &&
	Subject: [RFC PATCH 1/1] header with . in it
	EOF
	git format-patch -n -1 --stdout --rfc >patch &&
	grep ^Subject: patch >actual &&
	test_cmp expect actual
'

test_expect_success '--from=ident notices bogus ident' '
	test_must_fail git format-patch -1 --stdout --from=foo >patch
'

test_expect_success '--from=ident replaces author' '
	git format-patch -1 --stdout --from="Me <me@example.com>" >patch &&
	cat >expect <<-\EOF &&
	From: Me <me@example.com>

	From: A U Thor <author@example.com>

	EOF
	sed -ne "/^From:/p; /^$/p; /^---$/q" <patch >patch.head &&
	test_cmp expect patch.head
'

test_expect_success '--from uses committer ident' '
	git format-patch -1 --stdout --from >patch &&
	cat >expect <<-\EOF &&
	From: C O Mitter <committer@example.com>

	From: A U Thor <author@example.com>

	EOF
	sed -ne "/^From:/p; /^$/p; /^---$/q" <patch >patch.head &&
	test_cmp expect patch.head
'

test_expect_success '--from omits redundant in-body header' '
	git format-patch -1 --stdout --from="A U Thor <author@example.com>" >patch &&
	cat >expect <<-\EOF &&
	From: A U Thor <author@example.com>

	EOF
	sed -ne "/^From:/p; /^$/p; /^---$/q" <patch >patch.head &&
	test_cmp expect patch.head
'

test_expect_success 'in-body headers trigger content encoding' '
	test_env GIT_AUTHOR_NAME="éxötìc" test_commit exotic &&
	test_when_finished "git reset --hard HEAD^" &&
	git format-patch -1 --stdout --from >patch &&
	cat >expect <<-\EOF &&
	From: C O Mitter <committer@example.com>
	Content-Type: text/plain; charset=UTF-8

	From: éxötìc <author@example.com>

	EOF
	sed -ne "/^From:/p; /^$/p; /^Content-Type/p; /^---$/q" <patch >patch.head &&
	test_cmp expect patch.head
'

append_signoff()
{
	C=$(git commit-tree HEAD^^{tree} -p HEAD) &&
	git format-patch --stdout --signoff $C^..$C >append_signoff.patch &&
	sed -n -e "1,/^---$/p" append_signoff.patch |
		egrep -n "^Subject|Sign|^$"
}

test_expect_success 'signoff: commit with no body' '
	append_signoff </dev/null >actual &&
	cat <<\EOF | sed "s/EOL$//" >expected &&
4:Subject: [PATCH] EOL
8:
9:Signed-off-by: C O Mitter <committer@example.com>
EOF
	test_cmp expected actual
'

test_expect_success 'signoff: commit with only subject' '
	echo subject | append_signoff >actual &&
	cat >expected <<\EOF &&
4:Subject: [PATCH] subject
8:
9:Signed-off-by: C O Mitter <committer@example.com>
EOF
	test_cmp expected actual
'

test_expect_success 'signoff: commit with only subject that does not end with NL' '
	printf subject | append_signoff >actual &&
	cat >expected <<\EOF &&
4:Subject: [PATCH] subject
8:
9:Signed-off-by: C O Mitter <committer@example.com>
EOF
	test_cmp expected actual
'

test_expect_success 'signoff: no existing signoffs' '
	append_signoff <<\EOF >actual &&
subject

body
EOF
	cat >expected <<\EOF &&
4:Subject: [PATCH] subject
8:
10:
11:Signed-off-by: C O Mitter <committer@example.com>
EOF
	test_cmp expected actual
'

test_expect_success 'signoff: no existing signoffs and no trailing NL' '
	printf "subject\n\nbody" | append_signoff >actual &&
	cat >expected <<\EOF &&
4:Subject: [PATCH] subject
8:
10:
11:Signed-off-by: C O Mitter <committer@example.com>
EOF
	test_cmp expected actual
'

test_expect_success 'signoff: some random signoff' '
	append_signoff <<\EOF >actual &&
subject

body

Signed-off-by: my@house
EOF
	cat >expected <<\EOF &&
4:Subject: [PATCH] subject
8:
10:
11:Signed-off-by: my@house
12:Signed-off-by: C O Mitter <committer@example.com>
EOF
	test_cmp expected actual
'

test_expect_success 'signoff: misc conforming footer elements' '
	append_signoff <<\EOF >actual &&
subject

body

Signed-off-by: my@house
(cherry picked from commit da39a3ee5e6b4b0d3255bfef95601890afd80709)
Tested-by: Some One <someone@example.com>
Bug: 1234
EOF
	cat >expected <<\EOF &&
4:Subject: [PATCH] subject
8:
10:
11:Signed-off-by: my@house
15:Signed-off-by: C O Mitter <committer@example.com>
EOF
	test_cmp expected actual
'

test_expect_success 'signoff: some random signoff-alike' '
	append_signoff <<\EOF >actual &&
subject

body
Fooled-by-me: my@house
EOF
	cat >expected <<\EOF &&
4:Subject: [PATCH] subject
8:
11:
12:Signed-off-by: C O Mitter <committer@example.com>
EOF
	test_cmp expected actual
'

test_expect_success 'signoff: not really a signoff' '
	append_signoff <<\EOF >actual &&
subject

I want to mention about Signed-off-by: here.
EOF
	cat >expected <<\EOF &&
4:Subject: [PATCH] subject
8:
9:I want to mention about Signed-off-by: here.
10:
11:Signed-off-by: C O Mitter <committer@example.com>
EOF
	test_cmp expected actual
'

test_expect_success 'signoff: not really a signoff (2)' '
	append_signoff <<\EOF >actual &&
subject

My unfortunate
Signed-off-by: example happens to be wrapped here.
EOF
	cat >expected <<\EOF &&
4:Subject: [PATCH] subject
8:
10:Signed-off-by: example happens to be wrapped here.
11:Signed-off-by: C O Mitter <committer@example.com>
EOF
	test_cmp expected actual
'

test_expect_success 'signoff: valid S-o-b paragraph in the middle' '
	append_signoff <<\EOF >actual &&
subject

Signed-off-by: my@house
Signed-off-by: your@house

A lot of houses.
EOF
	cat >expected <<\EOF &&
4:Subject: [PATCH] subject
8:
9:Signed-off-by: my@house
10:Signed-off-by: your@house
11:
13:
14:Signed-off-by: C O Mitter <committer@example.com>
EOF
	test_cmp expected actual
'

test_expect_success 'signoff: the same signoff at the end' '
	append_signoff <<\EOF >actual &&
subject

body

Signed-off-by: C O Mitter <committer@example.com>
EOF
	cat >expected <<\EOF &&
4:Subject: [PATCH] subject
8:
10:
11:Signed-off-by: C O Mitter <committer@example.com>
EOF
	test_cmp expected actual
'

test_expect_success 'signoff: the same signoff at the end, no trailing NL' '
	printf "subject\n\nSigned-off-by: C O Mitter <committer@example.com>" |
		append_signoff >actual &&
	cat >expected <<\EOF &&
4:Subject: [PATCH] subject
8:
9:Signed-off-by: C O Mitter <committer@example.com>
EOF
	test_cmp expected actual
'

test_expect_success 'signoff: the same signoff NOT at the end' '
	append_signoff <<\EOF >actual &&
subject

body

Signed-off-by: C O Mitter <committer@example.com>
Signed-off-by: my@house
EOF
	cat >expected <<\EOF &&
4:Subject: [PATCH] subject
8:
10:
11:Signed-off-by: C O Mitter <committer@example.com>
12:Signed-off-by: my@house
EOF
	test_cmp expected actual
'

test_expect_success 'signoff: tolerate garbage in conforming footer' '
	append_signoff <<\EOF >actual &&
subject

body

Tested-by: my@house
Some Trash
Signed-off-by: C O Mitter <committer@example.com>
EOF
	cat >expected <<\EOF &&
4:Subject: [PATCH] subject
8:
10:
13:Signed-off-by: C O Mitter <committer@example.com>
EOF
	test_cmp expected actual
'

test_expect_success 'signoff: respect trailer config' '
	append_signoff <<\EOF >actual &&
subject

Myfooter: x
Some Trash
EOF
	cat >expected <<\EOF &&
4:Subject: [PATCH] subject
8:
11:
12:Signed-off-by: C O Mitter <committer@example.com>
EOF
	test_cmp expected actual &&

	test_config trailer.Myfooter.ifexists add &&
	append_signoff <<\EOF >actual &&
subject

Myfooter: x
Some Trash
EOF
	cat >expected <<\EOF &&
4:Subject: [PATCH] subject
8:
11:Signed-off-by: C O Mitter <committer@example.com>
EOF
	test_cmp expected actual
'

test_expect_success 'signoff: footer begins with non-signoff without @ sign' '
	append_signoff <<\EOF >actual &&
subject

body

Reviewed-id: Noone
Tested-by: my@house
Change-id: Ideadbeef
Signed-off-by: C O Mitter <committer@example.com>
Bug: 1234
EOF
	cat >expected <<\EOF &&
4:Subject: [PATCH] subject
8:
10:
14:Signed-off-by: C O Mitter <committer@example.com>
EOF
	test_cmp expected actual
'

test_expect_success 'format patch ignores color.ui' '
	test_unconfig color.ui &&
	git format-patch --stdout -1 >expect &&
	test_config color.ui always &&
	git format-patch --stdout -1 >actual &&
	test_cmp expect actual
'

test_expect_success 'cover letter using branch description (1)' '
	git checkout rebuild-1 &&
	test_config branch.rebuild-1.description hello &&
	git format-patch --stdout --cover-letter master >actual &&
	grep hello actual >/dev/null
'

test_expect_success 'cover letter using branch description (2)' '
	git checkout rebuild-1 &&
	test_config branch.rebuild-1.description hello &&
	git format-patch --stdout --cover-letter rebuild-1~2..rebuild-1 >actual &&
	grep hello actual >/dev/null
'

test_expect_success 'cover letter using branch description (3)' '
	git checkout rebuild-1 &&
	test_config branch.rebuild-1.description hello &&
	git format-patch --stdout --cover-letter ^master rebuild-1 >actual &&
	grep hello actual >/dev/null
'

test_expect_success 'cover letter using branch description (4)' '
	git checkout rebuild-1 &&
	test_config branch.rebuild-1.description hello &&
	git format-patch --stdout --cover-letter master.. >actual &&
	grep hello actual >/dev/null
'

test_expect_success 'cover letter using branch description (5)' '
	git checkout rebuild-1 &&
	test_config branch.rebuild-1.description hello &&
	git format-patch --stdout --cover-letter -2 HEAD >actual &&
	grep hello actual >/dev/null
'

test_expect_success 'cover letter using branch description (6)' '
	git checkout rebuild-1 &&
	test_config branch.rebuild-1.description hello &&
	git format-patch --stdout --cover-letter -2 >actual &&
	grep hello actual >/dev/null
'

test_expect_success 'cover letter with nothing' '
	git format-patch --stdout --cover-letter >actual &&
	test_line_count = 0 actual
'

test_expect_success 'cover letter auto' '
	mkdir -p tmp &&
	test_when_finished "rm -rf tmp;
		git config --unset format.coverletter" &&

	git config format.coverletter auto &&
	git format-patch -o tmp -1 >list &&
	test_line_count = 1 list &&
	git format-patch -o tmp -2 >list &&
	test_line_count = 3 list
'

test_expect_success 'cover letter auto user override' '
	mkdir -p tmp &&
	test_when_finished "rm -rf tmp;
		git config --unset format.coverletter" &&

	git config format.coverletter auto &&
	git format-patch -o tmp --cover-letter -1 >list &&
	test_line_count = 2 list &&
	git format-patch -o tmp --cover-letter -2 >list &&
	test_line_count = 3 list &&
	git format-patch -o tmp --no-cover-letter -1 >list &&
	test_line_count = 1 list &&
	git format-patch -o tmp --no-cover-letter -2 >list &&
	test_line_count = 2 list
'

test_expect_success 'format-patch --zero-commit' '
	git format-patch --zero-commit --stdout v2..v1 >patch2 &&
	grep "^From " patch2 | sort | uniq >actual &&
	echo "From $ZERO_OID Mon Sep 17 00:00:00 2001" >expect &&
	test_cmp expect actual
'

test_expect_success 'From line has expected format' '
	git format-patch --stdout v2..v1 >patch2 &&
	grep "^From " patch2 >from &&
	grep "^From $OID_REGEX Mon Sep 17 00:00:00 2001$" patch2 >filtered &&
	test_cmp from filtered
'

test_expect_success 'format-patch format.outputDirectory option' '
	test_config format.outputDirectory patches &&
	rm -fr patches &&
	git format-patch master..side &&
	test $(git rev-list master..side | wc -l) -eq $(ls patches | wc -l)
'

test_expect_success 'format-patch -o overrides format.outputDirectory' '
	test_config format.outputDirectory patches &&
	rm -fr patches patchset &&
	git format-patch master..side -o patchset &&
	test_path_is_missing patches &&
	test_path_is_dir patchset
'

test_expect_success 'format-patch --base' '
	git checkout patchid &&
	git format-patch --stdout --base=HEAD~3 -1 | tail -n 7 >actual1 &&
	git format-patch --stdout --base=HEAD~3 HEAD~.. | tail -n 7 >actual2 &&
	echo >expected &&
	echo "base-commit: $(git rev-parse HEAD~3)" >>expected &&
	echo "prerequisite-patch-id: $(git show --patch HEAD~2 | git patch-id --stable | awk "{print \$1}")" >>expected &&
	echo "prerequisite-patch-id: $(git show --patch HEAD~1 | git patch-id --stable | awk "{print \$1}")" >>expected &&
	signature >> expected &&
	test_cmp expected actual1 &&
	test_cmp expected actual2 &&
	echo >fail &&
	echo "base-commit: $(git rev-parse HEAD~3)" >>fail &&
	echo "prerequisite-patch-id: $(git show --patch HEAD~2 | git patch-id --unstable | awk "{print \$1}")" >>fail &&
	echo "prerequisite-patch-id: $(git show --patch HEAD~1 | git patch-id --unstable | awk "{print \$1}")" >>fail &&
	signature >> fail &&
	! test_cmp fail actual1 &&
	! test_cmp fail actual2
'

test_expect_success 'format-patch --base errors out when base commit is in revision list' '
	test_must_fail git format-patch --base=HEAD -2 &&
	test_must_fail git format-patch --base=HEAD~1 -2 &&
	git format-patch --stdout --base=HEAD~2 -2 >patch &&
	grep "^base-commit:" patch >actual &&
	echo "base-commit: $(git rev-parse HEAD~2)" >expected &&
	test_cmp expected actual
'

test_expect_success 'format-patch --base errors out when base commit is not ancestor of revision list' '
	# For history as below:
	#
	#    ---Q---P---Z---Y---*---X
	#	 \             /
	#	  ------------W
	#
	# If "format-patch Z..X" is given, P and Z can not be specified as the base commit
	git checkout -b topic1 master &&
	git rev-parse HEAD >commit-id-base &&
	test_commit P &&
	git rev-parse HEAD >commit-id-P &&
	test_commit Z &&
	git rev-parse HEAD >commit-id-Z &&
	test_commit Y &&
	git checkout -b topic2 master &&
	test_commit W &&
	git merge topic1 &&
	test_commit X &&
	test_must_fail git format-patch --base=$(cat commit-id-P) -3 &&
	test_must_fail git format-patch --base=$(cat commit-id-Z) -3 &&
	git format-patch --stdout --base=$(cat commit-id-base) -3 >patch &&
	grep "^base-commit:" patch >actual &&
	echo "base-commit: $(cat commit-id-base)" >expected &&
	test_cmp expected actual
'

test_expect_success 'format-patch --base=auto' '
	git checkout -b upstream master &&
	git checkout -b local upstream &&
	git branch --set-upstream-to=upstream &&
	test_commit N1 &&
	test_commit N2 &&
	git format-patch --stdout --base=auto -2 >patch &&
	grep "^base-commit:" patch >actual &&
	echo "base-commit: $(git rev-parse upstream)" >expected &&
	test_cmp expected actual
'

test_expect_success 'format-patch errors out when history involves criss-cross' '
	# setup criss-cross history
	#
	#   B---M1---D
	#  / \ /
	# A   X
	#  \ / \
	#   C---M2---E
	#
	git checkout master &&
	test_commit A &&
	git checkout -b xb master &&
	test_commit B &&
	git checkout -b xc master &&
	test_commit C &&
	git checkout -b xbc xb -- &&
	git merge xc &&
	git checkout -b xcb xc -- &&
	git branch --set-upstream-to=xbc &&
	git merge xb &&
	git checkout xbc &&
	test_commit D &&
	git checkout xcb &&
	test_commit E &&
	test_must_fail 	git format-patch --base=auto -1
'

test_expect_success 'format-patch format.useAutoBaseoption' '
	test_when_finished "git config --unset format.useAutoBase" &&
	git checkout local &&
	git config format.useAutoBase true &&
	git format-patch --stdout -1 >patch &&
	grep "^base-commit:" patch >actual &&
	echo "base-commit: $(git rev-parse upstream)" >expected &&
	test_cmp expected actual
'

test_expect_success 'format-patch --base overrides format.useAutoBase' '
	test_when_finished "git config --unset format.useAutoBase" &&
	git config format.useAutoBase true &&
	git format-patch --stdout --base=HEAD~1 -1 >patch &&
	grep "^base-commit:" patch >actual &&
	echo "base-commit: $(git rev-parse HEAD~1)" >expected &&
	test_cmp expected actual
'

test_expect_success 'format-patch --base with --attach' '
	git format-patch --attach=mimemime --stdout --base=HEAD~ -1 >patch &&
	sed -n -e "/^base-commit:/s/.*/1/p" -e "/^---*mimemime--$/s/.*/2/p" \
		patch >actual &&
	test_write_lines 1 2 >expect &&
	test_cmp expect actual
'
test_expect_success 'format-patch --attach cover-letter only is non-multipart' '
	test_when_finished "rm -fr patches" &&
	git format-patch -o patches --cover-letter --attach=mimemime --base=HEAD~ -1 &&
	! egrep "^--+mimemime" patches/0000*.patch &&
	egrep "^--+mimemime$" patches/0001*.patch >output &&
	test_line_count = 2 output &&
	egrep "^--+mimemime--$" patches/0001*.patch >output &&
	test_line_count = 1 output
'

test_expect_success 'format-patch --pretty=mboxrd' '
	sp=" " &&
	cat >msg <<-INPUT_END &&
	mboxrd should escape the body

	From could trip up a loose mbox parser
	>From extra escape for reversibility
	>>From extra escape for reversibility 2
	from lower case not escaped
	Fromm bad speling not escaped
	 From with leading space not escaped

	F
	From
	From$sp
	From    $sp
	From	$sp
	INPUT_END

	cat >expect <<-INPUT_END &&
	>From could trip up a loose mbox parser
	>>From extra escape for reversibility
	>>>From extra escape for reversibility 2
	from lower case not escaped
	Fromm bad speling not escaped
	 From with leading space not escaped

	F
	From
	From
	From
	From
	INPUT_END

	C=$(git commit-tree HEAD^^{tree} -p HEAD <msg) &&
	git format-patch --pretty=mboxrd --stdout -1 $C~1..$C >patch &&
	git grep -h --no-index -A11 \
		"^>From could trip up a loose mbox parser" patch >actual &&
	test_cmp expect actual
'

test_expect_success 'interdiff: setup' '
	git checkout -b boop master &&
	test_commit fnorp blorp &&
	test_commit fleep blorp
'

test_expect_success 'interdiff: cover-letter' '
	sed "y/q/ /" >expect <<-\EOF &&
	+fleep
	--q
	EOF
	git format-patch --cover-letter --interdiff=boop~2 -1 boop &&
	test_i18ngrep "^Interdiff:$" 0000-cover-letter.patch &&
	test_i18ngrep ! "^Interdiff:$" 0001-fleep.patch &&
	sed "1,/^@@ /d; /^-- $/q" <0000-cover-letter.patch >actual &&
	test_cmp expect actual
'

test_expect_success 'interdiff: reroll-count' '
	git format-patch --cover-letter --interdiff=boop~2 -v2 -1 boop &&
	test_i18ngrep "^Interdiff ..* v1:$" v2-0000-cover-letter.patch
'

test_expect_success 'interdiff: solo-patch' '
	cat >expect <<-\EOF &&
	  +fleep

	EOF
	git format-patch --interdiff=boop~2 -1 boop &&
	test_i18ngrep "^Interdiff:$" 0001-fleep.patch &&
	sed "1,/^  @@ /d; /^$/q" <0001-fleep.patch >actual &&
	test_cmp expect actual
'

test_done
