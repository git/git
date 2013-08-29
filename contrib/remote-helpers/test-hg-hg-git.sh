#!/bin/sh
#
# Copyright (c) 2012 Felipe Contreras
#
# Base commands from hg-git tests:
# https://bitbucket.org/durin42/hg-git/src
#

test_description='Test remote-hg output compared to hg-git'

. ./test-lib.sh

if ! test_have_prereq PYTHON
then
	skip_all='skipping remote-hg tests; python not available'
	test_done
fi

if ! python -c 'import mercurial'
then
	skip_all='skipping remote-hg tests; mercurial not available'
	test_done
fi

if ! python -c 'import hggit'
then
	skip_all='skipping remote-hg tests; hg-git not available'
	test_done
fi

# clone to a git repo with git
git_clone_git () {
	git clone -q "hg::$1" $2 &&
	(cd $2 && git checkout master && git branch -D default)
}

# clone to an hg repo with git
hg_clone_git () {
	(
	hg init $2 &&
	hg -R $2 bookmark -i master &&
	cd $1 &&
	git push -q "hg::../$2" 'refs/tags/*:refs/tags/*' 'refs/heads/*:refs/heads/*'
	) &&

	(cd $2 && hg -q update)
}

# clone to a git repo with hg
git_clone_hg () {
	(
	git init -q $2 &&
	cd $1 &&
	hg bookmark -i -f -r tip master &&
	hg -q push -r master ../$2 || true
	)
}

# clone to an hg repo with hg
hg_clone_hg () {
	hg -q clone $1 $2
}

# push an hg repo with git
hg_push_git () {
	(
	cd $2
	git checkout -q -b tmp &&
	git fetch -q "hg::../$1" 'refs/tags/*:refs/tags/*' 'refs/heads/*:refs/heads/*' &&
	git branch -D default &&
	git checkout -q @{-1} &&
	git branch -q -D tmp 2>/dev/null || true
	)
}

# push an hg git repo with hg
hg_push_hg () {
	(
	cd $1 &&
	hg -q push ../$2 || true
	)
}

hg_log () {
	hg -R $1 log --graph --debug >log &&
	grep -v 'tag: *default/' log
}

git_log () {
	git --git-dir=$1/.git fast-export --branches
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
	echo "[extensions]"
	echo "hgext.bookmarks ="
	echo "hggit ="
	echo "graphlog ="
	) >>"$HOME"/.hgrc &&
	git config --global receive.denycurrentbranch warn
	git config --global remote-hg.hg-git-compat true
	git config --global remote-hg.track-branches false

	HGEDITOR=true
	HGMERGE=true

	GIT_AUTHOR_DATE="2007-01-01 00:00:00 +0230"
	GIT_COMMITTER_DATE="$GIT_AUTHOR_DATE"
	export HGEDITOR HGMERGE GIT_AUTHOR_DATE GIT_COMMITTER_DATE
}

setup

test_expect_success 'executable bit' '
	test_when_finished "rm -rf gitrepo* hgrepo*" &&

	(
	git init -q gitrepo &&
	cd gitrepo &&
	echo alpha >alpha &&
	chmod 0644 alpha &&
	git add alpha &&
	git commit -m "add alpha" &&
	chmod 0755 alpha &&
	git add alpha &&
	git commit -m "set executable bit" &&
	chmod 0644 alpha &&
	git add alpha &&
	git commit -m "clear executable bit"
	) &&

	for x in hg git
	do
		(
		hg_clone_$x gitrepo hgrepo-$x &&
		cd hgrepo-$x &&
		hg_log . &&
		hg manifest -r 1 -v &&
		hg manifest -v
		) >"output-$x" &&

		git_clone_$x hgrepo-$x gitrepo2-$x &&
		git_log gitrepo2-$x >"log-$x"
	done &&

	test_cmp output-hg output-git &&
	test_cmp log-hg log-git
'

test_expect_success 'symlink' '
	test_when_finished "rm -rf gitrepo* hgrepo*" &&

	(
	git init -q gitrepo &&
	cd gitrepo &&
	echo alpha >alpha &&
	git add alpha &&
	git commit -m "add alpha" &&
	ln -s alpha beta &&
	git add beta &&
	git commit -m "add beta"
	) &&

	for x in hg git
	do
		(
		hg_clone_$x gitrepo hgrepo-$x &&
		cd hgrepo-$x &&
		hg_log . &&
		hg manifest -v
		) >"output-$x" &&

		git_clone_$x hgrepo-$x gitrepo2-$x &&
		git_log gitrepo2-$x >"log-$x"
	done &&

	test_cmp output-hg output-git &&
	test_cmp log-hg log-git
'

test_expect_success 'merge conflict 1' '
	test_when_finished "rm -rf gitrepo* hgrepo*" &&

	(
	hg init hgrepo1 &&
	cd hgrepo1 &&
	echo A >afile &&
	hg add afile &&
	hg ci -m "origin" &&

	echo B >afile &&
	hg ci -m "A->B" &&

	hg up -r0 &&
	echo C >afile &&
	hg ci -m "A->C" &&

	hg merge -r1 &&
	echo C >afile &&
	hg resolve -m afile &&
	hg ci -m "merge to C"
	) &&

	for x in hg git
	do
		git_clone_$x hgrepo1 gitrepo-$x &&
		hg_clone_$x gitrepo-$x hgrepo2-$x &&
		hg_log hgrepo2-$x >"hg-log-$x" &&
		git_log gitrepo-$x >"git-log-$x"
	done &&

	test_cmp hg-log-hg hg-log-git &&
	test_cmp git-log-hg git-log-git
'

test_expect_success 'merge conflict 2' '
	test_when_finished "rm -rf gitrepo* hgrepo*" &&

	(
	hg init hgrepo1 &&
	cd hgrepo1 &&
	echo A >afile &&
	hg add afile &&
	hg ci -m "origin" &&

	echo B >afile &&
	hg ci -m "A->B" &&

	hg up -r0 &&
	echo C >afile &&
	hg ci -m "A->C" &&

	hg merge -r1 || true &&
	echo B >afile &&
	hg resolve -m afile &&
	hg ci -m "merge to B"
	) &&

	for x in hg git
	do
		git_clone_$x hgrepo1 gitrepo-$x &&
		hg_clone_$x gitrepo-$x hgrepo2-$x &&
		hg_log hgrepo2-$x >"hg-log-$x" &&
		git_log gitrepo-$x >"git-log-$x"
	done &&

	test_cmp hg-log-hg hg-log-git &&
	test_cmp git-log-hg git-log-git
'

test_expect_success 'converged merge' '
	test_when_finished "rm -rf gitrepo* hgrepo*" &&

	(
	hg init hgrepo1 &&
	cd hgrepo1 &&
	echo A >afile &&
	hg add afile &&
	hg ci -m "origin" &&

	echo B >afile &&
	hg ci -m "A->B" &&

	echo C >afile &&
	hg ci -m "B->C" &&

	hg up -r0 &&
	echo C >afile &&
	hg ci -m "A->C" &&

	hg merge -r2 || true &&
	hg ci -m "merge"
	) &&

	for x in hg git
	do
		git_clone_$x hgrepo1 gitrepo-$x &&
		hg_clone_$x gitrepo-$x hgrepo2-$x &&
		hg_log hgrepo2-$x >"hg-log-$x" &&
		git_log gitrepo-$x >"git-log-$x"
	done &&

	test_cmp hg-log-hg hg-log-git &&
	test_cmp git-log-hg git-log-git
'

test_expect_success 'encoding' '
	test_when_finished "rm -rf gitrepo* hgrepo*" &&

	(
	git init -q gitrepo &&
	cd gitrepo &&

	echo alpha >alpha &&
	git add alpha &&
	git commit -m "add älphà" &&

	GIT_AUTHOR_NAME="tést èncödîng" &&
	export GIT_AUTHOR_NAME &&
	echo beta >beta &&
	git add beta &&
	git commit -m "add beta" &&

	echo gamma >gamma &&
	git add gamma &&
	git commit -m "add gämmâ" &&

	: TODO git config i18n.commitencoding latin-1 &&
	echo delta >delta &&
	git add delta &&
	git commit -m "add déltà"
	) &&

	for x in hg git
	do
		hg_clone_$x gitrepo hgrepo-$x &&
		git_clone_$x hgrepo-$x gitrepo2-$x &&

		HGENCODING=utf-8 hg_log hgrepo-$x >"hg-log-$x" &&
		git_log gitrepo2-$x >"git-log-$x"
	done &&

	test_cmp hg-log-hg hg-log-git &&
	test_cmp git-log-hg git-log-git
'

test_expect_success 'file removal' '
	test_when_finished "rm -rf gitrepo* hgrepo*" &&

	(
	git init -q gitrepo &&
	cd gitrepo &&
	echo alpha >alpha &&
	git add alpha &&
	git commit -m "add alpha" &&
	echo beta >beta &&
	git add beta &&
	git commit -m "add beta"
	mkdir foo &&
	echo blah >foo/bar &&
	git add foo &&
	git commit -m "add foo" &&
	git rm alpha &&
	git commit -m "remove alpha" &&
	git rm foo/bar &&
	git commit -m "remove foo/bar"
	) &&

	for x in hg git
	do
		(
		hg_clone_$x gitrepo hgrepo-$x &&
		cd hgrepo-$x &&
		hg_log . &&
		hg manifest -r 3 &&
		hg manifest
		) >"output-$x" &&

		git_clone_$x hgrepo-$x gitrepo2-$x &&
		git_log gitrepo2-$x >"log-$x"
	done &&

	test_cmp output-hg output-git &&
	test_cmp log-hg log-git
'

test_expect_success 'git tags' '
	test_when_finished "rm -rf gitrepo* hgrepo*" &&

	(
	git init -q gitrepo &&
	cd gitrepo &&
	git config receive.denyCurrentBranch ignore &&
	echo alpha >alpha &&
	git add alpha &&
	git commit -m "add alpha" &&
	git tag alpha &&

	echo beta >beta &&
	git add beta &&
	git commit -m "add beta" &&
	git tag -a -m "added tag beta" beta
	) &&

	for x in hg git
	do
		hg_clone_$x gitrepo hgrepo-$x &&
		hg_log hgrepo-$x >"log-$x"
	done &&

	test_cmp log-hg log-git
'

test_expect_success 'hg author' '
	test_when_finished "rm -rf gitrepo* hgrepo*" &&

	for x in hg git
	do
		(
		git init -q gitrepo-$x &&
		cd gitrepo-$x &&

		echo alpha >alpha &&
		git add alpha &&
		git commit -m "add alpha" &&
		git checkout -q -b not-master
		) &&

		(
		hg_clone_$x gitrepo-$x hgrepo-$x &&
		cd hgrepo-$x &&

		hg co master &&
		echo beta >beta &&
		hg add beta &&
		hg commit -u "test" -m "add beta" &&

		echo gamma >>beta &&
		hg commit -u "test <test@example.com> (comment)" -m "modify beta" &&

		echo gamma >gamma &&
		hg add gamma &&
		hg commit -u "<test@example.com>" -m "add gamma" &&

		echo delta >delta &&
		hg add delta &&
		hg commit -u "name<test@example.com>" -m "add delta" &&

		echo epsilon >epsilon &&
		hg add epsilon &&
		hg commit -u "name <test@example.com" -m "add epsilon" &&

		echo zeta >zeta &&
		hg add zeta &&
		hg commit -u " test " -m "add zeta" &&

		echo eta >eta &&
		hg add eta &&
		hg commit -u "test < test@example.com >" -m "add eta" &&

		echo theta >theta &&
		hg add theta &&
		hg commit -u "test >test@example.com>" -m "add theta" &&

		echo iota >iota &&
		hg add iota &&
		hg commit -u "test <test <at> example <dot> com>" -m "add iota"
		) &&

		hg_push_$x hgrepo-$x gitrepo-$x &&
		hg_clone_$x gitrepo-$x hgrepo2-$x &&

		hg_log hgrepo2-$x >"hg-log-$x" &&
		git_log gitrepo-$x >"git-log-$x"
	done &&

	test_cmp hg-log-hg hg-log-git &&
	test_cmp git-log-hg git-log-git
'

test_expect_success 'hg branch' '
	test_when_finished "rm -rf gitrepo* hgrepo*" &&

	for x in hg git
	do
		(
		git init -q gitrepo-$x &&
		cd gitrepo-$x &&

		echo alpha >alpha &&
		git add alpha &&
		git commit -q -m "add alpha" &&
		git checkout -q -b not-master
		) &&

		(
		hg_clone_$x gitrepo-$x hgrepo-$x &&

		cd hgrepo-$x &&
		hg -q co master &&
		hg mv alpha beta &&
		hg -q commit -m "rename alpha to beta" &&
		hg branch gamma | grep -v "permanent and global" &&
		hg -q commit -m "started branch gamma"
		) &&

		hg_push_$x hgrepo-$x gitrepo-$x &&
		hg_clone_$x gitrepo-$x hgrepo2-$x &&

		hg_log hgrepo2-$x >"hg-log-$x" &&
		git_log gitrepo-$x >"git-log-$x"
	done &&

	test_cmp hg-log-hg hg-log-git &&
	test_cmp git-log-hg git-log-git
'

test_expect_success 'hg tags' '
	test_when_finished "rm -rf gitrepo* hgrepo*" &&

	for x in hg git
	do
		(
		git init -q gitrepo-$x &&
		cd gitrepo-$x &&

		echo alpha >alpha &&
		git add alpha &&
		git commit -m "add alpha" &&
		git checkout -q -b not-master
		) &&

		(
		hg_clone_$x gitrepo-$x hgrepo-$x &&

		cd hgrepo-$x &&
		hg co master &&
		hg tag alpha
		) &&

		hg_push_$x hgrepo-$x gitrepo-$x &&
		hg_clone_$x gitrepo-$x hgrepo2-$x &&

		(
		git --git-dir=gitrepo-$x/.git tag -l &&
		hg_log hgrepo2-$x &&
		cat hgrepo2-$x/.hgtags
		) >"output-$x"
	done &&

	test_cmp output-hg output-git
'

test_done
