#!/bin/sh

test_description='git log with invalid commit headers'

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit foo &&

	git cat-file commit HEAD |
	sed "/^author /s/>/>-<>/" >broken_email.commit &&
	git hash-object -w -t commit broken_email.commit >broken_email.hash &&
	git update-ref refs/heads/broken_email $(cat broken_email.hash)
'

test_expect_success 'fsck notices broken commit' '
	git fsck 2>actual &&
	test_i18ngrep invalid.author actual
'

test_expect_success 'git log with broken author email' '
	{
		echo commit $(cat broken_email.hash)
		echo "Author: A U Thor <author@example.com>"
		echo "Date:   Thu Apr 7 15:13:13 2005 -0700"
		echo
		echo "    foo"
	} >expect.out &&
	: >expect.err &&

	git log broken_email >actual.out 2>actual.err &&

	test_cmp expect.out actual.out &&
	test_cmp expect.err actual.err
'

test_expect_success 'git log --format with broken author email' '
	echo "A U Thor+author@example.com+Thu Apr 7 15:13:13 2005 -0700" >expect.out &&
	: >expect.err &&

	git log --format="%an+%ae+%ad" broken_email >actual.out 2>actual.err &&

	test_cmp expect.out actual.out &&
	test_cmp expect.err actual.err
'

test_done
