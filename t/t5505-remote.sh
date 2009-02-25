#!/bin/sh

test_description='git remote porcelain-ish'

. ./test-lib.sh

setup_repository () {
	mkdir "$1" && (
	cd "$1" &&
	git init &&
	>file &&
	git add file &&
	test_tick &&
	git commit -m "Initial" &&
	git checkout -b side &&
	>elif &&
	git add elif &&
	test_tick &&
	git commit -m "Second" &&
	git checkout master
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
		cd two && git branch another
	) &&
	git clone one test

'

test_expect_success 'remote information for the origin' '
(
	cd test &&
	tokens_match origin "$(git remote)" &&
	check_remote_track origin master side &&
	check_tracking_branch origin HEAD master side
)
'

test_expect_success 'add another remote' '
(
	cd test &&
	git remote add -f second ../two &&
	tokens_match "origin second" "$(git remote)" &&
	check_remote_track origin master side &&
	check_remote_track second master side another &&
	check_tracking_branch second master side another &&
	git for-each-ref "--format=%(refname)" refs/remotes |
	sed -e "/^refs\/remotes\/origin\//d" \
	    -e "/^refs\/remotes\/second\//d" >actual &&
	>expect &&
	test_cmp expect actual
)
'

test_expect_success 'remote forces tracking branches' '
(
	cd test &&
	case `git config remote.second.fetch` in
	+*) true ;;
	 *) false ;;
	esac
)
'

test_expect_success 'remove remote' '
(
	cd test &&
	git symbolic-ref refs/remotes/second/HEAD refs/remotes/second/master &&
	git remote rm second
)
'

test_expect_success 'remove remote' '
(
	cd test &&
	tokens_match origin "$(git remote)" &&
	check_remote_track origin master side &&
	git for-each-ref "--format=%(refname)" refs/remotes |
	sed -e "/^refs\/remotes\/origin\//d" >actual &&
	>expect &&
	test_cmp expect actual
)
'

test_expect_success 'remove remote protects non-remote branches' '
(
	cd test &&
	(cat >expect1 <<EOF
Note: A non-remote branch was not removed; to delete it, use:
  git branch -d master
EOF
    cat >expect2 <<EOF
Note: Non-remote branches were not removed; to delete them, use:
  git branch -d foobranch
  git branch -d master
EOF
) &&
	git tag footag
	git config --add remote.oops.fetch "+refs/*:refs/*" &&
	git remote rm oops 2>actual1 &&
	git branch foobranch &&
	git config --add remote.oops.fetch "+refs/*:refs/*" &&
	git remote rm oops 2>actual2 &&
	git branch -d foobranch &&
	git tag -d footag &&
	test_cmp expect1 actual1 &&
	test_cmp expect2 actual2
)
'

cat > test/expect << EOF
* remote origin
  URL: $(pwd)/one
  HEAD branch: master
  Remote branches:
    master new (next fetch will store in remotes/origin)
    side   tracked
  Local branches configured for 'git pull':
    ahead    merges with remote master
    master   merges with remote master
    octopus  merges with remote topic-a
                and with remote topic-b
                and with remote topic-c
    rebase  rebases onto remote master
  Local refs configured for 'git push':
    master pushes to master   (local out of date)
    master pushes to upstream (create)
* remote two
  URL: ../two
  HEAD branch (remote HEAD is ambiguous, may be one of the following):
    another
    master
  Local refs configured for 'git push':
    ahead  forces to master  (fast forwardable)
    master pushes to another (up to date)
EOF

test_expect_success 'show' '
	(cd test &&
	 git config --add remote.origin.fetch refs/heads/master:refs/heads/upstream &&
	 git fetch &&
	 git checkout -b ahead origin/master &&
	 echo 1 >> file &&
	 test_tick &&
	 git commit -m update file &&
	 git checkout master &&
	 git branch --track octopus origin/master &&
	 git branch --track rebase origin/master &&
	 git branch -d -r origin/master &&
	 git config --add remote.two.url ../two &&
	 git config branch.rebase.rebase true &&
	 git config branch.octopus.merge "topic-a topic-b topic-c" &&
	 (cd ../one &&
	  echo 1 > file &&
	  test_tick &&
	  git commit -m update file) &&
	 git config --add remote.origin.push : &&
	 git config --add remote.origin.push refs/heads/master:refs/heads/upstream &&
	 git config --add remote.origin.push +refs/tags/lastbackup &&
	 git config --add remote.two.push +refs/heads/ahead:refs/heads/master &&
	 git config --add remote.two.push refs/heads/master:refs/heads/another &&
	 git remote show origin two > output &&
	 git branch -d rebase octopus &&
	 test_cmp expect output)
'

cat > test/expect << EOF
* remote origin
  URL: $(pwd)/one
  HEAD branch: (not queried)
  Remote branches: (status not queried)
    master
    side
  Local branches configured for 'git pull':
    ahead  merges with remote master
    master merges with remote master
  Local refs configured for 'git push' (status not queried):
    (matching)           pushes to (matching)
    refs/heads/master    pushes to refs/heads/upstream
    refs/tags/lastbackup forces to refs/tags/lastbackup
EOF

test_expect_success 'show -n' '
	(mv one one.unreachable &&
	 cd test &&
	 git remote show -n origin > output &&
	 mv ../one.unreachable ../one &&
	 test_cmp expect output)
'

test_expect_success 'prune' '
	(cd one &&
	 git branch -m side side2) &&
	(cd test &&
	 git fetch origin &&
	 git remote prune origin &&
	 git rev-parse refs/remotes/origin/side2 &&
	 test_must_fail git rev-parse refs/remotes/origin/side)
'

test_expect_success 'set-head --delete' '
	(cd test &&
	 git symbolic-ref refs/remotes/origin/HEAD &&
	 git remote set-head --delete origin &&
	 test_must_fail git symbolic-ref refs/remotes/origin/HEAD)
'

test_expect_success 'set-head --auto' '
	(cd test &&
	 git remote set-head --auto origin &&
	 echo refs/remotes/origin/master >expect &&
	 git symbolic-ref refs/remotes/origin/HEAD >output &&
	 test_cmp expect output
	)
'

cat >test/expect <<EOF
error: Multiple remote HEAD branches. Please choose one explicitly with:
  git remote set-head two another
  git remote set-head two master
EOF

test_expect_success 'set-head --auto fails w/multiple HEADs' '
	(cd test &&
	 test_must_fail git remote set-head --auto two >output 2>&1 &&
	test_cmp expect output)
'

cat >test/expect <<EOF
refs/remotes/origin/side2
EOF

test_expect_success 'set-head explicit' '
	(cd test &&
	 git remote set-head origin side2 &&
	 git symbolic-ref refs/remotes/origin/HEAD >output &&
	 git remote set-head origin master &&
	 test_cmp expect output)
'

cat > test/expect << EOF
Pruning origin
URL: $(pwd)/one
 * [would prune] origin/side2
EOF

test_expect_success 'prune --dry-run' '
	(cd one &&
	 git branch -m side2 side) &&
	(cd test &&
	 git remote prune --dry-run origin > output &&
	 git rev-parse refs/remotes/origin/side2 &&
	 test_must_fail git rev-parse refs/remotes/origin/side &&
	(cd ../one &&
	 git branch -m side side2) &&
	 test_cmp expect output)
'

test_expect_success 'add --mirror && prune' '
	(mkdir mirror &&
	 cd mirror &&
	 git init --bare &&
	 git remote add --mirror -f origin ../one) &&
	(cd one &&
	 git branch -m side2 side) &&
	(cd mirror &&
	 git rev-parse --verify refs/heads/side2 &&
	 test_must_fail git rev-parse --verify refs/heads/side &&
	 git fetch origin &&
	 git remote prune origin &&
	 test_must_fail git rev-parse --verify refs/heads/side2 &&
	 git rev-parse --verify refs/heads/side)
'

test_expect_success 'add alt && prune' '
	(mkdir alttst &&
	 cd alttst &&
	 git init &&
	 git remote add -f origin ../one &&
	 git config remote.alt.url ../one &&
	 git config remote.alt.fetch "+refs/heads/*:refs/remotes/origin/*") &&
	(cd one &&
	 git branch -m side side2) &&
	(cd alttst &&
	 git rev-parse --verify refs/remotes/origin/side &&
	 test_must_fail git rev-parse --verify refs/remotes/origin/side2 &&
	 git fetch alt &&
	 git remote prune alt &&
	 test_must_fail git rev-parse --verify refs/remotes/origin/side &&
	 git rev-parse --verify refs/remotes/origin/side2)
'

cat > one/expect << EOF
  apis/master
  apis/side
  drosophila/another
  drosophila/master
  drosophila/side
EOF

test_expect_success 'update' '

	(cd one &&
	 git remote add drosophila ../two &&
	 git remote add apis ../mirror &&
	 git remote update &&
	 git branch -r > output &&
	 test_cmp expect output)

'

cat > one/expect << EOF
  drosophila/another
  drosophila/master
  drosophila/side
  manduca/master
  manduca/side
  megaloprepus/master
  megaloprepus/side
EOF

test_expect_success 'update with arguments' '

	(cd one &&
	 for b in $(git branch -r)
	 do
		git branch -r -d $b || break
	 done &&
	 git remote add manduca ../mirror &&
	 git remote add megaloprepus ../mirror &&
	 git config remotes.phobaeticus "drosophila megaloprepus" &&
	 git config remotes.titanus manduca &&
	 git remote update phobaeticus titanus &&
	 git branch -r > output &&
	 test_cmp expect output)

'

cat > one/expect << EOF
  apis/master
  apis/side
  manduca/master
  manduca/side
  megaloprepus/master
  megaloprepus/side
EOF

test_expect_success 'update default' '

	(cd one &&
	 for b in $(git branch -r)
	 do
		git branch -r -d $b || break
	 done &&
	 git config remote.drosophila.skipDefaultUpdate true &&
	 git remote update default &&
	 git branch -r > output &&
	 test_cmp expect output)

'

cat > one/expect << EOF
  drosophila/another
  drosophila/master
  drosophila/side
EOF

test_expect_success 'update default (overridden, with funny whitespace)' '

	(cd one &&
	 for b in $(git branch -r)
	 do
		git branch -r -d $b || break
	 done &&
	 git config remotes.default "$(printf "\t drosophila  \n")" &&
	 git remote update default &&
	 git branch -r > output &&
	 test_cmp expect output)

'

test_expect_success '"remote show" does not show symbolic refs' '

	git clone one three &&
	(cd three &&
	 git remote show origin > output &&
	 ! grep "^ *HEAD$" < output &&
	 ! grep -i stale < output)

'

test_expect_success 'reject adding remote with an invalid name' '

	test_must_fail git remote add some:url desired-name

'

# The first three test if the tracking branches are properly renamed,
# the last two ones check if the config is updated.

test_expect_success 'rename a remote' '

	git clone one four &&
	(cd four &&
	 git remote rename origin upstream &&
	 rmdir .git/refs/remotes/origin &&
	 test "$(git symbolic-ref refs/remotes/upstream/HEAD)" = "refs/remotes/upstream/master" &&
	 test "$(git rev-parse upstream/master)" = "$(git rev-parse master)" &&
	 test "$(git config remote.upstream.fetch)" = "+refs/heads/*:refs/remotes/upstream/*" &&
	 test "$(git config branch.master.remote)" = "upstream")

'

cat > remotes_origin << EOF
URL: $(pwd)/one
Push: refs/heads/master:refs/heads/upstream
Pull: refs/heads/master:refs/heads/origin
EOF

test_expect_success 'migrate a remote from named file in $GIT_DIR/remotes' '
	git clone one five &&
	origin_url=$(pwd)/one &&
	(cd five &&
	 git remote rm origin &&
	 mkdir -p .git/remotes &&
	 cat ../remotes_origin > .git/remotes/origin &&
	 git remote rename origin origin &&
	 ! test -f .git/remotes/origin &&
	 test "$(git config remote.origin.url)" = "$origin_url" &&
	 test "$(git config remote.origin.push)" = "refs/heads/master:refs/heads/upstream" &&
	 test "$(git config remote.origin.fetch)" = "refs/heads/master:refs/heads/origin")
'

test_expect_success 'migrate a remote from named file in $GIT_DIR/branches' '
	git clone one six &&
	origin_url=$(pwd)/one &&
	(cd six &&
	 git remote rm origin &&
	 echo "$origin_url" > .git/branches/origin &&
	 git remote rename origin origin &&
	 ! test -f .git/branches/origin &&
	 test "$(git config remote.origin.url)" = "$origin_url" &&
	 test "$(git config remote.origin.fetch)" = "refs/heads/master:refs/heads/origin")
'

test_expect_success 'remote prune to cause a dangling symref' '
	git clone one seven &&
	(
		cd one &&
		git checkout side2 &&
		git branch -D master
	) &&
	(
		cd seven &&
		git remote prune origin
	) 2>err &&
	grep "has become dangling" err &&

	: And the dangling symref will not cause other annoying errors
	(
		cd seven &&
		git branch -a
	) 2>err &&
	! grep "points nowhere" err
	(
		cd seven &&
		test_must_fail git branch nomore origin
	) 2>err &&
	grep "dangling symref" err
'

test_done

