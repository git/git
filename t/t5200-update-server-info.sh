#!/bin/sh

test_description='Test but update-server-info'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' 'test_cummit file'

test_expect_success 'create info/refs' '
	but update-server-info &&
	test_path_is_file .but/info/refs
'

test_expect_success 'modify and store mtime' '
	test-tool chmtime =0 .but/info/refs &&
	test-tool chmtime --get .but/info/refs >a
'

test_expect_success 'info/refs is not needlessly overwritten' '
	but update-server-info &&
	test-tool chmtime --get .but/info/refs >b &&
	test_cmp a b
'

test_expect_success 'info/refs can be forced to update' '
	but update-server-info -f &&
	test-tool chmtime --get .but/info/refs >b &&
	! test_cmp a b
'

test_expect_success 'info/refs updates when changes are made' '
	test-tool chmtime =0 .but/info/refs &&
	test-tool chmtime --get .but/info/refs >b &&
	test_cmp a b &&
	but update-ref refs/heads/foo HEAD &&
	but update-server-info &&
	test-tool chmtime --get .but/info/refs >b &&
	! test_cmp a b
'

test_done
