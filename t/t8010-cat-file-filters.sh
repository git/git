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

test_done
