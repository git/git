#!/bin/sh

test_description='git remote porcelain-ish'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

setup_repository () {
	mkdir "$1" && (
	cd "$1" &&
	git init -b main &&
	>file &&
	git add file &&
	test_tick &&
	git commit -m "Initial" &&
	git checkout -b side &&
	>elif &&
	git add elif &&
	test_tick &&
	git commit -m "Second" &&
	git checkout main
	)
}

tokens_match () {
	echo "$1" | tr ' ' '\012' | sort | sed -e '/^$/d' >expect &&
	echo "$2" | tr ' ' '\012' | sort | sed -e '/^$/d' >actual &&
	test_cmp expect actual
}

check_remote_track () {
	actual=$(git remote show "$1" | sed -ne 's|^    \(.*\) tracked$|\1|p')
	shift &&
	tokens_match "$*" "$actual"
}

check_tracking_branch () {
	f="" &&
	r=$(git for-each-ref "--format=%(refname)" |
		sed -ne "s|^refs/remotes/$1/||p") &&
	shift &&
	tokens_match "$*" "$r"
}

test_expect_success setup '
	setup_repository one &&
	setup_repository two &&
	(
		cd two &&
		git branch another
	) &&
	git clone one test
'

test_expect_success 'add remote whose URL agrees with url.<...>.insteadOf' '
	test_config url.git@host.com:team/repo.git.insteadOf myremote &&
	git remote add myremote git@host.com:team/repo.git
'

test_expect_success 'remote information for the origin' '
	(
		cd test &&
		tokens_match origin "$(git remote)" &&
		check_remote_track origin main side &&
		check_tracking_branch origin HEAD main side
	)
'

test_expect_success 'add another remote' '
	(
		cd test &&
		git remote add -f second ../two &&
		tokens_match "origin second" "$(git remote)" &&
		check_tracking_branch second main side another &&
		git for-each-ref "--format=%(refname)" refs/remotes |
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
		case $(git config remote.second.fetch) in
		+*) true ;;
		 *) false ;;
		esac
	)
'

test_expect_success 'remove remote' '
	(
		cd test &&
		git symbolic-ref refs/remotes/second/HEAD refs/remotes/second/main &&
		git remote rm second
	)
'

test_expect_success 'remove remote' '
	(
		cd test &&
		tokens_match origin "$(git remote)" &&
		check_remote_track origin main side &&
		git for-each-ref "--format=%(refname)" refs/remotes |
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
		  git branch -d main
		EOF
		cat >expect2 <<-\EOF &&
		Note: Some branches outside the refs/remotes/ hierarchy were not removed;
		to delete them, use:
		  git branch -d foobranch
		  git branch -d main
		EOF
		git tag footag &&
		git config --add remote.oops.fetch "+refs/*:refs/*" &&
		git remote remove oops 2>actual1 &&
		git branch foobranch &&
		git config --add remote.oops.fetch "+refs/*:refs/*" &&
		git remote rm oops 2>actual2 &&
		git branch -d foobranch &&
		git tag -d footag &&
		test_cmp expect1 actual1 &&
		test_cmp expect2 actual2
	)
'

test_expect_success 'remove errors out early when deleting non-existent branch' '
	(
		cd test &&
		echo "error: No such remote: '\''foo'\''" >expect &&
		test_expect_code 2 git remote rm foo 2>actual &&
		test_cmp expect actual
	)
'

test_expect_success 'remove remote with a branch without configured merge' '
	test_when_finished "(
		git -C test checkout main;
		git -C test branch -D two;
		git -C test config --remove-section remote.two;
		git -C test config --remove-section branch.second;
		true
	)" &&
	(
		cd test &&
		git remote add two ../two &&
		git fetch two &&
		git checkout -b second two/main^0 &&
		git config branch.second.remote two &&
		git checkout main &&
		git remote rm two
	)
'

test_expect_success 'rename errors out early when deleting non-existent branch' '
	(
		cd test &&
		echo "error: No such remote: '\''foo'\''" >expect &&
		test_expect_code 2 git remote rename foo bar 2>actual &&
		test_cmp expect actual
	)
'

test_expect_success 'rename errors out early when new name is invalid' '
	test_config remote.foo.vcs bar &&
	echo "fatal: '\''invalid...name'\'' is not a valid remote name" >expect &&
	test_must_fail git remote rename foo invalid...name 2>actual &&
	test_cmp expect actual
'

test_expect_success 'add existing foreign_vcs remote' '
	test_config remote.foo.vcs bar &&
	echo "error: remote foo already exists." >expect &&
	test_expect_code 3 git remote add foo bar 2>actual &&
	test_cmp expect actual
'

test_expect_success 'add existing foreign_vcs remote' '
	test_config remote.foo.vcs bar &&
	test_config remote.bar.vcs bar &&
	echo "error: remote bar already exists." >expect &&
	test_expect_code 3 git remote rename foo bar 2>actual &&
	test_cmp expect actual
'

test_expect_success 'add invalid foreign_vcs remote' '
	echo "fatal: '\''invalid...name'\'' is not a valid remote name" >expect &&
	test_must_fail git remote add invalid...name bar 2>actual &&
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
  Local branches configured for 'git pull':
    ahead    merges with remote main
    main     merges with remote main
    octopus  merges with remote topic-a
                and with remote topic-b
                and with remote topic-c
    rebase  rebases onto remote main
  Local refs configured for 'git push':
    main pushes to main     (local out of date)
    main pushes to upstream (create)
* remote two
  Fetch URL: ../two
  Push  URL: ../three
  HEAD branch: main
  Local refs configured for 'git push':
    ahead forces to main    (fast-forwardable)
    main  pushes to another (up to date)
EOF

test_expect_success 'show' '
	(
		cd test &&
		git config --add remote.origin.fetch refs/heads/main:refs/heads/upstream &&
		git fetch &&
		git checkout -b ahead origin/main &&
		echo 1 >>file &&
		test_tick &&
		git commit -m update file &&
		git checkout main &&
		git branch --track octopus origin/main &&
		git branch --track rebase origin/main &&
		git branch -d -r origin/main &&
		git config --add remote.two.url ../two &&
		git config --add remote.two.pushurl ../three &&
		git config branch.rebase.rebase true &&
		git config branch.octopus.merge "topic-a topic-b topic-c" &&
		(
			cd ../one &&
			echo 1 >file &&
			test_tick &&
			git commit -m update file
		) &&
		git config --add remote.origin.push : &&
		git config --add remote.origin.push refs/heads/main:refs/heads/upstream &&
		git config --add remote.origin.push +refs/tags/lastbackup &&
		git config --add remote.two.push +refs/heads/ahead:refs/heads/main &&
		git config --add remote.two.push refs/heads/main:refs/heads/another &&
		git remote show origin two >output &&
		git branch -d rebase octopus &&
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
  Local branches configured for 'git pull':
    ahead merges with remote main
    main  merges with remote main
  Local refs configured for 'git push' (status not queried):
    (matching)           pushes to (matching)
    refs/heads/main      pushes to refs/heads/upstream
    refs/tags/lastbackup forces to refs/tags/lastbackup
EOF

test_expect_success 'show -n' '
	mv one one.unreachable &&
	(
		cd test &&
		git remote show -n origin >output &&
		mv ../one.unreachable ../one &&
		test_cmp expect output
	)
'

test_expect_success 'prune' '
	(
		cd one &&
		git branch -m side side2
	) &&
	(
		cd test &&
		git fetch origin &&
		git remote prune origin &&
		git rev-parse refs/remotes/origin/side2 &&
		test_must_fail git rev-parse refs/remotes/origin/side
	)
'

test_expect_success 'set-head --delete' '
	(
		cd test &&
		git symbolic-ref refs/remotes/origin/HEAD &&
		git remote set-head --delete origin &&
		test_must_fail git symbolic-ref refs/remotes/origin/HEAD
	)
'

test_expect_success 'set-head --auto' '
	(
		cd test &&
		git remote set-head --auto origin &&
		echo refs/remotes/origin/main >expect &&
		git symbolic-ref refs/remotes/origin/HEAD >output &&
		test_cmp expect output
	)
'

test_expect_success 'set-head --auto has no problem w/multiple HEADs' '
	(
		cd test &&
		git fetch two "refs/heads/*:refs/remotes/two/*" &&
		git remote set-head --auto two >output 2>&1 &&
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
		git remote set-head origin side2 &&
		git symbolic-ref refs/remotes/origin/HEAD >output &&
		git remote set-head origin main &&
		test_cmp expect output
	)
'

cat >test/expect <<EOF
Pruning origin
URL: $(pwd)/one
 * [would prune] origin/side2
EOF

test_expect_success 'prune --dry-run' '
	git -C one branch -m side2 side &&
	test_when_finished "git -C one branch -m side side2" &&
	(
		cd test &&
		git remote prune --dry-run origin >output &&
		git rev-parse refs/remotes/origin/side2 &&
		test_must_fail git rev-parse refs/remotes/origin/side &&
		test_cmp expect output
	)
'

test_expect_success 'add --mirror && prune' '
	mkdir mirror &&
	(
		cd mirror &&
		git init --bare &&
		git remote add --mirror -f origin ../one
	) &&
	(
		cd one &&
		git branch -m side2 side
	) &&
	(
		cd mirror &&
		git rev-parse --verify refs/heads/side2 &&
		test_must_fail git rev-parse --verify refs/heads/side &&
		git fetch origin &&
		git remote prune origin &&
		test_must_fail git rev-parse --verify refs/heads/side2 &&
		git rev-parse --verify refs/heads/side
	)
'

test_expect_success 'add --mirror=fetch' '
	mkdir mirror-fetch &&
	git init -b main mirror-fetch/parent &&
	(
		cd mirror-fetch/parent &&
		test_commit one
	) &&
	git init --bare mirror-fetch/child &&
	(
		cd mirror-fetch/child &&
		git remote add --mirror=fetch -f parent ../parent
	)
'

test_expect_success 'fetch mirrors act as mirrors during fetch' '
	(
		cd mirror-fetch/parent &&
		git branch new &&
		git branch -m main renamed
	) &&
	(
		cd mirror-fetch/child &&
		git fetch parent &&
		git rev-parse --verify refs/heads/new &&
		git rev-parse --verify refs/heads/renamed
	)
'

test_expect_success 'fetch mirrors can prune' '
	(
		cd mirror-fetch/child &&
		git remote prune parent &&
		test_must_fail git rev-parse --verify refs/heads/main
	)
'

test_expect_success 'fetch mirrors do not act as mirrors during push' '
	(
		cd mirror-fetch/parent &&
		git checkout HEAD^0
	) &&
	(
		cd mirror-fetch/child &&
		git branch -m renamed renamed2 &&
		git push parent :
	) &&
	(
		cd mirror-fetch/parent &&
		git rev-parse --verify renamed &&
		test_must_fail git rev-parse --verify refs/heads/renamed2
	)
'

test_expect_success 'add fetch mirror with specific branches' '
	git init --bare mirror-fetch/track &&
	(
		cd mirror-fetch/track &&
		git remote add --mirror=fetch -t heads/new parent ../parent
	)
'

test_expect_success 'fetch mirror respects specific branches' '
	(
		cd mirror-fetch/track &&
		git fetch parent &&
		git rev-parse --verify refs/heads/new &&
		test_must_fail git rev-parse --verify refs/heads/renamed
	)
'

test_expect_success 'add --mirror=push' '
	mkdir mirror-push &&
	git init --bare mirror-push/public &&
	git init -b main mirror-push/private &&
	(
		cd mirror-push/private &&
		test_commit one &&
		git remote add --mirror=push public ../public
	)
'

test_expect_success 'push mirrors act as mirrors during push' '
	(
		cd mirror-push/private &&
		git branch new &&
		git branch -m main renamed &&
		git push public
	) &&
	(
		cd mirror-push/private &&
		git rev-parse --verify refs/heads/new &&
		git rev-parse --verify refs/heads/renamed &&
		test_must_fail git rev-parse --verify refs/heads/main
	)
'

test_expect_success 'push mirrors do not act as mirrors during fetch' '
	(
		cd mirror-push/public &&
		git branch -m renamed renamed2 &&
		git symbolic-ref HEAD refs/heads/renamed2
	) &&
	(
		cd mirror-push/private &&
		git fetch public &&
		git rev-parse --verify refs/heads/renamed &&
		test_must_fail git rev-parse --verify refs/heads/renamed2
	)
'

test_expect_success 'push mirrors do not allow you to specify refs' '
	git init mirror-push/track &&
	(
		cd mirror-push/track &&
		test_must_fail git remote add --mirror=push -t new public ../public
	)
'

test_expect_success 'add alt && prune' '
	mkdir alttst &&
	(
		cd alttst &&
		git init &&
		git remote add -f origin ../one &&
		git config remote.alt.url ../one &&
		git config remote.alt.fetch "+refs/heads/*:refs/remotes/origin/*"
	) &&
	(
		cd one &&
		git branch -m side side2
	) &&
	(
		cd alttst &&
		git rev-parse --verify refs/remotes/origin/side &&
		test_must_fail git rev-parse --verify refs/remotes/origin/side2 &&
		git fetch alt &&
		git remote prune alt &&
		test_must_fail git rev-parse --verify refs/remotes/origin/side &&
		git rev-parse --verify refs/remotes/origin/side2
	)
'

cat >test/expect <<\EOF
some-tag
EOF

test_expect_success 'add with reachable tags (default)' '
	(
		cd one &&
		>foobar &&
		git add foobar &&
		git commit -m "Foobar" &&
		git tag -a -m "Foobar tag" foobar-tag &&
		git reset --hard HEAD~1 &&
		git tag -a -m "Some tag" some-tag
	) &&
	mkdir add-tags &&
	(
		cd add-tags &&
		git init &&
		git remote add -f origin ../one &&
		git tag -l some-tag >../test/output &&
		git tag -l foobar-tag >>../test/output &&
		test_must_fail git config remote.origin.tagopt
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
		git init &&
		git remote add -f --tags origin ../one &&
		git tag -l some-tag >../test/output &&
		git tag -l foobar-tag >>../test/output &&
		git config remote.origin.tagopt >>../test/output
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
		git init &&
		git remote add -f --no-tags origin ../one &&
		grep tagOpt .git/config &&
		git tag -l some-tag >../test/output &&
		git tag -l foobar-tag >../test/output &&
		git config remote.origin.tagopt >>../test/output
	) &&
	(
		cd one &&
		git tag -d some-tag foobar-tag
	) &&
	test_cmp test/expect test/output
'

test_expect_success 'reject --no-no-tags' '
	(
		cd add-no-tags &&
		test_must_fail git remote add -f --no-no-tags neworigin ../one
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
		git remote add drosophila ../two &&
		git remote add apis ../mirror &&
		git remote update &&
		git branch -r >output &&
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
		for b in $(git branch -r)
		do
		git branch -r -d $b || exit 1
		done &&
		git remote add manduca ../mirror &&
		git remote add megaloprepus ../mirror &&
		git config remotes.phobaeticus "drosophila megaloprepus" &&
		git config remotes.titanus manduca &&
		git remote update phobaeticus titanus &&
		git branch -r >output &&
		test_cmp expect output
	)
'

test_expect_success 'update --prune' '
	(
		cd one &&
		git branch -m side2 side3
	) &&
	(
		cd test &&
		git remote update --prune &&
		(
			cd ../one &&
			git branch -m side3 side2
		) &&
		git rev-parse refs/remotes/origin/side3 &&
		test_must_fail git rev-parse refs/remotes/origin/side2
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
		for b in $(git branch -r)
		do
		git branch -r -d $b || exit 1
		done &&
		git config remote.drosophila.skipDefaultUpdate true &&
		git remote update default &&
		git branch -r >output &&
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
		for b in $(git branch -r)
		do
		git branch -r -d $b || exit 1
		done &&
		git config remotes.default "$(printf "\t drosophila  \n")" &&
		git remote update default &&
		git branch -r >output &&
		test_cmp expect output
	)
'

test_expect_success 'update (with remotes.default defined)' '
	(
		cd one &&
		for b in $(git branch -r)
		do
		git branch -r -d $b || exit 1
		done &&
		git config remotes.default "drosophila" &&
		git remote update &&
		git branch -r >output &&
		test_cmp expect output
	)
'

test_expect_success '"remote show" does not show symbolic refs' '
	git clone one three &&
	(
		cd three &&
		git remote show origin >output &&
		! grep "^ *HEAD$" < output &&
		! grep -i stale < output
	)
'

test_expect_success 'reject adding remote with an invalid name' '
	test_must_fail git remote add some:url desired-name
'

# The first three test if the tracking branches are properly renamed,
# the last two ones check if the config is updated.

test_expect_success 'rename a remote' '
	test_config_global remote.pushDefault origin &&
	git clone one four &&
	(
		cd four &&
		git config branch.main.pushRemote origin &&
		git remote rename origin upstream &&
		grep "pushRemote" .git/config &&
		test -z "$(git for-each-ref refs/remotes/origin)" &&
		test "$(git symbolic-ref refs/remotes/upstream/HEAD)" = "refs/remotes/upstream/main" &&
		test "$(git rev-parse upstream/main)" = "$(git rev-parse main)" &&
		test "$(git config remote.upstream.fetch)" = "+refs/heads/*:refs/remotes/upstream/*" &&
		test "$(git config branch.main.remote)" = "upstream" &&
		test "$(git config branch.main.pushRemote)" = "upstream" &&
		test "$(git config --global remote.pushDefault)" = "origin"
	)
'

test_expect_success 'rename a remote renames repo remote.pushDefault' '
	git clone one four.1 &&
	(
		cd four.1 &&
		git config remote.pushDefault origin &&
		git remote rename origin upstream &&
		grep pushDefault .git/config &&
		test "$(git config --local remote.pushDefault)" = "upstream"
	)
'

test_expect_success 'rename a remote renames repo remote.pushDefault but ignores global' '
	test_config_global remote.pushDefault other &&
	git clone one four.2 &&
	(
		cd four.2 &&
		git config remote.pushDefault origin &&
		git remote rename origin upstream &&
		test "$(git config --global remote.pushDefault)" = "other" &&
		test "$(git config --local remote.pushDefault)" = "upstream"
	)
'

test_expect_success 'rename a remote renames repo remote.pushDefault but keeps global' '
	test_config_global remote.pushDefault origin &&
	git clone one four.3 &&
	(
		cd four.3 &&
		git config remote.pushDefault origin &&
		git remote rename origin upstream &&
		test "$(git config --global remote.pushDefault)" = "origin" &&
		test "$(git config --local remote.pushDefault)" = "upstream"
	)
'

test_expect_success 'rename does not update a non-default fetch refspec' '
	git clone one four.one &&
	(
		cd four.one &&
		git config remote.origin.fetch +refs/heads/*:refs/heads/origin/* &&
		git remote rename origin upstream &&
		test "$(git config remote.upstream.fetch)" = "+refs/heads/*:refs/heads/origin/*" &&
		git rev-parse -q origin/main
	)
'

test_expect_success 'rename a remote with name part of fetch spec' '
	git clone one four.two &&
	(
		cd four.two &&
		git remote rename origin remote &&
		git remote rename remote upstream &&
		test "$(git config remote.upstream.fetch)" = "+refs/heads/*:refs/remotes/upstream/*"
	)
'

test_expect_success 'rename a remote with name prefix of other remote' '
	git clone one four.three &&
	(
		cd four.three &&
		git remote add o git://example.com/repo.git &&
		git remote rename o upstream &&
		test "$(git rev-parse origin/main)" = "$(git rev-parse main)"
	)
'

test_expect_success 'rename succeeds with existing remote.<target>.prune' '
	git clone one four.four &&
	test_when_finished git config --global --unset remote.upstream.prune &&
	git config --global remote.upstream.prune true &&
	git -C four.four remote rename origin upstream
'

test_expect_success 'remove a remote' '
	test_config_global remote.pushDefault origin &&
	git clone one four.five &&
	(
		cd four.five &&
		git config branch.main.pushRemote origin &&
		git remote remove origin &&
		test -z "$(git for-each-ref refs/remotes/origin)" &&
		test_must_fail git config branch.main.remote &&
		test_must_fail git config branch.main.pushRemote &&
		test "$(git config --global remote.pushDefault)" = "origin"
	)
'

test_expect_success 'remove a remote removes repo remote.pushDefault' '
	git clone one four.five.1 &&
	(
		cd four.five.1 &&
		git config remote.pushDefault origin &&
		git remote remove origin &&
		test_must_fail git config --local remote.pushDefault
	)
'

test_expect_success 'remove a remote removes repo remote.pushDefault but ignores global' '
	test_config_global remote.pushDefault other &&
	git clone one four.five.2 &&
	(
		cd four.five.2 &&
		git config remote.pushDefault origin &&
		git remote remove origin &&
		test "$(git config --global remote.pushDefault)" = "other" &&
		test_must_fail git config --local remote.pushDefault
	)
'

test_expect_success 'remove a remote removes repo remote.pushDefault but keeps global' '
	test_config_global remote.pushDefault origin &&
	git clone one four.five.3 &&
	(
		cd four.five.3 &&
		git config remote.pushDefault origin &&
		git remote remove origin &&
		test "$(git config --global remote.pushDefault)" = "origin" &&
		test_must_fail git config --local remote.pushDefault
	)
'

cat >remotes_origin <<EOF
URL: $(pwd)/one
Push: refs/heads/main:refs/heads/upstream
Push: refs/heads/next:refs/heads/upstream2
Pull: refs/heads/main:refs/heads/origin
Pull: refs/heads/next:refs/heads/origin2
EOF

test_expect_success 'migrate a remote from named file in $GIT_DIR/remotes' '
	git clone one five &&
	origin_url=$(pwd)/one &&
	(
		cd five &&
		git remote remove origin &&
		mkdir -p .git/remotes &&
		cat ../remotes_origin >.git/remotes/origin &&
		git remote rename origin origin &&
		test_path_is_missing .git/remotes/origin &&
		test "$(git config remote.origin.url)" = "$origin_url" &&
		cat >push_expected <<-\EOF &&
		refs/heads/main:refs/heads/upstream
		refs/heads/next:refs/heads/upstream2
		EOF
		cat >fetch_expected <<-\EOF &&
		refs/heads/main:refs/heads/origin
		refs/heads/next:refs/heads/origin2
		EOF
		git config --get-all remote.origin.push >push_actual &&
		git config --get-all remote.origin.fetch >fetch_actual &&
		test_cmp push_expected push_actual &&
		test_cmp fetch_expected fetch_actual
	)
'

test_expect_success 'migrate a remote from named file in $GIT_DIR/branches' '
	git clone one six &&
	origin_url=$(pwd)/one &&
	(
		cd six &&
		git remote rm origin &&
		mkdir -p .git/branches &&
		echo "$origin_url#main" >.git/branches/origin &&
		git remote rename origin origin &&
		test_path_is_missing .git/branches/origin &&
		test "$(git config remote.origin.url)" = "$origin_url" &&
		test "$(git config remote.origin.fetch)" = "refs/heads/main:refs/heads/origin" &&
		test "$(git config remote.origin.push)" = "HEAD:refs/heads/main"
	)
'

test_expect_success 'migrate a remote from named file in $GIT_DIR/branches (2)' '
	git clone one seven &&
	(
		cd seven &&
		git remote rm origin &&
		mkdir -p .git/branches &&
		echo "quux#foom" > .git/branches/origin &&
		git remote rename origin origin &&
		test_path_is_missing .git/branches/origin &&
		test "$(git config remote.origin.url)" = "quux" &&
		test "$(git config remote.origin.fetch)" = "refs/heads/foom:refs/heads/origin" &&
		test "$(git config remote.origin.push)" = "HEAD:refs/heads/foom"
	)
'

test_expect_success 'remote prune to cause a dangling symref' '
	git clone one eight &&
	(
		cd one &&
		git checkout side2 &&
		git branch -D main
	) &&
	(
		cd eight &&
		git remote prune origin
	) >err 2>&1 &&
	test_i18ngrep "has become dangling" err &&

	: And the dangling symref will not cause other annoying errors &&
	(
		cd eight &&
		git branch -a
	) 2>err &&
	! grep "points nowhere" err &&
	(
		cd eight &&
		test_must_fail git branch nomore origin
	) 2>err &&
	test_i18ngrep "dangling symref" err
'

test_expect_success 'show empty remote' '
	test_create_repo empty &&
	git clone empty empty-clone &&
	(
		cd empty-clone &&
		git remote show origin
	)
'

test_expect_success 'remote set-branches requires a remote' '
	test_must_fail git remote set-branches &&
	test_must_fail git remote set-branches --add
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

	git clone .git/ setbranches &&
	(
		cd setbranches &&
		git remote rename origin scratch &&
		git config --get-all remote.scratch.fetch >config-result &&
		sort <config-result >../actual.initial &&

		git remote set-branches scratch --add other &&
		git config --get-all remote.scratch.fetch >config-result &&
		sort <config-result >../actual.add &&

		git remote set-branches scratch maint main next &&
		git config --get-all remote.scratch.fetch >config-result &&
		sort <config-result >../actual.replace &&

		git remote set-branches --add scratch seen t/topic &&
		git config --get-all remote.scratch.fetch >config-result &&
		sort <config-result >../actual.add-two &&

		git config --unset-all remote.scratch.fetch &&
		git config remote.scratch.fetch \
			refs/heads/main:refs/remotes/scratch/main &&
		git config --add remote.scratch.fetch \
			+refs/heads/next:refs/remotes/scratch/next &&
		git config --get-all remote.scratch.fetch >config-result &&
		sort <config-result >../actual.setup-ffonly &&

		git remote set-branches --add scratch seen &&
		git config --get-all remote.scratch.fetch >config-result &&
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
	git clone --mirror .git/ setbranches-mirror &&
	(
		cd setbranches-mirror &&
		git remote rename origin scratch &&
		git config --get-all remote.scratch.fetch >../actual.initial &&

		git remote set-branches scratch heads/main &&
		git config --get-all remote.scratch.fetch >../actual.replace
	) &&
	test_cmp expect.initial actual.initial &&
	test_cmp expect.replace actual.replace
'

test_expect_success 'new remote' '
	git remote add someremote foo &&
	echo foo >expect &&
	git config --get-all remote.someremote.url >actual &&
	cmp expect actual
'

get_url_test () {
	cat >expect &&
	git remote get-url "$@" >actual &&
	test_cmp expect actual
}

test_expect_success 'get-url on new remote' '
	echo foo | get_url_test someremote &&
	echo foo | get_url_test --all someremote &&
	echo foo | get_url_test --push someremote &&
	echo foo | get_url_test --push --all someremote
'

test_expect_success 'remote set-url with locked config' '
	test_when_finished "rm -f .git/config.lock" &&
	git config --get-all remote.someremote.url >expect &&
	>.git/config.lock &&
	test_must_fail git remote set-url someremote baz &&
	git config --get-all remote.someremote.url >actual &&
	cmp expect actual
'

test_expect_success 'remote set-url bar' '
	git remote set-url someremote bar &&
	echo bar >expect &&
	git config --get-all remote.someremote.url >actual &&
	cmp expect actual
'

test_expect_success 'remote set-url baz bar' '
	git remote set-url someremote baz bar &&
	echo baz >expect &&
	git config --get-all remote.someremote.url >actual &&
	cmp expect actual
'

test_expect_success 'remote set-url zot bar' '
	test_must_fail git remote set-url someremote zot bar &&
	echo baz >expect &&
	git config --get-all remote.someremote.url >actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --push zot baz' '
	test_must_fail git remote set-url --push someremote zot baz &&
	echo "YYY" >expect &&
	echo baz >>expect &&
	test_must_fail git config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	git config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --push zot' '
	git remote set-url --push someremote zot &&
	echo zot >expect &&
	echo "YYY" >>expect &&
	echo baz >>expect &&
	git config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	git config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'get-url with different urls' '
	echo baz | get_url_test someremote &&
	echo baz | get_url_test --all someremote &&
	echo zot | get_url_test --push someremote &&
	echo zot | get_url_test --push --all someremote
'

test_expect_success 'remote set-url --push qux zot' '
	git remote set-url --push someremote qux zot &&
	echo qux >expect &&
	echo "YYY" >>expect &&
	echo baz >>expect &&
	git config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	git config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --push foo qu+x' '
	git remote set-url --push someremote foo qu+x &&
	echo foo >expect &&
	echo "YYY" >>expect &&
	echo baz >>expect &&
	git config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	git config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --push --add aaa' '
	git remote set-url --push --add someremote aaa &&
	echo foo >expect &&
	echo aaa >>expect &&
	echo "YYY" >>expect &&
	echo baz >>expect &&
	git config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	git config --get-all remote.someremote.url >>actual &&
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
	git remote set-url --push someremote bar aaa &&
	echo foo >expect &&
	echo bar >>expect &&
	echo "YYY" >>expect &&
	echo baz >>expect &&
	git config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	git config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --push --delete bar' '
	git remote set-url --push --delete someremote bar &&
	echo foo >expect &&
	echo "YYY" >>expect &&
	echo baz >>expect &&
	git config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	git config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --push --delete foo' '
	git remote set-url --push --delete someremote foo &&
	echo "YYY" >expect &&
	echo baz >>expect &&
	test_must_fail git config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	git config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --add bbb' '
	git remote set-url --add someremote bbb &&
	echo "YYY" >expect &&
	echo baz >>expect &&
	echo bbb >>expect &&
	test_must_fail git config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	git config --get-all remote.someremote.url >>actual &&
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
	test_must_fail git remote set-url --delete someremote .\* &&
	echo "YYY" >expect &&
	echo baz >>expect &&
	echo bbb >>expect &&
	test_must_fail git config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	git config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --delete bbb' '
	git remote set-url --delete someremote bbb &&
	echo "YYY" >expect &&
	echo baz >>expect &&
	test_must_fail git config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	git config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --delete baz' '
	test_must_fail git remote set-url --delete someremote baz &&
	echo "YYY" >expect &&
	echo baz >>expect &&
	test_must_fail git config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	git config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --add ccc' '
	git remote set-url --add someremote ccc &&
	echo "YYY" >expect &&
	echo baz >>expect &&
	echo ccc >>expect &&
	test_must_fail git config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	git config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'remote set-url --delete baz' '
	git remote set-url --delete someremote baz &&
	echo "YYY" >expect &&
	echo ccc >>expect &&
	test_must_fail git config --get-all remote.someremote.pushurl >actual &&
	echo "YYY" >>actual &&
	git config --get-all remote.someremote.url >>actual &&
	cmp expect actual
'

test_expect_success 'extra args: setup' '
	# add a dummy origin so that this does not trigger failure
	git remote add origin .
'

test_extra_arg () {
	test_expect_success "extra args: $*" "
		test_must_fail git remote $* bogus_extra_arg 2>actual &&
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
	git config url.xyz@example.com.insteadOf backup &&
	git remote add backup xyz@example.com
'

test_expect_success 'unqualified <dst> refspec DWIM and advice' '
	test_when_finished "(cd test && git tag -d some-tag)" &&
	(
		cd test &&
		git tag -a -m "Some tag" some-tag main &&
		exit_with=true &&
		for type in commit tag tree blob
		do
			if test "$type" = "blob"
			then
				oid=$(git rev-parse some-tag:file)
			else
				oid=$(git rev-parse some-tag^{$type})
			fi &&
			test_must_fail git push origin $oid:dst 2>err &&
			test_i18ngrep "error: The destination you" err &&
			test_i18ngrep "hint: Did you mean" err &&
			test_must_fail git -c advice.pushUnqualifiedRefName=false \
				push origin $oid:dst 2>err &&
			test_i18ngrep "error: The destination you" err &&
			test_i18ngrep ! "hint: Did you mean" err ||
			exit_with=false
		done &&
		$exit_with
	)
'

test_expect_success 'refs/remotes/* <src> refspec and unqualified <dst> DWIM and advice' '
	(
		cd two &&
		git tag -a -m "Some tag" my-tag main &&
		git update-ref refs/trees/my-head-tree HEAD^{tree} &&
		git update-ref refs/blobs/my-file-blob HEAD:file
	) &&
	(
		cd test &&
		git config --add remote.two.fetch "+refs/tags/*:refs/remotes/tags-from-two/*" &&
		git config --add remote.two.fetch "+refs/trees/*:refs/remotes/trees-from-two/*" &&
		git config --add remote.two.fetch "+refs/blobs/*:refs/remotes/blobs-from-two/*" &&
		git fetch --no-tags two &&

		test_must_fail git push origin refs/remotes/two/another:dst 2>err &&
		test_i18ngrep "error: The destination you" err &&

		test_must_fail git push origin refs/remotes/tags-from-two/my-tag:dst-tag 2>err &&
		test_i18ngrep "error: The destination you" err &&

		test_must_fail git push origin refs/remotes/trees-from-two/my-head-tree:dst-tree 2>err &&
		test_i18ngrep "error: The destination you" err &&

		test_must_fail git push origin refs/remotes/blobs-from-two/my-file-blob:dst-blob 2>err &&
		test_i18ngrep "error: The destination you" err
	)
'

test_done
