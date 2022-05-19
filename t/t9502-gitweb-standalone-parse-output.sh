#!/bin/sh
#
# Copyright (c) 2009 Mark Rada
#

test_description='butweb as standalone script (parsing script output).

This test runs butweb (but web interface) as a CGI script from the
commandline, and checks that it produces the correct output, either
in the HTTP header or the actual script output.'


GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./lib-butweb.sh

# ----------------------------------------------------------------------
# snapshot file name and prefix

cat >>butweb_config.perl <<\EOF

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
# This will check that butweb HTTP header contains proposed filename
# as <basename> with '.tar' suffix added, and that generated tarfile
# (butweb message body) has <prefix> as prefix for all files in tarfile
#
# <prefix> default to <basename>
check_snapshot () {
	basename=$1
	prefix=${2:-"$1"}
	echo "basename=$basename"
	grep "filename=.*$basename.tar" butweb.headers >/dev/null 2>&1 &&
	"$TAR" tf butweb.body >file_list &&
	! grep -v -e "^$prefix$" -e "^$prefix/" -e "^pax_global_header$" file_list
}

test_expect_success setup '
	test_cummit first foo &&
	but branch xx/test &&
	FULL_ID=$(but rev-parse --verify HEAD) &&
	SHORT_ID=$(but rev-parse --verify --short=7 HEAD)
'
test_debug '
	echo "FULL_ID  = $FULL_ID"
	echo "SHORT_ID = $SHORT_ID"
'

test_expect_success 'snapshot: full sha1' '
	butweb_run "p=.but;a=snapshot;h=$FULL_ID;sf=tar" &&
	check_snapshot ".but-$SHORT_ID"
'
test_debug 'cat butweb.headers && cat file_list'

test_expect_success 'snapshot: shortened sha1' '
	butweb_run "p=.but;a=snapshot;h=$SHORT_ID;sf=tar" &&
	check_snapshot ".but-$SHORT_ID"
'
test_debug 'cat butweb.headers && cat file_list'

test_expect_success 'snapshot: almost full sha1' '
	ID=$(but rev-parse --short=30 HEAD) &&
	butweb_run "p=.but;a=snapshot;h=$ID;sf=tar" &&
	check_snapshot ".but-$SHORT_ID"
'
test_debug 'cat butweb.headers && cat file_list'

test_expect_success 'snapshot: HEAD' '
	butweb_run "p=.but;a=snapshot;h=HEAD;sf=tar" &&
	check_snapshot ".but-HEAD-$SHORT_ID"
'
test_debug 'cat butweb.headers && cat file_list'

test_expect_success 'snapshot: short branch name (main)' '
	butweb_run "p=.but;a=snapshot;h=main;sf=tar" &&
	ID=$(but rev-parse --verify --short=7 main) &&
	check_snapshot ".but-main-$ID"
'
test_debug 'cat butweb.headers && cat file_list'

test_expect_success 'snapshot: short tag name (first)' '
	butweb_run "p=.but;a=snapshot;h=first;sf=tar" &&
	ID=$(but rev-parse --verify --short=7 first) &&
	check_snapshot ".but-first-$ID"
'
test_debug 'cat butweb.headers && cat file_list'

test_expect_success 'snapshot: full branch name (refs/heads/main)' '
	butweb_run "p=.but;a=snapshot;h=refs/heads/main;sf=tar" &&
	ID=$(but rev-parse --verify --short=7 main) &&
	check_snapshot ".but-main-$ID"
'
test_debug 'cat butweb.headers && cat file_list'

test_expect_success 'snapshot: full tag name (refs/tags/first)' '
	butweb_run "p=.but;a=snapshot;h=refs/tags/first;sf=tar" &&
	check_snapshot ".but-first"
'
test_debug 'cat butweb.headers && cat file_list'

test_expect_success 'snapshot: hierarchical branch name (xx/test)' '
	butweb_run "p=.but;a=snapshot;h=xx/test;sf=tar" &&
	! grep "filename=.*/" butweb.headers
'
test_debug 'cat butweb.headers'

# ----------------------------------------------------------------------
# forks of projects

test_expect_success 'forks: setup' '
	but init --bare foo.but &&
	echo file > file &&
	but --but-dir=foo.but --work-tree=. add file &&
	but --but-dir=foo.but --work-tree=. cummit -m "Initial cummit" &&
	echo "foo" > foo.but/description &&
	but clone --bare foo.but foo.bar.but &&
	echo "foo.bar" > foo.bar.but/description &&
	but clone --bare foo.but foo_baz.but &&
	echo "foo_baz" > foo_baz.but/description &&
	rm -fr   foo &&
	mkdir -p foo &&
	(
		cd foo &&
		but clone --shared --bare ../foo.but foo-forked.but &&
		echo "fork of foo" > foo-forked.but/description
	)
'

test_expect_success 'forks: not skipped unless "forks" feature enabled' '
	butweb_run "a=project_list" &&
	grep -q ">\\.but<"               butweb.body &&
	grep -q ">foo\\.but<"            butweb.body &&
	grep -q ">foo_baz\\.but<"        butweb.body &&
	grep -q ">foo\\.bar\\.but<"      butweb.body &&
	grep -q ">foo_baz\\.but<"        butweb.body &&
	grep -q ">foo/foo-forked\\.but<" butweb.body &&
	grep -q ">fork of .*<"           butweb.body
'

test_expect_success 'enable forks feature' '
	cat >>butweb_config.perl <<-\EOF
	$feature{"forks"}{"default"} = [1];
	EOF
'

test_expect_success 'forks: forks skipped if "forks" feature enabled' '
	butweb_run "a=project_list" &&
	grep -q ">\\.but<"               butweb.body &&
	grep -q ">foo\\.but<"            butweb.body &&
	grep -q ">foo_baz\\.but<"        butweb.body &&
	grep -q ">foo\\.bar\\.but<"      butweb.body &&
	grep -q ">foo_baz\\.but<"        butweb.body &&
	grep -v ">foo/foo-forked\\.but<" butweb.body &&
	grep -v ">fork of .*<"           butweb.body
'

test_expect_success 'forks: "forks" action for forked repository' '
	butweb_run "p=foo.but;a=forks" &&
	grep -q ">foo/foo-forked\\.but<" butweb.body &&
	grep -q ">fork of foo<"          butweb.body
'

test_expect_success 'forks: can access forked repository' '
	butweb_run "p=foo/foo-forked.but;a=summary" &&
	grep -q "200 OK"        butweb.headers &&
	grep -q ">fork of foo<" butweb.body
'

test_expect_success 'forks: project_index lists all projects (incl. forks)' '
	cat >expected <<-\EOF &&
	.but
	foo.bar.but
	foo.but
	foo/foo-forked.but
	foo_baz.but
	EOF
	butweb_run "a=project_index" &&
	sed -e "s/ .*//" <butweb.body | sort >actual &&
	test_cmp expected actual
'

xss() {
	echo >&2 "Checking $*..." &&
	butweb_run "$@" &&
	if grep "$TAG" butweb.body; then
		echo >&2 "xss: $TAG should have been quoted in output"
		return 1
	fi
	return 0
}

test_expect_success 'xss checks' '
	TAG="<magic-xss-tag>" &&
	xss "a=rss&p=$TAG" &&
	xss "a=rss&p=foo.but&f=$TAG" &&
	xss "" "$TAG+"
'

no_http_equiv_content_type() {
	butweb_run "$@" &&
	! grep -E "http-equiv=['\"]?content-type" butweb.body
}

# See: <https://html.spec.whatwg.org/dev/semantics.html#attr-meta-http-equiv-content-type>
test_expect_success 'no http-equiv="content-type" in XHTML' '
	no_http_equiv_content_type &&
	no_http_equiv_content_type "p=.but" &&
	no_http_equiv_content_type "p=.but;a=log" &&
	no_http_equiv_content_type "p=.but;a=tree"
'

test_done
