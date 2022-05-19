#!/bin/sh
#
# Copyright (C) 2012
#     Charles Roussel <charles.roussel@ensimag.imag.fr>
#     Simon Cathebras <simon.cathebras@ensimag.imag.fr>
#     Julien Khayat <julien.khayat@ensimag.imag.fr>
#     Guillaume Sasdy <guillaume.sasdy@ensimag.imag.fr>
#     Simon Perrat <simon.perrat@ensimag.imag.fr>
#
# License: GPL v2 or later

# tests for but-remote-mediawiki

test_description='Test the Git Mediawiki remote helper: but push and but pull simple test cases'

. ./test-butmw-lib.sh
. $TEST_DIRECTORY/test-lib.sh


test_check_precond


test_but_reimport () {
	but -c remote.origin.dumbPush=true push &&
	but -c remote.origin.mediaImport=true pull --rebase
}

# Don't bother with permissions, be administrator by default
test_expect_success 'setup config' '
	but config --global remote.origin.mwLogin "$WIKI_ADMIN" &&
	but config --global remote.origin.mwPassword "$WIKI_PASSW" &&
	test_might_fail but config --global --unset remote.origin.mediaImport
'

test_expect_failure 'but push can upload media (File:) files' '
	wiki_reset &&
	but clone mediawiki::'"$WIKI_URL"' mw_dir &&
	(
		cd mw_dir &&
		echo "hello world" >Foo.txt &&
		but add Foo.txt &&
		but cummit -m "add a text file" &&
		but push &&
		"$PERL_PATH" -e "print STDOUT \"binary content: \".chr(255);" >Foo.txt &&
		but add Foo.txt &&
		but cummit -m "add a text file with binary content" &&
		but push
	)
'

test_expect_failure 'but clone works on previously created wiki with media files' '
	test_when_finished "rm -rf mw_dir mw_dir_clone" &&
	but clone -c remote.origin.mediaimport=true \
		mediawiki::'"$WIKI_URL"' mw_dir_clone &&
	test_cmp mw_dir_clone/Foo.txt mw_dir/Foo.txt &&
	(cd mw_dir_clone && but checkout HEAD^) &&
	(cd mw_dir && but checkout HEAD^) &&
	test_path_is_file mw_dir_clone/Foo.txt &&
	test_cmp mw_dir_clone/Foo.txt mw_dir/Foo.txt
'

test_expect_success 'but push can upload media (File:) files containing valid UTF-8' '
	wiki_reset &&
	but clone mediawiki::'"$WIKI_URL"' mw_dir &&
	(
		cd mw_dir &&
		"$PERL_PATH" -e "print STDOUT \"UTF-8 content: éèàéê€.\";" >Bar.txt &&
		but add Bar.txt &&
		but cummit -m "add a text file with UTF-8 content" &&
		but push
	)
'

test_expect_success 'but clone works on previously created wiki with media files containing valid UTF-8' '
	test_when_finished "rm -rf mw_dir mw_dir_clone" &&
	but clone -c remote.origin.mediaimport=true \
		mediawiki::'"$WIKI_URL"' mw_dir_clone &&
	test_cmp mw_dir_clone/Bar.txt mw_dir/Bar.txt
'

test_expect_success 'but push & pull work with locally renamed media files' '
	wiki_reset &&
	but clone mediawiki::'"$WIKI_URL"' mw_dir &&
	test_when_finished "rm -fr mw_dir" &&
	(
		cd mw_dir &&
		echo "A File" >Foo.txt &&
		but add Foo.txt &&
		but cummit -m "add a file" &&
		but mv Foo.txt Bar.txt &&
		but cummit -m "Rename a file" &&
		test_but_reimport &&
		echo "A File" >expect &&
		test_cmp expect Bar.txt &&
		test_path_is_missing Foo.txt
	)
'

test_expect_success 'but push can propagate local page deletion' '
	wiki_reset &&
	but clone mediawiki::'"$WIKI_URL"' mw_dir &&
	test_when_finished "rm -fr mw_dir" &&
	(
		cd mw_dir &&
		test_path_is_missing Foo.mw &&
		echo "hello world" >Foo.mw &&
		but add Foo.mw &&
		but cummit -m "Add the page Foo" &&
		but push &&
		rm -f Foo.mw &&
		but cummit -am "Delete the page Foo" &&
		test_but_reimport &&
		test_path_is_missing Foo.mw
	)
'

test_expect_success 'but push can propagate local media file deletion' '
	wiki_reset &&
	but clone mediawiki::'"$WIKI_URL"' mw_dir &&
	test_when_finished "rm -fr mw_dir" &&
	(
		cd mw_dir &&
		echo "hello world" >Foo.txt &&
		but add Foo.txt &&
		but cummit -m "Add the text file Foo" &&
		but rm Foo.txt &&
		but cummit -m "Delete the file Foo" &&
		test_but_reimport &&
		test_path_is_missing Foo.txt
	)
'

# test failure: the file is correctly uploaded, and then deleted but
# as no page link to it, the import (which looks at page revisions)
# doesn't notice the file deletion on the wiki. We fetch the list of
# files from the wiki, but as the file is deleted, it doesn't appear.
test_expect_failure 'but pull correctly imports media file deletion when no page link to it' '
	wiki_reset &&
	but clone mediawiki::'"$WIKI_URL"' mw_dir &&
	test_when_finished "rm -fr mw_dir" &&
	(
		cd mw_dir &&
		echo "hello world" >Foo.txt &&
		but add Foo.txt &&
		but cummit -m "Add the text file Foo" &&
		but push &&
		but rm Foo.txt &&
		but cummit -m "Delete the file Foo" &&
		test_but_reimport &&
		test_path_is_missing Foo.txt
	)
'

test_expect_success 'but push properly warns about insufficient permissions' '
	wiki_reset &&
	but clone mediawiki::'"$WIKI_URL"' mw_dir &&
	test_when_finished "rm -fr mw_dir" &&
	(
		cd mw_dir &&
		echo "A File" >foo.forbidden &&
		but add foo.forbidden &&
		but cummit -m "add a file" &&
		but push 2>actual &&
		test_i18ngrep "foo.forbidden is not a permitted file" actual
	)
'

test_expect_success 'setup a repository with media files' '
	wiki_reset &&
	wiki_editpage testpage "I am linking a file [[File:File.txt]]" false &&
	echo "File content" >File.txt &&
	wiki_upload_file File.txt &&
	echo "Another file content" >AnotherFile.txt &&
	wiki_upload_file AnotherFile.txt
'

test_expect_success 'but clone works with one specific page cloned and mediaimport=true' '
	but clone -c remote.origin.pages=testpage \
		  -c remote.origin.mediaimport=true \
			mediawiki::'"$WIKI_URL"' mw_dir_15 &&
	test_when_finished "rm -rf mw_dir_15" &&
	test_contains_N_files mw_dir_15 3 &&
	test_path_is_file mw_dir_15/Testpage.mw &&
	test_path_is_file mw_dir_15/File:File.txt.mw &&
	test_path_is_file mw_dir_15/File.txt &&
	test_path_is_missing mw_dir_15/Main_Page.mw &&
	test_path_is_missing mw_dir_15/File:AnotherFile.txt.mw &&
	test_path_is_missing mw_dir_15/AnothetFile.txt &&
	wiki_check_content mw_dir_15/Testpage.mw Testpage &&
	test_cmp mw_dir_15/File.txt File.txt
'

test_expect_success 'but clone works with one specific page cloned and mediaimport=false' '
	test_when_finished "rm -rf mw_dir_16" &&
	but clone -c remote.origin.pages=testpage \
			mediawiki::'"$WIKI_URL"' mw_dir_16 &&
	test_contains_N_files mw_dir_16 1 &&
	test_path_is_file mw_dir_16/Testpage.mw &&
	test_path_is_missing mw_dir_16/File:File.txt.mw &&
	test_path_is_missing mw_dir_16/File.txt &&
	test_path_is_missing mw_dir_16/Main_Page.mw &&
	wiki_check_content mw_dir_16/Testpage.mw Testpage
'

# should behave like mediaimport=false
test_expect_success 'but clone works with one specific page cloned and mediaimport unset' '
	test_when_finished "rm -fr mw_dir_17" &&
	but clone -c remote.origin.pages=testpage \
		mediawiki::'"$WIKI_URL"' mw_dir_17 &&
	test_contains_N_files mw_dir_17 1 &&
	test_path_is_file mw_dir_17/Testpage.mw &&
	test_path_is_missing mw_dir_17/File:File.txt.mw &&
	test_path_is_missing mw_dir_17/File.txt &&
	test_path_is_missing mw_dir_17/Main_Page.mw &&
	wiki_check_content mw_dir_17/Testpage.mw Testpage
'

test_done
