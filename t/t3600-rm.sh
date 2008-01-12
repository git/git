#!/bin/sh
#
# Copyright (c) 2006 Carl D. Worth
#

test_description='Test of the various options to git rm.'

. ./test-lib.sh

# Setup some files to be removed, some with funny characters
test_expect_success \
    'Initialize test directory' \
    "touch -- foo bar baz 'space embedded' -q &&
     git add -- foo bar baz 'space embedded' -q &&
     git-commit -m 'add normal files' &&
     test_tabs=y &&
     if touch -- 'tab	embedded' 'newline
embedded'
     then
     git add -- 'tab	embedded' 'newline
embedded' &&
     git-commit -m 'add files with tabs and newlines'
     else
         say 'Your filesystem does not allow tabs in filenames.'
         test_tabs=n
     fi"

# Later we will try removing an unremovable path to make sure
# git rm barfs, but if the test is run as root that cannot be
# arranged.
test_expect_success \
    'Determine rm behavior' \
    ': >test-file
     chmod a-w .
     rm -f test-file
     test -f test-file && test_failed_remove=y
     chmod 775 .
     rm -f test-file'

test_expect_success \
    'Pre-check that foo exists and is in index before git rm foo' \
    '[ -f foo ] && git ls-files --error-unmatch foo'

test_expect_success \
    'Test that git rm foo succeeds' \
    'git rm --cached foo'

test_expect_success \
    'Test that git rm --cached foo succeeds if the index matches the file' \
    'echo content > foo
     git add foo
     git rm --cached foo'

test_expect_success \
    'Test that git rm --cached foo succeeds if the index matches the file' \
    'echo content > foo
     git add foo
     git commit -m foo
     echo "other content" > foo
     git rm --cached foo'

test_expect_failure \
    'Test that git rm --cached foo fails if the index matches neither the file nor HEAD' \
    'echo content > foo
     git add foo
     git commit -m foo
     echo "other content" > foo
     git add foo
     echo "yet another content" > foo
     git rm --cached foo'

test_expect_success \
    'Test that git rm --cached -f foo works in case where --cached only did not' \
    'echo content > foo
     git add foo
     git commit -m foo
     echo "other content" > foo
     git add foo
     echo "yet another content" > foo
     git rm --cached -f foo'

test_expect_success \
    'Post-check that foo exists but is not in index after git rm foo' \
    '[ -f foo ] && ! git ls-files --error-unmatch foo'

test_expect_success \
    'Pre-check that bar exists and is in index before "git rm bar"' \
    '[ -f bar ] && git ls-files --error-unmatch bar'

test_expect_success \
    'Test that "git rm bar" succeeds' \
    'git rm bar'

test_expect_success \
    'Post-check that bar does not exist and is not in index after "git rm -f bar"' \
    '! [ -f bar ] && ! git ls-files --error-unmatch bar'

test_expect_success \
    'Test that "git rm -- -q" succeeds (remove a file that looks like an option)' \
    'git rm -- -q'

test "$test_tabs" = y && test_expect_success \
    "Test that \"git rm -f\" succeeds with embedded space, tab, or newline characters." \
    "git rm -f 'space embedded' 'tab	embedded' 'newline
embedded'"

if test "$test_failed_remove" = y; then
chmod a-w .
test_expect_failure \
    'Test that "git rm -f" fails if its rm fails' \
    'git rm -f baz'
chmod 775 .
else
    test_expect_success 'skipping removal failure (perhaps running as root?)' :
fi

test_expect_success \
    'When the rm in "git rm -f" fails, it should not remove the file from the index' \
    'git ls-files --error-unmatch baz'

test_expect_success 'Remove nonexistent file with --ignore-unmatch' '
	git rm --ignore-unmatch nonexistent
'

test_expect_success '"rm" command printed' '
	echo frotz > test-file &&
	git add test-file &&
	git commit -m "add file for rm test" &&
	git rm test-file > rm-output &&
	test `grep "^rm " rm-output | wc -l` = 1 &&
	rm -f test-file rm-output &&
	git commit -m "remove file from rm test"
'

test_expect_success '"rm" command suppressed with --quiet' '
	echo frotz > test-file &&
	git add test-file &&
	git commit -m "add file for rm --quiet test" &&
	git rm --quiet test-file > rm-output &&
	test `wc -l < rm-output` = 0 &&
	rm -f test-file rm-output &&
	git commit -m "remove file from rm --quiet test"
'

# Now, failure cases.
test_expect_success 'Re-add foo and baz' '
	git add foo baz &&
	git ls-files --error-unmatch foo baz
'

test_expect_success 'Modify foo -- rm should refuse' '
	echo >>foo &&
	! git rm foo baz &&
	test -f foo &&
	test -f baz &&
	git ls-files --error-unmatch foo baz
'

test_expect_success 'Modified foo -- rm -f should work' '
	git rm -f foo baz &&
	test ! -f foo &&
	test ! -f baz &&
	! git ls-files --error-unmatch foo &&
	! git ls-files --error-unmatch bar
'

test_expect_success 'Re-add foo and baz for HEAD tests' '
	echo frotz >foo &&
	git checkout HEAD -- baz &&
	git add foo baz &&
	git ls-files --error-unmatch foo baz
'

test_expect_success 'foo is different in index from HEAD -- rm should refuse' '
	! git rm foo baz &&
	test -f foo &&
	test -f baz &&
	git ls-files --error-unmatch foo baz
'

test_expect_success 'but with -f it should work.' '
	git rm -f foo baz &&
	test ! -f foo &&
	test ! -f baz &&
	! git ls-files --error-unmatch foo
	! git ls-files --error-unmatch baz
'

test_expect_success 'Recursive test setup' '
	mkdir -p frotz &&
	echo qfwfq >frotz/nitfol &&
	git add frotz &&
	git commit -m "subdir test"
'

test_expect_success 'Recursive without -r fails' '
	! git rm frotz &&
	test -d frotz &&
	test -f frotz/nitfol
'

test_expect_success 'Recursive with -r but dirty' '
	echo qfwfq >>frotz/nitfol
	! git rm -r frotz &&
	test -d frotz &&
	test -f frotz/nitfol
'

test_expect_success 'Recursive with -r -f' '
	git rm -f -r frotz &&
	! test -f frotz/nitfol &&
	! test -d frotz
'

test_expect_failure 'Remove nonexistent file returns nonzero exit status' '
	git rm nonexistent
'

test_done
