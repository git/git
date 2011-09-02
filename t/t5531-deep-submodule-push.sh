#!/bin/sh

test_description='unpack-objects'

. ./test-lib.sh

test_expect_success setup '
	mkdir pub.git &&
	GIT_DIR=pub.git git init --bare &&
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

test_expect_success 'push if submodule has no remote' '
	(
		cd work/gar/bage &&
		>junk2 &&
		git add junk2 &&
		git commit -m "Second junk"
	) &&
	(
		cd work &&
		git add gar/bage &&
		git commit -m "Second commit for gar/bage" &&
		git push --recurse-submodules=check ../pub.git master
	)
'

test_expect_success 'push fails if submodule commit not on remote' '
	(
		cd work/gar &&
		git clone --bare bage ../../submodule.git &&
		cd bage &&
		git remote add origin ../../../submodule.git &&
		git fetch &&
		>junk3 &&
		git add junk3 &&
		git commit -m "Third junk"
	) &&
	(
		cd work &&
		git add gar/bage &&
		git commit -m "Third commit for gar/bage" &&
		test_must_fail git push --recurse-submodules=check ../pub.git master
	)
'

test_expect_success 'push succeeds after commit was pushed to remote' '
	(
		cd work/gar/bage &&
		git push origin master
	) &&
	(
		cd work &&
		git push --recurse-submodules=check ../pub.git master
	)
'

test_expect_success 'push fails when commit on multiple branches if one branch has no remote' '
	(
		cd work/gar/bage &&
		>junk4 &&
		git add junk4 &&
		git commit -m "Fourth junk"
	) &&
	(
		cd work &&
		git branch branch2 &&
		git add gar/bage &&
		git commit -m "Fourth commit for gar/bage" &&
		git checkout branch2 &&
		(
			cd gar/bage &&
			git checkout HEAD~1
		) &&
		>junk1 &&
		git add junk1 &&
		git commit -m "First junk" &&
		test_must_fail git push --recurse-submodules=check ../pub.git
	)
'

test_expect_success 'push succeeds if submodule has no remote and is on the first superproject commit' '
	git init --bare a
	git clone a a1 &&
	(
		cd a1 &&
		git init b
		(
			cd b &&
			>junk &&
			git add junk &&
			git commit -m "initial"
		) &&
		git add b &&
		git commit -m "added submodule" &&
		git push --recurse-submodule=check origin master
	)
'

test_done
