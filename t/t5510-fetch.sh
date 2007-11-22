#!/bin/sh
# Copyright (c) 2006, Junio C Hamano.

test_description='Per branch config variables affects "git fetch".

'

. ./test-lib.sh

D=`pwd`

test_expect_success setup '
	echo >file original &&
	git add file &&
	git commit -a -m original'

test_expect_success "clone and setup child repos" '
	git clone . one &&
	cd one &&
	echo >file updated by one &&
	git commit -a -m "updated by one" &&
	cd .. &&
	git clone . two &&
	cd two &&
	git config branch.master.remote one &&
	git config remote.one.url ../one/.git/ &&
	git config remote.one.fetch refs/heads/master:refs/heads/one &&
	cd .. &&
	git clone . three &&
	cd three &&
	git config branch.master.remote two &&
	git config branch.master.merge refs/heads/one &&
	mkdir -p .git/remotes &&
	{
		echo "URL: ../two/.git/"
		echo "Pull: refs/heads/master:refs/heads/two"
		echo "Pull: refs/heads/one:refs/heads/one"
	} >.git/remotes/two &&
	cd .. &&
	git clone . bundle
'

test_expect_success "fetch test" '
	cd "$D" &&
	echo >file updated by origin &&
	git commit -a -m "updated by origin" &&
	cd two &&
	git fetch &&
	test -f .git/refs/heads/one &&
	mine=`git rev-parse refs/heads/one` &&
	his=`cd ../one && git rev-parse refs/heads/master` &&
	test "z$mine" = "z$his"
'

test_expect_success "fetch test for-merge" '
	cd "$D" &&
	cd three &&
	git fetch &&
	test -f .git/refs/heads/two &&
	test -f .git/refs/heads/one &&
	master_in_two=`cd ../two && git rev-parse master` &&
	one_in_two=`cd ../two && git rev-parse one` &&
	{
		echo "$master_in_two	not-for-merge"
		echo "$one_in_two	"
	} >expected &&
	cut -f -2 .git/FETCH_HEAD >actual &&
	diff expected actual'

test_expect_success 'fetch following tags' '

	cd "$D" &&
	git tag -a -m 'annotated' anno HEAD &&
	git tag light HEAD &&

	mkdir four &&
	cd four &&
	git init &&

	git fetch .. :track &&
	git show-ref --verify refs/tags/anno &&
	git show-ref --verify refs/tags/light

'

test_expect_success 'create bundle 1' '
	cd "$D" &&
	echo >file updated again by origin &&
	git commit -a -m "tip" &&
	git bundle create bundle1 master^..master
'

test_expect_success 'header of bundle looks right' '
	head -n 1 "$D"/bundle1 | grep "^#" &&
	head -n 2 "$D"/bundle1 | grep "^-[0-9a-f]\{40\} " &&
	head -n 3 "$D"/bundle1 | grep "^[0-9a-f]\{40\} " &&
	head -n 4 "$D"/bundle1 | grep "^$"
'

test_expect_success 'create bundle 2' '
	cd "$D" &&
	git bundle create bundle2 master~2..master
'

test_expect_failure 'unbundle 1' '
	cd "$D/bundle" &&
	git checkout -b some-branch &&
	git fetch "$D/bundle1" master:master
'

test_expect_success 'bundle 1 has only 3 files ' '
	cd "$D" &&
	(
		while read x && test -n "$x"
		do
			:;
		done
		cat
	) <bundle1 >bundle.pack &&
	git index-pack bundle.pack &&
	verify=$(git verify-pack -v bundle.pack) &&
	test 4 = $(echo "$verify" | wc -l)
'

test_expect_success 'unbundle 2' '
	cd "$D/bundle" &&
	git fetch ../bundle2 master:master &&
	test "tip" = "$(git log -1 --pretty=oneline master | cut -b42-)"
'

test_expect_success 'bundle does not prerequisite objects' '
	cd "$D" &&
	touch file2 &&
	git add file2 &&
	git commit -m add.file2 file2 &&
	git bundle create bundle3 -1 HEAD &&
	(
		while read x && test -n "$x"
		do
			:;
		done
		cat
	) <bundle3 >bundle.pack &&
	git index-pack bundle.pack &&
	test 4 = $(git verify-pack -v bundle.pack | wc -l)
'

test_expect_success 'bundle should be able to create a full history' '

	cd "$D" &&
	git tag -a -m '1.0' v1.0 master &&
	git bundle create bundle4 v1.0

'

test_expect_success 'bundle should record HEAD correctly' '

	cd "$D" &&
	git bundle create bundle5 HEAD master &&
	git bundle list-heads bundle5 >actual &&
	for h in HEAD refs/heads/master
	do
		echo "$(git rev-parse --verify $h) $h"
	done >expect &&
	diff -u expect actual

'

test_done
