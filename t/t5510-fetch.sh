#!/bin/sh
# Copyright (c) 2006, Junio C Hamano.

test_description='Per branch config variables affects "git fetch".

'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-bundle.sh

D=$(pwd)

test_expect_success setup '
	echo >file original &&
	git add file &&
	git commit -a -m original &&
	git branch -M main
'

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
		git config branch.main.remote one &&
		git config remote.one.url ../one/.git/ &&
		git config remote.one.fetch refs/heads/main:refs/heads/one
	) &&
	git clone . three &&
	(
		cd three &&
		git config branch.main.remote two &&
		git config branch.main.merge refs/heads/one &&
		mkdir -p .git/remotes &&
		cat >.git/remotes/two <<-\EOF
		URL: ../two/.git/
		Pull: refs/heads/main:refs/heads/two
		Pull: refs/heads/one:refs/heads/one
		EOF
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
	git rev-parse --verify refs/heads/one &&
	mine=$(git rev-parse refs/heads/one) &&
	his=$(cd ../one && git rev-parse refs/heads/main) &&
	test "z$mine" = "z$his"
'

test_expect_success "fetch test for-merge" '
	cd "$D" &&
	cd three &&
	git fetch &&
	git rev-parse --verify refs/heads/two &&
	git rev-parse --verify refs/heads/one &&
	main_in_two=$(cd ../two && git rev-parse main) &&
	one_in_two=$(cd ../two && git rev-parse one) &&
	{
		echo "$one_in_two	" &&
		echo "$main_in_two	not-for-merge"
	} >expected &&
	cut -f -2 .git/FETCH_HEAD >actual &&
	test_cmp expected actual'

test_expect_success 'fetch --prune on its own works as expected' '
	cd "$D" &&
	git clone . prune &&
	cd prune &&
	git update-ref refs/remotes/origin/extrabranch main &&

	git fetch --prune origin &&
	test_must_fail git rev-parse origin/extrabranch
'

test_expect_success 'fetch --prune with a branch name keeps branches' '
	cd "$D" &&
	git clone . prune-branch &&
	cd prune-branch &&
	git update-ref refs/remotes/origin/extrabranch main &&

	git fetch --prune origin main &&
	git rev-parse origin/extrabranch
'

test_expect_success 'fetch --prune with a namespace keeps other namespaces' '
	cd "$D" &&
	git clone . prune-namespace &&
	cd prune-namespace &&

	git fetch --prune origin refs/heads/a/*:refs/remotes/origin/a/* &&
	git rev-parse origin/main
'

test_expect_success 'fetch --prune handles overlapping refspecs' '
	cd "$D" &&
	git update-ref refs/pull/42/head main &&
	git clone . prune-overlapping &&
	cd prune-overlapping &&
	git config --add remote.origin.fetch refs/pull/*/head:refs/remotes/origin/pr/* &&

	git fetch --prune origin &&
	git rev-parse origin/main &&
	git rev-parse origin/pr/42 &&

	git config --unset-all remote.origin.fetch &&
	git config remote.origin.fetch refs/pull/*/head:refs/remotes/origin/pr/* &&
	git config --add remote.origin.fetch refs/heads/*:refs/remotes/origin/* &&

	git fetch --prune origin &&
	git rev-parse origin/main &&
	git rev-parse origin/pr/42
'

test_expect_success 'fetch --prune --tags prunes branches but not tags' '
	cd "$D" &&
	git clone . prune-tags &&
	cd prune-tags &&
	git tag sometag main &&
	# Create what looks like a remote-tracking branch from an earlier
	# fetch that has since been deleted from the remote:
	git update-ref refs/remotes/origin/fake-remote main &&

	git fetch --prune --tags origin &&
	git rev-parse origin/main &&
	test_must_fail git rev-parse origin/fake-remote &&
	git rev-parse sometag
'

test_expect_success 'fetch --prune --tags with branch does not prune other things' '
	cd "$D" &&
	git clone . prune-tags-branch &&
	cd prune-tags-branch &&
	git tag sometag main &&
	git update-ref refs/remotes/origin/extrabranch main &&

	git fetch --prune --tags origin main &&
	git rev-parse origin/extrabranch &&
	git rev-parse sometag
'

test_expect_success 'fetch --prune --tags with refspec prunes based on refspec' '
	cd "$D" &&
	git clone . prune-tags-refspec &&
	cd prune-tags-refspec &&
	git tag sometag main &&
	git update-ref refs/remotes/origin/foo/otherbranch main &&
	git update-ref refs/remotes/origin/extrabranch main &&

	git fetch --prune --tags origin refs/heads/foo/*:refs/remotes/origin/foo/* &&
	test_must_fail git rev-parse refs/remotes/origin/foo/otherbranch &&
	git rev-parse origin/extrabranch &&
	git rev-parse sometag
'

test_expect_success REFFILES 'fetch --prune fails to delete branches' '
	cd "$D" &&
	git clone . prune-fail &&
	cd prune-fail &&
	git update-ref refs/remotes/origin/extrabranch main &&
	: this will prevent --prune from locking packed-refs for deleting refs, but adding loose refs still succeeds  &&
	>.git/packed-refs.new &&

	test_must_fail git fetch --prune origin
'

test_expect_success 'fetch --atomic works with a single branch' '
	test_when_finished "rm -rf \"$D\"/atomic" &&

	cd "$D" &&
	git clone . atomic &&
	git branch atomic-branch &&
	oid=$(git rev-parse atomic-branch) &&
	echo "$oid" >expected &&

	git -C atomic fetch --atomic origin &&
	git -C atomic rev-parse origin/atomic-branch >actual &&
	test_cmp expected actual &&
	test $oid = "$(git -C atomic rev-parse --verify FETCH_HEAD)"
'

test_expect_success 'fetch --atomic works with multiple branches' '
	test_when_finished "rm -rf \"$D\"/atomic" &&

	cd "$D" &&
	git clone . atomic &&
	git branch atomic-branch-1 &&
	git branch atomic-branch-2 &&
	git branch atomic-branch-3 &&
	git rev-parse refs/heads/atomic-branch-1 refs/heads/atomic-branch-2 refs/heads/atomic-branch-3 >actual &&

	git -C atomic fetch --atomic origin &&
	git -C atomic rev-parse refs/remotes/origin/atomic-branch-1 refs/remotes/origin/atomic-branch-2 refs/remotes/origin/atomic-branch-3 >expected &&
	test_cmp expected actual
'

test_expect_success 'fetch --atomic works with mixed branches and tags' '
	test_when_finished "rm -rf \"$D\"/atomic" &&

	cd "$D" &&
	git clone . atomic &&
	git branch atomic-mixed-branch &&
	git tag atomic-mixed-tag &&
	git rev-parse refs/heads/atomic-mixed-branch refs/tags/atomic-mixed-tag >actual &&

	git -C atomic fetch --tags --atomic origin &&
	git -C atomic rev-parse refs/remotes/origin/atomic-mixed-branch refs/tags/atomic-mixed-tag >expected &&
	test_cmp expected actual
'

test_expect_success 'fetch --atomic prunes references' '
	test_when_finished "rm -rf \"$D\"/atomic" &&

	cd "$D" &&
	git branch atomic-prune-delete &&
	git clone . atomic &&
	git branch --delete atomic-prune-delete &&
	git branch atomic-prune-create &&
	git rev-parse refs/heads/atomic-prune-create >actual &&

	git -C atomic fetch --prune --atomic origin &&
	test_must_fail git -C atomic rev-parse refs/remotes/origin/atomic-prune-delete &&
	git -C atomic rev-parse refs/remotes/origin/atomic-prune-create >expected &&
	test_cmp expected actual
'

test_expect_success 'fetch --atomic aborts with non-fast-forward update' '
	test_when_finished "rm -rf \"$D\"/atomic" &&

	cd "$D" &&
	git branch atomic-non-ff &&
	git clone . atomic &&
	git rev-parse HEAD >actual &&

	git branch atomic-new-branch &&
	parent_commit=$(git rev-parse atomic-non-ff~) &&
	git update-ref refs/heads/atomic-non-ff $parent_commit &&

	test_must_fail git -C atomic fetch --atomic origin refs/heads/*:refs/remotes/origin/* &&
	test_must_fail git -C atomic rev-parse refs/remotes/origin/atomic-new-branch &&
	git -C atomic rev-parse refs/remotes/origin/atomic-non-ff >expected &&
	test_cmp expected actual &&
	test_must_be_empty atomic/.git/FETCH_HEAD
'

test_expect_success 'fetch --atomic executes a single reference transaction only' '
	test_when_finished "rm -rf \"$D\"/atomic" &&

	cd "$D" &&
	git clone . atomic &&
	git branch atomic-hooks-1 &&
	git branch atomic-hooks-2 &&
	head_oid=$(git rev-parse HEAD) &&

	cat >expected <<-EOF &&
		prepared
		$ZERO_OID $head_oid refs/remotes/origin/atomic-hooks-1
		$ZERO_OID $head_oid refs/remotes/origin/atomic-hooks-2
		committed
		$ZERO_OID $head_oid refs/remotes/origin/atomic-hooks-1
		$ZERO_OID $head_oid refs/remotes/origin/atomic-hooks-2
	EOF

	rm -f atomic/actual &&
	test_hook -C atomic reference-transaction <<-\EOF &&
		( echo "$*" && cat ) >>actual
	EOF

	git -C atomic fetch --atomic origin &&
	test_cmp expected atomic/actual
'

test_expect_success 'fetch --atomic aborts all reference updates if hook aborts' '
	test_when_finished "rm -rf \"$D\"/atomic" &&

	cd "$D" &&
	git clone . atomic &&
	git branch atomic-hooks-abort-1 &&
	git branch atomic-hooks-abort-2 &&
	git branch atomic-hooks-abort-3 &&
	git tag atomic-hooks-abort &&
	head_oid=$(git rev-parse HEAD) &&

	cat >expected <<-EOF &&
		prepared
		$ZERO_OID $head_oid refs/remotes/origin/atomic-hooks-abort-1
		$ZERO_OID $head_oid refs/remotes/origin/atomic-hooks-abort-2
		$ZERO_OID $head_oid refs/remotes/origin/atomic-hooks-abort-3
		$ZERO_OID $head_oid refs/tags/atomic-hooks-abort
		aborted
		$ZERO_OID $head_oid refs/remotes/origin/atomic-hooks-abort-1
		$ZERO_OID $head_oid refs/remotes/origin/atomic-hooks-abort-2
		$ZERO_OID $head_oid refs/remotes/origin/atomic-hooks-abort-3
		$ZERO_OID $head_oid refs/tags/atomic-hooks-abort
	EOF

	rm -f atomic/actual &&
	test_hook -C atomic/.git reference-transaction <<-\EOF &&
		( echo "$*" && cat ) >>actual
		exit 1
	EOF

	git -C atomic for-each-ref >expected-refs &&
	test_must_fail git -C atomic fetch --tags --atomic origin &&
	git -C atomic for-each-ref >actual-refs &&
	test_cmp expected-refs actual-refs &&
	test_must_be_empty atomic/.git/FETCH_HEAD
'

test_expect_success 'fetch --atomic --append appends to FETCH_HEAD' '
	test_when_finished "rm -rf \"$D\"/atomic" &&

	cd "$D" &&
	git clone . atomic &&
	oid=$(git rev-parse HEAD) &&

	git branch atomic-fetch-head-1 &&
	git -C atomic fetch --atomic origin atomic-fetch-head-1 &&
	test_line_count = 1 atomic/.git/FETCH_HEAD &&

	git branch atomic-fetch-head-2 &&
	git -C atomic fetch --atomic --append origin atomic-fetch-head-2 &&
	test_line_count = 2 atomic/.git/FETCH_HEAD &&
	cp atomic/.git/FETCH_HEAD expected &&

	test_hook -C atomic reference-transaction <<-\EOF &&
		exit 1
	EOF

	git branch atomic-fetch-head-3 &&
	test_must_fail git -C atomic fetch --atomic --append origin atomic-fetch-head-3 &&
	test_cmp expected atomic/.git/FETCH_HEAD
'

test_expect_success '--refmap="" ignores configured refspec' '
	cd "$TRASH_DIRECTORY" &&
	git clone "$D" remote-refs &&
	git -C remote-refs rev-parse remotes/origin/main >old &&
	git -C remote-refs update-ref refs/remotes/origin/main main~1 &&
	git -C remote-refs rev-parse remotes/origin/main >new &&
	git -C remote-refs fetch --refmap= origin "+refs/heads/*:refs/hidden/origin/*" &&
	git -C remote-refs rev-parse remotes/origin/main >actual &&
	test_cmp new actual &&
	git -C remote-refs fetch origin &&
	git -C remote-refs rev-parse remotes/origin/main >actual &&
	test_cmp old actual
'

test_expect_success '--refmap="" and --prune' '
	git -C remote-refs update-ref refs/remotes/origin/foo/otherbranch main &&
	git -C remote-refs update-ref refs/hidden/foo/otherbranch main &&
	git -C remote-refs fetch --prune --refmap="" origin +refs/heads/*:refs/hidden/* &&
	git -C remote-refs rev-parse remotes/origin/foo/otherbranch &&
	test_must_fail git -C remote-refs rev-parse refs/hidden/foo/otherbranch &&
	git -C remote-refs fetch --prune origin &&
	test_must_fail git -C remote-refs rev-parse remotes/origin/foo/otherbranch
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
	git tag -a -m "annotated" anno HEAD &&
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
		test_i18ngrep "new branch.* -> refs/crazyheads/descriptive-branch$" actual &&
		test_i18ngrep "new tag.* -> descriptive-tag$" actual &&
		test_i18ngrep "new ref.* -> crazy$" actual
	) &&
	git checkout main
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
	git bundle create --version=3 bundle1 main^..main
'

test_expect_success 'header of bundle looks right' '
	cat >expect <<-EOF &&
	# v3 git bundle
	@object-format=$(test_oid algo)
	-OID updated by origin
	OID refs/heads/main

	EOF
	sed -e "s/$OID_REGEX/OID/g" -e "5q" "$D"/bundle1 >actual &&
	test_cmp expect actual
'

test_expect_success 'create bundle 2' '
	cd "$D" &&
	git bundle create bundle2 main~2..main
'

test_expect_success 'unbundle 1' '
	cd "$D/bundle" &&
	git checkout -b some-branch &&
	test_must_fail git fetch "$D/bundle1" main:main
'


test_expect_success 'bundle 1 has only 3 files ' '
	cd "$D" &&
	test_bundle_object_count bundle1 3
'

test_expect_success 'unbundle 2' '
	cd "$D/bundle" &&
	git fetch ../bundle2 main:main &&
	test "tip" = "$(git log -1 --pretty=oneline main | cut -d" " -f2)"
'

test_expect_success 'bundle does not prerequisite objects' '
	cd "$D" &&
	touch file2 &&
	git add file2 &&
	git commit -m add.file2 file2 &&
	git bundle create bundle3 -1 HEAD &&
	test_bundle_object_count bundle3 3
'

test_expect_success 'bundle should be able to create a full history' '

	cd "$D" &&
	git tag -a -m "1.0" v1.0 main &&
	git bundle create bundle4 v1.0

'

test_expect_success 'fetch with a non-applying branch.<name>.merge' '
	git config branch.main.remote yeti &&
	git config branch.main.merge refs/heads/bigfoot &&
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
	git config branch.main.remote blub &&
	git config branch.main.merge "$one_ref" &&
	git update-ref -d FETCH_HEAD &&
	git fetch one &&
	test $one_head = "$(git rev-parse --verify FETCH_HEAD)" &&
	test $this_head = "$(git rev-parse --verify HEAD)"
'

# URL supplied to fetch matches the url of the configured branch's remote, but
# the merge spec does not match the branch the remote HEAD points to
test_expect_success 'fetch from GIT URL with a non-applying branch.<name>.merge [3]' '
	git config branch.main.merge "${one_ref}_not" &&
	git update-ref -d FETCH_HEAD &&
	git fetch one &&
	test $one_head = "$(git rev-parse --verify FETCH_HEAD)" &&
	test $this_head = "$(git rev-parse --verify HEAD)"
'

# the strange name is: a\!'b
test_expect_success 'quoting of a strangely named repo' '
	test_must_fail git fetch "a\\!'\''b" > result 2>&1 &&
	grep "fatal: '\''a\\\\!'\''b'\''" result
'

test_expect_success 'bundle should record HEAD correctly' '

	cd "$D" &&
	git bundle create bundle5 HEAD main &&
	git bundle list-heads bundle5 >actual &&
	for h in HEAD refs/heads/main
	do
		echo "$(git rev-parse --verify $h) $h" || return 1
	done >expect &&
	test_cmp expect actual

'

test_expect_success 'mark initial state of origin/main' '
	(
		cd three &&
		git tag base-origin-main refs/remotes/origin/main
	)
'

test_expect_success 'explicit fetch should update tracking' '

	cd "$D" &&
	git branch -f side &&
	(
		cd three &&
		git update-ref refs/remotes/origin/main base-origin-main &&
		o=$(git rev-parse --verify refs/remotes/origin/main) &&
		git fetch origin main &&
		n=$(git rev-parse --verify refs/remotes/origin/main) &&
		test "$o" != "$n" &&
		test_must_fail git rev-parse --verify refs/remotes/origin/side
	)
'

test_expect_success 'explicit pull should update tracking' '

	cd "$D" &&
	git branch -f side &&
	(
		cd three &&
		git update-ref refs/remotes/origin/main base-origin-main &&
		o=$(git rev-parse --verify refs/remotes/origin/main) &&
		git pull origin main &&
		n=$(git rev-parse --verify refs/remotes/origin/main) &&
		test "$o" != "$n" &&
		test_must_fail git rev-parse --verify refs/remotes/origin/side
	)
'

test_expect_success 'explicit --refmap is allowed only with command-line refspec' '
	cd "$D" &&
	(
		cd three &&
		test_must_fail git fetch --refmap="*:refs/remotes/none/*"
	)
'

test_expect_success 'explicit --refmap option overrides remote.*.fetch' '
	cd "$D" &&
	git branch -f side &&
	(
		cd three &&
		git update-ref refs/remotes/origin/main base-origin-main &&
		o=$(git rev-parse --verify refs/remotes/origin/main) &&
		git fetch --refmap="refs/heads/*:refs/remotes/other/*" origin main &&
		n=$(git rev-parse --verify refs/remotes/origin/main) &&
		test "$o" = "$n" &&
		test_must_fail git rev-parse --verify refs/remotes/origin/side &&
		git rev-parse --verify refs/remotes/other/main
	)
'

test_expect_success 'explicitly empty --refmap option disables remote.*.fetch' '
	cd "$D" &&
	git branch -f side &&
	(
		cd three &&
		git update-ref refs/remotes/origin/main base-origin-main &&
		o=$(git rev-parse --verify refs/remotes/origin/main) &&
		git fetch --refmap="" origin main &&
		n=$(git rev-parse --verify refs/remotes/origin/main) &&
		test "$o" = "$n" &&
		test_must_fail git rev-parse --verify refs/remotes/origin/side
	)
'

test_expect_success 'configured fetch updates tracking' '

	cd "$D" &&
	git branch -f side &&
	(
		cd three &&
		git update-ref refs/remotes/origin/main base-origin-main &&
		o=$(git rev-parse --verify refs/remotes/origin/main) &&
		git fetch origin &&
		n=$(git rev-parse --verify refs/remotes/origin/main) &&
		test "$o" != "$n" &&
		git rev-parse --verify refs/remotes/origin/side
	)
'

test_expect_success 'non-matching refspecs do not confuse tracking update' '
	cd "$D" &&
	git update-ref refs/odd/location HEAD &&
	(
		cd three &&
		git update-ref refs/remotes/origin/main base-origin-main &&
		git config --add remote.origin.fetch \
			refs/odd/location:refs/remotes/origin/odd &&
		o=$(git rev-parse --verify refs/remotes/origin/main) &&
		git fetch origin main &&
		n=$(git rev-parse --verify refs/remotes/origin/main) &&
		test "$o" != "$n" &&
		test_must_fail git rev-parse --verify refs/remotes/origin/odd
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
	git checkout main &&
	(
		cd follow &&
		git fetch
	)
'

test_expect_success 'refuse to fetch into the current branch' '

	test_must_fail git fetch . side:main

'

test_expect_success 'fetch into the current branch with --update-head-ok' '

	git fetch --update-head-ok . side:main

'

test_expect_success 'fetch --dry-run does not touch FETCH_HEAD, but still prints what would be written' '
	rm -f .git/FETCH_HEAD err &&
	git fetch --dry-run . 2>err &&
	! test -f .git/FETCH_HEAD &&
	grep FETCH_HEAD err
'

test_expect_success '--no-write-fetch-head does not touch FETCH_HEAD, and does not print what would be written' '
	rm -f .git/FETCH_HEAD err &&
	git fetch --no-write-fetch-head . 2>err &&
	! test -f .git/FETCH_HEAD &&
	! grep FETCH_HEAD err
'

test_expect_success '--write-fetch-head gets defeated by --dry-run' '
	rm -f .git/FETCH_HEAD &&
	git fetch --dry-run --write-fetch-head . &&
	! test -f .git/FETCH_HEAD
'

test_expect_success "should be able to fetch with duplicate refspecs" '
	mkdir dups &&
	(
		cd dups &&
		git init &&
		git config branch.main.remote three &&
		git config remote.three.url ../three/.git &&
		git config remote.three.fetch +refs/heads/*:refs/remotes/origin/* &&
		git config --add remote.three.fetch +refs/heads/*:refs/remotes/origin/* &&
		git fetch three
	)
'

test_expect_success 'LHS of refspec follows ref disambiguation rules' '
	mkdir lhs-ambiguous &&
	(
		cd lhs-ambiguous &&
		git init server &&
		test_commit -C server unwanted &&
		test_commit -C server wanted &&

		git init client &&

		# Check a name coming after "refs" alphabetically ...
		git -C server update-ref refs/heads/s wanted &&
		git -C server update-ref refs/heads/refs/heads/s unwanted &&
		git -C client fetch ../server +refs/heads/s:refs/heads/checkthis &&
		git -C server rev-parse wanted >expect &&
		git -C client rev-parse checkthis >actual &&
		test_cmp expect actual &&

		# ... and one before.
		git -C server update-ref refs/heads/q wanted &&
		git -C server update-ref refs/heads/refs/heads/q unwanted &&
		git -C client fetch ../server +refs/heads/q:refs/heads/checkthis &&
		git -C server rev-parse wanted >expect &&
		git -C client rev-parse checkthis >actual &&
		test_cmp expect actual &&

		# Tags are preferred over branches like refs/{heads,tags}/*
		git -C server update-ref refs/tags/t wanted &&
		git -C server update-ref refs/heads/t unwanted &&
		git -C client fetch ../server +t:refs/heads/checkthis &&
		git -C server rev-parse wanted >expect &&
		git -C client rev-parse checkthis >actual
	)
'

test_expect_success 'fetch.writeCommitGraph' '
	git clone three write &&
	(
		cd three &&
		test_commit new
	) &&
	(
		cd write &&
		git -c fetch.writeCommitGraph fetch origin &&
		test_path_is_file .git/objects/info/commit-graphs/commit-graph-chain
	)
'

test_expect_success 'fetch.writeCommitGraph with submodules' '
	test_config_global protocol.file.allow always &&
	git clone dups super &&
	(
		cd super &&
		git submodule add "file://$TRASH_DIRECTORY/three" &&
		git commit -m "add submodule"
	) &&
	git clone "super" super-clone &&
	(
		cd super-clone &&
		rm -rf .git/objects/info &&
		git -c fetch.writeCommitGraph=true fetch origin &&
		test_path_is_file .git/objects/info/commit-graphs/commit-graph-chain
	)
'

# fetches from first configured url
test_expect_success 'fetch from multiple configured URLs in single remote' '
	git init url1 &&
	git remote add multipleurls url1 &&
	git remote set-url --add multipleurls url2 &&
	git fetch multipleurls
'

# configured prune tests

set_config_tristate () {
	# var=$1 val=$2
	case "$2" in
	unset)
		test_unconfig "$1"
		;;
	*)
		git config "$1" "$2"
		key=$(echo $1 | sed -e 's/^remote\.origin/fetch/')
		git_fetch_c="$git_fetch_c -c $key=$2"
		;;
	esac
}

test_configured_prune () {
	test_configured_prune_type "$@" "name"
	test_configured_prune_type "$@" "link"
}

test_configured_prune_type () {
	fetch_prune=$1
	remote_origin_prune=$2
	fetch_prune_tags=$3
	remote_origin_prune_tags=$4
	expected_branch=$5
	expected_tag=$6
	cmdline=$7
	mode=$8

	if test -z "$cmdline_setup"
	then
		test_expect_success 'setup cmdline_setup variable for subsequent test' '
			remote_url="file://$(git -C one config remote.origin.url)" &&
			remote_fetch="$(git -C one config remote.origin.fetch)" &&
			cmdline_setup="\"$remote_url\" \"$remote_fetch\""
		'
	fi

	if test "$mode" = 'link'
	then
		new_cmdline=""

		if test "$cmdline" = ""
		then
			new_cmdline=$cmdline_setup
		else
			new_cmdline=$(perl -e '
				my ($cmdline, $url) = @ARGV;
				$cmdline =~ s[origin(?!/)][quotemeta($url)]ge;
				print $cmdline;
			' -- "$cmdline" "$remote_url")
		fi

		if test "$fetch_prune_tags" = 'true' ||
		   test "$remote_origin_prune_tags" = 'true'
		then
			if ! printf '%s' "$cmdline\n" | grep -q refs/remotes/origin/
			then
				new_cmdline="$new_cmdline refs/tags/*:refs/tags/*"
			fi
		fi

		cmdline="$new_cmdline"
	fi

	test_expect_success "$mode prune fetch.prune=$1 remote.origin.prune=$2 fetch.pruneTags=$3 remote.origin.pruneTags=$4${7:+ $7}; branch:$5 tag:$6" '
		# make sure a newbranch is there in . and also in one
		git branch -f newbranch &&
		git tag -f newtag &&
		(
			cd one &&
			test_unconfig fetch.prune &&
			test_unconfig fetch.pruneTags &&
			test_unconfig remote.origin.prune &&
			test_unconfig remote.origin.pruneTags &&
			git fetch '"$cmdline_setup"' &&
			git rev-parse --verify refs/remotes/origin/newbranch &&
			git rev-parse --verify refs/tags/newtag
		) &&

		# now remove them
		git branch -d newbranch &&
		git tag -d newtag &&

		# then test
		(
			cd one &&
			git_fetch_c="" &&
			set_config_tristate fetch.prune $fetch_prune &&
			set_config_tristate fetch.pruneTags $fetch_prune_tags &&
			set_config_tristate remote.origin.prune $remote_origin_prune &&
			set_config_tristate remote.origin.pruneTags $remote_origin_prune_tags &&

			if test "$mode" != "link"
			then
				git_fetch_c=""
			fi &&
			git$git_fetch_c fetch '"$cmdline"' &&
			case "$expected_branch" in
			pruned)
				test_must_fail git rev-parse --verify refs/remotes/origin/newbranch
				;;
			kept)
				git rev-parse --verify refs/remotes/origin/newbranch
				;;
			esac &&
			case "$expected_tag" in
			pruned)
				test_must_fail git rev-parse --verify refs/tags/newtag
				;;
			kept)
				git rev-parse --verify refs/tags/newtag
				;;
			esac
		)
	'
}

# $1 config: fetch.prune
# $2 config: remote.<name>.prune
# $3 config: fetch.pruneTags
# $4 config: remote.<name>.pruneTags
# $5 expect: branch to be pruned?
# $6 expect: tag to be pruned?
# $7 git-fetch $cmdline:
#
#                     $1    $2    $3    $4    $5     $6     $7
test_configured_prune unset unset unset unset kept   kept   ""
test_configured_prune unset unset unset unset kept   kept   "--no-prune"
test_configured_prune unset unset unset unset pruned kept   "--prune"
test_configured_prune unset unset unset unset kept   pruned \
	"--prune origin refs/tags/*:refs/tags/*"
test_configured_prune unset unset unset unset pruned pruned \
	"--prune origin refs/tags/*:refs/tags/* +refs/heads/*:refs/remotes/origin/*"

test_configured_prune false unset unset unset kept   kept   ""
test_configured_prune false unset unset unset kept   kept   "--no-prune"
test_configured_prune false unset unset unset pruned kept   "--prune"

test_configured_prune true  unset unset unset pruned kept   ""
test_configured_prune true  unset unset unset pruned kept   "--prune"
test_configured_prune true  unset unset unset kept   kept   "--no-prune"

test_configured_prune unset false unset unset kept   kept   ""
test_configured_prune unset false unset unset kept   kept   "--no-prune"
test_configured_prune unset false unset unset pruned kept   "--prune"

test_configured_prune false false unset unset kept   kept   ""
test_configured_prune false false unset unset kept   kept   "--no-prune"
test_configured_prune false false unset unset pruned kept   "--prune"
test_configured_prune false false unset unset kept   pruned \
	"--prune origin refs/tags/*:refs/tags/*"
test_configured_prune false false unset unset pruned pruned \
	"--prune origin refs/tags/*:refs/tags/* +refs/heads/*:refs/remotes/origin/*"

test_configured_prune true  false unset unset kept   kept   ""
test_configured_prune true  false unset unset pruned kept   "--prune"
test_configured_prune true  false unset unset kept   kept   "--no-prune"

test_configured_prune unset true  unset unset pruned kept   ""
test_configured_prune unset true  unset unset kept   kept   "--no-prune"
test_configured_prune unset true  unset unset pruned kept   "--prune"

test_configured_prune false true  unset unset pruned kept   ""
test_configured_prune false true  unset unset kept   kept   "--no-prune"
test_configured_prune false true  unset unset pruned kept   "--prune"

test_configured_prune true  true  unset unset pruned kept   ""
test_configured_prune true  true  unset unset pruned kept   "--prune"
test_configured_prune true  true  unset unset kept   kept   "--no-prune"
test_configured_prune true  true  unset unset kept   pruned \
	"--prune origin refs/tags/*:refs/tags/*"
test_configured_prune true  true  unset unset pruned pruned \
	"--prune origin refs/tags/*:refs/tags/* +refs/heads/*:refs/remotes/origin/*"

# --prune-tags on its own does nothing, needs --prune as well, same
# for fetch.pruneTags without fetch.prune
test_configured_prune unset unset unset unset kept kept     "--prune-tags"
test_configured_prune unset unset true unset  kept kept     ""
test_configured_prune unset unset unset true  kept kept     ""

# These will prune the tags
test_configured_prune unset unset unset unset pruned pruned "--prune --prune-tags"
test_configured_prune true  unset true  unset pruned pruned ""
test_configured_prune unset true  unset true  pruned pruned ""

# remote.<name>.pruneTags overrides fetch.pruneTags, just like
# remote.<name>.prune overrides fetch.prune if set.
test_configured_prune true  unset true unset pruned pruned  ""
test_configured_prune false true  false true  pruned pruned ""
test_configured_prune true  false true  false kept   kept   ""

# When --prune-tags is supplied it's ignored if an explicit refspec is
# given, same for the configuration options.
test_configured_prune unset unset unset unset pruned kept \
	"--prune --prune-tags origin +refs/heads/*:refs/remotes/origin/*"
test_configured_prune unset unset true  unset pruned kept \
	"--prune origin +refs/heads/*:refs/remotes/origin/*"
test_configured_prune unset unset unset true pruned  kept \
	"--prune origin +refs/heads/*:refs/remotes/origin/*"

# Pruning that also takes place if a file:// url replaces a named
# remote. However, because there's no implicit
# +refs/heads/*:refs/remotes/origin/* refspec and supplying it on the
# command-line negates --prune-tags, the branches will not be pruned.
test_configured_prune_type unset unset unset unset kept   kept   "origin --prune-tags" "name"
test_configured_prune_type unset unset unset unset kept   kept   "origin --prune-tags" "link"
test_configured_prune_type unset unset unset unset pruned pruned "origin --prune --prune-tags" "name"
test_configured_prune_type unset unset unset unset kept   pruned "origin --prune --prune-tags" "link"
test_configured_prune_type unset unset unset unset pruned pruned "--prune --prune-tags origin" "name"
test_configured_prune_type unset unset unset unset kept   pruned "--prune --prune-tags origin" "link"
test_configured_prune_type unset unset true  unset pruned pruned "--prune origin" "name"
test_configured_prune_type unset unset true  unset kept   pruned "--prune origin" "link"
test_configured_prune_type unset unset unset true  pruned pruned "--prune origin" "name"
test_configured_prune_type unset unset unset true  kept   pruned "--prune origin" "link"
test_configured_prune_type true  unset true  unset pruned pruned "origin" "name"
test_configured_prune_type true  unset true  unset kept   pruned "origin" "link"
test_configured_prune_type unset  true true  unset pruned pruned "origin" "name"
test_configured_prune_type unset  true true  unset kept   pruned "origin" "link"
test_configured_prune_type unset  true unset true  pruned pruned "origin" "name"
test_configured_prune_type unset  true unset true  kept   pruned "origin" "link"

# When all remote.origin.fetch settings are deleted a --prune
# --prune-tags still implicitly supplies refs/tags/*:refs/tags/* so
# tags, but not tracking branches, will be deleted.
test_expect_success 'remove remote.origin.fetch "one"' '
	(
		cd one &&
		git config --unset-all remote.origin.fetch
	)
'
test_configured_prune_type unset unset unset unset kept pruned "origin --prune --prune-tags" "name"
test_configured_prune_type unset unset unset unset kept pruned "origin --prune --prune-tags" "link"

test_expect_success 'all boundary commits are excluded' '
	test_commit base &&
	test_commit oneside &&
	git checkout HEAD^ &&
	test_commit otherside &&
	git checkout main &&
	test_tick &&
	git merge otherside &&
	ad=$(git log --no-walk --format=%ad HEAD) &&
	git bundle create twoside-boundary.bdl main --since="$ad" &&
	test_bundle_object_count --thin twoside-boundary.bdl 3
'

test_expect_success 'fetch --prune prints the remotes url' '
	git branch goodbye &&
	git clone . only-prunes &&
	git branch -D goodbye &&
	(
		cd only-prunes &&
		git fetch --prune origin 2>&1 | head -n1 >../actual
	) &&
	echo "From ${D}/." >expect &&
	test_cmp expect actual
'

test_expect_success 'branchname D/F conflict resolved by --prune' '
	git branch dir/file &&
	git clone . prune-df-conflict &&
	git branch -D dir/file &&
	git branch dir &&
	(
		cd prune-df-conflict &&
		git fetch --prune &&
		git rev-parse origin/dir >../actual
	) &&
	git rev-parse dir >expect &&
	test_cmp expect actual
'

test_expect_success 'fetching a one-level ref works' '
	test_commit extra &&
	git reset --hard HEAD^ &&
	git update-ref refs/foo extra &&
	git init one-level &&
	(
		cd one-level &&
		git fetch .. HEAD refs/foo
	)
'

test_expect_success 'fetching with auto-gc does not lock up' '
	write_script askyesno <<-\EOF &&
	echo "$*" &&
	false
	EOF
	git clone "file://$D" auto-gc &&
	test_commit test2 &&
	(
		cd auto-gc &&
		git config fetch.unpackLimit 1 &&
		git config gc.autoPackLimit 1 &&
		git config gc.autoDetach false &&
		GIT_ASK_YESNO="$D/askyesno" git fetch --verbose >fetch.out 2>&1 &&
		test_i18ngrep "Auto packing the repository" fetch.out &&
		! grep "Should I try again" fetch.out
	)
'

for section in fetch transfer
do
	test_expect_success "$section.hideRefs affects connectivity check" '
		GIT_TRACE="$PWD"/trace git -c $section.hideRefs=refs -c \
			$section.hideRefs="!refs/tags/" fetch &&
		grep "git rev-list .*--exclude-hidden=fetch" trace
	'
done

setup_negotiation_tip () {
	SERVER="$1"
	URL="$2"
	USE_PROTOCOL_V2="$3"

	rm -rf "$SERVER" client trace &&
	git init -b main "$SERVER" &&
	test_commit -C "$SERVER" alpha_1 &&
	test_commit -C "$SERVER" alpha_2 &&
	git -C "$SERVER" checkout --orphan beta &&
	test_commit -C "$SERVER" beta_1 &&
	test_commit -C "$SERVER" beta_2 &&

	git clone "$URL" client &&

	if test "$USE_PROTOCOL_V2" -eq 1
	then
		git -C "$SERVER" config protocol.version 2 &&
		git -C client config protocol.version 2
	fi &&

	test_commit -C "$SERVER" beta_s &&
	git -C "$SERVER" checkout main &&
	test_commit -C "$SERVER" alpha_s &&
	git -C "$SERVER" tag -d alpha_1 alpha_2 beta_1 beta_2
}

check_negotiation_tip () {
	# Ensure that {alpha,beta}_1 are sent as "have", but not {alpha_beta}_2
	ALPHA_1=$(git -C client rev-parse alpha_1) &&
	grep "fetch> have $ALPHA_1" trace &&
	BETA_1=$(git -C client rev-parse beta_1) &&
	grep "fetch> have $BETA_1" trace &&
	ALPHA_2=$(git -C client rev-parse alpha_2) &&
	! grep "fetch> have $ALPHA_2" trace &&
	BETA_2=$(git -C client rev-parse beta_2) &&
	! grep "fetch> have $BETA_2" trace
}

test_expect_success '--negotiation-tip limits "have" lines sent' '
	setup_negotiation_tip server server 0 &&
	GIT_TRACE_PACKET="$(pwd)/trace" git -C client fetch \
		--negotiation-tip=alpha_1 --negotiation-tip=beta_1 \
		origin alpha_s beta_s &&
	check_negotiation_tip
'

test_expect_success '--negotiation-tip understands globs' '
	setup_negotiation_tip server server 0 &&
	GIT_TRACE_PACKET="$(pwd)/trace" git -C client fetch \
		--negotiation-tip=*_1 \
		origin alpha_s beta_s &&
	check_negotiation_tip
'

test_expect_success '--negotiation-tip understands abbreviated SHA-1' '
	setup_negotiation_tip server server 0 &&
	GIT_TRACE_PACKET="$(pwd)/trace" git -C client fetch \
		--negotiation-tip=$(git -C client rev-parse --short alpha_1) \
		--negotiation-tip=$(git -C client rev-parse --short beta_1) \
		origin alpha_s beta_s &&
	check_negotiation_tip
'

test_expect_success '--negotiation-tip rejects missing OIDs' '
	setup_negotiation_tip server server 0 &&
	test_must_fail git -C client fetch \
		--negotiation-tip=alpha_1 \
		--negotiation-tip=$(test_oid zero) \
		origin alpha_s beta_s 2>err &&
	cat >fatal-expect <<-EOF &&
	fatal: the object $(test_oid zero) does not exist
EOF
	grep fatal: err >fatal-actual &&
	test_cmp fatal-expect fatal-actual
'

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success '--negotiation-tip limits "have" lines sent with HTTP protocol v2' '
	setup_negotiation_tip "$HTTPD_DOCUMENT_ROOT_PATH/server" \
		"$HTTPD_URL/smart/server" 1 &&
	GIT_TRACE_PACKET="$(pwd)/trace" git -C client fetch \
		--negotiation-tip=alpha_1 --negotiation-tip=beta_1 \
		origin alpha_s beta_s &&
	check_negotiation_tip
'

# DO NOT add non-httpd-specific tests here, because the last part of this
# test script is only executed when httpd is available and enabled.

test_done
