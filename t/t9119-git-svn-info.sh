#!/bin/sh
#
# Copyright (c) 2007 David D. Kilzer

test_description='git-svn info'

. ./lib-git-svn.sh

ptouch() {
	perl -w -e '
		use strict;
		die "ptouch requires exactly 2 arguments" if @ARGV != 2;
		die "$ARGV[0] does not exist" if ! -e $ARGV[0];
		my @s = stat $ARGV[0];
		utime $s[8], $s[9], $ARGV[1];
	' "$1" "$2"
}

test_expect_success 'setup repository and import' "
	mkdir info &&
	cd info &&
		echo FIRST > A &&
		echo one > file &&
		ln -s file symlink-file &&
		mkdir directory &&
		touch directory/.placeholder &&
		ln -s directory symlink-directory &&
		svn import -m 'initial' . $svnrepo &&
	cd .. &&
	mkdir gitwc &&
	cd gitwc &&
		git-svn init $svnrepo &&
		git-svn fetch &&
	cd .. &&
	svn co $svnrepo svnwc &&
	ptouch svnwc/file gitwc/file &&
	ptouch svnwc/directory gitwc/directory &&
	ptouch svnwc/symlink-file gitwc/symlink-file &&
	ptouch svnwc/symlink-directory gitwc/symlink-directory
	"

test_expect_success 'info' "
	(cd svnwc; svn info) > expected.info &&
	(cd gitwc; git-svn info) > actual.info &&
	git-diff expected.info actual.info
	"

test_expect_success 'info --url' '
	test $(cd gitwc; git-svn info --url) = $svnrepo
	'

test_expect_success 'info .' "
	(cd svnwc; svn info .) > expected.info-dot &&
	(cd gitwc; git-svn info .) > actual.info-dot &&
	git-diff expected.info-dot actual.info-dot
	"

test_expect_success 'info --url .' '
	test $(cd gitwc; git-svn info --url .) = $svnrepo
	'

test_expect_success 'info file' "
	(cd svnwc; svn info file) > expected.info-file &&
	(cd gitwc; git-svn info file) > actual.info-file &&
	git-diff expected.info-file actual.info-file
	"

test_expect_success 'info --url file' '
	test $(cd gitwc; git-svn info --url file) = "$svnrepo/file"
	'

test_expect_success 'info directory' "
	(cd svnwc; svn info directory) > expected.info-directory &&
	(cd gitwc; git-svn info directory) > actual.info-directory &&
	git-diff expected.info-directory actual.info-directory
	"

test_expect_success 'info --url directory' '
	test $(cd gitwc; git-svn info --url directory) = "$svnrepo/directory"
	'

test_expect_success 'info symlink-file' "
	(cd svnwc; svn info symlink-file) > expected.info-symlink-file &&
	(cd gitwc; git-svn info symlink-file) > actual.info-symlink-file &&
	git-diff expected.info-symlink-file actual.info-symlink-file
	"

test_expect_success 'info --url symlink-file' '
	test $(cd gitwc; git-svn info --url symlink-file) \
	     = "$svnrepo/symlink-file"
	'

test_expect_success 'info symlink-directory' "
	(cd svnwc; svn info symlink-directory) \
		> expected.info-symlink-directory &&
	(cd gitwc; git-svn info symlink-directory) \
		> actual.info-symlink-directory &&
	git-diff expected.info-symlink-directory actual.info-symlink-directory
	"

test_expect_success 'info --url symlink-directory' '
	test $(cd gitwc; git-svn info --url symlink-directory) \
	     = "$svnrepo/symlink-directory"
	'

test_expect_success 'info added-file' "
	echo two > gitwc/added-file &&
	cd gitwc &&
		git add added-file &&
	cd .. &&
	cp gitwc/added-file svnwc/added-file &&
	ptouch gitwc/added-file svnwc/added-file &&
	cd svnwc &&
		svn add added-file > /dev/null &&
	cd .. &&
	(cd svnwc; svn info added-file) > expected.info-added-file &&
	(cd gitwc; git-svn info added-file) > actual.info-added-file &&
	git-diff expected.info-added-file actual.info-added-file
	"

test_expect_success 'info --url added-file' '
	test $(cd gitwc; git-svn info --url added-file) \
	     = "$svnrepo/added-file"
	'

test_expect_success 'info added-directory' "
	mkdir gitwc/added-directory svnwc/added-directory &&
	ptouch gitwc/added-directory svnwc/added-directory &&
	touch gitwc/added-directory/.placeholder &&
	cd svnwc &&
		svn add added-directory > /dev/null &&
	cd .. &&
	cd gitwc &&
		git add added-directory &&
	cd .. &&
	(cd svnwc; svn info added-directory) \
		> expected.info-added-directory &&
	(cd gitwc; git-svn info added-directory) \
		> actual.info-added-directory &&
	git-diff expected.info-added-directory actual.info-added-directory
	"

test_expect_success 'info --url added-directory' '
	test $(cd gitwc; git-svn info --url added-directory) \
	     = "$svnrepo/added-directory"
	'

test_expect_success 'info added-symlink-file' "
	cd gitwc &&
		ln -s added-file added-symlink-file &&
		git add added-symlink-file &&
	cd .. &&
	cd svnwc &&
		ln -s added-file added-symlink-file &&
		svn add added-symlink-file > /dev/null &&
	cd .. &&
	ptouch gitwc/added-symlink-file svnwc/added-symlink-file &&
	(cd svnwc; svn info added-symlink-file) \
		> expected.info-added-symlink-file &&
	(cd gitwc; git-svn info added-symlink-file) \
		> actual.info-added-symlink-file &&
	git-diff expected.info-added-symlink-file \
		 actual.info-added-symlink-file
	"

test_expect_success 'info --url added-symlink-file' '
	test $(cd gitwc; git-svn info --url added-symlink-file) \
	     = "$svnrepo/added-symlink-file"
	'

test_expect_success 'info added-symlink-directory' "
	cd gitwc &&
		ln -s added-directory added-symlink-directory &&
		git add added-symlink-directory &&
	cd .. &&
	cd svnwc &&
		ln -s added-directory added-symlink-directory &&
		svn add added-symlink-directory > /dev/null &&
	cd .. &&
	ptouch gitwc/added-symlink-directory svnwc/added-symlink-directory &&
	(cd svnwc; svn info added-symlink-directory) \
		> expected.info-added-symlink-directory &&
	(cd gitwc; git-svn info added-symlink-directory) \
		> actual.info-added-symlink-directory &&
	git-diff expected.info-added-symlink-directory \
		 actual.info-added-symlink-directory
	"

test_expect_success 'info --url added-symlink-directory' '
	test $(cd gitwc; git-svn info --url added-symlink-directory) \
	     = "$svnrepo/added-symlink-directory"
	'

# The next few tests replace the "Text Last Updated" value with a
# placeholder since git doesn't have a way to know the date that a
# now-deleted file was last checked out locally.  Internally it
# simply reuses the Last Changed Date.

test_expect_success 'info deleted-file' "
	cd gitwc &&
		git rm -f file > /dev/null &&
	cd .. &&
	cd svnwc &&
		svn rm --force file > /dev/null &&
	cd .. &&
	(cd svnwc; svn info file) |
	sed -e 's/^\(Text Last Updated:\).*/\1 TEXT-LAST-UPDATED-STRING/' \
		> expected.info-deleted-file &&
	(cd gitwc; git-svn info file) |
	sed -e 's/^\(Text Last Updated:\).*/\1 TEXT-LAST-UPDATED-STRING/' \
		> actual.info-deleted-file &&
	git-diff expected.info-deleted-file actual.info-deleted-file
	"

test_expect_success 'info --url file (deleted)' '
	test $(cd gitwc; git-svn info --url file) \
	     = "$svnrepo/file"
	'

test_expect_success 'info deleted-directory' "
	cd gitwc &&
		git rm -r -f directory > /dev/null &&
	cd .. &&
	cd svnwc &&
		svn rm --force directory > /dev/null &&
	cd .. &&
	(cd svnwc; svn info directory) |
	sed -e 's/^\(Text Last Updated:\).*/\1 TEXT-LAST-UPDATED-STRING/' \
		> expected.info-deleted-directory &&
	(cd gitwc; git-svn info directory) |
	sed -e 's/^\(Text Last Updated:\).*/\1 TEXT-LAST-UPDATED-STRING/' \
		> actual.info-deleted-directory &&
	git-diff expected.info-deleted-directory actual.info-deleted-directory
	"

test_expect_success 'info --url directory (deleted)' '
	test $(cd gitwc; git-svn info --url directory) \
	     = "$svnrepo/directory"
	'

test_expect_success 'info deleted-symlink-file' "
	cd gitwc &&
		git rm -f symlink-file > /dev/null &&
	cd .. &&
	cd svnwc &&
		svn rm --force symlink-file > /dev/null &&
	cd .. &&
	(cd svnwc; svn info symlink-file) |
	sed -e 's/^\(Text Last Updated:\).*/\1 TEXT-LAST-UPDATED-STRING/' \
		> expected.info-deleted-symlink-file &&
	(cd gitwc; git-svn info symlink-file) |
	sed -e 's/^\(Text Last Updated:\).*/\1 TEXT-LAST-UPDATED-STRING/' \
		> actual.info-deleted-symlink-file &&
	git-diff expected.info-deleted-symlink-file \
		 actual.info-deleted-symlink-file
	"

test_expect_success 'info --url symlink-file (deleted)' '
	test $(cd gitwc; git-svn info --url symlink-file) \
	     = "$svnrepo/symlink-file"
	'

test_expect_success 'info deleted-symlink-directory' "
	cd gitwc &&
		git rm -f symlink-directory > /dev/null &&
	cd .. &&
	cd svnwc &&
		svn rm --force symlink-directory > /dev/null &&
	cd .. &&
	(cd svnwc; svn info symlink-directory) |
	sed -e 's/^\(Text Last Updated:\).*/\1 TEXT-LAST-UPDATED-STRING/' \
		 > expected.info-deleted-symlink-directory &&
	(cd gitwc; git-svn info symlink-directory) |
	sed -e 's/^\(Text Last Updated:\).*/\1 TEXT-LAST-UPDATED-STRING/' \
		 > actual.info-deleted-symlink-directory &&
	git-diff expected.info-deleted-symlink-directory \
		 actual.info-deleted-symlink-directory
	"

test_expect_success 'info --url symlink-directory (deleted)' '
	test $(cd gitwc; git-svn info --url symlink-directory) \
	     = "$svnrepo/symlink-directory"
	'

# NOTE: git does not have the concept of replaced objects,
# so we can't test for files in that state.

test_expect_success 'info unknown-file' "
	echo two > gitwc/unknown-file &&
	cp gitwc/unknown-file svnwc/unknown-file &&
	ptouch gitwc/unknown-file svnwc/unknown-file &&
	(cd svnwc; svn info unknown-file) 2> expected.info-unknown-file &&
	(cd gitwc; git-svn info unknown-file) 2> actual.info-unknown-file &&
	git-diff expected.info-unknown-file actual.info-unknown-file
	"

test_expect_success 'info --url unknown-file' '
	test -z $(cd gitwc; git-svn info --url unknown-file \
			2> ../actual.info--url-unknown-file) &&
	git-diff expected.info-unknown-file actual.info--url-unknown-file
	'

test_expect_success 'info unknown-directory' "
	mkdir gitwc/unknown-directory svnwc/unknown-directory &&
	ptouch gitwc/unknown-directory svnwc/unknown-directory &&
	touch gitwc/unknown-directory/.placeholder &&
	(cd svnwc; svn info unknown-directory) \
		2> expected.info-unknown-directory &&
	(cd gitwc; git-svn info unknown-directory) \
		2> actual.info-unknown-directory &&
	git-diff expected.info-unknown-directory actual.info-unknown-directory
	"

test_expect_success 'info --url unknown-directory' '
	test -z $(cd gitwc; git-svn info --url unknown-directory \
			2> ../actual.info--url-unknown-directory) &&
	git-diff expected.info-unknown-directory \
		 actual.info--url-unknown-directory
	'

test_expect_success 'info unknown-symlink-file' "
	cd gitwc &&
		ln -s unknown-file unknown-symlink-file &&
	cd .. &&
	cd svnwc &&
		ln -s unknown-file unknown-symlink-file &&
	cd .. &&
	ptouch gitwc/unknown-symlink-file svnwc/unknown-symlink-file &&
	(cd svnwc; svn info unknown-symlink-file) \
		2> expected.info-unknown-symlink-file &&
	(cd gitwc; git-svn info unknown-symlink-file) \
		2> actual.info-unknown-symlink-file &&
	git-diff expected.info-unknown-symlink-file \
		 actual.info-unknown-symlink-file
	"

test_expect_success 'info --url unknown-symlink-file' '
	test -z $(cd gitwc; git-svn info --url unknown-symlink-file \
			2> ../actual.info--url-unknown-symlink-file) &&
	git-diff expected.info-unknown-symlink-file \
		 actual.info--url-unknown-symlink-file
	'

test_expect_success 'info unknown-symlink-directory' "
	cd gitwc &&
		ln -s unknown-directory unknown-symlink-directory &&
	cd .. &&
	cd svnwc &&
		ln -s unknown-directory unknown-symlink-directory &&
	cd .. &&
	ptouch gitwc/unknown-symlink-directory \
	       svnwc/unknown-symlink-directory &&
	(cd svnwc; svn info unknown-symlink-directory) \
		2> expected.info-unknown-symlink-directory &&
	(cd gitwc; git-svn info unknown-symlink-directory) \
		2> actual.info-unknown-symlink-directory &&
	git-diff expected.info-unknown-symlink-directory \
		 actual.info-unknown-symlink-directory
	"

test_expect_success 'info --url unknown-symlink-directory' '
	test -z $(cd gitwc; git-svn info --url unknown-symlink-directory \
			2> ../actual.info--url-unknown-symlink-directory) &&
	git-diff expected.info-unknown-symlink-directory \
		 actual.info--url-unknown-symlink-directory
	'

test_done
