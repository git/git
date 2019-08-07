#!/bin/sh

test_description='Test git update-server-info'

. ./test-lib.sh

test_expect_success 'setup' 'test_commit file'

test_expect_success 'create info/refs' '
	git update-server-info &&
	test_path_is_file .git/info/refs
'

test_expect_success 'modify and store mtime' '
	test-tool chmtime =0 .git/info/refs &&
	test-tool chmtime --get .git/info/refs >a
'

test_expect_success 'info/refs is not needlessly overwritten' '
	git update-server-info &&
	test-tool chmtime --get .git/info/refs >b &&
	test_cmp a b
'

test_expect_success 'info/refs can be forced to update' '
	git update-server-info -f &&
	test-tool chmtime --get .git/info/refs >b &&
	! test_cmp a b
'

test_expect_success 'info/refs updates when changes are made' '
	test-tool chmtime =0 .git/info/refs &&
	test-tool chmtime --get .git/info/refs >b &&
	test_cmp a b &&
	git update-ref refs/heads/foo HEAD &&
	git update-server-info &&
	test-tool chmtime --get .git/info/refs >b &&
	! test_cmp a b
'

test_done
