#!/bin/sh
#
# Copyright (c) 2007 Eric Wong


test_description='git svn dcommit can commit renames of files with ugly names'

. ./lib-git-svn.sh

test_expect_success 'load repository with strange names' '
	svnadmin load -q "$rawsvnrepo" < "$TEST_DIRECTORY"/t9115/funky-names.dump &&
	start_httpd gtk+
	'

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

test_expect_success 'clone the repository to test rebase' '
	git svn clone "$svnrepo" test-rebase &&
	cd test-rebase &&
		echo test-rebase > test-rebase &&
		git add test-rebase &&
		git commit -m test-rebase &&
		cd ..
	'

test_expect_success 'make a commit to test rebase' '
		echo test-rebase-main > test-rebase-main &&
		git add test-rebase-main &&
		git commit -m test-rebase-main &&
		git svn dcommit
	'

test_expect_success 'git svn rebase works inside a fresh-cloned repository' '
	cd test-rebase &&
		git svn rebase &&
		test -e test-rebase-main &&
		test -e test-rebase
	'

stop_httpd

test_done
