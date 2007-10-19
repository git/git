#!/bin/sh

test_description='git remote porcelain-ish'

. ./test-lib.sh

GIT_CONFIG=.git/config
export GIT_CONFIG

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

test_done
