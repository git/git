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

	PATH="$PATH:." &&
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

for immediate in -1 immediate
do
	test_expect_success 'autocorrect running commands' '
		git config help.autocorrect $immediate &&

		git lfg >actual &&
		echo "a single log entry" >expect &&
		test_cmp expect actual &&

		git distimdist >actual &&
		echo "distimdistim was called" >expect &&
		test_cmp expect actual
	'
done

test_expect_success 'autocorrect can be declined altogether' '
	git config help.autocorrect never &&

	test_must_fail git lfg 2>actual &&
	grep "is not a git command" actual &&
	test_line_count = 1 actual
'

test_done
