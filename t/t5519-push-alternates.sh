#!/bin/sh

test_description='push to a repository that borrows from elsewhere'

. ./test-lib.sh

test_expect_success setup '
	mkdir alice-pub &&
	(
		cd alice-pub &&
		GIT_DIR=. git init
	) &&
	mkdir alice-work &&
	(
		cd alice-work &&
		git init &&
		>file &&
		git add . &&
		git commit -m initial &&
		git push ../alice-pub master
	) &&

	# Project Bob is a fork of project Alice
	mkdir bob-pub &&
	(
		cd bob-pub &&
		GIT_DIR=. git init &&
		mkdir -p objects/info &&
		echo ../../alice-pub/objects >objects/info/alternates
	) &&
	git clone alice-pub bob-work &&
	(
		cd bob-work &&
		git push ../bob-pub master
	)
'

test_expect_success 'alice works and pushes' '
	(
		cd alice-work &&
		echo more >file &&
		git commit -a -m second &&
		git push ../alice-pub :
	)
'

test_expect_success 'bob fetches from alice, works and pushes' '
	(
		# Bob acquires what Alice did in his work tree first.
		# Even though these objects are not directly in
		# the public repository of Bob, this push does not
		# need to send the commit Bob received from Alice
		# to his public repository, as all the object Alice
		# has at her public repository are available to it
		# via its alternates.
		cd bob-work &&
		git pull ../alice-pub master &&
		echo more bob >file &&
		git commit -a -m third &&
		git push ../bob-pub :
	) &&

	# Check that the second commit by Alice is not sent
	# to ../bob-pub
	(
		cd bob-pub &&
		second=$(git rev-parse HEAD^) &&
		rm -f objects/info/alternates &&
		test_must_fail git cat-file -t $second &&
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
		git commit -a -m fourth &&
		git push ../alice-pub :
	)
'

test_expect_success 'bob works and pushes' '
	(
		# This time Bob does not pull from Alice, and
		# the master branch at her public repository points
		# at a commit Bob does not know about.  This should
		# not prevent the push by Bob from succeeding.
		cd bob-work &&
		echo yet more bob >file &&
		git commit -a -m fifth &&
		git push ../bob-pub :
	)
'

test_expect_success 'alice works and pushes yet again' '
	(
		# Alice does not care what Bob does.  She does not
		# even have to be aware of his existence.  She just
		# keeps working and pushing
		cd alice-work &&
		echo more and more alice >file &&
		git commit -a -m sixth.1 &&
		echo more and more alice >>file &&
		git commit -a -m sixth.2 &&
		echo more and more alice >>file &&
		git commit -a -m sixth.3 &&
		git push ../alice-pub :
	)
'

test_expect_success 'bob works and pushes again' '
	(
		cd alice-pub &&
		git cat-file commit master >../bob-work/commit
	) &&
	(
		# This time Bob does not pull from Alice, and
		# the master branch at her public repository points
		# at a commit Bob does not fully know about, but
		# he happens to have the commit object (but not the
		# necessary tree) in his repository from Alice.
		# This should not prevent the push by Bob from
		# succeeding.
		cd bob-work &&
		git hash-object -t commit -w commit &&
		echo even more bob >file &&
		git commit -a -m seventh &&
		git push ../bob-pub :
	)
'

test_done
