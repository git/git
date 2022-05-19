#!/bin/sh

test_description='git log with invalid commit headers'

. ./test-lib.sh

test_expect_success 'setup' '
	test_cummit foo &&

	git cat-file commit HEAD |
	sed "/^author /s/>/>-<>/" >broken_email.cummit &&
	git hash-object -w -t cummit broken_email.cummit >broken_email.hash &&
	git update-ref refs/heads/broken_email $(cat broken_email.hash)
'

test_expect_success 'fsck notices broken cummit' '
	test_must_fail git fsck 2>actual &&
	test_i18ngrep invalid.author actual
'

test_expect_success 'git log with broken author email' '
	{
		echo cummit $(cat broken_email.hash) &&
		echo "Author: A U Thor <author@example.com>" &&
		echo "Date:   Thu Apr 7 15:13:13 2005 -0700" &&
		echo &&
		echo "    foo"
	} >expect.out &&

	git log broken_email >actual.out 2>actual.err &&

	test_cmp expect.out actual.out &&
	test_must_be_empty actual.err
'

test_expect_success 'git log --format with broken author email' '
	echo "A U Thor+author@example.com+Thu Apr 7 15:13:13 2005 -0700" >expect.out &&

	git log --format="%an+%ae+%ad" broken_email >actual.out 2>actual.err &&

	test_cmp expect.out actual.out &&
	test_must_be_empty actual.err
'

munge_author_date () {
	git cat-file cummit "$1" >cummit.orig &&
	sed "s/^\(author .*>\) [0-9]*/\1 $2/" <cummit.orig >cummit.munge &&
	git hash-object -w -t cummit cummit.munge
}

test_expect_success 'unparsable dates produce sentinel value' '
	cummit=$(munge_author_date HEAD totally_bogus) &&
	echo "Date:   Thu Jan 1 00:00:00 1970 +0000" >expect &&
	git log -1 $cummit >actual.full &&
	grep Date <actual.full >actual &&
	test_cmp expect actual
'

test_expect_success 'unparsable dates produce sentinel value (%ad)' '
	cummit=$(munge_author_date HEAD totally_bogus) &&
	echo >expect &&
	git log -1 --format=%ad $cummit >actual &&
	test_cmp expect actual
'

# date is 2^64 + 1
test_expect_success 'date parser recognizes integer overflow' '
	cummit=$(munge_author_date HEAD 18446744073709551617) &&
	echo "Thu Jan 1 00:00:00 1970 +0000" >expect &&
	git log -1 --format=%ad $cummit >actual &&
	test_cmp expect actual
'

# date is 2^64 - 2
test_expect_success 'date parser recognizes time_t overflow' '
	cummit=$(munge_author_date HEAD 18446744073709551614) &&
	echo "Thu Jan 1 00:00:00 1970 +0000" >expect &&
	git log -1 --format=%ad $cummit >actual &&
	test_cmp expect actual
'

# date is within 2^63-1, but enough to choke glibc's gmtime
test_expect_success 'absurdly far-in-future date' '
	cummit=$(munge_author_date HEAD 999999999999999999) &&
	git log -1 --format=%ad $cummit
'

test_done
