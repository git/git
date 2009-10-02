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

test_commit \
	'SnapshotTests' \
	'i can has snapshot?'

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
test_debug 'cat gitweb.output'


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
test_debug 'cat gitweb.output'


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
test_debug 'cat gitweb.output'


test_done
