#!/bin/sh

test_description='rev-list testing in-cummit-order'

. ./test-lib.sh

test_expect_success 'setup a commit history with trees, blobs' '
	for x in one two three four
	do
		echo $x >$x &&
		but add $x &&
		but cummit -m "add file $x" ||
		return 1
	done &&
	for x in four three
	do
		but rm $x &&
		but cummit -m "remove $x" ||
		return 1
	done
'

test_expect_success 'rev-list --in-cummit-order' '
	but rev-list --in-cummit-order --objects HEAD >actual.raw &&
	cut -d" " -f1 >actual <actual.raw &&

	but cat-file --batch-check="%(objectname)" >expect.raw <<-\EOF &&
		HEAD^{cummit}
		HEAD^{tree}
		HEAD^{tree}:one
		HEAD^{tree}:two
		HEAD~1^{cummit}
		HEAD~1^{tree}
		HEAD~1^{tree}:three
		HEAD~2^{cummit}
		HEAD~2^{tree}
		HEAD~2^{tree}:four
		HEAD~3^{cummit}
		# HEAD~3^{tree} skipped, same as HEAD~1^{tree}
		HEAD~4^{cummit}
		# HEAD~4^{tree} skipped, same as HEAD^{tree}
		HEAD~5^{cummit}
		HEAD~5^{tree}
	EOF
	grep -v "#" >expect <expect.raw &&

	test_cmp expect actual
'

test_expect_success 'rev-list lists blobs and trees after cummits' '
	but rev-list --objects HEAD >actual.raw &&
	cut -d" " -f1 >actual <actual.raw &&

	but cat-file --batch-check="%(objectname)" >expect.raw <<-\EOF &&
		HEAD^{cummit}
		HEAD~1^{cummit}
		HEAD~2^{cummit}
		HEAD~3^{cummit}
		HEAD~4^{cummit}
		HEAD~5^{cummit}
		HEAD^{tree}
		HEAD^{tree}:one
		HEAD^{tree}:two
		HEAD~1^{tree}
		HEAD~1^{tree}:three
		HEAD~2^{tree}
		HEAD~2^{tree}:four
		# HEAD~3^{tree} skipped, same as HEAD~1^{tree}
		# HEAD~4^{tree} skipped, same as HEAD^{tree}
		HEAD~5^{tree}
	EOF
	grep -v "#" >expect <expect.raw &&

	test_cmp expect actual
'

test_done
