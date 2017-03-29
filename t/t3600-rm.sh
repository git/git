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

if test_have_prereq !MINGW && touch -- 'tab	embedded' 'newline
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

test_expect_success \
    'Pre-check that foo exists and is in index before git rm foo' \
    '[ -f foo ] && git ls-files --error-unmatch foo'

test_expect_success \
    'Test that git rm foo succeeds' \
    'git rm --cached foo'

test_expect_success \
    'Test that git rm --cached foo succeeds if the index matches the file' \
    'echo content >foo &&
     git add foo &&
     git rm --cached foo'

test_expect_success \
    'Test that git rm --cached foo succeeds if the index matches the file' \
    'echo content >foo &&
     git add foo &&
     git commit -m foo &&
     echo "other content" >foo &&
     git rm --cached foo'

test_expect_success \
    'Test that git rm --cached foo fails if the index matches neither the file nor HEAD' '
     echo content >foo &&
     git add foo &&
     git commit -m foo --allow-empty &&
     echo "other content" >foo &&
     git add foo &&
     echo "yet another content" >foo &&
     test_must_fail git rm --cached foo
'

test_expect_success \
    'Test that git rm --cached -f foo works in case where --cached only did not' \
    'echo content >foo &&
     git add foo &&
     git commit -m foo --allow-empty &&
     echo "other content" >foo &&
     git add foo &&
     echo "yet another content" >foo &&
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

test_expect_success SANITY 'Test that "git rm -f" fails if its rm fails' '
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
	echo frotz >test-file &&
	git add test-file &&
	git commit -m "add file for rm test" &&
	git rm test-file >rm-output &&
	test $(grep "^rm " rm-output | wc -l) = 1 &&
	rm -f test-file rm-output &&
	git commit -m "remove file from rm test"
'

test_expect_success '"rm" command suppressed with --quiet' '
	echo frotz >test-file &&
	git add test-file &&
	git commit -m "add file for rm --quiet test" &&
	git rm --quiet test-file >rm-output &&
	test_must_be_empty rm-output &&
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
	test_must_fail git ls-files --error-unmatch foo &&
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
	git add -N intent-to-add &&
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
	echo qfwfq >>frotz/nitfol &&
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
	 echo something >somefile &&
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
	test_when_finished "rm -f .git/index.lock && git reset -q --hard" &&
	i=0 &&
	while test $i -lt 12000
	do
	    echo "100644 1234567890123456789012345678901234567890 0	some-file-$i"
	    i=$(( $i + 1 ))
	done | git update-index --index-info &&
	git rm -n "some-file-*" | : &&
	test_path_is_missing .git/index.lock
'

test_expect_success 'rm removes subdirectories recursively' '
	mkdir -p dir/subdir/subsubdir &&
	echo content >dir/subdir/subsubdir/file &&
	git add dir/subdir/subsubdir/file &&
	git rm -f dir/subdir/subsubdir/file &&
	! test -d dir
'

cat >expect <<EOF
M  .gitmodules
D  submod
EOF

cat >expect.modified <<EOF
 M submod
EOF

cat >expect.modified_inside <<EOF
 m submod
EOF

cat >expect.modified_untracked <<EOF
 ? submod
EOF

cat >expect.cached <<EOF
D  submod
EOF

cat >expect.both_deleted<<EOF
D  .gitmodules
D  submod
EOF

test_expect_success 'rm removes empty submodules from work tree' '
	mkdir submod &&
	git update-index --add --cacheinfo 160000 $(git rev-parse HEAD) submod &&
	git config -f .gitmodules submodule.sub.url ./. &&
	git config -f .gitmodules submodule.sub.path submod &&
	git submodule init &&
	git add .gitmodules &&
	git commit -m "add submodule" &&
	git rm submod &&
	test ! -e submod &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual &&
	test_must_fail git config -f .gitmodules submodule.sub.url &&
	test_must_fail git config -f .gitmodules submodule.sub.path
'

test_expect_success 'rm removes removed submodule from index and .gitmodules' '
	git reset --hard &&
	git submodule update &&
	rm -rf submod &&
	git rm submod &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual &&
	test_must_fail git config -f .gitmodules submodule.sub.url &&
	test_must_fail git config -f .gitmodules submodule.sub.path
'

test_expect_success 'rm removes work tree of unmodified submodules' '
	git reset --hard &&
	git submodule update &&
	git rm submod &&
	test ! -d submod &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual &&
	test_must_fail git config -f .gitmodules submodule.sub.url &&
	test_must_fail git config -f .gitmodules submodule.sub.path
'

test_expect_success 'rm removes a submodule with a trailing /' '
	git reset --hard &&
	git submodule update &&
	git rm submod/ &&
	test ! -d submod &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success 'rm fails when given a file with a trailing /' '
	test_must_fail git rm empty/
'

test_expect_success 'rm succeeds when given a directory with a trailing /' '
	git rm -r frotz/
'

test_expect_success 'rm of a populated submodule with different HEAD fails unless forced' '
	git reset --hard &&
	git submodule update &&
	git -C submod checkout HEAD^ &&
	test_must_fail git rm submod &&
	test -d submod &&
	test -f submod/.git &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.modified actual &&
	git rm -f submod &&
	test ! -d submod &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual &&
	test_must_fail git config -f .gitmodules submodule.sub.url &&
	test_must_fail git config -f .gitmodules submodule.sub.path
'

test_expect_success 'rm --cached leaves work tree of populated submodules and .gitmodules alone' '
	git reset --hard &&
	git submodule update &&
	git rm --cached submod &&
	test -d submod &&
	test -f submod/.git &&
	git status -s -uno >actual &&
	test_cmp expect.cached actual &&
	git config -f .gitmodules submodule.sub.url &&
	git config -f .gitmodules submodule.sub.path
'

test_expect_success 'rm --dry-run does not touch the submodule or .gitmodules' '
	git reset --hard &&
	git submodule update &&
	git rm -n submod &&
	test -f submod/.git &&
	git diff-index --exit-code HEAD
'

test_expect_success 'rm does not complain when no .gitmodules file is found' '
	git reset --hard &&
	git submodule update &&
	git rm .gitmodules &&
	git rm submod >actual 2>actual.err &&
	! test -s actual.err &&
	! test -d submod &&
	! test -f submod/.git &&
	git status -s -uno >actual &&
	test_cmp expect.both_deleted actual
'

test_expect_success 'rm will error out on a modified .gitmodules file unless staged' '
	git reset --hard &&
	git submodule update &&
	git config -f .gitmodules foo.bar true &&
	test_must_fail git rm submod >actual 2>actual.err &&
	test -s actual.err &&
	test -d submod &&
	test -f submod/.git &&
	git diff-files --quiet -- submod &&
	git add .gitmodules &&
	git rm submod >actual 2>actual.err &&
	! test -s actual.err &&
	! test -d submod &&
	! test -f submod/.git &&
	git status -s -uno >actual &&
	test_cmp expect actual
'

test_expect_success 'rm issues a warning when section is not found in .gitmodules' '
	git reset --hard &&
	git submodule update &&
	git config -f .gitmodules --remove-section submodule.sub &&
	git add .gitmodules &&
	echo "warning: Could not find section in .gitmodules where path=submod" >expect.err &&
	git rm submod >actual 2>actual.err &&
	test_i18ncmp expect.err actual.err &&
	! test -d submod &&
	! test -f submod/.git &&
	git status -s -uno >actual &&
	test_cmp expect actual
'

test_expect_success 'rm of a populated submodule with modifications fails unless forced' '
	git reset --hard &&
	git submodule update &&
	echo X >submod/empty &&
	test_must_fail git rm submod &&
	test -d submod &&
	test -f submod/.git &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.modified_inside actual &&
	git rm -f submod &&
	test ! -d submod &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success 'rm of a populated submodule with untracked files fails unless forced' '
	git reset --hard &&
	git submodule update &&
	echo X >submod/untracked &&
	test_must_fail git rm submod &&
	test -d submod &&
	test -f submod/.git &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.modified_untracked actual &&
	git rm -f submod &&
	test ! -d submod &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success 'setup submodule conflict' '
	git reset --hard &&
	git submodule update &&
	git checkout -b branch1 &&
	echo 1 >nitfol &&
	git add nitfol &&
	git commit -m "added nitfol 1" &&
	git checkout -b branch2 master &&
	echo 2 >nitfol &&
	git add nitfol &&
	git commit -m "added nitfol 2" &&
	git checkout -b conflict1 master &&
	git -C submod fetch &&
	git -C submod checkout branch1 &&
	git add submod &&
	git commit -m "submod 1" &&
	git checkout -b conflict2 master &&
	git -C submod checkout branch2 &&
	git add submod &&
	git commit -m "submod 2"
'

cat >expect.conflict <<EOF
UU submod
EOF

test_expect_success 'rm removes work tree of unmodified conflicted submodule' '
	git checkout conflict1 &&
	git reset --hard &&
	git submodule update &&
	test_must_fail git merge conflict2 &&
	git rm submod &&
	test ! -d submod &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success 'rm of a conflicted populated submodule with different HEAD fails unless forced' '
	git checkout conflict1 &&
	git reset --hard &&
	git submodule update &&
	git -C submod checkout HEAD^ &&
	test_must_fail git merge conflict2 &&
	test_must_fail git rm submod &&
	test -d submod &&
	test -f submod/.git &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.conflict actual &&
	git rm -f submod &&
	test ! -d submod &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual &&
	test_must_fail git config -f .gitmodules submodule.sub.url &&
	test_must_fail git config -f .gitmodules submodule.sub.path
'

test_expect_success 'rm of a conflicted populated submodule with modifications fails unless forced' '
	git checkout conflict1 &&
	git reset --hard &&
	git submodule update &&
	echo X >submod/empty &&
	test_must_fail git merge conflict2 &&
	test_must_fail git rm submod &&
	test -d submod &&
	test -f submod/.git &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.conflict actual &&
	git rm -f submod &&
	test ! -d submod &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual &&
	test_must_fail git config -f .gitmodules submodule.sub.url &&
	test_must_fail git config -f .gitmodules submodule.sub.path
'

test_expect_success 'rm of a conflicted populated submodule with untracked files fails unless forced' '
	git checkout conflict1 &&
	git reset --hard &&
	git submodule update &&
	echo X >submod/untracked &&
	test_must_fail git merge conflict2 &&
	test_must_fail git rm submod &&
	test -d submod &&
	test -f submod/.git &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.conflict actual &&
	git rm -f submod &&
	test ! -d submod &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success 'rm of a conflicted populated submodule with a .git directory fails even when forced' '
	git checkout conflict1 &&
	git reset --hard &&
	git submodule update &&
	(cd submod &&
		rm .git &&
		cp -R ../.git/modules/sub .git &&
		GIT_WORK_TREE=. git config --unset core.worktree
	) &&
	test_must_fail git merge conflict2 &&
	test_must_fail git rm submod &&
	test -d submod &&
	test -d submod/.git &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.conflict actual &&
	test_must_fail git rm -f submod &&
	test -d submod &&
	test -d submod/.git &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.conflict actual &&
	git merge --abort &&
	rm -rf submod
'

test_expect_success 'rm of a conflicted unpopulated submodule succeeds' '
	git checkout conflict1 &&
	git reset --hard &&
	test_must_fail git merge conflict2 &&
	git rm submod &&
	test ! -d submod &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success 'rm of a populated submodule with a .git directory migrates git dir' '
	git checkout -f master &&
	git reset --hard &&
	git submodule update &&
	(cd submod &&
		rm .git &&
		cp -R ../.git/modules/sub .git &&
		GIT_WORK_TREE=. git config --unset core.worktree &&
		rm -r ../.git/modules/sub
	) &&
	git rm submod 2>output.err &&
	! test -d submod &&
	! test -d submod/.git &&
	git status -s -uno --ignore-submodules=none >actual &&
	test -s actual &&
	test_i18ngrep Migrating output.err
'

cat >expect.deepmodified <<EOF
 M submod/subsubmod
EOF

test_expect_success 'setup subsubmodule' '
	git reset --hard &&
	git submodule update &&
	(cd submod &&
		git update-index --add --cacheinfo 160000 $(git rev-parse HEAD) subsubmod &&
		git config -f .gitmodules submodule.sub.url ../. &&
		git config -f .gitmodules submodule.sub.path subsubmod &&
		git submodule init &&
		git add .gitmodules &&
		git commit -m "add subsubmodule" &&
		git submodule update subsubmod
	) &&
	git commit -a -m "added deep submodule"
'

test_expect_success 'rm recursively removes work tree of unmodified submodules' '
	git rm submod &&
	test ! -d submod &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success 'rm of a populated nested submodule with different nested HEAD fails unless forced' '
	git reset --hard &&
	git submodule update --recursive &&
	git -C submod/subsubmod checkout HEAD^ &&
	test_must_fail git rm submod &&
	test -d submod &&
	test -f submod/.git &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.modified_inside actual &&
	git rm -f submod &&
	test ! -d submod &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success 'rm of a populated nested submodule with nested modifications fails unless forced' '
	git reset --hard &&
	git submodule update --recursive &&
	echo X >submod/subsubmod/empty &&
	test_must_fail git rm submod &&
	test -d submod &&
	test -f submod/.git &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.modified_inside actual &&
	git rm -f submod &&
	test ! -d submod &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success 'rm of a populated nested submodule with nested untracked files fails unless forced' '
	git reset --hard &&
	git submodule update --recursive &&
	echo X >submod/subsubmod/untracked &&
	test_must_fail git rm submod &&
	test -d submod &&
	test -f submod/.git &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.modified_untracked actual &&
	git rm -f submod &&
	test ! -d submod &&
	git status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success 'rm of a populated nested submodule with a nested .git directory fails even when forced' '
	git reset --hard &&
	git submodule update --recursive &&
	(cd submod/subsubmod &&
		rm .git &&
		mv ../../.git/modules/sub/modules/sub .git &&
		GIT_WORK_TREE=. git config --unset core.worktree
	) &&
	git rm submod 2>output.err &&
	! test -d submod &&
	! test -d submod/subsubmod/.git &&
	git status -s -uno --ignore-submodules=none >actual &&
	test -s actual &&
	test_i18ngrep Migrating output.err
'

test_expect_success 'checking out a commit after submodule removal needs manual updates' '
	git commit -m "submodule removal" submod .gitmodules &&
	git checkout HEAD^ &&
	git submodule update &&
	git checkout -q HEAD^ &&
	git checkout -q master 2>actual &&
	test_i18ngrep "^warning: unable to rmdir submod:" actual &&
	git status -s submod >actual &&
	echo "?? submod/" >expected &&
	test_cmp expected actual &&
	rm -rf submod &&
	git status -s -uno --ignore-submodules=none >actual &&
	! test -s actual
'

test_expect_success 'rm of d/f when d has become a non-directory' '
	rm -rf d &&
	mkdir d &&
	>d/f &&
	git add d &&
	rm -rf d &&
	>d &&
	git rm d/f &&
	test_must_fail git rev-parse --verify :d/f &&
	test_path_is_file d
'

test_expect_success SYMLINKS 'rm of d/f when d has become a dangling symlink' '
	rm -rf d &&
	mkdir d &&
	>d/f &&
	git add d &&
	rm -rf d &&
	ln -s nonexistent d &&
	git rm d/f &&
	test_must_fail git rev-parse --verify :d/f &&
	test -h d &&
	test_path_is_missing d
'

test_expect_success 'rm of file when it has become a directory' '
	rm -rf d &&
	>d &&
	git add d &&
	rm -f d &&
	mkdir d &&
	>d/f &&
	test_must_fail git rm d &&
	git rev-parse --verify :d &&
	test_path_is_file d/f
'

test_expect_success SYMLINKS 'rm across a symlinked leading path (no index)' '
	rm -rf d e &&
	mkdir e &&
	echo content >e/f &&
	ln -s e d &&
	git add -A e d &&
	git commit -m "symlink d to e, e/f exists" &&
	test_must_fail git rm d/f &&
	git rev-parse --verify :d &&
	git rev-parse --verify :e/f &&
	test -h d &&
	test_path_is_file e/f
'

test_expect_failure SYMLINKS 'rm across a symlinked leading path (w/ index)' '
	rm -rf d e &&
	mkdir d &&
	echo content >d/f &&
	git add -A e d &&
	git commit -m "d/f exists" &&
	mv d e &&
	ln -s e d &&
	test_must_fail git rm d/f &&
	git rev-parse --verify :d/f &&
	test -h d &&
	test_path_is_file e/f
'

test_expect_success 'setup for testing rm messages' '
	>bar.txt &&
	>foo.txt &&
	git add bar.txt foo.txt
'

test_expect_success 'rm files with different staged content' '
	cat >expect <<-\EOF &&
	error: the following files have staged content different from both the
	file and the HEAD:
	    bar.txt
	    foo.txt
	(use -f to force removal)
	EOF
	echo content1 >foo.txt &&
	echo content1 >bar.txt &&
	test_must_fail git rm foo.txt bar.txt 2>actual &&
	test_i18ncmp expect actual
'

test_expect_success 'rm files with different staged content without hints' '
	cat >expect <<-\EOF &&
	error: the following files have staged content different from both the
	file and the HEAD:
	    bar.txt
	    foo.txt
	EOF
	echo content2 >foo.txt &&
	echo content2 >bar.txt &&
	test_must_fail git -c advice.rmhints=false rm foo.txt bar.txt 2>actual &&
	test_i18ncmp expect actual
'

test_expect_success 'rm file with local modification' '
	cat >expect <<-\EOF &&
	error: the following file has local modifications:
	    foo.txt
	(use --cached to keep the file, or -f to force removal)
	EOF
	git commit -m "testing rm 3" &&
	echo content3 >foo.txt &&
	test_must_fail git rm foo.txt 2>actual &&
	test_i18ncmp expect actual
'

test_expect_success 'rm file with local modification without hints' '
	cat >expect <<-\EOF &&
	error: the following file has local modifications:
	    bar.txt
	EOF
	echo content4 >bar.txt &&
	test_must_fail git -c advice.rmhints=false rm bar.txt 2>actual &&
	test_i18ncmp expect actual
'

test_expect_success 'rm file with changes in the index' '
	cat >expect <<-\EOF &&
	error: the following file has changes staged in the index:
	    foo.txt
	(use --cached to keep the file, or -f to force removal)
	EOF
	git reset --hard &&
	echo content5 >foo.txt &&
	git add foo.txt &&
	test_must_fail git rm foo.txt 2>actual &&
	test_i18ncmp expect actual
'

test_expect_success 'rm file with changes in the index without hints' '
	cat >expect <<-\EOF &&
	error: the following file has changes staged in the index:
	    foo.txt
	EOF
	test_must_fail git -c advice.rmhints=false rm foo.txt 2>actual &&
	test_i18ncmp expect actual
'

test_expect_success 'rm files with two different errors' '
	cat >expect <<-\EOF &&
	error: the following file has staged content different from both the
	file and the HEAD:
	    foo1.txt
	(use -f to force removal)
	error: the following file has changes staged in the index:
	    bar1.txt
	(use --cached to keep the file, or -f to force removal)
	EOF
	echo content >foo1.txt &&
	git add foo1.txt &&
	echo content6 >foo1.txt &&
	echo content6 >bar1.txt &&
	git add bar1.txt &&
	test_must_fail git rm bar1.txt foo1.txt 2>actual &&
	test_i18ncmp expect actual
'

test_expect_success 'rm empty string should invoke warning' '
	git rm -rf "" 2>output &&
	test_i18ngrep "warning: empty strings" output
'

test_done
