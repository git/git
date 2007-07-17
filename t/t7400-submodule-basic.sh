#!/bin/sh
#
# Copyright (c) 2007 Lars Hjemli
#

test_description='Basic porcelain support for submodules

This test tries to verify basic sanity of the init, update and status
subcommands of git-submodule.
'

. ./test-lib.sh

#
# Test setup:
#  -create a repository in directory lib
#  -add a couple of files
#  -add directory lib to 'superproject', this creates a DIRLINK entry
#  -add a couple of regular files to enable testing of submodule filtering
#  -mv lib subrepo
#  -add an entry to .gitmodules for submodule 'example'
#
test_expect_success 'Prepare submodule testing' '
	: > t &&
	git-add t &&
	git-commit -m "initial commit" &&
	git branch initial HEAD &&
	mkdir lib &&
	cd lib &&
	git init &&
	echo a >a &&
	git add a &&
	git-commit -m "submodule commit 1" &&
	git-tag -a -m "rev-1" rev-1 &&
	rev1=$(git rev-parse HEAD) &&
	if test -z "$rev1"
	then
		echo "[OOPS] submodule git rev-parse returned nothing"
		false
	fi &&
	cd .. &&
	echo a >a &&
	echo z >z &&
	git add a lib z &&
	git-commit -m "super commit 1" &&
	mv lib .subrepo &&
	GIT_CONFIG=.gitmodules git config submodule.example.url git://example.com/lib.git
'

test_expect_success 'status should fail for unmapped paths' '
	if git-submodule status
	then
		echo "[OOPS] submodule status succeeded"
		false
	elif ! GIT_CONFIG=.gitmodules git config submodule.example.path lib
	then
		echo "[OOPS] git config failed to update .gitmodules"
		false
	fi
'

test_expect_success 'status should only print one line' '
	lines=$(git-submodule status | wc -l) &&
	test $lines = 1
'

test_expect_success 'status should initially be "missing"' '
	git-submodule status | grep "^-$rev1"
'

test_expect_success 'init should register submodule url in .git/config' '
	git-submodule init &&
	url=$(git config submodule.example.url) &&
	if test "$url" != "git://example.com/lib.git"
	then
		echo "[OOPS] init succeeded but submodule url is wrong"
		false
	elif ! git config submodule.example.url ./.subrepo
	then
		echo "[OOPS] init succeeded but update of url failed"
		false
	fi
'

test_expect_success 'update should fail when path is used by a file' '
	echo "hello" >lib &&
	if git-submodule update
	then
		echo "[OOPS] update should have failed"
		false
	elif test "$(cat lib)" != "hello"
	then
		echo "[OOPS] update failed but lib file was molested"
		false
	else
		rm lib
	fi
'

test_expect_success 'update should fail when path is used by a nonempty directory' '
	mkdir lib &&
	echo "hello" >lib/a &&
	if git-submodule update
	then
		echo "[OOPS] update should have failed"
		false
	elif test "$(cat lib/a)" != "hello"
	then
		echo "[OOPS] update failed but lib/a was molested"
		false
	else
		rm lib/a
	fi
'

test_expect_success 'update should work when path is an empty dir' '
	rm -rf lib &&
	mkdir lib &&
	git-submodule update &&
	head=$(cd lib && git rev-parse HEAD) &&
	if test -z "$head"
	then
		echo "[OOPS] Failed to obtain submodule head"
		false
	elif test "$head" != "$rev1"
	then
		echo "[OOPS] Submodule head is $head but should have been $rev1"
		false
	fi
'

test_expect_success 'status should be "up-to-date" after update' '
	git-submodule status | grep "^ $rev1"
'

test_expect_success 'status should be "modified" after submodule commit' '
	cd lib &&
	echo b >b &&
	git add b &&
	git-commit -m "submodule commit 2" &&
	rev2=$(git rev-parse HEAD) &&
	cd .. &&
	if test -z "$rev2"
	then
		echo "[OOPS] submodule git rev-parse returned nothing"
		false
	fi &&
	git-submodule status | grep "^+$rev2"
'

test_expect_success 'the --cached sha1 should be rev1' '
	git-submodule --cached status | grep "^+$rev1"
'

test_expect_success 'update should checkout rev1' '
	git-submodule update &&
	head=$(cd lib && git rev-parse HEAD) &&
	if test -z "$head"
	then
		echo "[OOPS] submodule git rev-parse returned nothing"
		false
	elif test "$head" != "$rev1"
	then
		echo "[OOPS] init did not checkout correct head"
		false
	fi
'

test_expect_success 'status should be "up-to-date" after update' '
	git-submodule status | grep "^ $rev1"
'

test_expect_success 'checkout superproject with subproject already present' '
	git-checkout initial &&
	git-checkout master
'

test_done
