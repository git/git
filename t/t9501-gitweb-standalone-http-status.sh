#!/bin/sh
#
# Copyright (c) 2009 Mark Rada
#

test_description='gitweb as standalone script (http status tests).

This test runs gitweb (git web interface) as a CGI script from the
commandline, and checks that it returns the expected HTTP status
code and message.'


. ./gitweb-lib.sh

# ----------------------------------------------------------------------
# snapshot settings

test_expect_success 'setup' "
	test_commit 'SnapshotTests' 'i can has snapshot'
"


cat >>gitweb_config.perl <<\EOF
$feature{'snapshot'}{'override'} = 0;
EOF

test_expect_success \
    'snapshots: tgz only default format enabled' \
    'gitweb_run "p=.git;a=snapshot;h=HEAD;sf=tgz" &&
    grep "Status: 200 OK" gitweb.output &&
    gitweb_run "p=.git;a=snapshot;h=HEAD;sf=tbz2" &&
    grep "403 - Unsupported snapshot format" gitweb.output &&
    gitweb_run "p=.git;a=snapshot;h=HEAD;sf=txz" &&
    grep "403 - Snapshot format not allowed" gitweb.output &&
    gitweb_run "p=.git;a=snapshot;h=HEAD;sf=zip" &&
    grep "403 - Unsupported snapshot format" gitweb.output'


cat >>gitweb_config.perl <<\EOF
$feature{'snapshot'}{'default'} = ['tgz','tbz2','txz','zip'];
EOF

test_expect_success \
    'snapshots: all enabled in default, use default disabled value' \
    'gitweb_run "p=.git;a=snapshot;h=HEAD;sf=tgz" &&
    grep "Status: 200 OK" gitweb.output &&
    gitweb_run "p=.git;a=snapshot;h=HEAD;sf=tbz2" &&
    grep "Status: 200 OK" gitweb.output &&
    gitweb_run "p=.git;a=snapshot;h=HEAD;sf=txz" &&
    grep "403 - Snapshot format not allowed" gitweb.output &&
    gitweb_run "p=.git;a=snapshot;h=HEAD;sf=zip" &&
    grep "Status: 200 OK" gitweb.output'


cat >>gitweb_config.perl <<\EOF
$known_snapshot_formats{'zip'}{'disabled'} = 1;
EOF

test_expect_success \
    'snapshots: zip explicitly disabled' \
    'gitweb_run "p=.git;a=snapshot;h=HEAD;sf=zip" &&
    grep "403 - Snapshot format not allowed" gitweb.output'
test_debug 'cat gitweb.output'


cat >>gitweb_config.perl <<\EOF
$known_snapshot_formats{'tgz'}{'disabled'} = 0;
EOF

test_expect_success \
    'snapshots: tgz explicitly enabled' \
    'gitweb_run "p=.git;a=snapshot;h=HEAD;sf=tgz" &&
    grep "Status: 200 OK" gitweb.output'
test_debug 'cat gitweb.headers'


# ----------------------------------------------------------------------
# snapshot hash ids

test_expect_success 'snapshots: good tree-ish id' '
	gitweb_run "p=.git;a=snapshot;h=master;sf=tgz" &&
	grep "Status: 200 OK" gitweb.output
'
test_debug 'cat gitweb.headers'

test_expect_success 'snapshots: bad tree-ish id' '
	gitweb_run "p=.git;a=snapshot;h=frizzumFrazzum;sf=tgz" &&
	grep "404 - Object does not exist" gitweb.output
'
test_debug 'cat gitweb.output'

test_expect_success 'snapshots: bad tree-ish id (tagged object)' '
	echo object > tag-object &&
	git add tag-object &&
	git commit -m "Object to be tagged" &&
	git tag tagged-object `git hash-object tag-object` &&
	gitweb_run "p=.git;a=snapshot;h=tagged-object;sf=tgz" &&
	grep "400 - Object is not a tree-ish" gitweb.output
'
test_debug 'cat gitweb.output'

test_expect_success 'snapshots: good object id' '
	ID=`git rev-parse --verify HEAD` &&
	gitweb_run "p=.git;a=snapshot;h=$ID;sf=tgz" &&
	grep "Status: 200 OK" gitweb.output
'
test_debug 'cat gitweb.headers'

test_expect_success 'snapshots: bad object id' '
	gitweb_run "p=.git;a=snapshot;h=abcdef01234;sf=tgz" &&
	grep "404 - Object does not exist" gitweb.output
'
test_debug 'cat gitweb.output'


# ----------------------------------------------------------------------
# load checking

# always hit the load limit
cat >>gitweb_config.perl <<\EOF
our $maxload = -1;
EOF

test_expect_success 'load checking: load too high (default action)' '
	gitweb_run "p=.git" &&
	grep "Status: 503 Service Unavailable" gitweb.headers &&
	grep "503 - The load average on the server is too high" gitweb.body
'
test_debug 'cat gitweb.headers'

# turn off load checking
cat >>gitweb_config.perl <<\EOF
our $maxload = undef;
EOF


# ----------------------------------------------------------------------
# invalid arguments

test_expect_success 'invalid arguments: invalid regexp (in project search)' '
	gitweb_run "a=project_list;s=*\.git;sr=1" &&
	grep "Status: 400" gitweb.headers &&
	grep "400 - Invalid.*regexp" gitweb.body
'
test_debug 'cat gitweb.headers'

test_done
