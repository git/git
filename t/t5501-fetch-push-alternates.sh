#!/bin/sh

test_description='fetch/push involving alternates'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

count_objects () {
	loose=0 inpack=0
	eval "$(
		but count-objects -v |
		sed -n -e 's/^count: \(.*\)/loose=\1/p' \
		    -e 's/^in-pack: \(.*\)/inpack=\1/p'
	)" &&
	echo $(( $loose + $inpack ))
}


test_expect_success setup '
	(
		but init original &&
		cd original &&
		i=0 &&
		while test $i -le 100
		do
			echo "$i" >count &&
			but add count &&
			but cummit -m "$i" || exit
			i=$(($i + 1))
		done
	) &&
	(
		but clone --reference=original "file://$(pwd)/original" one &&
		cd one &&
		echo Z >count &&
		but add count &&
		but cummit -m Z &&
		count_objects >../one.count
	) &&
	A=$(pwd)/original/.but/objects &&
	but init receiver &&
	echo "$A" >receiver/.but/objects/info/alternates &&
	but init fetcher &&
	echo "$A" >fetcher/.but/objects/info/alternates
'

test_expect_success 'pushing into a repository with the same alternate' '
	(
		cd one &&
		but push ../receiver main:refs/heads/it
	) &&
	(
		cd receiver &&
		count_objects >../receiver.count
	) &&
	test_cmp one.count receiver.count
'

test_expect_success 'fetching from a repository with the same alternate' '
	(
		cd fetcher &&
		but fetch ../one main:refs/heads/it &&
		count_objects >../fetcher.count
	) &&
	test_cmp one.count fetcher.count
'

test_done
