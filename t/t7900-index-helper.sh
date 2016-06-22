#!/bin/sh
#
# Copyright (c) 2016, Twitter, Inc
#

test_description='git-index-helper

Testing git index-helper
'

. ./test-lib.sh

test -n "$NO_MMAP" && {
	skip_all='skipping index-helper tests: no mmap'
	test_done
}

test_expect_success 'index-helper smoke test' '
	# We need an existing commit so that the index exists (otherwise,
	# the index-helper will not be autostarted)
	test_commit x &&
	git index-helper --exit-after 1 &&
	test_path_is_missing .git/index-helper.sock
'

test_expect_success 'index-helper creates usable path file and can be killed' '
	test_when_finished "git index-helper --kill" &&
	test_path_is_missing .git/index-helper.sock &&
	git index-helper --detach &&
	test -S .git/index-helper.sock &&
	git index-helper --kill &&
	test_path_is_missing .git/index-helper.sock
'

test_expect_success 'index-helper will not start if already running' '
	test_when_finished "git index-helper --kill" &&
	git index-helper --detach &&
	test -S .git/index-helper.sock &&
	test_must_fail git index-helper 2>err &&
	test -S .git/index-helper.sock &&
	grep "Already running" err
'

test_expect_success 'index-helper is quiet with --autorun' '
	test_when_finished "git index-helper --kill" &&
	git index-helper --kill &&
	git index-helper --detach &&
	test -S .git/index-helper.sock &&
	git index-helper --autorun
'

test_expect_success 'index-helper autorun works' '
	test_when_finished "git index-helper --kill" &&
	rm -f .git/index-helper.sock &&
	git status &&
	test_path_is_missing .git/index-helper.sock &&
	test_config indexhelper.autorun true &&
	git status &&
	test -S .git/index-helper.sock &&
	git status 2>err &&
	test -S .git/index-helper.sock &&
	test_must_be_empty err &&
	git index-helper --kill &&
	test_config indexhelper.autorun false &&
	git status &&
	test_path_is_missing .git/index-helper.sock
'

test_expect_success 'indexhelper.exitafter config works' '
	test_when_finished "git index-helper --kill" &&
	test_config indexhelper.exitafter 1 &&
	git index-helper --detach &&
	sleep 3 &&
	test_path_is_missing .git/index-helper.sock
'

test_done
