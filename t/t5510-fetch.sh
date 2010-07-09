#!/bin/sh
# Copyright (c) 2006, Junio C Hamano.

test_description='Per branch config variables affects "git fetch".

'

. ./test-lib.sh

D=`pwd`

test_bundle_object_count () {
	git verify-pack -v "$1" >verify.out &&
	test "$2" = $(grep '^[0-9a-f]\{40\} ' verify.out | wc -l)
}

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
	git clone . bundle &&
	git clone . seven
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
	test_cmp expected actual'

test_expect_success 'fetch tags when there is no tags' '

    cd "$D" &&

    mkdir notags &&
    cd notags &&
    git init &&

    git fetch -t ..

'

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

test_expect_success 'fetch must not resolve short tag name' '

	cd "$D" &&

	mkdir five &&
	cd five &&
	git init &&

	test_must_fail git fetch .. anno:five

'

test_expect_success 'fetch must not resolve short remote name' '

	cd "$D" &&
	git update-ref refs/remotes/six/HEAD HEAD

	mkdir six &&
	cd six &&
	git init &&

	test_must_fail git fetch .. six:six

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

test_expect_success 'unbundle 1' '
	cd "$D/bundle" &&
	git checkout -b some-branch &&
	test_must_fail git fetch "$D/bundle1" master:master
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
	test_bundle_object_count bundle.pack 3
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
	test_bundle_object_count bundle.pack 3
'

test_expect_success 'bundle should be able to create a full history' '

	cd "$D" &&
	git tag -a -m '1.0' v1.0 master &&
	git bundle create bundle4 v1.0

'

! rsync --help > /dev/null 2> /dev/null &&
say 'Skipping rsync tests because rsync was not found' || {
test_expect_success 'fetch via rsync' '
	git pack-refs &&
	mkdir rsynced &&
	(cd rsynced &&
	 git init --bare &&
	 git fetch "rsync:$(pwd)/../.git" master:refs/heads/master &&
	 git gc --prune &&
	 test $(git rev-parse master) = $(cd .. && git rev-parse master) &&
	 git fsck --full)
'

test_expect_success 'push via rsync' '
	mkdir rsynced2 &&
	(cd rsynced2 &&
	 git init) &&
	(cd rsynced &&
	 git push "rsync:$(pwd)/../rsynced2/.git" master) &&
	(cd rsynced2 &&
	 git gc --prune &&
	 test $(git rev-parse master) = $(cd .. && git rev-parse master) &&
	 git fsck --full)
'

test_expect_success 'push via rsync' '
	mkdir rsynced3 &&
	(cd rsynced3 &&
	 git init) &&
	git push --all "rsync:$(pwd)/rsynced3/.git" &&
	(cd rsynced3 &&
	 test $(git rev-parse master) = $(cd .. && git rev-parse master) &&
	 git fsck --full)
'
}

test_expect_success 'fetch with a non-applying branch.<name>.merge' '
	git config branch.master.remote yeti &&
	git config branch.master.merge refs/heads/bigfoot &&
	git config remote.blub.url one &&
	git config remote.blub.fetch "refs/heads/*:refs/remotes/one/*" &&
	git fetch blub
'

# the strange name is: a\!'b
test_expect_success 'quoting of a strangely named repo' '
	test_must_fail git fetch "a\\!'\''b" > result 2>&1 &&
	cat result &&
	grep "fatal: '\''a\\\\!'\''b'\''" result
'

test_expect_success 'bundle should record HEAD correctly' '

	cd "$D" &&
	git bundle create bundle5 HEAD master &&
	git bundle list-heads bundle5 >actual &&
	for h in HEAD refs/heads/master
	do
		echo "$(git rev-parse --verify $h) $h"
	done >expect &&
	test_cmp expect actual

'

test_expect_success 'explicit fetch should not update tracking' '

	cd "$D" &&
	git branch -f side &&
	(
		cd three &&
		o=$(git rev-parse --verify refs/remotes/origin/master) &&
		git fetch origin master &&
		n=$(git rev-parse --verify refs/remotes/origin/master) &&
		test "$o" = "$n" &&
		test_must_fail git rev-parse --verify refs/remotes/origin/side
	)
'

test_expect_success 'explicit pull should not update tracking' '

	cd "$D" &&
	git branch -f side &&
	(
		cd three &&
		o=$(git rev-parse --verify refs/remotes/origin/master) &&
		git pull origin master &&
		n=$(git rev-parse --verify refs/remotes/origin/master) &&
		test "$o" = "$n" &&
		test_must_fail git rev-parse --verify refs/remotes/origin/side
	)
'

test_expect_success 'configured fetch updates tracking' '

	cd "$D" &&
	git branch -f side &&
	(
		cd three &&
		o=$(git rev-parse --verify refs/remotes/origin/master) &&
		git fetch origin &&
		n=$(git rev-parse --verify refs/remotes/origin/master) &&
		test "$o" != "$n" &&
		git rev-parse --verify refs/remotes/origin/side
	)
'

test_expect_success 'pushing nonexistent branch by mistake should not segv' '

	cd "$D" &&
	test_must_fail git push seven no:no

'

test_expect_success 'auto tag following fetches minimum' '

	cd "$D" &&
	git clone .git follow &&
	git checkout HEAD^0 &&
	(
		for i in 1 2 3 4 5 6 7
		do
			echo $i >>file &&
			git commit -m $i -a &&
			git tag -a -m $i excess-$i || exit 1
		done
	) &&
	git checkout master &&
	(
		cd follow &&
		git fetch
	)
'

test_expect_success 'refuse to fetch into the current branch' '

	test_must_fail git fetch . side:master

'

test_expect_success 'fetch into the current branch with --update-head-ok' '

	git fetch --update-head-ok . side:master

'

test_expect_success 'fetch --dry-run' '

	rm -f .git/FETCH_HEAD &&
	git fetch --dry-run . &&
	! test -f .git/FETCH_HEAD
'

test_expect_success "should be able to fetch with duplicate refspecs" '
        mkdir dups &&
        cd dups &&
        git init &&
        git config branch.master.remote three &&
        git config remote.three.url ../three/.git &&
        git config remote.three.fetch +refs/heads/*:refs/remotes/origin/* &&
        git config --add remote.three.fetch +refs/heads/*:refs/remotes/origin/* &&
        git fetch three
'

test_done
