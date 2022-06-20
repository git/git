#!/bin/sh
#
# Copyright (c) 2009 Mark Rada
#

test_description='gitweb as standalone script (parsing script output).

This test runs gitweb (git web interface) as a CGI script from the
commandline, and checks that it produces the correct output, either
in the HTTP header or the actual script output.'


GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./lib-gitweb.sh

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
# (gitweb message body) has <prefix> as prefix for all files in tarfile
#
# <prefix> default to <basename>
check_snapshot () {
	basename=$1
	prefix=${2:-"$1"}
	echo "basename=$basename"
	grep "filename=.*$basename.tar" gitweb.headers >/dev/null 2>&1 &&
	"$TAR" tf gitweb.body >file_list &&
	! grep -v -e "^$prefix$" -e "^$prefix/" -e "^pax_global_header$" file_list
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

test_expect_success 'snapshot: short branch name (main)' '
	gitweb_run "p=.git;a=snapshot;h=main;sf=tar" &&
	ID=$(git rev-parse --verify --short=7 main) &&
	check_snapshot ".git-main-$ID"
'
test_debug 'cat gitweb.headers && cat file_list'

test_expect_success 'snapshot: short tag name (first)' '
	gitweb_run "p=.git;a=snapshot;h=first;sf=tar" &&
	ID=$(git rev-parse --verify --short=7 first) &&
	check_snapshot ".git-first-$ID"
'
test_debug 'cat gitweb.headers && cat file_list'

test_expect_success 'snapshot: full branch name (refs/heads/main)' '
	gitweb_run "p=.git;a=snapshot;h=refs/heads/main;sf=tar" &&
	ID=$(git rev-parse --verify --short=7 main) &&
	check_snapshot ".git-main-$ID"
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

# ----------------------------------------------------------------------
# forks of projects

test_expect_success 'forks: setup' '
	git init --bare foo.git &&
	echo file > file &&
	git --git-dir=foo.git --work-tree=. add file &&
	git --git-dir=foo.git --work-tree=. commit -m "Initial commit" &&
	echo "foo" > foo.git/description &&
	git clone --bare foo.git foo.bar.git &&
	echo "foo.bar" > foo.bar.git/description &&
	git clone --bare foo.git foo_baz.git &&
	echo "foo_baz" > foo_baz.git/description &&
	rm -fr   foo &&
	mkdir -p foo &&
	(
		cd foo &&
		git clone --shared --bare ../foo.git foo-forked.git &&
		echo "fork of foo" > foo-forked.git/description
	)
'

test_expect_success 'forks: not skipped unless "forks" feature enabled' '
	gitweb_run "a=project_list" &&
	grep -q ">\\.git<"               gitweb.body &&
	grep -q ">foo\\.git<"            gitweb.body &&
	grep -q ">foo_baz\\.git<"        gitweb.body &&
	grep -q ">foo\\.bar\\.git<"      gitweb.body &&
	grep -q ">foo_baz\\.git<"        gitweb.body &&
	grep -q ">foo/foo-forked\\.git<" gitweb.body &&
	grep -q ">fork of .*<"           gitweb.body
'

test_expect_success 'enable forks feature' '
	cat >>gitweb_config.perl <<-\EOF
	$feature{"forks"}{"default"} = [1];
	EOF
'

test_expect_success 'forks: forks skipped if "forks" feature enabled' '
	gitweb_run "a=project_list" &&
	grep -q ">\\.git<"               gitweb.body &&
	grep -q ">foo\\.git<"            gitweb.body &&
	grep -q ">foo_baz\\.git<"        gitweb.body &&
	grep -q ">foo\\.bar\\.git<"      gitweb.body &&
	grep -q ">foo_baz\\.git<"        gitweb.body &&
	grep -v ">foo/foo-forked\\.git<" gitweb.body &&
	grep -v ">fork of .*<"           gitweb.body
'

test_expect_success 'forks: "forks" action for forked repository' '
	gitweb_run "p=foo.git;a=forks" &&
	grep -q ">foo/foo-forked\\.git<" gitweb.body &&
	grep -q ">fork of foo<"          gitweb.body
'

test_expect_success 'forks: can access forked repository' '
	gitweb_run "p=foo/foo-forked.git;a=summary" &&
	grep -q "200 OK"        gitweb.headers &&
	grep -q ">fork of foo<" gitweb.body
'

test_expect_success 'forks: project_index lists all projects (incl. forks)' '
	cat >expected <<-\EOF &&
	.git
	foo.bar.git
	foo.git
	foo/foo-forked.git
	foo_baz.git
	EOF
	gitweb_run "a=project_index" &&
	sed -e "s/ .*//" <gitweb.body | sort >actual &&
	test_cmp expected actual
'

xss() {
	echo >&2 "Checking $*..." &&
	gitweb_run "$@" &&
	if grep "$TAG" gitweb.body; then
		echo >&2 "xss: $TAG should have been quoted in output"
		return 1
	fi
	return 0
}

test_expect_success 'xss checks' '
	TAG="<magic-xss-tag>" &&
	xss "a=rss&p=$TAG" &&
	xss "a=rss&p=foo.git&f=$TAG" &&
	xss "" "$TAG+"
'

no_http_equiv_content_type() {
	gitweb_run "$@" &&
	! grep -E "http-equiv=['\"]?content-type" gitweb.body
}

# See: <https://html.spec.whatwg.org/dev/semantics.html#attr-meta-http-equiv-content-type>
test_expect_success 'no http-equiv="content-type" in XHTML' '
	no_http_equiv_content_type &&
	no_http_equiv_content_type "p=.git" &&
	no_http_equiv_content_type "p=.git;a=log" &&
	no_http_equiv_content_type "p=.git;a=tree"
'

proper_doctype() {
	gitweb_run "$@" &&
	grep -F "<!DOCTYPE html [" gitweb.body &&
	grep "<!ENTITY nbsp" gitweb.body &&
	grep "<!ENTITY sdot" gitweb.body
}

test_expect_success 'Proper DOCTYPE with entity declarations' '
	proper_doctype &&
	proper_doctype "p=.git" &&
	proper_doctype "p=.git;a=log" &&
	proper_doctype "p=.git;a=tree"
'

test_done
