#!/bin/sh

test_description='git remote group handling'
. ./test-lib.sh

mark() {
	echo "$1" >mark
}

update_repo() {
	(cd $1 &&
	echo content >>file &&
	git add file &&
	git commit -F ../mark)
}

update_repos() {
	update_repo one $1 &&
	update_repo two $1
}

repo_fetched() {
	if test "`git log -1 --pretty=format:%s $1 --`" = "`cat mark`"; then
		echo >&2 "repo was fetched: $1"
		return 0
	fi
	echo >&2 "repo was not fetched: $1"
	return 1
}

test_expect_success 'setup' '
	mkdir one && (cd one && git init) &&
	mkdir two && (cd two && git init) &&
	git remote add -m master one one &&
	git remote add -m master two two
'

test_expect_success 'no group updates all' '
	mark update-all &&
	update_repos &&
	git remote update &&
	repo_fetched one &&
	repo_fetched two
'

test_expect_success 'nonexistent group produces error' '
	mark nonexistent &&
	update_repos &&
	test_must_fail git remote update nonexistent &&
	! repo_fetched one &&
	! repo_fetched two
'

test_expect_success 'updating group updates all members (remote update)' '
	mark group-all &&
	update_repos &&
	git config --add remotes.all one &&
	git config --add remotes.all two &&
	git remote update all &&
	repo_fetched one &&
	repo_fetched two
'

test_expect_success 'updating group updates all members (fetch)' '
	mark fetch-group-all &&
	update_repos &&
	git fetch all &&
	repo_fetched one &&
	repo_fetched two
'

test_expect_success 'updating group does not update non-members (remote update)' '
	mark group-some &&
	update_repos &&
	git config --add remotes.some one &&
	git remote update some &&
	repo_fetched one &&
	! repo_fetched two
'

test_expect_success 'updating group does not update non-members (fetch)' '
	mark fetch-group-some &&
	update_repos &&
	git config --add remotes.some one &&
	git remote update some &&
	repo_fetched one &&
	! repo_fetched two
'

test_expect_success 'updating remote name updates that remote' '
	mark remote-name &&
	update_repos &&
	git remote update one &&
	repo_fetched one &&
	! repo_fetched two
'

test_done
