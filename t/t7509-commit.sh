#!/bin/sh
#
# Copyright (c) 2009 Erick Mattos
#

test_description='git commit --reset-author'

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
	echo "Initial" >foo &&
	git add foo &&
	test_tick &&
	git commit -m "Initial Commit" --author Frigate\ \<flying@over.world\> &&
	git tag Initial &&
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
	author_header HEAD >actual
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
no_reflog() {
	cp .git/config .git/config.saved &&
	echo "[core] logallrefupdates = false" >>.git/config &&
	test_when_finished "mv -f .git/config.saved .git/config" &&

	if test -e .git/logs
	then
		mv .git/logs . &&
		test_when_finished "mv logs .git/"
	fi
}

test_expect_success '--amend option with empty author' '
	git cat-file commit Initial >tmp &&
	sed "s/author [^<]* </author  </" tmp >empty-author &&
	no_reflog &&
	sha=$(git hash-object -t commit -w empty-author) &&
	test_when_finished "remove_object $sha" &&
	git checkout $sha &&
	test_when_finished "git checkout Initial" &&
	echo "Empty author test" >>foo &&
	test_tick &&
	! git commit -a -m "empty author" --amend 2>err &&
	grep "empty ident" err
'

test_expect_success '--amend option with missing author' '
	git cat-file commit Initial >tmp &&
	sed "s/author [^<]* </author </" tmp >malformed &&
	no_reflog &&
	sha=$(git hash-object -t commit -w malformed) &&
	test_when_finished "remove_object $sha" &&
	git checkout $sha &&
	test_when_finished "git checkout Initial" &&
	echo "Missing author test" >>foo &&
	test_tick &&
	! git commit -a -m "malformed author" --amend 2>err &&
	grep "empty ident" err
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

test_done
