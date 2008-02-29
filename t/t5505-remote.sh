#!/bin/sh

test_description='git remote porcelain-ish'

. ./test-lib.sh

setup_repository () {
	mkdir "$1" && (
	cd "$1" &&
	git init &&
	>file &&
	git add file &&
	git commit -m "Initial" &&
	git checkout -b side &&
	>elif &&
	git add elif &&
	git commit -m "Second" &&
	git checkout master
	)
}

tokens_match () {
	echo "$1" | tr ' ' '\012' | sort | sed -e '/^$/d' >expect &&
	echo "$2" | tr ' ' '\012' | sort | sed -e '/^$/d' >actual &&
	diff -u expect actual
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
	diff -u expect actual
)
'

test_expect_success 'remove remote' '
(
	cd test &&
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
	diff -u expect actual
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
EOF

test_expect_success 'show' '
	(cd test &&
	 git config --add remote.origin.fetch \
		refs/heads/master:refs/heads/upstream &&
	 git fetch &&
	 git branch -d -r origin/master &&
	 (cd ../one &&
	  echo 1 > file &&
	  git commit -m update file) &&
	 git remote show origin > output &&
	 git diff expect output)
'

test_expect_success 'prune' '
	(cd one &&
	 git branch -m side side2) &&
	(cd test &&
	 git fetch origin &&
	 git remote prune origin &&
	 git rev-parse refs/remotes/origin/side2 &&
	 ! git rev-parse refs/remotes/origin/side)
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
	 ! git rev-parse --verify refs/heads/side &&
	 git fetch origin &&
	 git remote prune origin &&
	 ! git rev-parse --verify refs/heads/side2 &&
	 git rev-parse --verify refs/heads/side)
'

test_done
