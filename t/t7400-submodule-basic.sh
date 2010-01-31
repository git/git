#!/bin/sh
#
# Copyright (c) 2007 Lars Hjemli
#

test_description='Basic porcelain support for submodules

This test tries to verify basic sanity of the init, update and status
subcommands of git submodule.
'

. ./test-lib.sh

#
# Test setup:
#  -create a repository in directory init
#  -add a couple of files
#  -add directory init to 'superproject', this creates a DIRLINK entry
#  -add a couple of regular files to enable testing of submodule filtering
#  -mv init subrepo
#  -add an entry to .gitmodules for submodule 'example'
#
test_expect_success 'Prepare submodule testing' '
	: > t &&
	git add t &&
	git commit -m "initial commit" &&
	git branch initial HEAD &&
	mkdir init &&
	cd init &&
	git init &&
	echo a >a &&
	git add a &&
	git commit -m "submodule commit 1" &&
	git tag -a -m "rev-1" rev-1 &&
	rev1=$(git rev-parse HEAD) &&
	if test -z "$rev1"
	then
		echo "[OOPS] submodule git rev-parse returned nothing"
		false
	fi &&
	cd .. &&
	echo a >a &&
	echo z >z &&
	git add a init z &&
	git commit -m "super commit 1" &&
	mv init .subrepo &&
	GIT_CONFIG=.gitmodules git config submodule.example.url git://example.com/init.git
'

test_expect_success 'Prepare submodule add testing' '
	submodurl=$(pwd)
	(
		mkdir addtest &&
		cd addtest &&
		git init
	)
'

test_expect_success 'submodule add' '
	(
		cd addtest &&
		git submodule add "$submodurl" submod &&
		git submodule init
	)
'

test_expect_success 'submodule add --branch' '
	(
		cd addtest &&
		git submodule add -b initial "$submodurl" submod-branch &&
		git submodule init &&
		cd submod-branch &&
		git branch | grep initial
	)
'

test_expect_success 'submodule add with ./ in path' '
	(
		cd addtest &&
		git submodule add "$submodurl" ././dotsubmod/./frotz/./ &&
		git submodule init
	)
'

test_expect_success 'submodule add with // in path' '
	(
		cd addtest &&
		git submodule add "$submodurl" slashslashsubmod///frotz// &&
		git submodule init
	)
'

test_expect_success 'submodule add with /.. in path' '
	(
		cd addtest &&
		git submodule add "$submodurl" dotdotsubmod/../realsubmod/frotz/.. &&
		git submodule init
	)
'

test_expect_success 'submodule add with ./, /.. and // in path' '
	(
		cd addtest &&
		git submodule add "$submodurl" dot/dotslashsubmod/./../..////realsubmod2/a/b/c/d/../../../../frotz//.. &&
		git submodule init
	)
'

test_expect_success 'status should fail for unmapped paths' '
	if git submodule status
	then
		echo "[OOPS] submodule status succeeded"
		false
	elif ! GIT_CONFIG=.gitmodules git config submodule.example.path init
	then
		echo "[OOPS] git config failed to update .gitmodules"
		false
	fi
'

test_expect_success 'status should only print one line' '
	lines=$(git submodule status | wc -l) &&
	test $lines = 1
'

test_expect_success 'status should initially be "missing"' '
	git submodule status | grep "^-$rev1"
'

test_expect_success 'init should register submodule url in .git/config' '
	git submodule init &&
	url=$(git config submodule.example.url) &&
	if test "$url" != "git://example.com/init.git"
	then
		echo "[OOPS] init succeeded but submodule url is wrong"
		false
	elif test_must_fail git config submodule.example.url ./.subrepo
	then
		echo "[OOPS] init succeeded but update of url failed"
		false
	fi
'

test_expect_success 'update should fail when path is used by a file' '
	echo "hello" >init &&
	if git submodule update
	then
		echo "[OOPS] update should have failed"
		false
	elif test "$(cat init)" != "hello"
	then
		echo "[OOPS] update failed but init file was molested"
		false
	else
		rm init
	fi
'

test_expect_success 'update should fail when path is used by a nonempty directory' '
	mkdir init &&
	echo "hello" >init/a &&
	if git submodule update
	then
		echo "[OOPS] update should have failed"
		false
	elif test "$(cat init/a)" != "hello"
	then
		echo "[OOPS] update failed but init/a was molested"
		false
	else
		rm init/a
	fi
'

test_expect_success 'update should work when path is an empty dir' '
	rm -rf init &&
	mkdir init &&
	git submodule update &&
	head=$(cd init && git rev-parse HEAD) &&
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
	git submodule status | grep "^ $rev1"
'

test_expect_success 'status should be "modified" after submodule commit' '
	cd init &&
	echo b >b &&
	git add b &&
	git commit -m "submodule commit 2" &&
	rev2=$(git rev-parse HEAD) &&
	cd .. &&
	if test -z "$rev2"
	then
		echo "[OOPS] submodule git rev-parse returned nothing"
		false
	fi &&
	git submodule status | grep "^+$rev2"
'

test_expect_success 'the --cached sha1 should be rev1' '
	git submodule --cached status | grep "^+$rev1"
'

test_expect_success 'git diff should report the SHA1 of the new submodule commit' '
	git diff | grep "^+Subproject commit $rev2"
'

test_expect_success 'update should checkout rev1' '
	git submodule update init &&
	head=$(cd init && git rev-parse HEAD) &&
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
	git submodule status | grep "^ $rev1"
'

test_expect_success 'checkout superproject with subproject already present' '
	git checkout initial &&
	git checkout master
'

test_expect_success 'apply submodule diff' '
	git branch second &&
	(
		cd init &&
		echo s >s &&
		git add s &&
		git commit -m "change subproject"
	) &&
	git update-index --add init &&
	git commit -m "change init" &&
	git format-patch -1 --stdout >P.diff &&
	git checkout second &&
	git apply --index P.diff &&
	D=$(git diff --cached master) &&
	test -z "$D"
'

test_expect_success 'update --init' '

	mv init init2 &&
	git config -f .gitmodules submodule.example.url "$(pwd)/init2" &&
	git config --remove-section submodule.example
	git submodule update init > update.out &&
	grep "not initialized" update.out &&
	test ! -d init/.git &&
	git submodule update --init init &&
	test -d init/.git

'

test_expect_success 'do not add files from a submodule' '

	git reset --hard &&
	test_must_fail git add init/a

'

test_expect_success 'gracefully add submodule with a trailing slash' '

	git reset --hard &&
	git commit -m "commit subproject" init &&
	(cd init &&
	 echo b > a) &&
	git add init/ &&
	git diff --exit-code --cached init &&
	commit=$(cd init &&
	 git commit -m update a >/dev/null &&
	 git rev-parse HEAD) &&
	git add init/ &&
	test_must_fail git diff --exit-code --cached init &&
	test $commit = $(git ls-files --stage |
		sed -n "s/^160000 \([^ ]*\).*/\1/p")

'

test_expect_success 'ls-files gracefully handles trailing slash' '

	test "init" = "$(git ls-files init/)"

'

test_expect_success 'submodule <invalid-path> warns' '

	git submodule no-such-submodule 2> output.err &&
	grep "^error: .*no-such-submodule" output.err

'

test_done
