#!/bin/sh

test_description='push to a repository that borrows from elsewhere'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	mkdir alice-pub &&
	(
		cd alice-pub &&
		BUT_DIR=. but init
	) &&
	mkdir alice-work &&
	(
		cd alice-work &&
		but init &&
		>file &&
		but add . &&
		but cummit -m initial &&
		but push ../alice-pub main
	) &&

	# Project Bob is a fork of project Alice
	mkdir bob-pub &&
	(
		cd bob-pub &&
		BUT_DIR=. but init &&
		mkdir -p objects/info &&
		echo ../../alice-pub/objects >objects/info/alternates
	) &&
	but clone alice-pub bob-work &&
	(
		cd bob-work &&
		but push ../bob-pub main
	)
'

test_expect_success 'alice works and pushes' '
	(
		cd alice-work &&
		echo more >file &&
		but cummit -a -m second &&
		but push ../alice-pub :
	)
'

test_expect_success 'bob fetches from alice, works and pushes' '
	(
		# Bob acquires what Alice did in his work tree first.
		# Even though these objects are not directly in
		# the public repository of Bob, this push does not
		# need to send the cummit Bob received from Alice
		# to his public repository, as all the object Alice
		# has at her public repository are available to it
		# via its alternates.
		cd bob-work &&
		but pull ../alice-pub main &&
		echo more bob >file &&
		but cummit -a -m third &&
		but push ../bob-pub :
	) &&

	# Check that the second cummit by Alice is not sent
	# to ../bob-pub
	(
		cd bob-pub &&
		second=$(but rev-parse HEAD^) &&
		rm -f objects/info/alternates &&
		test_must_fail but cat-file -t $second &&
		echo ../../alice-pub/objects >objects/info/alternates
	)
'

test_expect_success 'clean-up in case the previous failed' '
	(
		cd bob-pub &&
		echo ../../alice-pub/objects >objects/info/alternates
	)
'

test_expect_success 'alice works and pushes again' '
	(
		# Alice does not care what Bob does.  She does not
		# even have to be aware of his existence.  She just
		# keeps working and pushing
		cd alice-work &&
		echo more alice >file &&
		but cummit -a -m fourth &&
		but push ../alice-pub :
	)
'

test_expect_success 'bob works and pushes' '
	(
		# This time Bob does not pull from Alice, and
		# the main branch at her public repository points
		# at a cummit Bob does not know about.  This should
		# not prevent the push by Bob from succeeding.
		cd bob-work &&
		echo yet more bob >file &&
		but cummit -a -m fifth &&
		but push ../bob-pub :
	)
'

test_expect_success 'alice works and pushes yet again' '
	(
		# Alice does not care what Bob does.  She does not
		# even have to be aware of his existence.  She just
		# keeps working and pushing
		cd alice-work &&
		echo more and more alice >file &&
		but cummit -a -m sixth.1 &&
		echo more and more alice >>file &&
		but cummit -a -m sixth.2 &&
		echo more and more alice >>file &&
		but cummit -a -m sixth.3 &&
		but push ../alice-pub :
	)
'

test_expect_success 'bob works and pushes again' '
	(
		cd alice-pub &&
		but cat-file cummit main >../bob-work/cummit
	) &&
	(
		# This time Bob does not pull from Alice, and
		# the main branch at her public repository points
		# at a cummit Bob does not fully know about, but
		# he happens to have the cummit object (but not the
		# necessary tree) in his repository from Alice.
		# This should not prevent the push by Bob from
		# succeeding.
		cd bob-work &&
		but hash-object -t cummit -w cummit &&
		echo even more bob >file &&
		but cummit -a -m seventh &&
		but push ../bob-pub :
	)
'

test_done
