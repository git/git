#!/bin/sh

test_description='git cat-file filters support'
. ./test-lib.sh

test_expect_success 'setup ' '
	echo "*.txt eol=crlf diff=txt" >.gitattributes &&
	echo "hello" | append_cr >world.txt &&
	git add .gitattributes world.txt &&
	test_tick &&
	git commit -m "Initial commit"
'

has_cr () {
	tr '\015' Q <"$1" | grep Q >/dev/null
}

test_expect_success 'no filters with `git show`' '
	git show HEAD:world.txt >actual &&
	! has_cr actual

'

test_expect_success 'no filters with cat-file' '
	git cat-file blob HEAD:world.txt >actual &&
	! has_cr actual
'

test_expect_success 'cat-file --filters converts to worktree version' '
	git cat-file --filters HEAD:world.txt >actual &&
	has_cr actual
'

test_expect_success 'cat-file --filters --path=<path> works' '
	sha1=$(git rev-parse -q --verify HEAD:world.txt) &&
	git cat-file --filters --path=world.txt $sha1 >actual &&
	has_cr actual
'

test_expect_success 'cat-file --textconv --path=<path> works' '
	sha1=$(git rev-parse -q --verify HEAD:world.txt) &&
	test_config diff.txt.textconv "tr A-Za-z N-ZA-Mn-za-m <" &&
	git cat-file --textconv --path=hello.txt $sha1 >rot13 &&
	test uryyb = "$(cat rot13 | remove_cr)"
'

test_expect_success '--path=<path> complains without --textconv/--filters' '
	sha1=$(git rev-parse -q --verify HEAD:world.txt) &&
	test_must_fail git cat-file --path=hello.txt blob $sha1 >actual 2>err &&
	test ! -s actual &&
	grep "path.*needs.*filters" err
'

test_done
