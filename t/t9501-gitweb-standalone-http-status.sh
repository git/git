#!/bin/sh
#
# Copyright (c) 2009 Mark Rada
#

test_description='butweb as standalone script (http status tests).

This test runs butweb (but web interface) as a CGI script from the
commandline, and checks that it returns the expected HTTP status
code and message.'


BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./lib-butweb.sh

#
# Gitweb only provides the functionality tested by the 'modification times'
# tests if it can access a date parser from one of these modules:
#
perl -MHTTP::Date -e 0 >/dev/null 2>&1 && test_set_prereq DATE_PARSER
perl -MTime::ParseDate -e 0 >/dev/null 2>&1 && test_set_prereq DATE_PARSER

# ----------------------------------------------------------------------
# snapshot settings

test_expect_success 'setup' "
	test_cummit 'SnapshotTests' 'i can has snapshot'
"


cat >>butweb_config.perl <<\EOF
$feature{'snapshot'}{'override'} = 0;
EOF

test_expect_success \
    'snapshots: tgz only default format enabled' \
    'butweb_run "p=.but;a=snapshot;h=HEAD;sf=tgz" &&
    grep "Status: 200 OK" butweb.output &&
    butweb_run "p=.but;a=snapshot;h=HEAD;sf=tbz2" &&
    grep "403 - Unsupported snapshot format" butweb.output &&
    butweb_run "p=.but;a=snapshot;h=HEAD;sf=txz" &&
    grep "403 - Snapshot format not allowed" butweb.output &&
    butweb_run "p=.but;a=snapshot;h=HEAD;sf=zip" &&
    grep "403 - Unsupported snapshot format" butweb.output'


cat >>butweb_config.perl <<\EOF
$feature{'snapshot'}{'default'} = ['tgz','tbz2','txz','zip'];
EOF

test_expect_success \
    'snapshots: all enabled in default, use default disabled value' \
    'butweb_run "p=.but;a=snapshot;h=HEAD;sf=tgz" &&
    grep "Status: 200 OK" butweb.output &&
    butweb_run "p=.but;a=snapshot;h=HEAD;sf=tbz2" &&
    grep "Status: 200 OK" butweb.output &&
    butweb_run "p=.but;a=snapshot;h=HEAD;sf=txz" &&
    grep "403 - Snapshot format not allowed" butweb.output &&
    butweb_run "p=.but;a=snapshot;h=HEAD;sf=zip" &&
    grep "Status: 200 OK" butweb.output'


cat >>butweb_config.perl <<\EOF
$known_snapshot_formats{'zip'}{'disabled'} = 1;
EOF

test_expect_success \
    'snapshots: zip explicitly disabled' \
    'butweb_run "p=.but;a=snapshot;h=HEAD;sf=zip" &&
    grep "403 - Snapshot format not allowed" butweb.output'
test_debug 'cat butweb.output'


cat >>butweb_config.perl <<\EOF
$known_snapshot_formats{'tgz'}{'disabled'} = 0;
EOF

test_expect_success \
    'snapshots: tgz explicitly enabled' \
    'butweb_run "p=.but;a=snapshot;h=HEAD;sf=tgz" &&
    grep "Status: 200 OK" butweb.output'
test_debug 'cat butweb.headers'


# ----------------------------------------------------------------------
# snapshot hash ids

test_expect_success 'snapshots: good tree-ish id' '
	butweb_run "p=.but;a=snapshot;h=main;sf=tgz" &&
	grep "Status: 200 OK" butweb.output
'
test_debug 'cat butweb.headers'

test_expect_success 'snapshots: bad tree-ish id' '
	butweb_run "p=.but;a=snapshot;h=frizzumFrazzum;sf=tgz" &&
	grep "404 - Object does not exist" butweb.output
'
test_debug 'cat butweb.output'

test_expect_success 'snapshots: bad tree-ish id (tagged object)' '
	echo object > tag-object &&
	but add tag-object &&
	test_tick && but cummit -m "Object to be tagged" &&
	but tag tagged-object $(but hash-object tag-object) &&
	butweb_run "p=.but;a=snapshot;h=tagged-object;sf=tgz" &&
	grep "400 - Object is not a tree-ish" butweb.output
'
test_debug 'cat butweb.output'

test_expect_success 'snapshots: good object id' '
	ID=$(but rev-parse --verify HEAD) &&
	butweb_run "p=.but;a=snapshot;h=$ID;sf=tgz" &&
	grep "Status: 200 OK" butweb.output
'
test_debug 'cat butweb.headers'

test_expect_success 'snapshots: bad object id' '
	butweb_run "p=.but;a=snapshot;h=abcdef01234;sf=tgz" &&
	grep "404 - Object does not exist" butweb.output
'
test_debug 'cat butweb.output'

# ----------------------------------------------------------------------
# modification times (Last-Modified and If-Modified-Since)

test_expect_success DATE_PARSER 'modification: feed last-modified' '
	butweb_run "p=.but;a=atom;h=main" &&
	grep "Status: 200 OK" butweb.headers &&
	grep "Last-modified: Thu, 7 Apr 2005 22:14:13 +0000" butweb.headers
'
test_debug 'cat butweb.headers'

test_expect_success DATE_PARSER 'modification: feed if-modified-since (modified)' '
	HTTP_IF_MODIFIED_SINCE="Wed, 6 Apr 2005 22:14:13 +0000" &&
	export HTTP_IF_MODIFIED_SINCE &&
	test_when_finished "unset HTTP_IF_MODIFIED_SINCE" &&
	butweb_run "p=.but;a=atom;h=main" &&
	grep "Status: 200 OK" butweb.headers
'
test_debug 'cat butweb.headers'

test_expect_success DATE_PARSER 'modification: feed if-modified-since (unmodified)' '
	HTTP_IF_MODIFIED_SINCE="Thu, 7 Apr 2005 22:14:13 +0000" &&
	export HTTP_IF_MODIFIED_SINCE &&
	test_when_finished "unset HTTP_IF_MODIFIED_SINCE" &&
	butweb_run "p=.but;a=atom;h=main" &&
	grep "Status: 304 Not Modified" butweb.headers
'
test_debug 'cat butweb.headers'

test_expect_success DATE_PARSER 'modification: snapshot last-modified' '
	butweb_run "p=.but;a=snapshot;h=main;sf=tgz" &&
	grep "Status: 200 OK" butweb.headers &&
	grep "Last-modified: Thu, 7 Apr 2005 22:14:13 +0000" butweb.headers
'
test_debug 'cat butweb.headers'

test_expect_success DATE_PARSER 'modification: snapshot if-modified-since (modified)' '
	HTTP_IF_MODIFIED_SINCE="Wed, 6 Apr 2005 22:14:13 +0000" &&
	export HTTP_IF_MODIFIED_SINCE &&
	test_when_finished "unset HTTP_IF_MODIFIED_SINCE" &&
	butweb_run "p=.but;a=snapshot;h=main;sf=tgz" &&
	grep "Status: 200 OK" butweb.headers
'
test_debug 'cat butweb.headers'

test_expect_success DATE_PARSER 'modification: snapshot if-modified-since (unmodified)' '
	HTTP_IF_MODIFIED_SINCE="Thu, 7 Apr 2005 22:14:13 +0000" &&
	export HTTP_IF_MODIFIED_SINCE &&
	test_when_finished "unset HTTP_IF_MODIFIED_SINCE" &&
	butweb_run "p=.but;a=snapshot;h=main;sf=tgz" &&
	grep "Status: 304 Not Modified" butweb.headers
'
test_debug 'cat butweb.headers'

test_expect_success DATE_PARSER 'modification: tree snapshot' '
	ID=$(but rev-parse --verify HEAD^{tree}) &&
	HTTP_IF_MODIFIED_SINCE="Wed, 6 Apr 2005 22:14:13 +0000" &&
	export HTTP_IF_MODIFIED_SINCE &&
	test_when_finished "unset HTTP_IF_MODIFIED_SINCE" &&
	butweb_run "p=.but;a=snapshot;h=$ID;sf=tgz" &&
	grep "Status: 200 OK" butweb.headers &&
	! grep -i "last-modified" butweb.headers
'
test_debug 'cat butweb.headers'

# ----------------------------------------------------------------------
# load checking

# always hit the load limit
cat >>butweb_config.perl <<\EOF
our $maxload = -1;
EOF

test_expect_success 'load checking: load too high (default action)' '
	butweb_run "p=.but" &&
	grep "Status: 503 Service Unavailable" butweb.headers &&
	grep "503 - The load average on the server is too high" butweb.body
'
test_debug 'cat butweb.headers'

# turn off load checking
cat >>butweb_config.perl <<\EOF
our $maxload = undef;
EOF


# ----------------------------------------------------------------------
# invalid arguments

test_expect_success 'invalid arguments: invalid regexp (in project search)' '
	butweb_run "a=project_list;s=*\.but;sr=1" &&
	grep "Status: 400" butweb.headers &&
	grep "400 - Invalid.*regexp" butweb.body
'
test_debug 'cat butweb.headers'

test_done
