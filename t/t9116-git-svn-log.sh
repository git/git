#!/bin/sh
#
# Copyright (c) 2007 Eric Wong
#

test_description='git-svn log tests'
. ./lib-git-svn.sh

test_expect_success 'setup repository and import' "
	mkdir import &&
	cd import &&
		for i in trunk branches/a branches/b \
		         tags/0.1 tags/0.2 tags/0.3; do
			mkdir -p \$i && \
			echo hello >> \$i/README || exit 1
		done && \
		svn import -m test . $svnrepo
		cd .. &&
	git-svn init $svnrepo -T trunk -b branches -t tags &&
	git-svn fetch &&
	git reset --hard trunk &&
	echo bye >> README &&
	git commit -a -m bye &&
	git svn dcommit &&
	git reset --hard a &&
	echo why >> FEEDME &&
	git update-index --add FEEDME &&
	git commit -m feedme &&
	git svn dcommit &&
	git reset --hard trunk &&
	echo aye >> README &&
	git commit -a -m aye &&
	git svn dcommit
	"

test_expect_success 'run log' "
	git reset --hard a &&
	git svn log -r2 trunk | grep ^r2 &&
	git svn log -r4 trunk | grep ^r4 &&
	git svn log -r3 | grep ^r3
	"

test_expect_success 'run log against a from trunk' "
	git reset --hard trunk &&
	git svn log -r3 a | grep ^r3
	"

test_done
