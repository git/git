#!/bin/sh

test_description='but remote porcelain-ish'

. ./test-lib.sh

setup_repository () {
	mkdir "$1" && (
	cd "$1" &&
	but init -b main &&
	>file &&
	but add file &&
	test_tick &&
	but cummit -m "Initial" &&
	but checkout -b side &&
	>elif &&
	but add elif &&
	test_tick &&
	but cummit -m "Second" &&
	but checkout main
	)
}

tokens_match () {
	echo "$1" | tr ' ' '\012' | sort | sed -e '/^$/d' >expect &&
	echo "$2" | tr ' ' '\012' | sort | sed -e '/^$/d' >actual &&
	test_cmp expect actual
}

check_remote_track () {
	actual=$(but remote show "$1" | sed -ne 's|^    \(.*\) tracked$|\1|p')
	shift &&
	tokens_match "$*" "$actual"
}

check_tracking_branch () {
	f="" &&
	r=$(but for-each-ref "--format=%(refname)" |
		sed -ne "s|^refs/remotes/$1/||p") &&
	shift &&
	tokens_match "$*" "$r"
}

test_expect_success setup '
	setup_repository one &&
	setup_repository two &&
	(
		cd two &&
		but branch another
	) &&
	but clone one test
'

test_expect_success 'add remote whose URL agrees with url.<...>.insteadOf' '
	test_config url.but@host.com:team/repo.but.insteadOf myremote &&
	but remote add myremote but@host.com:team/repo.but
'

test_expect_success 'remote information for the origin' '
	(
		cd test &&
		tokens_match origin "$(but remote)" &&
		check_remote_track origin main side &&
		check_tracking_branch origin HEAD main side
	)
'

test_expect_success 'add another remote' '
	(
		cd test &&
		but remote add -f second ../two &&
		tokens_match "origin second" "$(but remote)" &&
		check_tracking_branch second main side another &&
		but for-each-ref "--format=%(refname)" refs/remotes |
		sed -e "/^refs\/remotes\/origin\//d" \
		    -e "/^refs\/remotes\/second\//d" >actual &&
		test_must_be_empty actual
	)
'

test_expect_success 'check remote-tracking' '
	(
		cd test &&
		check_remote_track origin main side &&
		check_remote_track second main side another
	)
'

test_expect_success 'remote forces tracking branches' '
	(
		cd test &&
		case $(but config remote.second.fetch) in
		+*) true ;;
		 *) false ;;
		esac
	)
'

test_expect_success 'remove remote' '
	(
		cd test &&
		but symbolic-ref refs/remotes/second/HEAD refs/remotes/second/main &&
		but remote rm second
	)
'

test_expect_success 'remove remote' '
	(
		cd test &&
		tokens_match origin "$(but remote)" &&
		check_remote_track origin main side &&
		but for-each-ref "--format=%(refname)" refs/remotes |
		sed -e "/^refs\/remotes\/origin\//d" >actual &&
		test_must_be_empty actual
	)
'

test_expect_success 'remove remote protects local branches' '
	(
		cd test &&
		cat >expect1 <<-\EOF &&
		Note: A branch outside the refs/remotes/ hierarchy was not removed;
		to delete it, use:
		  but branch -d main
		EOF
		cat >expect2 <<-\EOF &&
		Note: Some branches outside the refs/remotes/ hierarchy were not removed;
		to delete them, use:
		  but branch -d foobranch
		  but branch -d main
		EOF
		but tag footag &&
		but config --add remote.oops.fetch "+refs/*:refs/*" &&
		but remote remove oops 2>actual1 &&
		but branch foobranch &&
		but config --add remote.oops.fetch "+refs/*:refs/*" &&
		but remote rm oops 2>actual2 &&
		but branch -d foobranch &&
		but tag -d footag &&
		test_cmp expect1 actual1 &&
		test_cmp expect2 actual2
	)
'

test_expect_success 'remove errors out early when deleting non-existent branch' '
	(
		cd test &&
		echo "error: No such remote: '\''foo'\''" >expect &&
		test_expect_code 2 but remote rm foo 2>actual &&
		test_cmp expect actual
	)
'

test_expect_success 'remove remote with a branch without configured merge' '
	test_when_finished "(
		but -C test checkout main;
		but -C test branch -D two;
		but -C test config --remove-section remote.two;
		but -C test config --remove-section branch.second;
		true
	)" &&
	(
		cd test &&
		but remote add two ../two &&
		but fetch two &&
		but checkout -b second two/main^0 &&
		but config branch.second.remote two &&
		but checkout main &&
		but remote rm two
	)
'

test_expect_success 'rename errors out early when deleting non-existent branch' '
	(
		cd test &&
		echo "error: No such remote: '\''foo'\''" >expect &&
		test_expect_code 2 but remote rename foo bar 2>actual &&
		test_cmp expect actual
	)
'

test_expect_success 'rename errors out early when new name is invalid' '
	test_config remote.foo.vcs bar &&
	echo "fatal: '\''invalid...name'\'' is not a valid remote name" >expect &&
	test_must_fail but remote rename foo invalid...name 2>actual &&
	test_cmp expect actual
'

test_expect_success 'add existing foreign_vcs remote' '
	test_config remote.foo.vcs bar &&
	echo "error: remote foo already exists." >expect &&
	test_expect_code 3 but remote add foo bar 2>actual &&
	test_cmp expect actual
'

test_expect_success 'add existing foreign_vcs remote' '
	test_config remote.foo.vcs bar &&
	test_config remote.bar.vcs bar &&
	echo "error: remote bar already exists." >expect &&
	test_expect_code 3 but remote rename foo bar 2>actual &&
	test_cmp expect actual
'

test_expect_success 'add invalid foreign_vcs remote' '
	echo "fatal: '\''invalid...name'\'' is not a valid remote name" >expect &&
	test_must_fail but remote add invalid...name bar 2>actual &&
	test_cmp expect actual
'

cat >test/expect <<EOF
* remote origin
  Fetch URL: $(pwd)/one
  Push  URL: $(pwd)/one
  HEAD branch: main
  Remote branches:
    main new (next fetch will store in remotes/origin)
    side tracked
  Local branches configured for 'but pull':
    ahead    merges with remote main
    main     merges with remote main
    octopus  merges with remote topic-a
                and with remote topic-b
                and with remote topic-c
    rebase  rebases onto remote main
  Local refs configured for 'but push':
    main pushes to main     (local out of date)
    main pushes to upstream (create)
* remote two
  Fetch URL: ../two
  Push  URL: ../three
  HEAD branch: main
  Local refs configured for 'but push':
    ahead forces to main    (fast-forwardable)
    main  pushes to another (up to date)
EOF

test_expect_success 'show' '
	(
		cd test &&
		but config --add remote.origin.fetch refs/heads/main:refs/heads/upstream &&
		but fetch &&
		but checkout -b ahead origin/main &&
		echo 1 >>file &&
		test_tick &&
		but cummit -m update file &&
		but checkout main &&
		but branch --track octopus origin/main &&
		but branch --track rebase origin/main &&
		but branch -d -r origin/main &&
		but config --add remote.two.url ../two &&
		but config --add remote.two.pushurl ../three &&
		but config branch.rebase.rebase true &&
		but config branch.octopus.merge "topic-a topic-b topic-c" &&
		(
			cd ../one &&
			echo 1 >file &&
			test_tick &&
			but cummit -m update file
		) &&
		but config --add remote.origin.push : &&
		but config --add remote.origin.push refs/heads/main:refs/heads/upstream &&
		but config --add remote.origin.push +refs/tags/lastbackup &&
		but config --add remote.two.push +refs/heads/ahead:refs/heads/main &&
		but config --add remote.two.push refs/heads/main:refs/heads/another &&
		but remote show origin two >output &&
		but branch -d rebase octopus &&
		test_cmp expect output
	)
'

cat >test/expect <<EOF
* remote origin
  Fetch URL: $(pwd)/one
  Push  URL: $(pwd)/one
  HEAD branch: (not queried)
  Remote branches: (status not queried)
    main
    side
  Local branches configured for 'but pull':
    ahead merges with remote main
    main  merges with remote main
  Local refs configured for 'but push' (status not queried):
    (matching)           pushes to (matching)
    refs/heads/main      pushes to refs/heads/upstream
    refs/tags/lastbackup forces to refs/tags/lastbackup
EOF

test_expect_success 'show -n' '
	mv one one.unreachable &&
	(
		cd test &&
		but remote show -n origin >output &&
		mv ../one.unreachable ../one &&
		test_cmp expect output
	)
'

test_expect_success 'prune' '
	(
		cd one &&
		but branch -m side side2
	) &&
	(
		cd test &&
		but fetch origin &&
		but remote prune origin &&
		but rev-parse refs/remotes/origin/side2 &&
		test_must_fail but rev-parse refs/remotes/origin/side
	)
'

test_expect_success 'set-head --delete' '
	(
		cd test &&
		but symbolic-ref refs/remotes/origin/HEAD &&
		but remote set-head --delete origin &&
		test_must_fail but symbolic-ref refs/remotes/origin/HEAD
	)
'

test_expect_success 'set-head --auto' '
	(
		cd test &&
		but remote set-head --auto origin &&
		echo refs/remotes/origin/main >expect &&
		but symbolic-ref refs/remotes/origin/HEAD >output &&
		test_cmp expect output
	)
'

test_expect_success 'set-head --auto has no problem w/multiple HEADs' '
	(
		cd test &&
		but fetch two "refs/heads/*:refs/remotes/two/*" &&
		but remote set-head --auto two >output 2>&1 &&
		echo "two/HEAD set to main" >expect &&
		test_cmp expect output
	)
'

cat >test/expect <<\EOF
refs/remotes/origin/side2
EOF

test_expect_success 'set-head explicit' '
	(
		cd test &&
		but remote set-head origin side2 &&
		but symbolic-ref refs/remotes/origin/HEAD >output &&
		but remote set-head origin main &&
		test_cmp expect output
	)
'

cat >test/expect <<EOF
Pruning origin
URL: $(pwd)/one
 * [would prune] origin/side2
EOF

test_expect_success 'prune --dry-run' '
	but -C one branch -m side2 side &&
	test_when_finished "but -C one branch -m side side2" &&
	(
		cd test &&
		but remote prune --dry-run origin >output &&
		but rev-parse refs/remotes/origin/side2 &&
		test_must_fail but rev-parse refs/remotes/origin/side &&
		test_cmp expect output
	)
'

test_expect_success 'add --mirror && prune' '
	mkdir mirror &&
	(
		cd mirror &&
		but init --bare &&
		but remote add --mirror -f origin ../one
	) &&
	(
		cd one &&
		but branch -m side2 side
	) &&
	(
		cd mirror &&
		but rev-parse --verify refs/heads/side2 &&
		test_must_fail but rev-parse --verify refs/heads/side &&
		but fetch origin &&
		but remote prune origin &&
		test_must_fail but rev-parse --verify refs/heads/side2 &&
		but rev-parse --verify refs/heads/side
	)
'

test_expect_success 'add --mirror=fetch' '
	mkdir mirror-fetch &&
	but init -b main mirror-fetch/parent &&
	(
		cd mirror-fetch/parent &&
		test_cummit one
	) &&
	but init --bare mirror-fetch/child &&
	(
		cd mirror-fetch/child &&
		but remote add --mirror=fetch -f parent ../parent
	)
'

test_expect_success 'fetch mirrors act as mirrors during fetch' '
	(
		cd mirror-fetch/parent &&
		but branch new &&
		but branch -m main renamed
	) &&
	(
		cd mirror-fetch/child &&
		but fetch parent &&
		but rev-parse --verify refs/heads/new &&
		but rev-parse --verify refs/heads/renamed
	)
'

test_expect_success 'fetch mirrors can prune' '
	(
		cd mirror-fetch/child &&
		but remote prune parent &&
		test_must_fail but rev-parse --verify refs/heads/main
	)
'

test_expect_success 'fetch mirrors do not act as mirrors during push' '
	(
		cd mirror-fetch/parent &&
		but checkout HEAD^0
	) &&
	(
		cd mirror-fetch/child &&
		but branch -m renamed renamed2 &&
		but push parent :
	) &&
	(
		cd mirror-fetch/parent &&
		but rev-parse --verify renamed &&
		test_must_fail but rev-parse --verify refs/heads/renamed2
	)
'

test_expect_success 'add fetch mirror with specific branches' '
	but init --bare mirror-fetch/track &&
	(
		cd mirror-fetch/track &&
		but remote add --mirror=fetch -t heads/new parent ../parent
	)
'

test_expect_success 'fetch mirror respects specific branches' '
	(
		cd mirror-fetch/track &&
		but fetch parent &&
		but rev-parse --verify refs/heads/new &&
		test_must_fail but rev-parse --verify refs/heads/renamed
	)
'

test_expect_success 'add --mirror=push' '
	mkdir mirror-push &&
	but init --bare mirror-push/public &&
	but init -b main mirror-push/private &&
	(
		cd mirror-push/private &&
		test_cummit one &&
		but remote add --mirror=push public ../public
	)
'

test_expect_success 'push mirrors act as mirrors during push' '
	(
		cd mirror-push/private &&
		but branch new &&
		but branch -m main renamed &&
		but push public
	) &&
	(
		cd mirror-push/private &&
		but rev-parse --verify refs/heads/new &&
		but rev-parse --verify refs/heads/renamed &&
		test_must_fail but rev-parse --verify refs/heads/main
	)
'

test_expect_success 'push mirrors do not act as mirrors during fetch' '
	(
		cd mirror-push/public &&
		but branch -m renamed renamed2 &&
		but symbolic-ref HEAD refs/heads/renamed2
	) &&
	(
		cd mirror-push/private &&
		but fetch public &&
		but rev-parse --verify refs/heads/renamed &&
		test_must_fail but rev-parse --verify refs/heads/renamed2
	)
'

test_expect_success 'push mirrors do not allow you to specify refs' '
	but init mirror-push/track &&
	(
		cd mirror-push/track &&
		test_must_fail but remote add --mirror=push -t new public ../public
	)
'

test_expect_success 'add alt && prune' '
	mkdir alttst &&
	(
		cd alttst &&
		but init &&
		but remote add -f origin ../one &&
		but config remote.alt.url ../one &&
		but config remote.alt.fetch "+refs/heads/*:refs/remotes/origin/*"
	) &&
	(
		cd one &&
		but branch -m side side2
	) &&
	(
		cd alttst &&
		but rev-parse --verify refs/remotes/origin/side &&
		test_must_fail but rev-parse --verify refs/remotes/origin/side2 &&
		but fetch alt &&
		but remote prune alt &&
		test_must_fail but rev-parse --verify refs/remotes/origin/side &&
		but rev-parse --verify refs/remotes/origin/side2
	)
'

cat >test/expect <<\EOF
some-tag
EOF

test_expect_success 'add with reachable tags (default)' '
	(
		cd one &&
		>foobar &&
		but add foobar &&
		but cummit -m "Foobar" &&
		but tag -a -m "Foobar tag" foobar-tag &&
		but reset --hard HEAD~1 &&
		but tag -a -m "Some tag" some-tag
	) &&
	mkdir add-tags &&
	(
		cd add-tags &&
		but init &&
		but remote add -f origin ../one &&
		but tag -l some-tag >../test/output &&
		but tag -l foobar-tag >>../test/output &&
		test_must_fail but config remote.origin.tagopt
	) &&
	test_cmp test/expect test/output
'

cat >test/expect <<\EOF
some-tag
foobar-tag
--tags
EOF

test_expect_success 'add --tags' '
	rm -rf add-tags &&
	(
		mkdir add-tags &&
		cd add-tags &&
		but init &&
		but remote add -f --tags origin ../one &&
		but tag -l some-tag >../test/output &&
		but tag -l foobar-tag >>../test/output &&
		but config remote.origin.tagopt >>../test/output
	) &&
	test_cmp test/expect test/output
'

cat >test/expect <<\EOF
--no-tags
EOF

test_expect_success 'add --no-tags' '
	rm -rf add-tags &&
	(
		mkdir add-no-tags &&
		cd add-no-tags &&
		but init &&
		but remote add -f --no-tags origin ../one &&
		grep tagOpt .but/config &&
		but tag -l some-tag >../test/output &&
		but tag -l foobar-tag >../test/output &&
		but config remote.origin.tagopt >>../test/output
	) &&
	(
		cd one &&
		but tag -d some-tag foobar-tag
	) &&
	test_cmp test/expect test/output
'

test_expect_success 'reject --no-no-tags' '
	(
		cd add-no-tags &&
		test_must_fail but remote add -f --no-no-tags neworigin ../one
	)
'

cat >one/expect <<\EOF
  apis/main
  apis/side
  drosophila/another
  drosophila/main
  drosophila/side
EOF

test_expect_success 'update' '
	(
		cd one &&
		but remote add drosophila ../two &&
		but remote add apis ../mirror &&
		but remote update &&
		but branch -r >output &&
		test_cmp expect output
	)
'

cat >one/expect <<\EOF
  drosophila/another
  drosophila/main
  drosophila/side
  manduca/main
  manduca/side
  megaloprepus/main
  megaloprepus/side
EOF

test_expect_success 'update with arguments' '
	(
		cd one &&
		for b in $(but branch -r)
		do
		but branch -r -d $b || exit 1
		done &&
		but remote add manduca ../mirror &&
		but remote add megaloprepus ../mirror &&
		but config remotes.phobaeticus "drosophila megaloprepus" &&
		but config remotes.titanus manduca &&
		but remote update phobaeticus titanus &&
		but branch -r >output &&
		test_cmp expect output
	)
'

test_expect_success 'update --prune' '
	(
		cd one &&
		but branch -m side2 side3
	) &&
	(
		cd test &&
		but remote update --prune &&
		(
			cd ../one &&
			but branch -m side3 side2
		) &&
		but rev-parse refs/remotes/origin/side3 &&
		test_must_fail but rev-parse refs/remotes/origin/side2
	)
'

cat >one/expect <<-\EOF
  apis/main
  apis/side
  manduca/main
  manduca/side
  megaloprepus/main
  megaloprepus/side
EOF

test_expect_success 'update default' '
	(
		cd one &&
		for b in $(but branch -r)
		do
		but branch -r -d $b || exit 1
		done &&
		but config remote.drosophila.skipDefaultUpdate true &&
		but remote update default &&
		but branch -r >output &&
		test_cmp expect output
	)
'

cat >one/expect <<\EOF
  drosophila/another
  drosophila/main
  drosophila/side
EOF

test_expect_success 'update default (overridden, with funny whitespace)' '
	(
		cd one &&
		for b in $(but branch -r)
		do
		but branch -r -d $b || exit 1
		done &&
		but config remotes.default "$(printf "\t drosophila  \n")" &&
		but remote update default &&
		but branch -r >output &&
		test_cmp expect output
	)
'

test_expect_success 'update (with remotes.default defined)' '
	(
		cd one &&
		for b in $(but branch -r)
		do
		but branch -r -d $b || exit 1
		done &&
		but config remotes.default "drosophila" &&
		but remote update &&
		but branch -r >output &&
		test_cmp expect output
	)
'

test_expect_success '"remote show" does not show symbolic refs' '
	but clone one three &&
	(
		cd three &&
		but remote show origin >output &&
		! grep "^ *HEAD$" < output &&
		! grep -i stale < output
	)
'

test_expect_success 'reject adding remote with an invalid name' '
	test_must_fail but remote add some:url desired-name
'

# The first three test if the tracking branches are properly renamed,
# the last two ones check if the config is updated.

test_expect_success 'rename a remote' '
	test_config_global remote.pushDefault origin &&
	but clone one four &&
	(
		cd four &&
		but config branch.main.pushRemote origin &&
		BUT_TRACE2_EVENT=$(pwd)/trace \
			but remote rename --progress origin upstream &&
		test_region progress "Renaming remote references" trace &&
		grep "pushRemote" .but/config &&
		test -z "$(but for-each-ref refs/remotes/origin)" &&
		test "$(but symbolic-ref refs/remotes/upstream/HEAD)" = "refs/remotes/upstream/main" &&
		test "$(but rev-parse upstream/main)" = "$(but rev-parse main)" &&
		test "$(but config remote.upstream.fetch)" = "+refs/heads/*:refs/remotes/upstream/*" &&
		test "$(but config branch.main.remote)" = "upstream" &&
		test "$(but config branch.main.pushRemote)" = "upstream" &&
		test "$(but config --global remote.pushDefault)" = "origin"
	)
'

test_expect_success 'rename a remote renames repo remote.pushDefault' '
	but clone one four.1 &&
	(
		cd four.1 &&
		but config remote.pushDefault origin &&
		but remote rename origin upstream &&
		grep pushDefault .but/config &&
		test "$(but config --local remote.pushDefault)" = "upstream"
	)
'

test_expect_success 'rename a remote renames repo remote.pushDefault but ignores global' '
	test_config_global remote.pushDefault other &&
	but clone one four.2 &&
	(
		cd four.2 &&
		but config remote.pushDefault origin &&
		but remote rename origin upstream &&
		test "$(but config --global remote.pushDefault)" = "other" &&
		test "$(but config --local remote.pushDefault)" = "upstream"
	)
'

test_expect_success 'rename a remote renames repo remote.pushDefault but keeps global' '
	test_config_global remote.pushDefault origin &&
	but clone one four.3 &&
	(
		cd four.3 &&
		but config remote.pushDefault origin &&
		but remote rename origin upstream &&
		test "$(but config --global remote.pushDefault)" = "origin" &&
		test "$(but config --local remote.pushDefault)" = "upstream"
	)
'

test_expect_success 'rename does not update a non-default fetch refspec' '
	but clone one four.one &&
	(
		cd four.one &&
		but config remote.origin.fetch +refs/heads/*:refs/heads/origin/* &&
		but remote rename origin upstream &&
		test "$(but config remote.upstream.fetch)" = "+refs/heads/*:refs/heads/origin/*" &&
		but rev-parse -q origin/main
	)
'

test_expect_success 'rename a remote with name part of fetch spec' '
	but clone one four.two &&
	(
		cd four.two &&
		but remote rename origin remote &&
		but remote rename remote upstream &&
		test "$(but config remote.upstream.fetch)" = "+refs/heads/*:refs/remotes/upstream/*"
	)
'

test_expect_success 'rename a remote with name prefix of other remote' '
	but clone one four.three &&
	(
		cd four.three &&
		but remote add o but://example.com/repo.but &&
		but remote rename o upstream &&
		test "$(but rev-parse origin/main)" = "$(but rev-parse main)"
	)
'

test_expect_success 'rename succeeds with existing remote.<target>.prune' '
	but clone one four.four &&
	test_when_finished but config --global --unset remote.upstream.prune &&
	but config --global remote.upstream.prune true &&
	but -C four.four remote rename origin upstream
'

test_expect_success 'remove a remote' '
	test_config_global remote.pushDefault origin &&
	but clone one four.five &&
	(
		cd four.five &&
		but config branch.main.pushRemote origin &&
		but remote remove origin &&
		test -z "$(but for-each-ref refs/remotes/origin)" &&
		test_must_fail but config branch.main.remote &&
		test_must_fail but config branch.main.pushRemote &&
		test "$(but config --global remote.pushDefault)" = "origin"
	)
'

test_expect_success 'remove a remote removes repo remote.pushDefault' '
	but clone one four.five.1 &&
	(
		cd four.five.1 &&
		but config remote.pushDefault origin &&
		but remote remove origin &&
		test_must_fail but config --local remote.pushDefault
	)
'

test_expect_success 'remove a remote removes repo remote.pushDefault but ignores global' '
	test_config_global remote.pushDefault other &&
	but clone one four.five.2 &&
	(
		cd four.five.2 &&
		but config remote.pushDefault origin &&
		but remote remove origin &&
		test "$(but config --global remote.pushDefault)" = "other" &&
		test_must_fail but config --local remote.pushDefault
	)
'

test_expect_success 'remove a remote removes repo remote.pushDefault but keeps global' '
	test_config_global remote.pushDefault origin &&
	but clone one four.five.3 &&
	(
		cd four.five.3 &&
		but config remote.pushDefault origin &&
		but remote remove origin &&
		test "$(but config --global remote.pushDefault)" = "origin" &&
		test_must_fail but config --local remote.pushDefault
	)
'

cat >remotes_origin <<EOF
URL: $(pwd)/one
Push: refs/heads/main:refs/heads/upstream
Push: refs/heads/next:refs/heads/upstream2
Pull: refs/heads/main:refs/heads/origin
Pull: refs/heads/next:refs/heads/origin2
EOF

test_expect_success 'migrate a remote from named file in $BUT_DIR/remotes' '
	but clone one five &&
	origin_url=$(pwd)/one &&
	(
		cd five &&
		but remote remove origin &&
		mkdir -p .but/remotes &&
		cat ../remotes_origin >.but/remotes/origin &&
		but remote rename origin origin &&
		test_path_is_missing .but/remotes/origin &&
		test "$(but config remote.origin.url)" = "$origin_url" &&
		cat >push_expected <<-\EOF &&
		refs/heads/main:refs/heads/upstream
		refs/heads/next:refs/heads/upstream2
		EOF
		cat >fetch_expected <<-\EOF &&
		refs/heads/main:refs/heads/origin
		refs/heads/next:refs/heads/origin2
		EOF
		but config --get-all remote.origin.push >push_actual &&
		but config --get-all remote.origin.fetch >fetch_actual &&
		test_cmp push_expected push_actual &&
		test_cmp fetch_expected fetch_actual
	)
'

test_expect_success 'migrate a remote from named file in $BUT_DIR/branches' '
	but clone one six &&
	origin_url=$(pwd)/one &&
	(
		cd six &&
		but remote rm origin &&
		echo "$origin_url#main" >.but/branches/origin &&
		but remote rename origin origin &&
		test_path_is_missing .but/branches/origin &&
		test "$(but config remote.origin.url)" = "$origin_url" &&
		test "$(but config remote.origin.fetch)" = "refs/heads/main:refs/heads/origin" &&
		test "$(but config remote.origin.push)" = "HEAD:refs/heads/main"
	)
'

test_expect_success 'migrate a remote from named file in $BUT_DIR/branches (2)' '
	but clone one seven &&
	(
		cd seven &&
		but remote rm origin &&
		echo "quux#foom" > .but/branches/origin &&
		but remote rename origin origin &&
		test_path_is_missing .but/branches/origin &&
		test "$(but config remote.origin.url)" = "quux" &&
		test "$(but config remote.origin.fetch)" = "refs/heads/foom:refs/heads/origin" &&
		test "$(but config remote.origin.push)" = "HEAD:refs/heads/foom"
	)
'

test_expect_success 'remote prune to cause a dangling symref' '
	but clone one eight &&
	(
		cd one &&
		but checkout side2 &&
		but branch -D main
	) &&
	(
		cd eight &&
		but remote prune origin
	) >err 2>&1 &&
	test_i18ngrep "has become dangling" err &&

	: And the dangling symref will not cause other annoying errors &&
	(
		cd eight &&
		but branch -a
	) 2>err &&
	! grep "points nowhere" err &&
	(
		cd eight &&
		test_must_fail but branch nomore origin
	) 2>err &&
	test_i18ngrep "dangling symref" err
'

test_expect_success 'show empty remote' '
	test_create_repo empty &&
	but clone empty empty-clone &&
	(
		cd empty-clone &&
		but remote show origin
	)
'

test_expect_success 'remote set-branches requires a remote' '
	test_must_fail but remote set-branches &&
	test_must_fail but remote set-branches --add
'

test_expect_success 'remote set-branches' '
	echo "+refs/heads/*:refs/remotes/scratch/*" >expect.initial &&
	sort <<-\EOF >expect.add &&
	+refs/heads/*:refs/remotes/scratch/*
	+refs/heads/other:refs/remotes/scratch/other
	EOF
	sort <<-\EOF >expect.replace &&
	+refs/heads/maint:refs/remotes/scratch/maint
	+refs/heads/main:refs/remotes/scratch/main
	+refs/heads/next:refs/remotes/scratch/next
	EOF
	sort <<-\EOF >expect.add-two &&
	+refs/heads/maint:refs/remotes/scratch/maint
	+refs/heads/main:refs/remotes/scratch/main
	+refs/heads/next:refs/remotes/scratch/next
	+refs/heads/seen:refs/remotes/scratch/seen
	+refs/heads/t/topic:refs/remotes/scratch/t/topic
	EOF
	sort <<-\EOF >expect.setup-ffonly &&
	refs/heads/main:refs/remotes/scratch/main
	+refs/heads/next:refs/remotes/scratch/next
	EOF
	sort <<-\EOF >expect.respect-ffonly &&
	refs/heads/main:refs/remotes/scratch/main
	+refs/heads/next:refs/remotes/scratch/next
	+refs/heads/seen:refs/remotes/scratch/seen
	EOF

	but clone .but/ setbranches &&
	(
		cd setbranches &&
		but remote rename origin scratch &&
		but config --get-all remote.scratch.fetch >config-result &&
		sort <config-result >../actual.initial &&

		but remote set-branches scratch --add other &&
		but config --get-all remote.scratch.fetch >config-result &&
		sort <config-result >../actual.add &&

		but remote set-branches scratch maint main next &&
		but config --get-all remote.scratch.fetch >config-result &&
		sort <config-result >../actual.replace &&

		but remote set-branches --add scratch seen t/topic &&
		but config --get-all remote.scratch.fetch >config-result &&
		sort <config-result >../actual.add-two &&

		but config --unset-all remote.scratch.fetch &&
		but config remote.scratch.fetch \
			refs/heads/main:refs/remotes/scratch/main &&
		but config --add remote.scratch.fetch \
			+refs/heads/next:refs/remotes/scratch/next &&
		but config --get-all remote.scratch.fetch >config-result &&
		sort <config-result >../actual.setup-ffonly &&

		but remote set-branches --add scratch seen &&
		but config --get-all remote.scratch.fetch >config-result &&
		sort <config-result >../actual.respect-ffonly
	) &&
	test_cmp expect.initial actual.initial &&
	test_cmp expect.add actual.add &&
	test_cmp expect.replace actual.replace &&
	test_cmp expect.add-two actual.add-two &&
	test_cmp expect.setup-ffonly actual.setup-ffonly &&
	test_cmp expect.respect-ffonly actual.respect-ffonly
'

test_expect_success 'remote set-branches with --mirror' '
	echo "+refs/*:refs/*" >expect.initial &&
	echo "+refs/heads/main:refs/heads/main" >expect.replace &&
	but clone --mirror .but/ setbranches-mirror &&
	(
		cd setbranches-mirror &&
		but remote rename origin scratch &&
		but config --get-all remote.scratch.fetch >../actual.initial &&

		but remote set-branches scratch heads/main &&
		but config --get-all remote.scratch.fetch >../actual.replace
	) &&
	test_cmp expect.initial actual.initial &&
	test_cmp expect.replace actual.replace
'

test_expect_success 'new remote' '
	but remote add someremote foo &&
	echo foo >expect &&
	but config --get-all remote.someremote.url >actual &&
	cmp expect actual
'

get_url_test () {
	cat >expect &&
	but remote get-url "$@" >actual &&
	test_cmp expect actual
}

test_expect_success 'get-url on new remote' '
	echo foo | get_url_test someremote &&
	echo foo | get_url_test --all someremote &&
	echo foo | get_url_test --push someremote &&
	echo foo | get_url_test --push --all someremote
'

test_expect_success 'remote set-url with locked config' '
	test_when_finished "rm -f .but/config.lock" &&
	but config --get-all remote.someremote.url >expect &&
	>.but/config.lock &&
	test_must_fail but remote set-url someremote baz &&
	but config --get-all remote.someremote.url >actual &&
	cmp expect actual
'

test_expect_success 'remote set-url bar' '
	but remote set-url someremote bar &&
	echo bar >expect &&
	but config --get-all remote.someremote.url >actual &&
	cmp expect actual
'

test_expect_success 'remote set-url baz bar' '
	but remote set-url someremote baz bar &&
	echo baz >expect &&
	but config --get-all remote.someremote.url >actual &&
	cmp expect actual
'

test_expect_success 'remote set-url zot bar' '
	test_must_fail but remote set-url someremote zot bar &&
	echo baz >expect &&
	but config --get-all remote.someremote.url >actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --push zot baz' '
	test_must_fail but remote set-url --push someremote zot baz &&
	echo "YYY" >expect &&
	echo baz >>expect &&
	test_must_fail but config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	but config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --push zot' '
	but remote set-url --push someremote zot &&
	echo zot >expect &&
	echo "YYY" >>expect &&
	echo baz >>expect &&
	but config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	but config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'get-url with different urls' '
	echo baz | get_url_test someremote &&
	echo baz | get_url_test --all someremote &&
	echo zot | get_url_test --push someremote &&
	echo zot | get_url_test --push --all someremote
'

test_expect_success 'remote set-url --push qux zot' '
	but remote set-url --push someremote qux zot &&
	echo qux >expect &&
	echo "YYY" >>expect &&
	echo baz >>expect &&
	but config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	but config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --push foo qu+x' '
	but remote set-url --push someremote foo qu+x &&
	echo foo >expect &&
	echo "YYY" >>expect &&
	echo baz >>expect &&
	but config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	but config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --push --add aaa' '
	but remote set-url --push --add someremote aaa &&
	echo foo >expect &&
	echo aaa >>expect &&
	echo "YYY" >>expect &&
	echo baz >>expect &&
	but config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	but config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'get-url on multi push remote' '
	echo foo | get_url_test --push someremote &&
	get_url_test --push --all someremote <<-\EOF
	foo
	aaa
	EOF
'

test_expect_success 'remote set-url --push bar aaa' '
	but remote set-url --push someremote bar aaa &&
	echo foo >expect &&
	echo bar >>expect &&
	echo "YYY" >>expect &&
	echo baz >>expect &&
	but config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	but config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --push --delete bar' '
	but remote set-url --push --delete someremote bar &&
	echo foo >expect &&
	echo "YYY" >>expect &&
	echo baz >>expect &&
	but config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	but config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --push --delete foo' '
	but remote set-url --push --delete someremote foo &&
	echo "YYY" >expect &&
	echo baz >>expect &&
	test_must_fail but config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	but config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --add bbb' '
	but remote set-url --add someremote bbb &&
	echo "YYY" >expect &&
	echo baz >>expect &&
	echo bbb >>expect &&
	test_must_fail but config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	but config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'get-url on multi fetch remote' '
	echo baz | get_url_test someremote &&
	get_url_test --all someremote <<-\EOF
	baz
	bbb
	EOF
'

test_expect_success 'remote set-url --delete .*' '
	test_must_fail but remote set-url --delete someremote .\* &&
	echo "YYY" >expect &&
	echo baz >>expect &&
	echo bbb >>expect &&
	test_must_fail but config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	but config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --delete bbb' '
	but remote set-url --delete someremote bbb &&
	echo "YYY" >expect &&
	echo baz >>expect &&
	test_must_fail but config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	but config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --delete baz' '
	test_must_fail but remote set-url --delete someremote baz &&
	echo "YYY" >expect &&
	echo baz >>expect &&
	test_must_fail but config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	but config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --add ccc' '
	but remote set-url --add someremote ccc &&
	echo "YYY" >expect &&
	echo baz >>expect &&
	echo ccc >>expect &&
	test_must_fail but config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	but config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --delete baz' '
	but remote set-url --delete someremote baz &&
	echo "YYY" >expect &&
	echo ccc >>expect &&
	test_must_fail but config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	but config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'extra args: setup' '
	# add a dummy origin so that this does not trigger failure
	but remote add origin .
'

test_extra_arg () {
	test_expect_success "extra args: $*" "
		test_must_fail but remote $* bogus_extra_arg 2>actual &&
		test_i18ngrep '^usage:' actual
	"
}

test_extra_arg add nick url
test_extra_arg rename origin newname
test_extra_arg remove origin
test_extra_arg set-head origin main
# set-branches takes any number of args
test_extra_arg get-url origin newurl
test_extra_arg set-url origin newurl oldurl
# show takes any number of args
# prune takes any number of args
# update takes any number of args

test_expect_success 'add remote matching the "insteadOf" URL' '
	but config url.xyz@example.com.insteadOf backup &&
	but remote add backup xyz@example.com
'

test_expect_success 'unqualified <dst> refspec DWIM and advice' '
	test_when_finished "(cd test && but tag -d some-tag)" &&
	(
		cd test &&
		but tag -a -m "Some tag" some-tag main &&
		for type in cummit tag tree blob
		do
			if test "$type" = "blob"
			then
				oid=$(but rev-parse some-tag:file)
			else
				oid=$(but rev-parse some-tag^{$type})
			fi &&
			test_must_fail but push origin $oid:dst 2>err &&
			test_i18ngrep "error: The destination you" err &&
			test_i18ngrep "hint: Did you mean" err &&
			test_must_fail but -c advice.pushUnqualifiedRefName=false \
				push origin $oid:dst 2>err &&
			test_i18ngrep "error: The destination you" err &&
			test_i18ngrep ! "hint: Did you mean" err ||
			exit 1
		done
	)
'

test_expect_success 'refs/remotes/* <src> refspec and unqualified <dst> DWIM and advice' '
	(
		cd two &&
		but tag -a -m "Some tag" my-tag main &&
		but update-ref refs/trees/my-head-tree HEAD^{tree} &&
		but update-ref refs/blobs/my-file-blob HEAD:file
	) &&
	(
		cd test &&
		but config --add remote.two.fetch "+refs/tags/*:refs/remotes/tags-from-two/*" &&
		but config --add remote.two.fetch "+refs/trees/*:refs/remotes/trees-from-two/*" &&
		but config --add remote.two.fetch "+refs/blobs/*:refs/remotes/blobs-from-two/*" &&
		but fetch --no-tags two &&

		test_must_fail but push origin refs/remotes/two/another:dst 2>err &&
		test_i18ngrep "error: The destination you" err &&

		test_must_fail but push origin refs/remotes/tags-from-two/my-tag:dst-tag 2>err &&
		test_i18ngrep "error: The destination you" err &&

		test_must_fail but push origin refs/remotes/trees-from-two/my-head-tree:dst-tree 2>err &&
		test_i18ngrep "error: The destination you" err &&

		test_must_fail but push origin refs/remotes/blobs-from-two/my-file-blob:dst-blob 2>err &&
		test_i18ngrep "error: The destination you" err
	)
'

test_done
