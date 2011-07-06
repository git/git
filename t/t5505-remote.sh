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
	actual=$(git remote show "$1" | sed -n -e '$p') &&
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

cat > test/expect << EOF
* remote origin
  URL: $(pwd)/one/.git
  Remote branch merged with 'git pull' while on branch master
    master
  New remote branch (next fetch will store in remotes/origin)
    master
  Tracked remote branches
    side master
  Local branches pushed with 'git push'
    master:upstream +refs/tags/lastbackup
EOF

test_expect_success 'show' '
	(cd test &&
	 git config --add remote.origin.fetch \
		refs/heads/master:refs/heads/upstream &&
	 git fetch &&
	 git branch -d -r origin/master &&
	 (cd ../one &&
	  echo 1 > file &&
	  test_tick &&
	  git commit -m update file) &&
	 git config remote.origin.push \
		refs/heads/master:refs/heads/upstream &&
	 git config --add remote.origin.push \
		+refs/tags/lastbackup &&
	 git remote show origin > output &&
	 test_cmp expect output)
'

cat > test/expect << EOF
* remote origin
  URL: $(pwd)/one/.git
  Remote branch merged with 'git pull' while on branch master
    master
  Tracked remote branches
    master side
  Local branches pushed with 'git push'
    master:upstream +refs/tags/lastbackup
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

cat > test/expect << EOF
Pruning origin
URL: $(pwd)/one/.git
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
	 git init &&
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
	 ! grep HEAD < output &&
	 ! grep -i stale < output)

'

test_expect_success 'reject adding remote with an invalid name' '

	test_must_fail git remote add some:url desired-name

'

test_done
