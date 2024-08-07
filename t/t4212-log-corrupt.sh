#!/bin/sh

test_description='git log with invalid commit headers'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' '
	test_commit foo &&

	git cat-file commit HEAD >ok.commit &&
	sed "s/>/>-<>/" <ok.commit >broken_email.commit &&

	git hash-object --literally -w -t commit broken_email.commit >broken_email.hash &&
	git update-ref refs/heads/broken_email $(cat broken_email.hash)
'

test_expect_success 'fsck notices broken commit' '
	test_must_fail git fsck 2>actual &&
	test_grep invalid.author actual
'

test_expect_success 'git log with broken author email' '
	{
		echo commit $(cat broken_email.hash) &&
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

test_expect_success '--until handles broken email' '
	git rev-list --until=1980-01-01 broken_email >actual &&
	test_must_be_empty actual
'

munge_author_date () {
	git cat-file commit "$1" >commit.orig &&
	sed "s/^\(author .*>\) [0-9]*/\1 $2/" <commit.orig >commit.munge &&
	git hash-object --literally -w -t commit commit.munge
}

test_expect_success 'unparsable dates produce sentinel value' '
	commit=$(munge_author_date HEAD totally_bogus) &&
	echo "Date:   Thu Jan 1 00:00:00 1970 +0000" >expect &&
	git log -1 $commit >actual.full &&
	grep Date <actual.full >actual &&
	test_cmp expect actual
'

test_expect_success 'unparsable dates produce sentinel value (%ad)' '
	commit=$(munge_author_date HEAD totally_bogus) &&
	echo >expect &&
	git log -1 --format=%ad $commit >actual &&
	test_cmp expect actual
'

# date is 2^64 + 1
test_expect_success 'date parser recognizes integer overflow' '
	commit=$(munge_author_date HEAD 18446744073709551617) &&
	echo "Thu Jan 1 00:00:00 1970 +0000" >expect &&
	git log -1 --format=%ad $commit >actual &&
	test_cmp expect actual
'

# date is 2^64 - 2
test_expect_success 'date parser recognizes time_t overflow' '
	commit=$(munge_author_date HEAD 18446744073709551614) &&
	echo "Thu Jan 1 00:00:00 1970 +0000" >expect &&
	git log -1 --format=%ad $commit >actual &&
	test_cmp expect actual
'

# date is within 2^63-1, but enough to choke glibc's gmtime
test_expect_success 'absurdly far-in-future date' '
	commit=$(munge_author_date HEAD 999999999999999999) &&
	git log -1 --format=%ad $commit
'

test_expect_success 'create commits with whitespace committer dates' '
	# It is important that this subject line is numeric, since we want to
	# be sure we are not confused by skipping whitespace and accidentally
	# parsing the subject as a timestamp.
	#
	# Do not use munge_author_date here. Besides not hitting the committer
	# line, it leaves the timezone intact, and we want nothing but
	# whitespace.
	#
	# We will make two munged commits here. The first, ws_commit, will
	# be purely spaces. The second contains a vertical tab, which is
	# considered a space by strtoumax(), but not by our isspace().
	test_commit 1234567890 &&
	git cat-file commit HEAD >commit.orig &&
	sed "s/>.*/>    /" <commit.orig >commit.munge &&
	ws_commit=$(git hash-object --literally -w -t commit commit.munge) &&
	sed "s/>.*/>   $(printf "\013")/" <commit.orig >commit.munge &&
	vt_commit=$(git hash-object --literally -w -t commit commit.munge)
'

test_expect_success '--until treats whitespace date as sentinel' '
	echo $ws_commit >expect &&
	git rev-list --until=1980-01-01 $ws_commit >actual &&
	test_cmp expect actual &&

	echo $vt_commit >expect &&
	git rev-list --until=1980-01-01 $vt_commit >actual &&
	test_cmp expect actual
'

test_expect_success 'pretty-printer handles whitespace date' '
	# as with the %ad test above, we will show these as the empty string,
	# not the 1970 epoch date. This is intentional; see 7d9a281941 (t4212:
	# test bogus timestamps with git-log, 2014-02-24) for more discussion.
	echo : >expect &&
	git log -1 --format="%at:%ct" $ws_commit >actual &&
	test_cmp expect actual &&
	git log -1 --format="%at:%ct" $vt_commit >actual &&
	test_cmp expect actual
'

test_done
