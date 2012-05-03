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

convert_bundle_to_pack () {
	while read x && test -n "$x"
	do
		:;
	done
	cat
}

test_expect_success setup '
	echo >file original &&
	git add file &&
	git commit -a -m original'

test_expect_success "clone and setup child repos" '
	git clone . one &&
	(
		cd one &&
		echo >file updated by one &&
		git commit -a -m "updated by one"
	) &&
	git clone . two &&
	(
		cd two &&
		git config branch.master.remote one &&
		git config remote.one.url ../one/.git/ &&
		git config remote.one.fetch refs/heads/master:refs/heads/one
	) &&
	git clone . three &&
	(
		cd three &&
		git config branch.master.remote two &&
		git config branch.master.merge refs/heads/one &&
		mkdir -p .git/remotes &&
		{
			echo "URL: ../two/.git/"
			echo "Pull: refs/heads/master:refs/heads/two"
			echo "Pull: refs/heads/one:refs/heads/one"
		} >.git/remotes/two
	) &&
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
		echo "$one_in_two	"
		echo "$master_in_two	not-for-merge"
	} >expected &&
	cut -f -2 .git/FETCH_HEAD >actual &&
	test_cmp expected actual'

test_expect_success 'fetch --prune on its own works as expected' '
	cd "$D" &&
	git clone . prune &&
	cd prune &&
	git fetch origin refs/heads/master:refs/remotes/origin/extrabranch &&

	git fetch --prune origin &&
	test_must_fail git rev-parse origin/extrabranch
'

test_expect_success 'fetch --prune with a branch name keeps branches' '
	cd "$D" &&
	git clone . prune-branch &&
	cd prune-branch &&
	git fetch origin refs/heads/master:refs/remotes/origin/extrabranch &&

	git fetch --prune origin master &&
	git rev-parse origin/extrabranch
'

test_expect_success 'fetch --prune with a namespace keeps other namespaces' '
	cd "$D" &&
	git clone . prune-namespace &&
	cd prune-namespace &&

	git fetch --prune origin refs/heads/a/*:refs/remotes/origin/a/* &&
	git rev-parse origin/master
'

test_expect_success 'fetch --prune --tags does not delete the remote-tracking branches' '
	cd "$D" &&
	git clone . prune-tags &&
	cd prune-tags &&
	git fetch origin refs/heads/master:refs/tags/sometag &&

	git fetch --prune --tags origin &&
	git rev-parse origin/master &&
	test_must_fail git rev-parse somebranch
'

test_expect_success 'fetch --prune --tags with branch does not delete other remote-tracking branches' '
	cd "$D" &&
	git clone . prune-tags-branch &&
	cd prune-tags-branch &&
	git fetch origin refs/heads/master:refs/remotes/origin/extrabranch &&

	git fetch --prune --tags origin master &&
	git rev-parse origin/extrabranch
'

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

test_expect_success 'fetch uses remote ref names to describe new refs' '
	cd "$D" &&
	git init descriptive &&
	(
		cd descriptive &&
		git config remote.o.url .. &&
		git config remote.o.fetch "refs/heads/*:refs/crazyheads/*" &&
		git config --add remote.o.fetch "refs/others/*:refs/heads/*" &&
		git fetch o
	) &&
	git tag -a -m "Descriptive tag" descriptive-tag &&
	git branch descriptive-branch &&
	git checkout descriptive-branch &&
	echo "Nuts" >crazy &&
	git add crazy &&
	git commit -a -m "descriptive commit" &&
	git update-ref refs/others/crazy HEAD &&
	(
		cd descriptive &&
		git fetch o 2>actual &&
		grep " -> refs/crazyheads/descriptive-branch$" actual |
		test_i18ngrep "new branch" &&
		grep " -> descriptive-tag$" actual |
		test_i18ngrep "new tag" &&
		grep " -> crazy$" actual |
		test_i18ngrep "new ref"
	) &&
	git checkout master
'

test_expect_success 'fetch must not resolve short tag name' '

	cd "$D" &&

	mkdir five &&
	cd five &&
	git init &&

	test_must_fail git fetch .. anno:five

'

test_expect_success 'fetch can now resolve short remote name' '

	cd "$D" &&
	git update-ref refs/remotes/six/HEAD HEAD &&

	mkdir six &&
	cd six &&
	git init &&

	git fetch .. six:six
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
	convert_bundle_to_pack <bundle1 >bundle.pack &&
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
	convert_bundle_to_pack <bundle3 >bundle.pack &&
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

# URL supplied to fetch does not match the url of the configured branch's remote
test_expect_success 'fetch from GIT URL with a non-applying branch.<name>.merge [1]' '
	one_head=$(cd one && git rev-parse HEAD) &&
	this_head=$(git rev-parse HEAD) &&
	git update-ref -d FETCH_HEAD &&
	git fetch one &&
	test $one_head = "$(git rev-parse --verify FETCH_HEAD)" &&
	test $this_head = "$(git rev-parse --verify HEAD)"
'

# URL supplied to fetch matches the url of the configured branch's remote and
# the merge spec matches the branch the remote HEAD points to
test_expect_success 'fetch from GIT URL with a non-applying branch.<name>.merge [2]' '
	one_ref=$(cd one && git symbolic-ref HEAD) &&
	git config branch.master.remote blub &&
	git config branch.master.merge "$one_ref" &&
	git update-ref -d FETCH_HEAD &&
	git fetch one &&
	test $one_head = "$(git rev-parse --verify FETCH_HEAD)" &&
	test $this_head = "$(git rev-parse --verify HEAD)"
'

# URL supplied to fetch matches the url of the configured branch's remote, but
# the merge spec does not match the branch the remote HEAD points to
test_expect_success 'fetch from GIT URL with a non-applying branch.<name>.merge [3]' '
	git config branch.master.merge "${one_ref}_not" &&
	git update-ref -d FETCH_HEAD &&
	git fetch one &&
	test $one_head = "$(git rev-parse --verify FETCH_HEAD)" &&
	test $this_head = "$(git rev-parse --verify HEAD)"
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
	(
		cd dups &&
		git init &&
		git config branch.master.remote three &&
		git config remote.three.url ../three/.git &&
		git config remote.three.fetch +refs/heads/*:refs/remotes/origin/* &&
		git config --add remote.three.fetch +refs/heads/*:refs/remotes/origin/* &&
		git fetch three
	)
'

test_expect_success 'all boundary commits are excluded' '
	test_commit base &&
	test_commit oneside &&
	git checkout HEAD^ &&
	test_commit otherside &&
	git checkout master &&
	test_tick &&
	git merge otherside &&
	ad=$(git log --no-walk --format=%ad HEAD) &&
	git bundle create twoside-boundary.bdl master --since="$ad" &&
	convert_bundle_to_pack <twoside-boundary.bdl >twoside-boundary.pack &&
	pack=$(git index-pack --fix-thin --stdin <twoside-boundary.pack) &&
	test_bundle_object_count .git/objects/pack/pack-${pack##pack	}.pack 3
'

test_done
