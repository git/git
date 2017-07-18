#!/bin/sh
#
# Copyright (C) 2015 Kyle J. McKay
#

test_description='git apply test patches with whitespace expansion.'

. ./test-lib.sh

test_expect_success setup '
	#
	## create test-N, patchN.patch, expect-N files
	#

	# test 1
	printf "\t%s\n" 1 2 3 4 5 6 >before &&
	printf "\t%s\n" 1 2 3 >after &&
	printf "%64s\n" a b c >>after &&
	printf "\t%s\n" 4 5 6 >>after &&
	git diff --no-index before after |
		sed -e "s/before/test-1/" -e "s/after/test-1/" >patch1.patch &&
	printf "%64s\n" 1 2 3 4 5 6 >test-1 &&
	printf "%64s\n" 1 2 3 a b c 4 5 6 >expect-1 &&

	# test 2
	printf "\t%s\n" a b c d e f >before &&
	printf "\t%s\n" a b c >after &&
	n=10 &&
	x=1 &&
	while test $x -lt $n
	do
		printf "%63s%d\n" "" $x >>after
		x=$(( $x + 1 ))
	done &&
	printf "\t%s\n" d e f >>after &&
	git diff --no-index before after |
		sed -e "s/before/test-2/" -e "s/after/test-2/" >patch2.patch &&
	printf "%64s\n" a b c d e f >test-2 &&
	printf "%64s\n" a b c >expect-2 &&
	x=1 &&
	while test $x -lt $n
	do
		printf "%63s%d\n" "" $x >>expect-2
		x=$(( $x + 1 ))
	done &&
	printf "%64s\n" d e f >>expect-2 &&

	# test 3
	printf "\t%s\n" a b c d e f >before &&
	printf "\t%s\n" a b c >after &&
	n=100 &&
	x=0 &&
	while test $x -lt $n
	do
		printf "%63s%02d\n" "" $x >>after
		x=$(( $x + 1 ))
	done &&
	printf "\t%s\n" d e f >>after &&
	git diff --no-index before after |
	sed -e "s/before/test-3/" -e "s/after/test-3/" >patch3.patch &&
	printf "%64s\n" a b c d e f >test-3 &&
	printf "%64s\n" a b c >expect-3 &&
	x=0 &&
	while test $x -lt $n
	do
		printf "%63s%02d\n" "" $x >>expect-3
		x=$(( $x + 1 ))
	done &&
	printf "%64s\n" d e f >>expect-3 &&

	# test 4
	>before &&
	x=0 &&
	while test $x -lt 50
	do
		printf "\t%02d\n" $x >>before
		x=$(( $x + 1 ))
	done &&
	cat before >after &&
	printf "%64s\n" a b c >>after &&
	while test $x -lt 100
	do
		printf "\t%02d\n" $x >>before
		printf "\t%02d\n" $x >>after
		x=$(( $x + 1 ))
	done &&
	git diff --no-index before after |
	sed -e "s/before/test-4/" -e "s/after/test-4/" >patch4.patch &&
	>test-4 &&
	x=0 &&
	while test $x -lt 50
	do
		printf "%63s%02d\n" "" $x >>test-4
		x=$(( $x + 1 ))
	done &&
	cat test-4 >expect-4 &&
	printf "%64s\n" a b c >>expect-4 &&
	while test $x -lt 100
	do
		printf "%63s%02d\n" "" $x >>test-4
		printf "%63s%02d\n" "" $x >>expect-4
		x=$(( $x + 1 ))
	done &&

	git config core.whitespace tab-in-indent,tabwidth=63 &&
	git config apply.whitespace fix

'

# Note that `patch` can successfully apply all patches when run
# with the --ignore-whitespace option.

for t in 1 2 3 4
do
	test_expect_success 'apply with ws expansion (t=$t)' '
		git apply patch$t.patch &&
		test_cmp test-$t expect-$t
	'
done

test_done
