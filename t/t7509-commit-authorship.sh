#!/bin/sh
#
# Copyright (c) 2009 Erick Mattos
#

test_description='commit tests of various authorhip options. '

. ./test-lib.sh

author_header () {
	git cat-file commit "$1" |
	sed -n -e '/^$/q' -e '/^author /p'
}

message_body () {
	git cat-file commit "$1" |
	sed -e '1,/^$/d'
}

test_expect_success '-C option copies authorship and message' '
	test_commit --author Frigate\ \<flying@over.world\> \
		"Initial Commit" foo Initial Initial &&
	echo "Test 1" >>foo &&
	test_tick &&
	git commit -a -C Initial &&
	author_header Initial >expect &&
	author_header HEAD >actual &&
	test_cmp expect actual &&

	message_body Initial >expect &&
	message_body HEAD >actual &&
	test_cmp expect actual
'

test_expect_success '-C option copies only the message with --reset-author' '
	echo "Test 2" >>foo &&
	test_tick &&
	git commit -a -C Initial --reset-author &&
	echo "author $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> $GIT_AUTHOR_DATE" >expect &&
	author_header HEAD >actual &&
	test_cmp expect actual &&

	message_body Initial >expect &&
	message_body HEAD >actual &&
	test_cmp expect actual
'

test_expect_success '-c option copies authorship and message' '
	echo "Test 3" >>foo &&
	test_tick &&
	EDITOR=: VISUAL=: git commit -a -c Initial &&
	author_header Initial >expect &&
	author_header HEAD >actual &&
	test_cmp expect actual
'

test_expect_success '-c option copies only the message with --reset-author' '
	echo "Test 4" >>foo &&
	test_tick &&
	EDITOR=: VISUAL=: git commit -a -c Initial --reset-author &&
	echo "author $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> $GIT_AUTHOR_DATE" >expect &&
	author_header HEAD >actual &&
	test_cmp expect actual &&

	message_body Initial >expect &&
	message_body HEAD >actual &&
	test_cmp expect actual
'

test_expect_success '--amend option copies authorship' '
	git checkout Initial &&
	echo "Test 5" >>foo &&
	test_tick &&
	git commit -a --amend -m "amend test" &&
	author_header Initial >expect &&
	author_header HEAD >actual &&
	test_cmp expect actual &&

	echo "amend test" >expect &&
	message_body HEAD >actual &&
	test_cmp expect actual
'

sha1_file() {
	echo "$*" | sed "s#..#.git/objects/&/#"
}
remove_object() {
	rm -f $(sha1_file "$*")
}

test_expect_success '--amend option with empty author' '
	git cat-file commit Initial >tmp &&
	sed "s/author [^<]* </author  </" tmp >empty-author &&
	sha=$(git hash-object -t commit -w empty-author) &&
	test_when_finished "remove_object $sha" &&
	git checkout $sha &&
	test_when_finished "git checkout Initial" &&
	echo "Empty author test" >>foo &&
	test_tick &&
	test_must_fail git commit -a -m "empty author" --amend 2>err &&
	test_grep "empty ident" err
'

test_expect_success '--amend option with missing author' '
	git cat-file commit Initial >tmp &&
	sed "s/author [^<]* </author </" tmp >malformed &&
	sha=$(git hash-object --literally -t commit -w malformed) &&
	test_when_finished "remove_object $sha" &&
	git checkout $sha &&
	test_when_finished "git checkout Initial" &&
	echo "Missing author test" >>foo &&
	test_tick &&
	test_must_fail git commit -a -m "malformed author" --amend 2>err &&
	test_grep "empty ident" err
'

test_expect_success '--reset-author makes the commit ours even with --amend option' '
	git checkout Initial &&
	echo "Test 6" >>foo &&
	test_tick &&
	git commit -a --reset-author -m "Changed again" --amend &&
	echo "author $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> $GIT_AUTHOR_DATE" >expect &&
	author_header HEAD >actual &&
	test_cmp expect actual &&

	echo "Changed again" >expect &&
	message_body HEAD >actual &&
	test_cmp expect actual
'

test_expect_success '--reset-author and --author are mutually exclusive' '
	git checkout Initial &&
	echo "Test 7" >>foo &&
	test_tick &&
	test_must_fail git commit -a --reset-author --author="Xyzzy <frotz@nitfol.xz>"
'

test_expect_success '--reset-author should be rejected without -c/-C/--amend' '
	git checkout Initial &&
	echo "Test 7" >>foo &&
	test_tick &&
	test_must_fail git commit -a --reset-author -m done
'

test_expect_success 'commit respects CHERRY_PICK_HEAD and MERGE_MSG' '
	echo "cherry-pick 1a" >>foo &&
	test_tick &&
	git commit -am "cherry-pick 1" --author="Cherry <cherry@pick.er>" &&
	git tag cherry-pick-head &&
	git update-ref CHERRY_PICK_HEAD $(git rev-parse cherry-pick-head) &&
	echo "This is a MERGE_MSG" >.git/MERGE_MSG &&
	echo "cherry-pick 1b" >>foo &&
	test_tick &&
	git commit -a &&
	author_header cherry-pick-head >expect &&
	author_header HEAD >actual &&
	test_cmp expect actual &&

	echo "This is a MERGE_MSG" >expect &&
	message_body HEAD >actual &&
	test_cmp expect actual
'

test_expect_success '--reset-author with CHERRY_PICK_HEAD' '
	git update-ref CHERRY_PICK_HEAD $(git rev-parse cherry-pick-head) &&
	echo "cherry-pick 2" >>foo &&
	test_tick &&
	git commit -am "cherry-pick 2" --reset-author &&
	echo "author $GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> $GIT_AUTHOR_DATE" >expect &&
	author_header HEAD >actual &&
	test_cmp expect actual
'

test_done
