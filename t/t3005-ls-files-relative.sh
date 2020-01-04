#!/bin/sh

test_description='ls-files tests with relative paths

This test runs git ls-files with various relative path arguments.
'

. ./test-lib.sh

test_expect_success 'prepare' '
	: >never-mind-me &&
	git add never-mind-me &&
	mkdir top &&
	(
		cd top &&
		mkdir sub &&
		x="x xa xbc xdef xghij xklmno" &&
		y=$(echo "$x" | tr x y) &&
		touch $x &&
		touch $y &&
		cd sub &&
		git add ../x*
	)
'

test_expect_success 'ls-files with mixed levels' '
	(
		cd top/sub &&
		cat >expect <<-EOF &&
		../../never-mind-me
		../x
		EOF
		git ls-files $(cat expect) >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'ls-files -c' '
	(
		cd top/sub &&
		for f in ../y*
		do
			echo "error: pathspec $SQ$f$SQ did not match any file(s) known to git"
		done >expect.err &&
		echo "Did you forget to ${SQ}git add${SQ}?" >>expect.err &&
		ls ../x* >expect.out &&
		test_must_fail git ls-files -c --error-unmatch ../[xy]* >actual.out 2>actual.err &&
		test_cmp expect.out actual.out &&
		test_i18ncmp expect.err actual.err
	)
'

test_expect_success 'ls-files -o' '
	(
		cd top/sub &&
		for f in ../x*
		do
			echo "error: pathspec $SQ$f$SQ did not match any file(s) known to git"
		done >expect.err &&
		echo "Did you forget to ${SQ}git add${SQ}?" >>expect.err &&
		ls ../y* >expect.out &&
		test_must_fail git ls-files -o --error-unmatch ../[xy]* >actual.out 2>actual.err &&
		test_cmp expect.out actual.out &&
		test_i18ncmp expect.err actual.err
	)
'

test_done
