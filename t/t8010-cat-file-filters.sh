#!/bin/sh

test_description='but cat-file filters support'
. ./test-lib.sh

test_expect_success 'setup ' '
	echo "*.txt eol=crlf diff=txt" >.butattributes &&
	echo "hello" | append_cr >world.txt &&
	but add .butattributes world.txt &&
	test_tick &&
	but cummit -m "Initial cummit"
'

has_cr () {
	tr '\015' Q <"$1" | grep Q >/dev/null
}

test_expect_success 'no filters with `but show`' '
	but show HEAD:world.txt >actual &&
	! has_cr actual

'

test_expect_success 'no filters with cat-file' '
	but cat-file blob HEAD:world.txt >actual &&
	! has_cr actual
'

test_expect_success 'cat-file --filters converts to worktree version' '
	but cat-file --filters HEAD:world.txt >actual &&
	has_cr actual
'

test_expect_success 'cat-file --filters --path=<path> works' '
	sha1=$(but rev-parse -q --verify HEAD:world.txt) &&
	but cat-file --filters --path=world.txt $sha1 >actual &&
	has_cr actual
'

test_expect_success 'cat-file --textconv --path=<path> works' '
	sha1=$(but rev-parse -q --verify HEAD:world.txt) &&
	test_config diff.txt.textconv "tr A-Za-z N-ZA-Mn-za-m <" &&
	but cat-file --textconv --path=hello.txt $sha1 >rot13 &&
	test uryyb = "$(cat rot13 | remove_cr)"
'

test_expect_success '--path=<path> complains without --textconv/--filters' '
	sha1=$(but rev-parse -q --verify HEAD:world.txt) &&
	test_must_fail but cat-file --path=hello.txt blob $sha1 >actual 2>err &&
	test_must_be_empty actual &&
	grep "path.*needs.*filters" err
'

test_expect_success '--textconv/--filters complain without path' '
	test_must_fail but cat-file --textconv HEAD &&
	test_must_fail but cat-file --filters HEAD
'

test_expect_success 'cat-file --textconv --batch works' '
	sha1=$(but rev-parse -q --verify HEAD:world.txt) &&
	test_config diff.txt.textconv "tr A-Za-z N-ZA-Mn-za-m <" &&
	printf "%s hello.txt\n%s hello\n" $sha1 $sha1 |
	but cat-file --textconv --batch >actual &&
	printf "%s blob 6\nuryyb\r\n\n%s blob 6\nhello\n\n" \
		$sha1 $sha1 >expect &&
	test_cmp expect actual
'

test_done
