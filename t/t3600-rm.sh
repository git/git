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
     git commit -m 'add normal files'"

if touch -- 'tab	embedded' 'newline
embedded' 2>/dev/null
then
	test_set_prereq FUNNYNAMES
else
	say 'Your filesystem does not allow tabs in filenames.'
fi

test_expect_success FUNNYNAMES 'add files with funny names' "
     git add -- 'tab	embedded' 'newline
embedded' &&
     git commit -m 'add files with tabs and newlines'
"

# Determine rm behavior
# Later we will try removing an unremovable path to make sure
# git rm barfs, but if the test is run as root that cannot be
# arranged.
: >test-file
chmod a-w .
rm -f test-file 2>/dev/null
if test -f test-file
then
	test_set_prereq RO_DIR
else
	skip_all='skipping removal failure test (perhaps running as root?)'
fi
chmod 775 .
rm -f test-file

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

test_expect_success \
    'Test that git rm --cached foo fails if the index matches neither the file nor HEAD' '
     echo content > foo
     git add foo
     git commit -m foo
     echo "other content" > foo
     git add foo
     echo "yet another content" > foo
     test_must_fail git rm --cached foo
'

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
    '[ -f foo ] && test_must_fail git ls-files --error-unmatch foo'

test_expect_success \
    'Pre-check that bar exists and is in index before "git rm bar"' \
    '[ -f bar ] && git ls-files --error-unmatch bar'

test_expect_success \
    'Test that "git rm bar" succeeds' \
    'git rm bar'

test_expect_success \
    'Post-check that bar does not exist and is not in index after "git rm -f bar"' \
    '! [ -f bar ] && test_must_fail git ls-files --error-unmatch bar'

test_expect_success \
    'Test that "git rm -- -q" succeeds (remove a file that looks like an option)' \
    'git rm -- -q'

test_expect_success FUNNYNAMES \
    "Test that \"git rm -f\" succeeds with embedded space, tab, or newline characters." \
    "git rm -f 'space embedded' 'tab	embedded' 'newline
embedded'"

test_expect_success RO_DIR 'Test that "git rm -f" fails if its rm fails' '
	chmod a-w . &&
	test_must_fail git rm -f baz &&
	chmod 775 .
'

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
	test_must_fail git rm foo baz &&
	test -f foo &&
	test -f baz &&
	git ls-files --error-unmatch foo baz
'

test_expect_success 'Modified foo -- rm -f should work' '
	git rm -f foo baz &&
	test ! -f foo &&
	test ! -f baz &&
	test_must_fail git ls-files --error-unmatch foo &&
	test_must_fail git ls-files --error-unmatch bar
'

test_expect_success 'Re-add foo and baz for HEAD tests' '
	echo frotz >foo &&
	git checkout HEAD -- baz &&
	git add foo baz &&
	git ls-files --error-unmatch foo baz
'

test_expect_success 'foo is different in index from HEAD -- rm should refuse' '
	test_must_fail git rm foo baz &&
	test -f foo &&
	test -f baz &&
	git ls-files --error-unmatch foo baz
'

test_expect_success 'but with -f it should work.' '
	git rm -f foo baz &&
	test ! -f foo &&
	test ! -f baz &&
	test_must_fail git ls-files --error-unmatch foo
	test_must_fail git ls-files --error-unmatch baz
'

test_expect_success 'refuse to remove cached empty file with modifications' '
	>empty &&
	git add empty &&
	echo content >empty &&
	test_must_fail git rm --cached empty
'

test_expect_success 'remove intent-to-add file without --force' '
	echo content >intent-to-add &&
	git add -N intent-to-add
	git rm --cached intent-to-add
'

test_expect_success 'Recursive test setup' '
	mkdir -p frotz &&
	echo qfwfq >frotz/nitfol &&
	git add frotz &&
	git commit -m "subdir test"
'

test_expect_success 'Recursive without -r fails' '
	test_must_fail git rm frotz &&
	test -d frotz &&
	test -f frotz/nitfol
'

test_expect_success 'Recursive with -r but dirty' '
	echo qfwfq >>frotz/nitfol
	test_must_fail git rm -r frotz &&
	test -d frotz &&
	test -f frotz/nitfol
'

test_expect_success 'Recursive with -r -f' '
	git rm -f -r frotz &&
	! test -f frotz/nitfol &&
	! test -d frotz
'

test_expect_success 'Remove nonexistent file returns nonzero exit status' '
	test_must_fail git rm nonexistent
'

test_expect_success 'Call "rm" from outside the work tree' '
	mkdir repo &&
	(cd repo &&
	 git init &&
	 echo something > somefile &&
	 git add somefile &&
	 git commit -m "add a file" &&
	 (cd .. &&
	  git --git-dir=repo/.git --work-tree=repo rm somefile) &&
	test_must_fail git ls-files --error-unmatch somefile)
'

test_expect_success 'refresh index before checking if it is up-to-date' '

	git reset --hard &&
	test-chmtime -86400 frotz/nitfol &&
	git rm frotz/nitfol &&
	test ! -f frotz/nitfol

'

test_expect_success 'choking "git rm" should not let it die with cruft' '
	git reset -q --hard &&
	H=0000000000000000000000000000000000000000 &&
	i=0 &&
	while test $i -lt 12000
	do
	    echo "100644 $H 0	some-file-$i"
	    i=$(( $i + 1 ))
	done | git update-index --index-info &&
	git rm -n "some-file-*" | :;
	test -f .git/index.lock
	status=$?
	rm -f .git/index.lock
	git reset -q --hard
	test "$status" != 0
'

test_expect_success 'rm removes subdirectories recursively' '
	mkdir -p dir/subdir/subsubdir &&
	echo content >dir/subdir/subsubdir/file &&
	git add dir/subdir/subsubdir/file &&
	git rm -f dir/subdir/subsubdir/file &&
	! test -d dir
'

test_done
