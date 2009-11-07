#!/bin/sh
#
# Copyright (c) 2009 Mark Rada
#

test_description='gitweb as standalone script (parsing script output).

This test runs gitweb (git web interface) as a CGI script from the
commandline, and checks that it produces the correct output, either
in the HTTP header or the actual script output.'


. ./gitweb-lib.sh

# ----------------------------------------------------------------------
# snapshot file name and prefix

cat >>gitweb_config.perl <<\EOF

$known_snapshot_formats{'tar'} = {
	'display' => 'tar',
	'type' => 'application/x-tar',
	'suffix' => '.tar',
	'format' => 'tar',
};

$feature{'snapshot'}{'default'} = ['tar'];
EOF

# Call check_snapshot with the arguments "<basename> [<prefix>]"
#
# This will check that gitweb HTTP header contains proposed filename
# as <basename> with '.tar' suffix added, and that generated tarfile
# (gitweb message body) has <prefix> as prefix for al files in tarfile
#
# <prefix> default to <basename>
check_snapshot () {
	basename=$1
	prefix=${2:-"$1"}
	echo "basename=$basename"
	grep "filename=.*$basename.tar" gitweb.headers >/dev/null 2>&1 &&
	"$TAR" tf gitweb.body >file_list &&
	! grep -v "^$prefix/" file_list
}

test_expect_success setup '
	test_commit first foo &&
	git branch xx/test &&
	FULL_ID=$(git rev-parse --verify HEAD) &&
	SHORT_ID=$(git rev-parse --verify --short=7 HEAD)
'
test_debug '
	echo "FULL_ID  = $FULL_ID"
	echo "SHORT_ID = $SHORT_ID"
'

test_expect_success 'snapshot: full sha1' '
	gitweb_run "p=.git;a=snapshot;h=$FULL_ID;sf=tar" &&
	check_snapshot ".git-$SHORT_ID"
'
test_debug 'cat gitweb.headers && cat file_list'

test_expect_success 'snapshot: shortened sha1' '
	gitweb_run "p=.git;a=snapshot;h=$SHORT_ID;sf=tar" &&
	check_snapshot ".git-$SHORT_ID"
'
test_debug 'cat gitweb.headers && cat file_list'

test_expect_success 'snapshot: almost full sha1' '
	ID=$(git rev-parse --short=30 HEAD) &&
	gitweb_run "p=.git;a=snapshot;h=$ID;sf=tar" &&
	check_snapshot ".git-$SHORT_ID"
'
test_debug 'cat gitweb.headers && cat file_list'

test_expect_success 'snapshot: HEAD' '
	gitweb_run "p=.git;a=snapshot;h=HEAD;sf=tar" &&
	check_snapshot ".git-HEAD-$SHORT_ID"
'
test_debug 'cat gitweb.headers && cat file_list'

test_expect_success 'snapshot: short branch name (master)' '
	gitweb_run "p=.git;a=snapshot;h=master;sf=tar" &&
	ID=$(git rev-parse --verify --short=7 master) &&
	check_snapshot ".git-master-$ID"
'
test_debug 'cat gitweb.headers && cat file_list'

test_expect_success 'snapshot: short tag name (first)' '
	gitweb_run "p=.git;a=snapshot;h=first;sf=tar" &&
	ID=$(git rev-parse --verify --short=7 first) &&
	check_snapshot ".git-first-$ID"
'
test_debug 'cat gitweb.headers && cat file_list'

test_expect_success 'snapshot: full branch name (refs/heads/master)' '
	gitweb_run "p=.git;a=snapshot;h=refs/heads/master;sf=tar" &&
	ID=$(git rev-parse --verify --short=7 master) &&
	check_snapshot ".git-master-$ID"
'
test_debug 'cat gitweb.headers && cat file_list'

test_expect_success 'snapshot: full tag name (refs/tags/first)' '
	gitweb_run "p=.git;a=snapshot;h=refs/tags/first;sf=tar" &&
	check_snapshot ".git-first"
'
test_debug 'cat gitweb.headers && cat file_list'

test_expect_success 'snapshot: hierarchical branch name (xx/test)' '
	gitweb_run "p=.git;a=snapshot;h=xx/test;sf=tar" &&
	! grep "filename=.*/" gitweb.headers
'
test_debug 'cat gitweb.headers'

test_done
