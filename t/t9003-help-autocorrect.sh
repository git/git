#!/bin/sh

test_description='help.autocorrect finding a match'
. ./test-lib.sh

test_expect_success 'setup' '
	# An alias
	but config alias.lgf "log --format=%s --first-parent" &&

	# A random user-defined command
	write_script but-distimdistim <<-EOF &&
		echo distimdistim was called
	EOF

	PATH="$PATH:." &&
	export PATH &&

	but cummit --allow-empty -m "a single log entry" &&

	# Sanity check
	but lgf >actual &&
	echo "a single log entry" >expect &&
	test_cmp expect actual &&

	but distimdistim >actual &&
	echo "distimdistim was called" >expect &&
	test_cmp expect actual
'

test_expect_success 'autocorrect showing candidates' '
	but config help.autocorrect 0 &&

	test_must_fail but lfg 2>actual &&
	grep "^	lgf" actual &&

	test_must_fail but distimdist 2>actual &&
	grep "^	distimdistim" actual
'

for immediate in -1 immediate
do
	test_expect_success 'autocorrect running commands' '
		but config help.autocorrect $immediate &&

		but lfg >actual &&
		echo "a single log entry" >expect &&
		test_cmp expect actual &&

		but distimdist >actual &&
		echo "distimdistim was called" >expect &&
		test_cmp expect actual
	'
done

test_expect_success 'autocorrect can be declined altogether' '
	but config help.autocorrect never &&

	test_must_fail but lfg 2>actual &&
	grep "is not a but command" actual &&
	test_line_count = 1 actual
'

test_done
