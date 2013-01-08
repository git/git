#!/bin/sh
#
# Copyright (c) 2012 Felipe Contreras
#
# Base commands from hg-git tests:
# https://bitbucket.org/durin42/hg-git/src
#

test_description='Test bidirectionality of remote-hg'

. ./test-lib.sh

if ! test_have_prereq PYTHON; then
	skip_all='skipping remote-hg tests; python not available'
	test_done
fi

if ! "$PYTHON_PATH" -c 'import mercurial'; then
	skip_all='skipping remote-hg tests; mercurial not available'
	test_done
fi

# clone to a git repo
git_clone () {
	hg -R $1 bookmark -f -r tip master &&
	git clone -q "hg::$PWD/$1" $2
}

# clone to an hg repo
hg_clone () {
	(
	hg init $2 &&
	cd $1 &&
	git push -q "hg::$PWD/../$2" 'refs/tags/*:refs/tags/*' 'refs/heads/*:refs/heads/*'
	) &&

	(cd $2 && hg -q update)
}

# push an hg repo
hg_push () {
	(
	cd $2
	old=$(git symbolic-ref --short HEAD)
	git checkout -q -b tmp &&
	git fetch -q "hg::$PWD/../$1" 'refs/tags/*:refs/tags/*' 'refs/heads/*:refs/heads/*' &&
	git checkout -q $old &&
	git branch -q -D tmp 2> /dev/null || true
	)
}

hg_log () {
	hg -R $1 log --graph --debug | grep -v 'tag: *default/'
}

setup () {
	(
	echo "[ui]"
	echo "username = A U Thor <author@example.com>"
	echo "[defaults]"
	echo "backout = -d \"0 0\""
	echo "commit = -d \"0 0\""
	echo "debugrawcommit = -d \"0 0\""
	echo "tag = -d \"0 0\""
	) >> "$HOME"/.hgrc &&
	git config --global remote-hg.hg-git-compat true

	export HGEDITOR=/usr/bin/true

	export GIT_AUTHOR_DATE="2007-01-01 00:00:00 +0230"
	export GIT_COMMITTER_DATE="$GIT_AUTHOR_DATE"
}

setup

test_expect_success 'encoding' '
	mkdir -p tmp && cd tmp &&
	test_when_finished "cd .. && rm -rf tmp" &&

	(
	git init -q gitrepo &&
	cd gitrepo &&

	echo alpha > alpha &&
	git add alpha &&
	git commit -m "add älphà" &&

	export GIT_AUTHOR_NAME="tést èncödîng" &&
	echo beta > beta &&
	git add beta &&
	git commit -m "add beta" &&

	echo gamma > gamma &&
	git add gamma &&
	git commit -m "add gämmâ" &&

	: TODO git config i18n.commitencoding latin-1 &&
	echo delta > delta &&
	git add delta &&
	git commit -m "add déltà"
	) &&

	hg_clone gitrepo hgrepo &&
	git_clone hgrepo gitrepo2 &&
	hg_clone gitrepo2 hgrepo2 &&

	HGENCODING=utf-8 hg_log hgrepo > expected &&
	HGENCODING=utf-8 hg_log hgrepo2 > actual &&

	test_cmp expected actual
'

test_expect_success 'file removal' '
	mkdir -p tmp && cd tmp &&
	test_when_finished "cd .. && rm -rf tmp" &&

	(
	git init -q gitrepo &&
	cd gitrepo &&
	echo alpha > alpha &&
	git add alpha &&
	git commit -m "add alpha" &&
	echo beta > beta &&
	git add beta &&
	git commit -m "add beta"
	mkdir foo &&
	echo blah > foo/bar &&
	git add foo &&
	git commit -m "add foo" &&
	git rm alpha &&
	git commit -m "remove alpha" &&
	git rm foo/bar &&
	git commit -m "remove foo/bar"
	) &&

	hg_clone gitrepo hgrepo &&
	git_clone hgrepo gitrepo2 &&
	hg_clone gitrepo2 hgrepo2 &&

	hg_log hgrepo > expected &&
	hg_log hgrepo2 > actual &&

	test_cmp expected actual
'

test_expect_success 'git tags' '
	mkdir -p tmp && cd tmp &&
	test_when_finished "cd .. && rm -rf tmp" &&

	(
	git init -q gitrepo &&
	cd gitrepo &&
	git config receive.denyCurrentBranch ignore &&
	echo alpha > alpha &&
	git add alpha &&
	git commit -m "add alpha" &&
	git tag alpha &&

	echo beta > beta &&
	git add beta &&
	git commit -m "add beta" &&
	git tag -a -m "added tag beta" beta
	) &&

	hg_clone gitrepo hgrepo &&
	git_clone hgrepo gitrepo2 &&
	hg_clone gitrepo2 hgrepo2 &&

	hg_log hgrepo > expected &&
	hg_log hgrepo2 > actual &&

	test_cmp expected actual
'

test_expect_success 'hg branch' '
	mkdir -p tmp && cd tmp &&
	test_when_finished "cd .. && rm -rf tmp" &&

	(
	git init -q gitrepo &&
	cd gitrepo &&

	echo alpha > alpha &&
	git add alpha &&
	git commit -q -m "add alpha" &&
	git checkout -q -b not-master
	) &&

	(
	hg_clone gitrepo hgrepo &&

	cd hgrepo &&
	hg -q co master &&
	hg mv alpha beta &&
	hg -q commit -m "rename alpha to beta" &&
	hg branch gamma | grep -v "permanent and global" &&
	hg -q commit -m "started branch gamma"
	) &&

	hg_push hgrepo gitrepo &&
	hg_clone gitrepo hgrepo2 &&

	: TODO, avoid "master" bookmark &&
	(cd hgrepo2 && hg checkout gamma) &&

	hg_log hgrepo > expected &&
	hg_log hgrepo2 > actual &&

	test_cmp expected actual
'

test_expect_success 'hg tags' '
	mkdir -p tmp && cd tmp &&
	test_when_finished "cd .. && rm -rf tmp" &&

	(
	git init -q gitrepo &&
	cd gitrepo &&

	echo alpha > alpha &&
	git add alpha &&
	git commit -m "add alpha" &&
	git checkout -q -b not-master
	) &&

	(
	hg_clone gitrepo hgrepo &&

	cd hgrepo &&
	hg co master &&
	hg tag alpha
	) &&

	hg_push hgrepo gitrepo &&
	hg_clone gitrepo hgrepo2 &&

	hg_log hgrepo > expected &&
	hg_log hgrepo2 > actual &&

	test_cmp expected actual
'

test_done
