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

# Call list_snapshot with the argument "<basename>"
#
# This will check that gitweb HTTP header contains proposed filename
# as <basename> with '.tar' suffix added, and lists its content to
# stdout of this routine (in "tar test" default listing format)
#
# <prefix> defaults to <basename>
#
list_snapshot () {
	basename="`echo "$1" | sed 's,\/,\.,g'`"
	echo "basename=$basename"
	rm -f file_list
	grep "filename=.*$basename.tar" gitweb.headers >/dev/null 2>&1 &&
	( "$TAR" tf gitweb.body >file_list )
	# In case of grep error, no file_list as well as an error-code
	# In case of tar error, there is a file_list but also an error-code
}

#
# Call check_snapshot with the arguments "<basename> [<prefix>]"
#
# This uses list_snapshot() above to list the tarfile <basename>.tar received
# from gitweb, and that this generated tarfile (gitweb message body) has
# <prefix> prepended as prefix for all objects in the tarfile
# The tarfile listing is exchanged via the "file_list" temporary file
#
# <prefix> defaults to <basename>
#
check_snapshot () {
	basename="$1"
	prefix=${2:-"$1"}
	list_snapshot "$basename" &&
	! grep -v -e "^$prefix$" -e "^$prefix/" -e "^pax_global_header$" file_list
}

# Note: the "xx/test" branch only contains file "foo"; others land in "master"
# Call test_commit with the arguments "<message> [<file> [<contents> [<tag>]]]"
test_expect_success setup '
	test_commit first foo &&
	mkdir -p dir1 && test_commit bar dir1/second bar second &&
	git branch xx/test &&
	mkdir -p dir2 && test_commit pif dir2/third pif third &&
	test_commit wow dir2/"fourth file" wow wow &&
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

test_expect_success 'snapshot sanity: have expected content in xx/test branch - do not have /first file in full snapshot' '
	rm -f gitweb.body file_list &&
	BRANCH=xx/test &&
	gitweb_run "p=.git;a=snapshot;h=$BRANCH;sf=tar" &&
	ID=$(git rev-parse --verify --short=7 "$BRANCH") &&
	list_snapshot ".git-$BRANCH-$ID" &&
	! grep "first" file_list
'
test_debug 'cat gitweb.headers && cat file_list'

test_expect_success 'snapshot certain objects: have expected content in master branch - only those under subdir dir2/ and not others' '
	rm -f gitweb.body file_list &&
	BRANCH=master &&
	gitweb_run "p=.git;a=snapshot;h=$BRANCH;sf=tar;f=dir2" &&
	ID=$(git rev-parse --verify --short=7 "$BRANCH") &&
	list_snapshot ".git-$BRANCH-$ID" &&
	! grep "foo" file_list &&
	! grep "dir1/second" file_list &&
	grep "dir2/third" file_list &&
	grep "dir2/fourth file" file_list
'
test_debug 'cat gitweb.headers && cat file_list'

test_expect_success 'snapshot certain objects: have expected content in master branch - subdir name is required in requested nested path (bad path - empty output and/or HTTP-404)' '
	rm -f gitweb.body file_list &&
	BRANCH=master &&
	gitweb_run "p=.git;a=snapshot;h=$BRANCH;sf=tar;f=third" &&
	[ ! -s gitweb.body -o -n "`head -1 gitweb.headers | egrep "^Status: 404 "`" ]
'
test_debug 'cat gitweb.headers && ls -la gitweb.body file_list || true'

test_expect_success 'snapshot certain objects: have expected content in master branch - correct subdir name is required in requested nested path (bad path - empty output and/or HTTP-404)' '
	rm -f gitweb.body file_list &&
	BRANCH=master &&
	gitweb_run "p=.git;a=snapshot;h=$BRANCH;sf=tar;f=dir1/third" &&
	[ ! -s gitweb.body -o -n "`head -1 gitweb.headers | egrep "^Status: 404 "`" ]
'
test_debug 'cat gitweb.headers && ls -la gitweb.body file_list || true'

test_expect_success 'snapshot certain objects: have expected content in master branch - can request filenames with spaces (backslash + HTML-escape)' '
	rm -f gitweb.body file_list &&
	BRANCH=master &&
	gitweb_run "p=.git;a=snapshot;h=$BRANCH;sf=tar;f=dir2/fourth\%20file" &&
	ID=$(git rev-parse --verify --short=7 "$BRANCH") &&
	list_snapshot ".git-$BRANCH-$ID" &&
	! grep "foo" file_list &&
	! grep "dir1/second" file_list &&
	! grep "dir2/third" file_list &&
	grep "dir2/fourth file" file_list
'
test_debug 'cat gitweb.headers && cat file_list'

test_expect_success 'snapshot certain objects: have expected content in master branch - can request list of filenames separated by HTML-escaped spaces' '
	rm -f gitweb.body file_list &&
	BRANCH=master &&
	gitweb_run "p=.git;a=snapshot;h=$BRANCH;sf=tar;f=dir1/second%20dir2/third" &&
	ID=$(git rev-parse --verify --short=7 "$BRANCH") &&
	list_snapshot ".git-$BRANCH-$ID" &&
	! grep "foo" file_list &&
	grep "dir1/second" file_list &&
	grep "dir2/third" file_list &&
	! grep "dir2/fourth file" file_list
'
test_debug 'cat gitweb.headers && cat file_list'

test_expect_success 'snapshot certain objects: have expected content in master branch - can request list of filenames separated by HTML-escaped spaces including a filename with spaces (backslash + HTML-escape)' '
	rm -f gitweb.body file_list &&
	BRANCH=master &&
	gitweb_run "p=.git;a=snapshot;h=$BRANCH;sf=tar;f=foo%20dir2/fourth\%20file%20dir1/second" &&
	ID=$(git rev-parse --verify --short=7 "$BRANCH") &&
	list_snapshot ".git-$BRANCH-$ID" &&
	grep "foo" file_list &&
	grep "dir1/second" file_list &&
	! grep "dir2/third" file_list &&
	grep "dir2/fourth file" file_list
'
test_debug 'cat gitweb.headers && cat file_list'

test_expect_success 'snapshot certain objects: have only expected content in refs/tags/second full tag' '
	rm -f gitweb.body file_list &&
	BRANCH=second &&
	gitweb_run "p=.git;a=snapshot;h=refs/tags/$BRANCH;sf=tar;f=dir1/second" &&
	list_snapshot ".git-$BRANCH" &&
	! grep "foo" file_list &&
	grep "dir1/second" file_list &&
	! grep "dir2/third" file_list &&
	! grep "dir2/fourth file" file_list
'
test_debug 'cat gitweb.headers && cat file_list'

test_expect_success 'snapshot certain objects: have expected content in xx/test branch - request for only absent subdir dir2/ fails (empty output and/or HTTP-404)' '
	rm -f gitweb.body file_list &&
	BRANCH=xx/test &&
	gitweb_run "p=.git;a=snapshot;h=$BRANCH;sf=tar;f=dir2" &&
	[ ! -s gitweb.body -o -n "`head -1 gitweb.headers | egrep "^Status: 404 "`" ]
'
test_debug 'cat gitweb.headers && ls -la gitweb.body file_list || true'

test_expect_success 'snapshot certain objects: have expected content in xx/test branch - request for file /foo and absent subdir dir2/ also fails (empty output and/or HTTP-404)' '
	rm -f gitweb.body file_list &&
	BRANCH=xx/test &&
	gitweb_run "p=.git;a=snapshot;h=$BRANCH;sf=tar;f=dir2%20foo" &&
	[ ! -s gitweb.body -o -n "`head -1 gitweb.headers | egrep "^Status: 404 "`" ]
'
test_debug 'cat gitweb.headers && ls -la gitweb.body file_list || true'

test_expect_success 'snapshot certain objects: have expected content in xx/test branch - have /foo file (and only it)' '
	rm -f gitweb.body file_list &&
	BRANCH=xx/test &&
	gitweb_run "p=.git;a=snapshot;h=$BRANCH;sf=tar;f=foo" &&
	ID=$(git rev-parse --verify --short=7 "$BRANCH") &&
	list_snapshot ".git-$BRANCH-$ID" &&
	grep "foo" file_list &&
	! grep "dir1/second" file_list &&
	! grep "dir2/third" file_list &&
	! grep "dir2/fourth file" file_list
'
test_debug 'cat gitweb.headers && cat file_list'

# ----------------------------------------------------------------------
# optional debugging in log, if allowed on server and requested by user

test_expect_success 'snapshot: debugging logged as forbidden when not defined in server environment' '
	rm -f gitweb.body gitweb.log gitweb.headers gitweb.output &&
	gitweb_run "p=.git;a=snapshot;h=master;sf=tar;debug=yes" &&
	grep "GITWEB_MAY_DEBUG=yes is not set" < gitweb.log >/dev/null
'
test_debug 'cat gitweb.headers gitweb.log'

test_expect_success 'snapshot: debugging logged as forbidden when not allowed in server environment' '
	rm -f gitweb.body gitweb.log gitweb.headers gitweb.output &&
	GITWEB_MAY_DEBUG=xxx && export GITWEB_MAY_DEBUG &&
	gitweb_run "p=.git;a=snapshot;h=master;sf=tar;debug=yes" &&
	grep "GITWEB_MAY_DEBUG=yes is not set" < gitweb.log >/dev/null
'
test_debug 'cat gitweb.headers gitweb.log'

test_expect_success 'snapshot: debugging present when allowed in server environment' '
	rm -f gitweb.body gitweb.log gitweb.headers gitweb.output &&
	GITWEB_MAY_DEBUG=yes && export GITWEB_MAY_DEBUG &&
	gitweb_run "p=.git;a=snapshot;h=master;sf=tar;debug=yes" &&
	! grep "GITWEB_MAY_DEBUG=yes is not set" < gitweb.log >/dev/null &&
	grep "git-archive" < gitweb.log >/dev/null
'
test_debug 'cat gitweb.headers gitweb.log'

test_expect_success 'snapshot: debugging absent when not allowed in server environment' '
	rm -f gitweb.body gitweb.log gitweb.headers gitweb.output &&
	GITWEB_MAY_DEBUG=xxx && export GITWEB_MAY_DEBUG &&
	gitweb_run "p=.git;a=snapshot;h=master;sf=tar;debug=yes" &&
	! grep "git-archive" < gitweb.log >/dev/null
'
test_debug 'cat gitweb.headers gitweb.log'

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

test_done
