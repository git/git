#!/bin/sh

test_description='unpack-objects'

. ./test-lib.sh

test_expect_success setup '
	mkdir pub.git &&
	GIT_DIR=pub.git git init --bare
	GIT_DIR=pub.git git config receive.fsckobjects true &&
	mkdir work &&
	(
		cd work &&
		git init &&
		mkdir -p gar/bage &&
		(
			cd gar/bage &&
			git init &&
			>junk &&
			git add junk &&
			git commit -m "Initial junk"
		) &&
		git add gar/bage &&
		git commit -m "Initial superproject"
	)
'

test_expect_success push '
	(
		cd work &&
		git push ../pub.git master
	)
'

test_done
