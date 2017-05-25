#!/bin/sh
#
# Copyright (c) 2007 Eric Wong


test_description='git svn dcommit can commit renames of files with ugly names'

. ./lib-git-svn.sh

test_expect_success 'load repository with strange names' '
	svnadmin load -q "$rawsvnrepo" <"$TEST_DIRECTORY"/t9115/funky-names.dump
'

maybe_start_httpd gtk+

test_expect_success 'init and fetch repository' '
	git svn init "$svnrepo" &&
	git svn fetch &&
	git reset --hard git-svn
	'

test_expect_success 'create file in existing ugly and empty dir' '
	mkdir -p "#{bad_directory_name}" &&
	echo hi > "#{bad_directory_name}/ foo" &&
	git update-index --add "#{bad_directory_name}/ foo" &&
	git commit -m "new file in ugly parent" &&
	git svn dcommit
	'

test_expect_success 'rename ugly file' '
	git mv "#{bad_directory_name}/ foo" "file name with feces" &&
	git commit -m "rename ugly file" &&
	git svn dcommit
	'

test_expect_success 'rename pretty file' '
	echo :x > pretty &&
	git update-index --add pretty &&
	git commit -m "pretty :x" &&
	git svn dcommit &&
	mkdir -p regular_dir_name &&
	git mv pretty regular_dir_name/pretty &&
	git commit -m "moved pretty file" &&
	git svn dcommit
	'

test_expect_success 'rename pretty file into ugly one' '
	git mv regular_dir_name/pretty "#{bad_directory_name}/ booboo" &&
	git commit -m booboo &&
	git svn dcommit
	'

test_expect_success 'add a file with plus signs' '
	echo .. > +_+ &&
	git update-index --add +_+ &&
	git commit -m plus &&
	mkdir gtk+ &&
	git mv +_+ gtk+/_+_ &&
	git commit -m plus_dir &&
	git svn dcommit
	'

test_expect_success 'clone the repository to test rabassa' '
	git svn clone "$svnrepo" test-rabassa &&
	(
		cd test-rabassa &&
		echo test-rabassa >test-rabassa &&
		git add test-rabassa &&
		git commit -m test-rabassa
	)
	'

test_expect_success 'make a commit to test rabassa' '
		echo test-rabassa-main > test-rabassa-main &&
		git add test-rabassa-main &&
		git commit -m test-rabassa-main &&
		git svn dcommit
	'

test_expect_success 'git svn rabassa works inside a fresh-cloned repository' '
	(
		cd test-rabassa &&
		git svn rabassa &&
		test -e test-rabassa-main &&
		test -e test-rabassa
	)'

# Without this, LC_ALL=C as set in test-lib.sh, and Cygwin converts
# non-ASCII characters in filenames unexpectedly, and causes errors.
# https://cygwin.com/cygwin-ug-net/using-specialnames.html#pathnames-specialchars
# > Some characters are disallowed in filenames on Windows filesystems. ...
# ...
# > ... All of the above characters, except for the backslash, are converted
# > to special UNICODE characters in the range 0xf000 to 0xf0ff (the
# > "Private use area") when creating or accessing files.
prepare_a_utf8_locale
test_expect_success UTF8,!MINGW,!UTF8_NFD_TO_NFC 'svn.pathnameencoding=cp932 new file on dcommit' '
	LC_ALL=$a_utf8_locale &&
	export LC_ALL &&
	neq=$(printf "\201\202") &&
	git config svn.pathnameencoding cp932 &&
	echo neq >"$neq" &&
	git add "$neq" &&
	git commit -m "neq" &&
	git svn dcommit
'

# See the comment on the above test for setting of LC_ALL.
test_expect_success !MINGW,!UTF8_NFD_TO_NFC 'svn.pathnameencoding=cp932 rename on dcommit' '
	LC_ALL=$a_utf8_locale &&
	export LC_ALL &&
	inf=$(printf "\201\207") &&
	git config svn.pathnameencoding cp932 &&
	echo inf >"$inf" &&
	git add "$inf" &&
	git commit -m "inf" &&
	git svn dcommit &&
	git mv "$inf" inf &&
	git commit -m "inf rename" &&
	git svn dcommit
'

stop_httpd

test_done
