#!/bin/sh

test_description='rev-list testing in-commit-order'

. ./test-lib.sh

test_expect_success 'setup a commit history with trees, blobs' '
	for x in one two three four
	do
		echo $x >$x &&
		git add $x &&
		git commit -m "add file $x" ||
		return 1
	done &&
	for x in four three
	do
		git rm $x &&
		git commit -m "remove $x" ||
		return 1
	done
'

test_expect_success 'rev-list --in-commit-order' '
	git rev-list --in-commit-order --objects HEAD >actual.raw &&
	cut -c 1-40 >actual <actual.raw &&

	git cat-file --batch-check="%(objectname)" >expect.raw <<-\EOF &&
		HEAD^{commit}
		HEAD^{tree}
		HEAD^{tree}:one
		HEAD^{tree}:two
		HEAD~1^{commit}
		HEAD~1^{tree}
		HEAD~1^{tree}:three
		HEAD~2^{commit}
		HEAD~2^{tree}
		HEAD~2^{tree}:four
		HEAD~3^{commit}
		# HEAD~3^{tree} skipped, same as HEAD~1^{tree}
		HEAD~4^{commit}
		# HEAD~4^{tree} skipped, same as HEAD^{tree}
		HEAD~5^{commit}
		HEAD~5^{tree}
	EOF
	grep -v "#" >expect <expect.raw &&

	test_cmp expect actual
'

test_expect_success 'rev-list lists blobs and trees after commits' '
	git rev-list --objects HEAD >actual.raw &&
	cut -c 1-40 >actual <actual.raw &&

	git cat-file --batch-check="%(objectname)" >expect.raw <<-\EOF &&
		HEAD^{commit}
		HEAD~1^{commit}
		HEAD~2^{commit}
		HEAD~3^{commit}
		HEAD~4^{commit}
		HEAD~5^{commit}
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
