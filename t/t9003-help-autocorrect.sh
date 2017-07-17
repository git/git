#!/bin/sh

test_description='help.autocorrect finding a match'
. ./test-lib.sh

test_expect_success 'setup' '
	# An alias
	git config alias.lgf "log --format=%s --first-parent" &&

	# A random user-defined command
	write_script git-distimdistim <<-EOF &&
		echo distimdistim was called
	EOF

	PATH="$PATH$PATH_SEP." &&
	export PATH &&

	git commit --allow-empty -m "a single log entry" &&

	# Sanity check
	git lgf >actual &&
	echo "a single log entry" >expect &&
	test_cmp expect actual &&

	git distimdistim >actual &&
	echo "distimdistim was called" >expect &&
	test_cmp expect actual
'

test_expect_success 'autocorrect showing candidates' '
	git config help.autocorrect 0 &&

	test_must_fail git lfg 2>actual &&
	grep "^	lgf" actual &&

	test_must_fail git distimdist 2>actual &&
	grep "^	distimdistim" actual
'

test_expect_success 'autocorrect running commands' '
	git config help.autocorrect -1 &&

	git lfg >actual &&
	echo "a single log entry" >expect &&
	test_cmp expect actual &&

	git distimdist >actual &&
	echo "distimdistim was called" >expect &&
	test_cmp expect actual
'

test_done
