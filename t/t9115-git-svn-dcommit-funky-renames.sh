#!/bin/sh
#
# Copyright (c) 2007 Eric Wong


test_description='git-svn dcommit can commit renames of files with ugly names'

. ./lib-git-svn.sh

test_expect_success 'load repository with strange names' "
	svnadmin load -q $rawsvnrepo < ../t9115/funky-names.dump &&
	start_httpd
	"

test_expect_success 'init and fetch repository' "
	git svn init $svnrepo &&
	git svn fetch &&
	git reset --hard git-svn
	"

test_expect_success 'create file in existing ugly and empty dir' '
	mkdir "#{bad_directory_name}" &&
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
	mkdir regular_dir_name &&
	git mv pretty regular_dir_name/pretty &&
	git commit -m "moved pretty file" &&
	git svn dcommit
	'

test_expect_success 'rename pretty file into ugly one' '
	git mv regular_dir_name/pretty "#{bad_directory_name}/ booboo" &&
	git commit -m booboo &&
	git svn dcommit
	'

stop_httpd

test_done
