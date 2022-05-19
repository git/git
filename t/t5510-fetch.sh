#!/bin/sh
# Copyright (c) 2006, Junio C Hamano.

test_description='Per branch config variables affects "but fetch".

'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-bundle.sh

D=$(pwd)

test_expect_success setup '
	echo >file original &&
	but add file &&
	but cummit -a -m original &&
	but branch -M main
'

test_expect_success "clone and setup child repos" '
	but clone . one &&
	(
		cd one &&
		echo >file updated by one &&
		but cummit -a -m "updated by one"
	) &&
	but clone . two &&
	(
		cd two &&
		but config branch.main.remote one &&
		but config remote.one.url ../one/.but/ &&
		but config remote.one.fetch refs/heads/main:refs/heads/one
	) &&
	but clone . three &&
	(
		cd three &&
		but config branch.main.remote two &&
		but config branch.main.merge refs/heads/one &&
		mkdir -p .but/remotes &&
		cat >.but/remotes/two <<-\EOF
		URL: ../two/.but/
		Pull: refs/heads/main:refs/heads/two
		Pull: refs/heads/one:refs/heads/one
		EOF
	) &&
	but clone . bundle &&
	but clone . seven
'

test_expect_success "fetch test" '
	cd "$D" &&
	echo >file updated by origin &&
	but cummit -a -m "updated by origin" &&
	cd two &&
	but fetch &&
	but rev-parse --verify refs/heads/one &&
	mine=$(but rev-parse refs/heads/one) &&
	his=$(cd ../one && but rev-parse refs/heads/main) &&
	test "z$mine" = "z$his"
'

test_expect_success "fetch test for-merge" '
	cd "$D" &&
	cd three &&
	but fetch &&
	but rev-parse --verify refs/heads/two &&
	but rev-parse --verify refs/heads/one &&
	main_in_two=$(cd ../two && but rev-parse main) &&
	one_in_two=$(cd ../two && but rev-parse one) &&
	{
		echo "$one_in_two	" &&
		echo "$main_in_two	not-for-merge"
	} >expected &&
	cut -f -2 .but/FETCH_HEAD >actual &&
	test_cmp expected actual'

test_expect_success 'fetch --prune on its own works as expected' '
	cd "$D" &&
	but clone . prune &&
	cd prune &&
	but update-ref refs/remotes/origin/extrabranch main &&

	but fetch --prune origin &&
	test_must_fail but rev-parse origin/extrabranch
'

test_expect_success 'fetch --prune with a branch name keeps branches' '
	cd "$D" &&
	but clone . prune-branch &&
	cd prune-branch &&
	but update-ref refs/remotes/origin/extrabranch main &&

	but fetch --prune origin main &&
	but rev-parse origin/extrabranch
'

test_expect_success 'fetch --prune with a namespace keeps other namespaces' '
	cd "$D" &&
	but clone . prune-namespace &&
	cd prune-namespace &&

	but fetch --prune origin refs/heads/a/*:refs/remotes/origin/a/* &&
	but rev-parse origin/main
'

test_expect_success 'fetch --prune handles overlapping refspecs' '
	cd "$D" &&
	but update-ref refs/pull/42/head main &&
	but clone . prune-overlapping &&
	cd prune-overlapping &&
	but config --add remote.origin.fetch refs/pull/*/head:refs/remotes/origin/pr/* &&

	but fetch --prune origin &&
	but rev-parse origin/main &&
	but rev-parse origin/pr/42 &&

	but config --unset-all remote.origin.fetch &&
	but config remote.origin.fetch refs/pull/*/head:refs/remotes/origin/pr/* &&
	but config --add remote.origin.fetch refs/heads/*:refs/remotes/origin/* &&

	but fetch --prune origin &&
	but rev-parse origin/main &&
	but rev-parse origin/pr/42
'

test_expect_success 'fetch --prune --tags prunes branches but not tags' '
	cd "$D" &&
	but clone . prune-tags &&
	cd prune-tags &&
	but tag sometag main &&
	# Create what looks like a remote-tracking branch from an earlier
	# fetch that has since been deleted from the remote:
	but update-ref refs/remotes/origin/fake-remote main &&

	but fetch --prune --tags origin &&
	but rev-parse origin/main &&
	test_must_fail but rev-parse origin/fake-remote &&
	but rev-parse sometag
'

test_expect_success 'fetch --prune --tags with branch does not prune other things' '
	cd "$D" &&
	but clone . prune-tags-branch &&
	cd prune-tags-branch &&
	but tag sometag main &&
	but update-ref refs/remotes/origin/extrabranch main &&

	but fetch --prune --tags origin main &&
	but rev-parse origin/extrabranch &&
	but rev-parse sometag
'

test_expect_success 'fetch --prune --tags with refspec prunes based on refspec' '
	cd "$D" &&
	but clone . prune-tags-refspec &&
	cd prune-tags-refspec &&
	but tag sometag main &&
	but update-ref refs/remotes/origin/foo/otherbranch main &&
	but update-ref refs/remotes/origin/extrabranch main &&

	but fetch --prune --tags origin refs/heads/foo/*:refs/remotes/origin/foo/* &&
	test_must_fail but rev-parse refs/remotes/origin/foo/otherbranch &&
	but rev-parse origin/extrabranch &&
	but rev-parse sometag
'

test_expect_success REFFILES 'fetch --prune fails to delete branches' '
	cd "$D" &&
	but clone . prune-fail &&
	cd prune-fail &&
	but update-ref refs/remotes/origin/extrabranch main &&
	: this will prevent --prune from locking packed-refs for deleting refs, but adding loose refs still succeeds  &&
	>.but/packed-refs.new &&

	test_must_fail but fetch --prune origin
'

test_expect_success 'fetch --atomic works with a single branch' '
	test_when_finished "rm -rf \"$D\"/atomic" &&

	cd "$D" &&
	but clone . atomic &&
	but branch atomic-branch &&
	oid=$(but rev-parse atomic-branch) &&
	echo "$oid" >expected &&

	but -C atomic fetch --atomic origin &&
	but -C atomic rev-parse origin/atomic-branch >actual &&
	test_cmp expected actual &&
	test $oid = "$(but -C atomic rev-parse --verify FETCH_HEAD)"
'

test_expect_success 'fetch --atomic works with multiple branches' '
	test_when_finished "rm -rf \"$D\"/atomic" &&

	cd "$D" &&
	but clone . atomic &&
	but branch atomic-branch-1 &&
	but branch atomic-branch-2 &&
	but branch atomic-branch-3 &&
	but rev-parse refs/heads/atomic-branch-1 refs/heads/atomic-branch-2 refs/heads/atomic-branch-3 >actual &&

	but -C atomic fetch --atomic origin &&
	but -C atomic rev-parse refs/remotes/origin/atomic-branch-1 refs/remotes/origin/atomic-branch-2 refs/remotes/origin/atomic-branch-3 >expected &&
	test_cmp expected actual
'

test_expect_success 'fetch --atomic works with mixed branches and tags' '
	test_when_finished "rm -rf \"$D\"/atomic" &&

	cd "$D" &&
	but clone . atomic &&
	but branch atomic-mixed-branch &&
	but tag atomic-mixed-tag &&
	but rev-parse refs/heads/atomic-mixed-branch refs/tags/atomic-mixed-tag >actual &&

	but -C atomic fetch --tags --atomic origin &&
	but -C atomic rev-parse refs/remotes/origin/atomic-mixed-branch refs/tags/atomic-mixed-tag >expected &&
	test_cmp expected actual
'

test_expect_success 'fetch --atomic prunes references' '
	test_when_finished "rm -rf \"$D\"/atomic" &&

	cd "$D" &&
	but branch atomic-prune-delete &&
	but clone . atomic &&
	but branch --delete atomic-prune-delete &&
	but branch atomic-prune-create &&
	but rev-parse refs/heads/atomic-prune-create >actual &&

	but -C atomic fetch --prune --atomic origin &&
	test_must_fail but -C atomic rev-parse refs/remotes/origin/atomic-prune-delete &&
	but -C atomic rev-parse refs/remotes/origin/atomic-prune-create >expected &&
	test_cmp expected actual
'

test_expect_success 'fetch --atomic aborts with non-fast-forward update' '
	test_when_finished "rm -rf \"$D\"/atomic" &&

	cd "$D" &&
	but branch atomic-non-ff &&
	but clone . atomic &&
	but rev-parse HEAD >actual &&

	but branch atomic-new-branch &&
	parent_cummit=$(but rev-parse atomic-non-ff~) &&
	but update-ref refs/heads/atomic-non-ff $parent_cummit &&

	test_must_fail but -C atomic fetch --atomic origin refs/heads/*:refs/remotes/origin/* &&
	test_must_fail but -C atomic rev-parse refs/remotes/origin/atomic-new-branch &&
	but -C atomic rev-parse refs/remotes/origin/atomic-non-ff >expected &&
	test_cmp expected actual &&
	test_must_be_empty atomic/.but/FETCH_HEAD
'

test_expect_success 'fetch --atomic executes a single reference transaction only' '
	test_when_finished "rm -rf \"$D\"/atomic" &&

	cd "$D" &&
	but clone . atomic &&
	but branch atomic-hooks-1 &&
	but branch atomic-hooks-2 &&
	head_oid=$(but rev-parse HEAD) &&

	cat >expected <<-EOF &&
		prepared
		$ZERO_OID $head_oid refs/remotes/origin/atomic-hooks-1
		$ZERO_OID $head_oid refs/remotes/origin/atomic-hooks-2
		cummitted
		$ZERO_OID $head_oid refs/remotes/origin/atomic-hooks-1
		$ZERO_OID $head_oid refs/remotes/origin/atomic-hooks-2
	EOF

	rm -f atomic/actual &&
	test_hook -C atomic reference-transaction <<-\EOF &&
		( echo "$*" && cat ) >>actual
	EOF

	but -C atomic fetch --atomic origin &&
	test_cmp expected atomic/actual
'

test_expect_success 'fetch --atomic aborts all reference updates if hook aborts' '
	test_when_finished "rm -rf \"$D\"/atomic" &&

	cd "$D" &&
	but clone . atomic &&
	but branch atomic-hooks-abort-1 &&
	but branch atomic-hooks-abort-2 &&
	but branch atomic-hooks-abort-3 &&
	but tag atomic-hooks-abort &&
	head_oid=$(but rev-parse HEAD) &&

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
	test_hook -C atomic/.but reference-transaction <<-\EOF &&
		( echo "$*" && cat ) >>actual
		exit 1
	EOF

	but -C atomic for-each-ref >expected-refs &&
	test_must_fail but -C atomic fetch --tags --atomic origin &&
	but -C atomic for-each-ref >actual-refs &&
	test_cmp expected-refs actual-refs &&
	test_must_be_empty atomic/.but/FETCH_HEAD
'

test_expect_success 'fetch --atomic --append appends to FETCH_HEAD' '
	test_when_finished "rm -rf \"$D\"/atomic" &&

	cd "$D" &&
	but clone . atomic &&
	oid=$(but rev-parse HEAD) &&

	but branch atomic-fetch-head-1 &&
	but -C atomic fetch --atomic origin atomic-fetch-head-1 &&
	test_line_count = 1 atomic/.but/FETCH_HEAD &&

	but branch atomic-fetch-head-2 &&
	but -C atomic fetch --atomic --append origin atomic-fetch-head-2 &&
	test_line_count = 2 atomic/.but/FETCH_HEAD &&
	cp atomic/.but/FETCH_HEAD expected &&

	test_hook -C atomic reference-transaction <<-\EOF &&
		exit 1
	EOF

	but branch atomic-fetch-head-3 &&
	test_must_fail but -C atomic fetch --atomic --append origin atomic-fetch-head-3 &&
	test_cmp expected atomic/.but/FETCH_HEAD
'

test_expect_success '--refmap="" ignores configured refspec' '
	cd "$TRASH_DIRECTORY" &&
	but clone "$D" remote-refs &&
	but -C remote-refs rev-parse remotes/origin/main >old &&
	but -C remote-refs update-ref refs/remotes/origin/main main~1 &&
	but -C remote-refs rev-parse remotes/origin/main >new &&
	but -C remote-refs fetch --refmap= origin "+refs/heads/*:refs/hidden/origin/*" &&
	but -C remote-refs rev-parse remotes/origin/main >actual &&
	test_cmp new actual &&
	but -C remote-refs fetch origin &&
	but -C remote-refs rev-parse remotes/origin/main >actual &&
	test_cmp old actual
'

test_expect_success '--refmap="" and --prune' '
	but -C remote-refs update-ref refs/remotes/origin/foo/otherbranch main &&
	but -C remote-refs update-ref refs/hidden/foo/otherbranch main &&
	but -C remote-refs fetch --prune --refmap="" origin +refs/heads/*:refs/hidden/* &&
	but -C remote-refs rev-parse remotes/origin/foo/otherbranch &&
	test_must_fail but -C remote-refs rev-parse refs/hidden/foo/otherbranch &&
	but -C remote-refs fetch --prune origin &&
	test_must_fail but -C remote-refs rev-parse remotes/origin/foo/otherbranch
'

test_expect_success 'fetch tags when there is no tags' '

    cd "$D" &&

    mkdir notags &&
    cd notags &&
    but init &&

    but fetch -t ..

'

test_expect_success 'fetch following tags' '

	cd "$D" &&
	but tag -a -m "annotated" anno HEAD &&
	but tag light HEAD &&

	mkdir four &&
	cd four &&
	but init &&

	but fetch .. :track &&
	but show-ref --verify refs/tags/anno &&
	but show-ref --verify refs/tags/light

'

test_expect_success 'fetch uses remote ref names to describe new refs' '
	cd "$D" &&
	but init descriptive &&
	(
		cd descriptive &&
		but config remote.o.url .. &&
		but config remote.o.fetch "refs/heads/*:refs/crazyheads/*" &&
		but config --add remote.o.fetch "refs/others/*:refs/heads/*" &&
		but fetch o
	) &&
	but tag -a -m "Descriptive tag" descriptive-tag &&
	but branch descriptive-branch &&
	but checkout descriptive-branch &&
	echo "Nuts" >crazy &&
	but add crazy &&
	but cummit -a -m "descriptive cummit" &&
	but update-ref refs/others/crazy HEAD &&
	(
		cd descriptive &&
		but fetch o 2>actual &&
		test_i18ngrep "new branch.* -> refs/crazyheads/descriptive-branch$" actual &&
		test_i18ngrep "new tag.* -> descriptive-tag$" actual &&
		test_i18ngrep "new ref.* -> crazy$" actual
	) &&
	but checkout main
'

test_expect_success 'fetch must not resolve short tag name' '

	cd "$D" &&

	mkdir five &&
	cd five &&
	but init &&

	test_must_fail but fetch .. anno:five

'

test_expect_success 'fetch can now resolve short remote name' '

	cd "$D" &&
	but update-ref refs/remotes/six/HEAD HEAD &&

	mkdir six &&
	cd six &&
	but init &&

	but fetch .. six:six
'

test_expect_success 'create bundle 1' '
	cd "$D" &&
	echo >file updated again by origin &&
	but cummit -a -m "tip" &&
	but bundle create --version=3 bundle1 main^..main
'

test_expect_success 'header of bundle looks right' '
	cat >expect <<-EOF &&
	# v3 but bundle
	@object-format=$(test_oid algo)
	-OID updated by origin
	OID refs/heads/main

	EOF
	sed -e "s/$OID_REGEX/OID/g" -e "5q" "$D"/bundle1 >actual &&
	test_cmp expect actual
'

test_expect_success 'create bundle 2' '
	cd "$D" &&
	but bundle create bundle2 main~2..main
'

test_expect_success 'unbundle 1' '
	cd "$D/bundle" &&
	but checkout -b some-branch &&
	test_must_fail but fetch "$D/bundle1" main:main
'


test_expect_success 'bundle 1 has only 3 files ' '
	cd "$D" &&
	test_bundle_object_count bundle1 3
'

test_expect_success 'unbundle 2' '
	cd "$D/bundle" &&
	but fetch ../bundle2 main:main &&
	test "tip" = "$(but log -1 --pretty=oneline main | cut -d" " -f2)"
'

test_expect_success 'bundle does not prerequisite objects' '
	cd "$D" &&
	touch file2 &&
	but add file2 &&
	but cummit -m add.file2 file2 &&
	but bundle create bundle3 -1 HEAD &&
	test_bundle_object_count bundle3 3
'

test_expect_success 'bundle should be able to create a full history' '

	cd "$D" &&
	but tag -a -m "1.0" v1.0 main &&
	but bundle create bundle4 v1.0

'

test_expect_success 'fetch with a non-applying branch.<name>.merge' '
	but config branch.main.remote yeti &&
	but config branch.main.merge refs/heads/bigfoot &&
	but config remote.blub.url one &&
	but config remote.blub.fetch "refs/heads/*:refs/remotes/one/*" &&
	but fetch blub
'

# URL supplied to fetch does not match the url of the configured branch's remote
test_expect_success 'fetch from GIT URL with a non-applying branch.<name>.merge [1]' '
	one_head=$(cd one && but rev-parse HEAD) &&
	this_head=$(but rev-parse HEAD) &&
	but update-ref -d FETCH_HEAD &&
	but fetch one &&
	test $one_head = "$(but rev-parse --verify FETCH_HEAD)" &&
	test $this_head = "$(but rev-parse --verify HEAD)"
'

# URL supplied to fetch matches the url of the configured branch's remote and
# the merge spec matches the branch the remote HEAD points to
test_expect_success 'fetch from GIT URL with a non-applying branch.<name>.merge [2]' '
	one_ref=$(cd one && but symbolic-ref HEAD) &&
	but config branch.main.remote blub &&
	but config branch.main.merge "$one_ref" &&
	but update-ref -d FETCH_HEAD &&
	but fetch one &&
	test $one_head = "$(but rev-parse --verify FETCH_HEAD)" &&
	test $this_head = "$(but rev-parse --verify HEAD)"
'

# URL supplied to fetch matches the url of the configured branch's remote, but
# the merge spec does not match the branch the remote HEAD points to
test_expect_success 'fetch from GIT URL with a non-applying branch.<name>.merge [3]' '
	but config branch.main.merge "${one_ref}_not" &&
	but update-ref -d FETCH_HEAD &&
	but fetch one &&
	test $one_head = "$(but rev-parse --verify FETCH_HEAD)" &&
	test $this_head = "$(but rev-parse --verify HEAD)"
'

# the strange name is: a\!'b
test_expect_success 'quoting of a strangely named repo' '
	test_must_fail but fetch "a\\!'\''b" > result 2>&1 &&
	grep "fatal: '\''a\\\\!'\''b'\''" result
'

test_expect_success 'bundle should record HEAD correctly' '

	cd "$D" &&
	but bundle create bundle5 HEAD main &&
	but bundle list-heads bundle5 >actual &&
	for h in HEAD refs/heads/main
	do
		echo "$(but rev-parse --verify $h) $h" || return 1
	done >expect &&
	test_cmp expect actual

'

test_expect_success 'mark initial state of origin/main' '
	(
		cd three &&
		but tag base-origin-main refs/remotes/origin/main
	)
'

test_expect_success 'explicit fetch should update tracking' '

	cd "$D" &&
	but branch -f side &&
	(
		cd three &&
		but update-ref refs/remotes/origin/main base-origin-main &&
		o=$(but rev-parse --verify refs/remotes/origin/main) &&
		but fetch origin main &&
		n=$(but rev-parse --verify refs/remotes/origin/main) &&
		test "$o" != "$n" &&
		test_must_fail but rev-parse --verify refs/remotes/origin/side
	)
'

test_expect_success 'explicit pull should update tracking' '

	cd "$D" &&
	but branch -f side &&
	(
		cd three &&
		but update-ref refs/remotes/origin/main base-origin-main &&
		o=$(but rev-parse --verify refs/remotes/origin/main) &&
		but pull origin main &&
		n=$(but rev-parse --verify refs/remotes/origin/main) &&
		test "$o" != "$n" &&
		test_must_fail but rev-parse --verify refs/remotes/origin/side
	)
'

test_expect_success 'explicit --refmap is allowed only with command-line refspec' '
	cd "$D" &&
	(
		cd three &&
		test_must_fail but fetch --refmap="*:refs/remotes/none/*"
	)
'

test_expect_success 'explicit --refmap option overrides remote.*.fetch' '
	cd "$D" &&
	but branch -f side &&
	(
		cd three &&
		but update-ref refs/remotes/origin/main base-origin-main &&
		o=$(but rev-parse --verify refs/remotes/origin/main) &&
		but fetch --refmap="refs/heads/*:refs/remotes/other/*" origin main &&
		n=$(but rev-parse --verify refs/remotes/origin/main) &&
		test "$o" = "$n" &&
		test_must_fail but rev-parse --verify refs/remotes/origin/side &&
		but rev-parse --verify refs/remotes/other/main
	)
'

test_expect_success 'explicitly empty --refmap option disables remote.*.fetch' '
	cd "$D" &&
	but branch -f side &&
	(
		cd three &&
		but update-ref refs/remotes/origin/main base-origin-main &&
		o=$(but rev-parse --verify refs/remotes/origin/main) &&
		but fetch --refmap="" origin main &&
		n=$(but rev-parse --verify refs/remotes/origin/main) &&
		test "$o" = "$n" &&
		test_must_fail but rev-parse --verify refs/remotes/origin/side
	)
'

test_expect_success 'configured fetch updates tracking' '

	cd "$D" &&
	but branch -f side &&
	(
		cd three &&
		but update-ref refs/remotes/origin/main base-origin-main &&
		o=$(but rev-parse --verify refs/remotes/origin/main) &&
		but fetch origin &&
		n=$(but rev-parse --verify refs/remotes/origin/main) &&
		test "$o" != "$n" &&
		but rev-parse --verify refs/remotes/origin/side
	)
'

test_expect_success 'non-matching refspecs do not confuse tracking update' '
	cd "$D" &&
	but update-ref refs/odd/location HEAD &&
	(
		cd three &&
		but update-ref refs/remotes/origin/main base-origin-main &&
		but config --add remote.origin.fetch \
			refs/odd/location:refs/remotes/origin/odd &&
		o=$(but rev-parse --verify refs/remotes/origin/main) &&
		but fetch origin main &&
		n=$(but rev-parse --verify refs/remotes/origin/main) &&
		test "$o" != "$n" &&
		test_must_fail but rev-parse --verify refs/remotes/origin/odd
	)
'

test_expect_success 'pushing nonexistent branch by mistake should not segv' '

	cd "$D" &&
	test_must_fail but push seven no:no

'

test_expect_success 'auto tag following fetches minimum' '

	cd "$D" &&
	but clone .but follow &&
	but checkout HEAD^0 &&
	(
		for i in 1 2 3 4 5 6 7
		do
			echo $i >>file &&
			but cummit -m $i -a &&
			but tag -a -m $i excess-$i || exit 1
		done
	) &&
	but checkout main &&
	(
		cd follow &&
		but fetch
	)
'

test_expect_success 'refuse to fetch into the current branch' '

	test_must_fail but fetch . side:main

'

test_expect_success 'fetch into the current branch with --update-head-ok' '

	but fetch --update-head-ok . side:main

'

test_expect_success 'fetch --dry-run does not touch FETCH_HEAD, but still prints what would be written' '
	rm -f .but/FETCH_HEAD err &&
	but fetch --dry-run . 2>err &&
	! test -f .but/FETCH_HEAD &&
	grep FETCH_HEAD err
'

test_expect_success '--no-write-fetch-head does not touch FETCH_HEAD, and does not print what would be written' '
	rm -f .but/FETCH_HEAD err &&
	but fetch --no-write-fetch-head . 2>err &&
	! test -f .but/FETCH_HEAD &&
	! grep FETCH_HEAD err
'

test_expect_success '--write-fetch-head gets defeated by --dry-run' '
	rm -f .but/FETCH_HEAD &&
	but fetch --dry-run --write-fetch-head . &&
	! test -f .but/FETCH_HEAD
'

test_expect_success "should be able to fetch with duplicate refspecs" '
	mkdir dups &&
	(
		cd dups &&
		but init &&
		but config branch.main.remote three &&
		but config remote.three.url ../three/.but &&
		but config remote.three.fetch +refs/heads/*:refs/remotes/origin/* &&
		but config --add remote.three.fetch +refs/heads/*:refs/remotes/origin/* &&
		but fetch three
	)
'

test_expect_success 'LHS of refspec follows ref disambiguation rules' '
	mkdir lhs-ambiguous &&
	(
		cd lhs-ambiguous &&
		but init server &&
		test_cummit -C server unwanted &&
		test_cummit -C server wanted &&

		but init client &&

		# Check a name coming after "refs" alphabetically ...
		but -C server update-ref refs/heads/s wanted &&
		but -C server update-ref refs/heads/refs/heads/s unwanted &&
		but -C client fetch ../server +refs/heads/s:refs/heads/checkthis &&
		but -C server rev-parse wanted >expect &&
		but -C client rev-parse checkthis >actual &&
		test_cmp expect actual &&

		# ... and one before.
		but -C server update-ref refs/heads/q wanted &&
		but -C server update-ref refs/heads/refs/heads/q unwanted &&
		but -C client fetch ../server +refs/heads/q:refs/heads/checkthis &&
		but -C server rev-parse wanted >expect &&
		but -C client rev-parse checkthis >actual &&
		test_cmp expect actual &&

		# Tags are preferred over branches like refs/{heads,tags}/*
		but -C server update-ref refs/tags/t wanted &&
		but -C server update-ref refs/heads/t unwanted &&
		but -C client fetch ../server +t:refs/heads/checkthis &&
		but -C server rev-parse wanted >expect &&
		but -C client rev-parse checkthis >actual
	)
'

test_expect_success 'fetch.writecummitGraph' '
	but clone three write &&
	(
		cd three &&
		test_cummit new
	) &&
	(
		cd write &&
		but -c fetch.writecummitGraph fetch origin &&
		test_path_is_file .but/objects/info/cummit-graphs/cummit-graph-chain
	)
'

test_expect_success 'fetch.writecummitGraph with submodules' '
	but clone dups super &&
	(
		cd super &&
		but submodule add "file://$TRASH_DIRECTORY/three" &&
		but cummit -m "add submodule"
	) &&
	but clone "super" super-clone &&
	(
		cd super-clone &&
		rm -rf .but/objects/info &&
		but -c fetch.writecummitGraph=true fetch origin &&
		test_path_is_file .but/objects/info/cummit-graphs/cummit-graph-chain
	)
'

# configured prune tests

set_config_tristate () {
	# var=$1 val=$2
	case "$2" in
	unset)
		test_unconfig "$1"
		;;
	*)
		but config "$1" "$2"
		key=$(echo $1 | sed -e 's/^remote\.origin/fetch/')
		but_fetch_c="$but_fetch_c -c $key=$2"
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
			remote_url="file://$(but -C one config remote.origin.url)" &&
			remote_fetch="$(but -C one config remote.origin.fetch)" &&
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
			new_cmdline=$(printf "%s" "$cmdline" | perl -pe 's[origin(?!/)]["'"$remote_url"'"]g')
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
		but branch -f newbranch &&
		but tag -f newtag &&
		(
			cd one &&
			test_unconfig fetch.prune &&
			test_unconfig fetch.pruneTags &&
			test_unconfig remote.origin.prune &&
			test_unconfig remote.origin.pruneTags &&
			but fetch '"$cmdline_setup"' &&
			but rev-parse --verify refs/remotes/origin/newbranch &&
			but rev-parse --verify refs/tags/newtag
		) &&

		# now remove them
		but branch -d newbranch &&
		but tag -d newtag &&

		# then test
		(
			cd one &&
			but_fetch_c="" &&
			set_config_tristate fetch.prune $fetch_prune &&
			set_config_tristate fetch.pruneTags $fetch_prune_tags &&
			set_config_tristate remote.origin.prune $remote_origin_prune &&
			set_config_tristate remote.origin.pruneTags $remote_origin_prune_tags &&

			if test "$mode" != "link"
			then
				but_fetch_c=""
			fi &&
			but$but_fetch_c fetch '"$cmdline"' &&
			case "$expected_branch" in
			pruned)
				test_must_fail but rev-parse --verify refs/remotes/origin/newbranch
				;;
			kept)
				but rev-parse --verify refs/remotes/origin/newbranch
				;;
			esac &&
			case "$expected_tag" in
			pruned)
				test_must_fail but rev-parse --verify refs/tags/newtag
				;;
			kept)
				but rev-parse --verify refs/tags/newtag
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
# $7 but-fetch $cmdline:
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
		but config --unset-all remote.origin.fetch
	)
'
test_configured_prune_type unset unset unset unset kept pruned "origin --prune --prune-tags" "name"
test_configured_prune_type unset unset unset unset kept pruned "origin --prune --prune-tags" "link"

test_expect_success 'all boundary cummits are excluded' '
	test_cummit base &&
	test_cummit oneside &&
	but checkout HEAD^ &&
	test_cummit otherside &&
	but checkout main &&
	test_tick &&
	but merge otherside &&
	ad=$(but log --no-walk --format=%ad HEAD) &&
	but bundle create twoside-boundary.bdl main --since="$ad" &&
	test_bundle_object_count --thin twoside-boundary.bdl 3
'

test_expect_success 'fetch --prune prints the remotes url' '
	but branch goodbye &&
	but clone . only-prunes &&
	but branch -D goodbye &&
	(
		cd only-prunes &&
		but fetch --prune origin 2>&1 | head -n1 >../actual
	) &&
	echo "From ${D}/." >expect &&
	test_cmp expect actual
'

test_expect_success 'branchname D/F conflict resolved by --prune' '
	but branch dir/file &&
	but clone . prune-df-conflict &&
	but branch -D dir/file &&
	but branch dir &&
	(
		cd prune-df-conflict &&
		but fetch --prune &&
		but rev-parse origin/dir >../actual
	) &&
	but rev-parse dir >expect &&
	test_cmp expect actual
'

test_expect_success 'fetching a one-level ref works' '
	test_cummit extra &&
	but reset --hard HEAD^ &&
	but update-ref refs/foo extra &&
	but init one-level &&
	(
		cd one-level &&
		but fetch .. HEAD refs/foo
	)
'

test_expect_success 'fetching with auto-gc does not lock up' '
	write_script askyesno <<-\EOF &&
	echo "$*" &&
	false
	EOF
	but clone "file://$D" auto-gc &&
	test_cummit test2 &&
	(
		cd auto-gc &&
		but config fetch.unpackLimit 1 &&
		but config gc.autoPackLimit 1 &&
		but config gc.autoDetach false &&
		GIT_ASK_YESNO="$D/askyesno" but fetch --verbose >fetch.out 2>&1 &&
		test_i18ngrep "Auto packing the repository" fetch.out &&
		! grep "Should I try again" fetch.out
	)
'

test_expect_success 'fetch aligned output' '
	but clone . full-output &&
	test_cummit looooooooooooong-tag &&
	(
		cd full-output &&
		but -c fetch.output=full fetch origin >actual 2>&1 &&
		grep -e "->" actual | cut -c 22- >../actual
	) &&
	cat >expect <<-\EOF &&
	main                 -> origin/main
	looooooooooooong-tag -> looooooooooooong-tag
	EOF
	test_cmp expect actual
'

test_expect_success 'fetch compact output' '
	but clone . compact &&
	test_cummit extraaa &&
	(
		cd compact &&
		but -c fetch.output=compact fetch origin >actual 2>&1 &&
		grep -e "->" actual | cut -c 22- >../actual
	) &&
	cat >expect <<-\EOF &&
	main       -> origin/*
	extraaa    -> *
	EOF
	test_cmp expect actual
'

test_expect_success '--no-show-forced-updates' '
	mkdir forced-updates &&
	(
		cd forced-updates &&
		but init &&
		test_cummit 1 &&
		test_cummit 2
	) &&
	but clone forced-updates forced-update-clone &&
	but clone forced-updates no-forced-update-clone &&
	but -C forced-updates reset --hard HEAD~1 &&
	(
		cd forced-update-clone &&
		but fetch --show-forced-updates origin 2>output &&
		test_i18ngrep "(forced update)" output
	) &&
	(
		cd no-forced-update-clone &&
		but fetch --no-show-forced-updates origin 2>output &&
		test_i18ngrep ! "(forced update)" output
	)
'

setup_negotiation_tip () {
	SERVER="$1"
	URL="$2"
	USE_PROTOCOL_V2="$3"

	rm -rf "$SERVER" client trace &&
	but init -b main "$SERVER" &&
	test_cummit -C "$SERVER" alpha_1 &&
	test_cummit -C "$SERVER" alpha_2 &&
	but -C "$SERVER" checkout --orphan beta &&
	test_cummit -C "$SERVER" beta_1 &&
	test_cummit -C "$SERVER" beta_2 &&

	but clone "$URL" client &&

	if test "$USE_PROTOCOL_V2" -eq 1
	then
		but -C "$SERVER" config protocol.version 2 &&
		but -C client config protocol.version 2
	fi &&

	test_cummit -C "$SERVER" beta_s &&
	but -C "$SERVER" checkout main &&
	test_cummit -C "$SERVER" alpha_s &&
	but -C "$SERVER" tag -d alpha_1 alpha_2 beta_1 beta_2
}

check_negotiation_tip () {
	# Ensure that {alpha,beta}_1 are sent as "have", but not {alpha_beta}_2
	ALPHA_1=$(but -C client rev-parse alpha_1) &&
	grep "fetch> have $ALPHA_1" trace &&
	BETA_1=$(but -C client rev-parse beta_1) &&
	grep "fetch> have $BETA_1" trace &&
	ALPHA_2=$(but -C client rev-parse alpha_2) &&
	! grep "fetch> have $ALPHA_2" trace &&
	BETA_2=$(but -C client rev-parse beta_2) &&
	! grep "fetch> have $BETA_2" trace
}

test_expect_success '--negotiation-tip limits "have" lines sent' '
	setup_negotiation_tip server server 0 &&
	GIT_TRACE_PACKET="$(pwd)/trace" but -C client fetch \
		--negotiation-tip=alpha_1 --negotiation-tip=beta_1 \
		origin alpha_s beta_s &&
	check_negotiation_tip
'

test_expect_success '--negotiation-tip understands globs' '
	setup_negotiation_tip server server 0 &&
	GIT_TRACE_PACKET="$(pwd)/trace" but -C client fetch \
		--negotiation-tip=*_1 \
		origin alpha_s beta_s &&
	check_negotiation_tip
'

test_expect_success '--negotiation-tip understands abbreviated SHA-1' '
	setup_negotiation_tip server server 0 &&
	GIT_TRACE_PACKET="$(pwd)/trace" but -C client fetch \
		--negotiation-tip=$(but -C client rev-parse --short alpha_1) \
		--negotiation-tip=$(but -C client rev-parse --short beta_1) \
		origin alpha_s beta_s &&
	check_negotiation_tip
'

test_expect_success '--negotiation-tip rejects missing OIDs' '
	setup_negotiation_tip server server 0 &&
	test_must_fail but -C client fetch \
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
	GIT_TRACE_PACKET="$(pwd)/trace" but -C client fetch \
		--negotiation-tip=alpha_1 --negotiation-tip=beta_1 \
		origin alpha_s beta_s &&
	check_negotiation_tip
'

# DO NOT add non-httpd-specific tests here, because the last part of this
# test script is only executed when httpd is available and enabled.

test_done
