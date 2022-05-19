#!/bin/sh
#
# Copyright (c) 2007 David D. Kilzer

test_description='but svn info'

. ./lib-but-svn.sh

# Tested with: svn, version 1.4.4 (r25188)
# Tested with: svn, version 1.6.[12345689]
v=$(svn_cmd --version | sed -n -e 's/^svn, version \(1\.[0-9]*\.[0-9]*\).*$/\1/p')
case $v in
1.[456].*)
	;;
*)
	skip_all="skipping svn-info test (SVN version: $v not supported)"
	test_done
	;;
esac

# On the "Text Last Updated" line, "but svn info" does not return the
# same value as "svn info" (i.e. the cummit timestamp that touched the
# path most recently); do not expect that field to match.
test_cmp_info () {
	sed -e '/^Text Last Updated:/d' "$1" >tmp.expect &&
	sed -e '/^Text Last Updated:/d' "$2" >tmp.actual &&
	test_cmp tmp.expect tmp.actual &&
	rm -f tmp.expect tmp.actual
}

quoted_svnrepo="$(echo $svnrepo | sed 's/ /%20/')"

test_expect_success 'setup repository and import' '
	mkdir info &&
	(
		cd info &&
		echo FIRST >A &&
		echo one >file &&
		ln -s file symlink-file &&
		mkdir directory &&
		touch directory/.placeholder &&
		ln -s directory symlink-directory &&
		svn_cmd import -m "initial" . "$svnrepo"
	) &&
	svn_cmd co "$svnrepo" svnwc &&
	(
		cd svnwc &&
		echo foo >foo &&
		svn_cmd add foo &&
		svn_cmd cummit -m "change outside directory" &&
		svn_cmd update
	) &&
	mkdir butwc &&
	(
		cd butwc &&
		but svn init "$svnrepo" &&
		but svn fetch
	)
	'

test_expect_success 'info' "
	(cd svnwc && svn info) > expected.info &&
	(cd butwc && but svn info) > actual.info &&
	test_cmp_info expected.info actual.info
	"

test_expect_success 'info --url' '
	test "$(cd butwc && but svn info --url)" = "$quoted_svnrepo"
	'

test_expect_success 'info .' "
	(cd svnwc && svn info .) > expected.info-dot &&
	(cd butwc && but svn info .) > actual.info-dot &&
	test_cmp_info expected.info-dot actual.info-dot
	"

test_expect_success 'info $(pwd)' '
	(cd svnwc && svn info "$(pwd)") >expected.info-pwd &&
	(cd butwc && but svn info "$(pwd)") >actual.info-pwd &&
	grep -v ^Path: <expected.info-pwd >expected.info-np &&
	grep -v ^Path: <actual.info-pwd >actual.info-np &&
	test_cmp_info expected.info-np actual.info-np &&
	test "$(sed -ne \"/^Path:/ s!/svnwc!!\" <expected.info-pwd)" = \
	     "$(sed -ne \"/^Path:/ s!/butwc!!\" <actual.info-pwd)"
	'

test_expect_success 'info $(pwd)/../___wc' '
	(cd svnwc && svn info "$(pwd)/../svnwc") >expected.info-pwd &&
	(cd butwc && but svn info "$(pwd)/../butwc") >actual.info-pwd &&
	grep -v ^Path: <expected.info-pwd >expected.info-np &&
	grep -v ^Path: <actual.info-pwd >actual.info-np &&
	test_cmp_info expected.info-np actual.info-np &&
	test "$(sed -ne \"/^Path:/ s!/svnwc!!\" <expected.info-pwd)" = \
	     "$(sed -ne \"/^Path:/ s!/butwc!!\" <actual.info-pwd)"
	'

test_expect_success 'info $(pwd)/../___wc//file' '
	(cd svnwc && svn info "$(pwd)/../svnwc//file") >expected.info-pwd &&
	(cd butwc && but svn info "$(pwd)/../butwc//file") >actual.info-pwd &&
	grep -v ^Path: <expected.info-pwd >expected.info-np &&
	grep -v ^Path: <actual.info-pwd >actual.info-np &&
	test_cmp_info expected.info-np actual.info-np &&
	test "$(sed -ne \"/^Path:/ s!/svnwc!!\" <expected.info-pwd)" = \
	     "$(sed -ne \"/^Path:/ s!/butwc!!\" <actual.info-pwd)"
	'

test_expect_success 'info --url .' '
	test "$(cd butwc && but svn info --url .)" = "$quoted_svnrepo"
	'

test_expect_success 'info file' "
	(cd svnwc && svn info file) > expected.info-file &&
	(cd butwc && but svn info file) > actual.info-file &&
	test_cmp_info expected.info-file actual.info-file
	"

test_expect_success 'info --url file' '
	test "$(cd butwc && but svn info --url file)" = "$quoted_svnrepo/file"
	'

test_expect_success 'info directory' "
	(cd svnwc && svn info directory) > expected.info-directory &&
	(cd butwc && but svn info directory) > actual.info-directory &&
	test_cmp_info expected.info-directory actual.info-directory
	"

test_expect_success 'info inside directory' "
	(cd svnwc/directory && svn info) > expected.info-inside-directory &&
	(cd butwc/directory && but svn info) > actual.info-inside-directory &&
	test_cmp_info expected.info-inside-directory actual.info-inside-directory
	"

test_expect_success 'info --url directory' '
	test "$(cd butwc && but svn info --url directory)" = "$quoted_svnrepo/directory"
	'

test_expect_success 'info symlink-file' "
	(cd svnwc && svn info symlink-file) > expected.info-symlink-file &&
	(cd butwc && but svn info symlink-file) > actual.info-symlink-file &&
	test_cmp_info expected.info-symlink-file actual.info-symlink-file
	"

test_expect_success 'info --url symlink-file' '
	test "$(cd butwc && but svn info --url symlink-file)" \
	     = "$quoted_svnrepo/symlink-file"
	'

test_expect_success 'info symlink-directory' "
	(cd svnwc && svn info symlink-directory) \
		> expected.info-symlink-directory &&
	(cd butwc && but svn info symlink-directory) \
		> actual.info-symlink-directory &&
	test_cmp_info expected.info-symlink-directory actual.info-symlink-directory
	"

test_expect_success 'info --url symlink-directory' '
	test "$(cd butwc && but svn info --url symlink-directory)" \
	     = "$quoted_svnrepo/symlink-directory"
	'

test_expect_success 'info added-file' "
	echo two > butwc/added-file &&
	(
		cd butwc &&
		but add added-file
	) &&
	cp butwc/added-file svnwc/added-file &&
	(
		cd svnwc &&
		svn_cmd add added-file > /dev/null
	) &&
	(cd svnwc && svn info added-file) > expected.info-added-file &&
	(cd butwc && but svn info added-file) > actual.info-added-file &&
	test_cmp_info expected.info-added-file actual.info-added-file
	"

test_expect_success 'info --url added-file' '
	test "$(cd butwc && but svn info --url added-file)" \
	     = "$quoted_svnrepo/added-file"
	'

test_expect_success 'info added-directory' "
	mkdir butwc/added-directory svnwc/added-directory &&
	touch butwc/added-directory/.placeholder &&
	(
		cd svnwc &&
		svn_cmd add added-directory > /dev/null
	) &&
	(
		cd butwc &&
		but add added-directory
	) &&
	(cd svnwc && svn info added-directory) \
		> expected.info-added-directory &&
	(cd butwc && but svn info added-directory) \
		> actual.info-added-directory &&
	test_cmp_info expected.info-added-directory actual.info-added-directory
	"

test_expect_success 'info --url added-directory' '
	test "$(cd butwc && but svn info --url added-directory)" \
	     = "$quoted_svnrepo/added-directory"
	'

test_expect_success 'info added-symlink-file' "
	(
		cd butwc &&
		ln -s added-file added-symlink-file &&
		but add added-symlink-file
	) &&
	(
		cd svnwc &&
		ln -s added-file added-symlink-file &&
		svn_cmd add added-symlink-file > /dev/null
	) &&
	(cd svnwc && svn info added-symlink-file) \
		> expected.info-added-symlink-file &&
	(cd butwc && but svn info added-symlink-file) \
		> actual.info-added-symlink-file &&
	test_cmp_info expected.info-added-symlink-file \
		actual.info-added-symlink-file
	"

test_expect_success 'info --url added-symlink-file' '
	test "$(cd butwc && but svn info --url added-symlink-file)" \
	     = "$quoted_svnrepo/added-symlink-file"
	'

test_expect_success 'info added-symlink-directory' "
	(
		cd butwc &&
		ln -s added-directory added-symlink-directory &&
		but add added-symlink-directory
	) &&
	(
		cd svnwc &&
		ln -s added-directory added-symlink-directory &&
		svn_cmd add added-symlink-directory > /dev/null
	) &&
	(cd svnwc && svn info added-symlink-directory) \
		> expected.info-added-symlink-directory &&
	(cd butwc && but svn info added-symlink-directory) \
		> actual.info-added-symlink-directory &&
	test_cmp_info expected.info-added-symlink-directory \
		actual.info-added-symlink-directory
	"

test_expect_success 'info --url added-symlink-directory' '
	test "$(cd butwc && but svn info --url added-symlink-directory)" \
	     = "$quoted_svnrepo/added-symlink-directory"
	'

test_expect_success 'info deleted-file' "
	(
		cd butwc &&
		but rm -f file > /dev/null
	) &&
	(
		cd svnwc &&
		svn_cmd rm --force file > /dev/null
	) &&
	(cd svnwc && svn info file) >expected.info-deleted-file &&
	(cd butwc && but svn info file) >actual.info-deleted-file &&
	test_cmp_info expected.info-deleted-file actual.info-deleted-file
	"

test_expect_success 'info --url file (deleted)' '
	test "$(cd butwc && but svn info --url file)" \
	     = "$quoted_svnrepo/file"
	'

test_expect_success 'info deleted-directory' "
	(
		cd butwc &&
		but rm -r -f directory > /dev/null
	) &&
	(
		cd svnwc &&
		svn_cmd rm --force directory > /dev/null
	) &&
	(cd svnwc && svn info directory) >expected.info-deleted-directory &&
	(cd butwc && but svn info directory) >actual.info-deleted-directory &&
	test_cmp_info expected.info-deleted-directory actual.info-deleted-directory
	"

test_expect_success 'info --url directory (deleted)' '
	test "$(cd butwc && but svn info --url directory)" \
	     = "$quoted_svnrepo/directory"
	'

test_expect_success 'info deleted-symlink-file' "
	(
		cd butwc &&
		but rm -f symlink-file > /dev/null
	) &&
	(
		cd svnwc &&
		svn_cmd rm --force symlink-file > /dev/null
	) &&
	(cd svnwc && svn info symlink-file) >expected.info-deleted-symlink-file &&
	(cd butwc && but svn info symlink-file) >actual.info-deleted-symlink-file &&
	test_cmp_info expected.info-deleted-symlink-file actual.info-deleted-symlink-file
	"

test_expect_success 'info --url symlink-file (deleted)' '
	test "$(cd butwc && but svn info --url symlink-file)" \
	     = "$quoted_svnrepo/symlink-file"
	'

test_expect_success 'info deleted-symlink-directory' "
	(
		cd butwc &&
		but rm -f symlink-directory > /dev/null
	) &&
	(
		cd svnwc &&
		svn_cmd rm --force symlink-directory > /dev/null
	) &&
	(cd svnwc && svn info symlink-directory) >expected.info-deleted-symlink-directory &&
	(cd butwc && but svn info symlink-directory) >actual.info-deleted-symlink-directory &&
	test_cmp_info expected.info-deleted-symlink-directory actual.info-deleted-symlink-directory
	"

test_expect_success 'info --url symlink-directory (deleted)' '
	test "$(cd butwc && but svn info --url symlink-directory)" \
	     = "$quoted_svnrepo/symlink-directory"
	'

# NOTE: but does not have the concept of replaced objects,
# so we can't test for files in that state.

test_expect_success 'info unknown-file' "
	echo two > butwc/unknown-file &&
	(cd butwc && test_must_fail but svn info unknown-file) \
		 2> actual.info-unknown-file &&
	grep unknown-file actual.info-unknown-file
	"

test_expect_success 'info --url unknown-file' '
	echo two > butwc/unknown-file &&
	(cd butwc && test_must_fail but svn info --url unknown-file) \
		 2> actual.info-url-unknown-file &&
	grep unknown-file actual.info-url-unknown-file
	'

test_expect_success 'info unknown-directory' "
	mkdir butwc/unknown-directory svnwc/unknown-directory &&
	(cd butwc && test_must_fail but svn info unknown-directory) \
		 2> actual.info-unknown-directory &&
	grep unknown-directory actual.info-unknown-directory
	"

test_expect_success 'info --url unknown-directory' '
	(cd butwc && test_must_fail but svn info --url unknown-directory) \
		 2> actual.info-url-unknown-directory &&
	grep unknown-directory actual.info-url-unknown-directory
	'

test_expect_success 'info unknown-symlink-file' "
	(
		cd butwc &&
		ln -s unknown-file unknown-symlink-file
	) &&
	(cd butwc && test_must_fail but svn info unknown-symlink-file) \
		 2> actual.info-unknown-symlink-file &&
	grep unknown-symlink-file actual.info-unknown-symlink-file
	"

test_expect_success 'info --url unknown-symlink-file' '
	(cd butwc && test_must_fail but svn info --url unknown-symlink-file) \
		 2> actual.info-url-unknown-symlink-file &&
	grep unknown-symlink-file actual.info-url-unknown-symlink-file
	'

test_expect_success 'info unknown-symlink-directory' "
	(
		cd butwc &&
		ln -s unknown-directory unknown-symlink-directory
	) &&
	(cd butwc && test_must_fail but svn info unknown-symlink-directory) \
		 2> actual.info-unknown-symlink-directory &&
	grep unknown-symlink-directory actual.info-unknown-symlink-directory
	"

test_expect_success 'info --url unknown-symlink-directory' '
	(cd butwc && test_must_fail but svn info --url unknown-symlink-directory) \
		 2> actual.info-url-unknown-symlink-directory &&
	grep unknown-symlink-directory actual.info-url-unknown-symlink-directory
	'

test_done
