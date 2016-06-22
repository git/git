#!/bin/sh
test_description='
  test git-bundle under git for Windows

    When we select an empty set of commit (like git bundle create foobar.bundle master..master),
    we should not have problem with the foobar.bundle.lock being locked (especially on Windows).
'

. ./test-lib.sh

test_expect_failure 'try to create a bundle with empty ref count' '
	git bundle create foobar.bundle master..master
'

test_done
