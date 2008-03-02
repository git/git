#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='Format-patch skipping already incorporated patches'

. ./test-lib.sh

test_expect_success setup '

	for i in 1 2 3 4 5 6 7 8 9 10; do echo "$i"; done >file &&
	cat file >elif &&
	git add file elif &&
	git commit -m Initial &&
	git checkout -b side &&

	for i in 1 2 5 6 A B C 7 8 9 10; do echo "$i"; done >file &&
	chmod +x elif &&
	git update-index file elif &&
	git update-index --chmod=+x elif &&
	git commit -m "Side changes #1" &&

	for i in D E F; do echo "$i"; done >>file &&
	git update-index file &&
	git commit -m "Side changes #2" &&
	git tag C2 &&

	for i in 5 6 1 2 3 A 4 B C 7 8 9 10 D E F; do echo "$i"; done >file &&
	git update-index file &&
	git commit -m "Side changes #3 with \\n backslash-n in it." &&

	git checkout master &&
	git diff-tree -p C2 | git apply --index &&
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
	sed -e "/^$/q" patch2 > hdrs2 &&
	grep "^To: R. E. Cipient <rcipient@example.com>$" hdrs2 &&
	grep "^Cc: S. E. Cipient <scipient@example.com>$" hdrs2
	
'

test_expect_success 'extra headers without newlines' '

	git config --replace-all format.headers "To: R. E. Cipient <rcipient@example.com>" &&
	git config --add format.headers "Cc: S. E. Cipient <scipient@example.com>" &&
	git format-patch --stdout master..side >patch3 &&
	sed -e "/^$/q" patch3 > hdrs3 &&
	grep "^To: R. E. Cipient <rcipient@example.com>$" hdrs3 &&
	grep "^Cc: S. E. Cipient <scipient@example.com>$" hdrs3
	
'

test_expect_success 'extra headers with multiple To:s' '

	git config --replace-all format.headers "To: R. E. Cipient <rcipient@example.com>" &&
	git config --add format.headers "To: S. E. Cipient <scipient@example.com>" &&
	git format-patch --stdout master..side > patch4 &&
	sed -e "/^$/q" patch4 > hdrs4 &&
	grep "^To: R. E. Cipient <rcipient@example.com>,$" hdrs4 &&
	grep "^ *S. E. Cipient <scipient@example.com>$" hdrs4
'

test_expect_success 'additional command line cc' '

	git config --replace-all format.headers "Cc: R. E. Cipient <rcipient@example.com>" &&
	git format-patch --cc="S. E. Cipient <scipient@example.com>" --stdout master..side | sed -e "/^$/q" >patch5 &&
	grep "^Cc: R. E. Cipient <rcipient@example.com>,$" patch5 &&
	grep "^ *S. E. Cipient <scipient@example.com>$" patch5
'

test_expect_success 'multiple files' '

	rm -rf patches/ &&
	git checkout side &&
	git format-patch -o patches/ master &&
	ls patches/0001-Side-changes-1.patch patches/0002-Side-changes-2.patch patches/0003-Side-changes-3-with-n-backslash-n-in-it.patch
'

test_expect_success 'thread' '

	rm -rf patches/ &&
	git checkout side &&
	git format-patch --thread -o patches/ master &&
	FIRST_MID=$(grep "Message-Id:" patches/0001-* | sed "s/^[^<]*\(<[^>]*>\).*$/\1/") &&
	for i in patches/0002-* patches/0003-*
	do
	  grep "References: $FIRST_MID" $i &&
	  grep "In-Reply-To: $FIRST_MID" $i || break
	done
'

test_expect_success 'thread in-reply-to' '

	rm -rf patches/ &&
	git checkout side &&
	git format-patch --in-reply-to="<test.message>" --thread -o patches/ master &&
	FIRST_MID="<test.message>" &&
	for i in patches/*
	do
	  grep "References: $FIRST_MID" $i &&
	  grep "In-Reply-To: $FIRST_MID" $i || break
	done
'

test_expect_success 'thread cover-letter' '

	rm -rf patches/ &&
	git checkout side &&
	git format-patch --cover-letter --thread -o patches/ master &&
	FIRST_MID=$(grep "Message-Id:" patches/0000-* | sed "s/^[^<]*\(<[^>]*>\).*$/\1/") &&
	for i in patches/0001-* patches/0002-* patches/0003-* 
	do
	  grep "References: $FIRST_MID" $i &&
	  grep "In-Reply-To: $FIRST_MID" $i || break
	done
'

test_expect_success 'thread cover-letter in-reply-to' '

	rm -rf patches/ &&
	git checkout side &&
	git format-patch --cover-letter --in-reply-to="<test.message>" --thread -o patches/ master &&
	FIRST_MID="<test.message>" &&
	for i in patches/*
	do
	  grep "References: $FIRST_MID" $i &&
	  grep "In-Reply-To: $FIRST_MID" $i || break
	done
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
	! grep "file => foo .* 0 *$" 0000-cover-letter.patch &&
	git format-patch --cover-letter -1 -M &&
	grep "file => foo .* 0 *$" 0000-cover-letter.patch

'
test_done
