#!/bin/sh

test_description='rewrite diff'

. ./test-lib.sh

test_expect_success setup '

	cat ../../COPYING >test &&
	git add test &&
	tr \
	  "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ" \
	  "nopqrstuvwxyzabcdefghijklmNOPQRSTUVWXYZABCDEFGHIJKLM" \
	  <../../COPYING >test

'

test_expect_success 'detect rewrite' '

	actual=$(git diff-files -B --summary test) &&
	expr "$actual" : " rewrite test ([0-9]*%)$" || {
		echo "Eh? <<$actual>>"
		false
	}

'

test_done

