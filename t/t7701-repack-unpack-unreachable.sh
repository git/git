#!/bin/sh

test_description='git-repack works correctly'

. ./test-lib.sh

test_expect_success '-A option leaves unreachable objects unpacked' '
	echo content > file1 &&
	git add . &&
	git commit -m initial_commit &&
	# create a transient branch with unique content
	git checkout -b transient_branch &&
	echo more content >> file1 &&
	# record the objects created in the database for file, commit, tree
	fsha1=$(git hash-object file1) &&
	git commit -a -m more_content &&
	csha1=$(git rev-parse HEAD^{commit}) &&
	tsha1=$(git rev-parse HEAD^{tree}) &&
	git checkout master &&
	echo even more content >> file1 &&
	git commit -a -m even_more_content &&
	# delete the transient branch
	git branch -D transient_branch &&
	# pack the repo
	git repack -A -d -l &&
	# verify objects are packed in repository
	test 3 = $(git verify-pack -v -- .git/objects/pack/*.idx |
		   grep -e "^$fsha1 " -e "^$csha1 " -e "^$tsha1 " |
		   sort | uniq | wc -l) &&
	git show $fsha1 &&
	git show $csha1 &&
	git show $tsha1 &&
	# now expire the reflog
	sleep 1 &&
	git reflog expire --expire-unreachable=now --all &&
	# and repack
	git repack -A -d -l &&
	# verify objects are retained unpacked
	test 0 = $(git verify-pack -v -- .git/objects/pack/*.idx |
		   grep -e "^$fsha1 " -e "^$csha1 " -e "^$tsha1 " |
		   sort | uniq | wc -l) &&
	git show $fsha1 &&
	git show $csha1 &&
	git show $tsha1
'

test_done
