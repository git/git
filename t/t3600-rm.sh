#!/bin/sh
#
# Copyright (c) 2006 Carl D. Worth
#

test_description='Test of the various options to but rm.'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

# Setup some files to be removed, some with funny characters
test_expect_success 'Initialize test directory' '
	touch -- foo bar baz "space embedded" -q &&
	but add -- foo bar baz "space embedded" -q &&
	but cummit -m "add normal files"
'

if test_have_prereq !FUNNYNAMES
then
	say 'Your filesystem does not allow tabs in filenames.'
fi

test_expect_success FUNNYNAMES 'add files with funny names' '
	touch -- "tab	embedded" "newline${LF}embedded" &&
	but add -- "tab	embedded" "newline${LF}embedded" &&
	but cummit -m "add files with tabs and newlines"
'

test_expect_success 'Pre-check that foo exists and is in index before but rm foo' '
	test_path_is_file foo &&
	but ls-files --error-unmatch foo
'

test_expect_success 'Test that but rm foo succeeds' '
	but rm --cached foo
'

test_expect_success 'Test that but rm --cached foo succeeds if the index matches the file' '
	echo content >foo &&
	but add foo &&
	but rm --cached foo
'

test_expect_success 'Test that but rm --cached foo succeeds if the index matches the file' '
	echo content >foo &&
	but add foo &&
	but cummit -m foo &&
	echo "other content" >foo &&
	but rm --cached foo
'

test_expect_success 'Test that but rm --cached foo fails if the index matches neither the file nor HEAD' '
	echo content >foo &&
	but add foo &&
	but cummit -m foo --allow-empty &&
	echo "other content" >foo &&
	but add foo &&
	echo "yet another content" >foo &&
	test_must_fail but rm --cached foo
'

test_expect_success 'Test that but rm --cached -f foo works in case where --cached only did not' '
	echo content >foo &&
	but add foo &&
	but cummit -m foo --allow-empty &&
	echo "other content" >foo &&
	but add foo &&
	echo "yet another content" >foo &&
	but rm --cached -f foo
'

test_expect_success 'Post-check that foo exists but is not in index after but rm foo' '
	test_path_is_file foo &&
	test_must_fail but ls-files --error-unmatch foo
'

test_expect_success 'Pre-check that bar exists and is in index before "but rm bar"' '
	test_path_is_file bar &&
	but ls-files --error-unmatch bar
'

test_expect_success 'Test that "but rm bar" succeeds' '
	but rm bar
'

test_expect_success 'Post-check that bar does not exist and is not in index after "but rm -f bar"' '
	test_path_is_missing bar &&
	test_must_fail but ls-files --error-unmatch bar
'

test_expect_success 'Test that "but rm -- -q" succeeds (remove a file that looks like an option)' '
	but rm -- -q
'

test_expect_success FUNNYNAMES 'Test that "but rm -f" succeeds with embedded space, tab, or newline characters.' '
	but rm -f "space embedded" "tab	embedded" "newline${LF}embedded"
'

test_expect_success SANITY 'Test that "but rm -f" fails if its rm fails' '
	test_when_finished "chmod 775 ." &&
	chmod a-w . &&
	test_must_fail but rm -f baz
'

test_expect_success 'When the rm in "but rm -f" fails, it should not remove the file from the index' '
	but ls-files --error-unmatch baz
'

test_expect_success 'Remove nonexistent file with --ignore-unmatch' '
	but rm --ignore-unmatch nonexistent
'

test_expect_success '"rm" command printed' '
	echo frotz >test-file &&
	but add test-file &&
	but cummit -m "add file for rm test" &&
	but rm test-file >rm-output.raw &&
	grep "^rm " rm-output.raw >rm-output &&
	test_line_count = 1 rm-output &&
	rm -f test-file rm-output.raw rm-output &&
	but cummit -m "remove file from rm test"
'

test_expect_success '"rm" command suppressed with --quiet' '
	echo frotz >test-file &&
	but add test-file &&
	but cummit -m "add file for rm --quiet test" &&
	but rm --quiet test-file >rm-output &&
	test_must_be_empty rm-output &&
	rm -f test-file rm-output &&
	but cummit -m "remove file from rm --quiet test"
'

# Now, failure cases.
test_expect_success 'Re-add foo and baz' '
	but add foo baz &&
	but ls-files --error-unmatch foo baz
'

test_expect_success 'Modify foo -- rm should refuse' '
	echo >>foo &&
	test_must_fail but rm foo baz &&
	test_path_is_file foo &&
	test_path_is_file baz &&
	but ls-files --error-unmatch foo baz
'

test_expect_success 'Modified foo -- rm -f should work' '
	but rm -f foo baz &&
	test_path_is_missing foo &&
	test_path_is_missing baz &&
	test_must_fail but ls-files --error-unmatch foo &&
	test_must_fail but ls-files --error-unmatch bar
'

test_expect_success 'Re-add foo and baz for HEAD tests' '
	echo frotz >foo &&
	but checkout HEAD -- baz &&
	but add foo baz &&
	but ls-files --error-unmatch foo baz
'

test_expect_success 'foo is different in index from HEAD -- rm should refuse' '
	test_must_fail but rm foo baz &&
	test_path_is_file foo &&
	test_path_is_file baz &&
	but ls-files --error-unmatch foo baz
'

test_expect_success 'but with -f it should work.' '
	but rm -f foo baz &&
	test_path_is_missing foo &&
	test_path_is_missing baz &&
	test_must_fail but ls-files --error-unmatch foo &&
	test_must_fail but ls-files --error-unmatch baz
'

test_expect_success 'refuse to remove cached empty file with modifications' '
	>empty &&
	but add empty &&
	echo content >empty &&
	test_must_fail but rm --cached empty
'

test_expect_success 'remove intent-to-add file without --force' '
	echo content >intent-to-add &&
	but add -N intent-to-add &&
	but rm --cached intent-to-add
'

test_expect_success 'Recursive test setup' '
	mkdir -p frotz &&
	echo qfwfq >frotz/nitfol &&
	but add frotz &&
	but cummit -m "subdir test"
'

test_expect_success 'Recursive without -r fails' '
	test_must_fail but rm frotz &&
	test_path_is_dir frotz &&
	test_path_is_file frotz/nitfol
'

test_expect_success 'Recursive with -r but dirty' '
	echo qfwfq >>frotz/nitfol &&
	test_must_fail but rm -r frotz &&
	test_path_is_dir frotz &&
	test_path_is_file frotz/nitfol
'

test_expect_success 'Recursive with -r -f' '
	but rm -f -r frotz &&
	test_path_is_missing frotz/nitfol &&
	test_path_is_missing frotz
'

test_expect_success 'Remove nonexistent file returns nonzero exit status' '
	test_must_fail but rm nonexistent
'

test_expect_success 'Call "rm" from outside the work tree' '
	mkdir repo &&
	(
		cd repo &&
		but init &&
		echo something >somefile &&
		but add somefile &&
		but cummit -m "add a file" &&
		(
			cd .. &&
			but --but-dir=repo/.but --work-tree=repo rm somefile
		) &&
		test_must_fail but ls-files --error-unmatch somefile
	)
'

test_expect_success 'refresh index before checking if it is up-to-date' '
	but reset --hard &&
	test-tool chmtime -86400 frotz/nitfol &&
	but rm frotz/nitfol &&
	test_path_is_missing frotz/nitfol
'

choke_but_rm_setup() {
	but reset -q --hard &&
	test_when_finished "rm -f .but/index.lock && but reset -q --hard" &&
	i=0 &&
	hash=$(test_oid deadbeef) &&
	while test $i -lt 12000
	do
		echo "100644 $hash 0	some-file-$i"
		i=$(( $i + 1 ))
	done | but update-index --index-info
}

test_expect_success 'choking "but rm" should not let it die with cruft (induce SIGPIPE)' '
	choke_but_rm_setup &&
	# but command is intentionally placed upstream of pipe to induce SIGPIPE
	but rm -n "some-file-*" | : &&
	test_path_is_missing .but/index.lock
'


test_expect_success !MINGW 'choking "but rm" should not let it die with cruft (induce and check SIGPIPE)' '
	choke_but_rm_setup &&
	OUT=$( ((trap "" PIPE && but rm -n "some-file-*"; echo $? 1>&3) | :) 3>&1 ) &&
	test_match_signal 13 "$OUT" &&
	test_path_is_missing .but/index.lock
'

test_expect_success 'Resolving by removal is not a warning-worthy event' '
	but reset -q --hard &&
	test_when_finished "rm -f .but/index.lock msg && but reset -q --hard" &&
	blob=$(echo blob | but hash-object -w --stdin) &&
	printf "100644 $blob %d\tblob\n" 1 2 3 | but update-index --index-info &&
	but rm blob >msg 2>&1 &&
	test_i18ngrep ! "needs merge" msg &&
	test_must_fail but ls-files -s --error-unmatch blob
'

test_expect_success 'rm removes subdirectories recursively' '
	mkdir -p dir/subdir/subsubdir &&
	echo content >dir/subdir/subsubdir/file &&
	but add dir/subdir/subsubdir/file &&
	but rm -f dir/subdir/subsubdir/file &&
	test_path_is_missing dir
'

cat >expect <<EOF
M  .butmodules
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
D  .butmodules
D  submod
EOF

test_expect_success 'rm removes empty submodules from work tree' '
	mkdir submod &&
	hash=$(but rev-parse HEAD) &&
	but update-index --add --cacheinfo 160000 "$hash" submod &&
	but config -f .butmodules submodule.sub.url ./. &&
	but config -f .butmodules submodule.sub.path submod &&
	but submodule init &&
	but add .butmodules &&
	but cummit -m "add submodule" &&
	but rm submod &&
	test_path_is_missing submod &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual &&
	test_must_fail but config -f .butmodules submodule.sub.url &&
	test_must_fail but config -f .butmodules submodule.sub.path
'

test_expect_success 'rm removes removed submodule from index and .butmodules' '
	but reset --hard &&
	but submodule update &&
	rm -rf submod &&
	but rm submod &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual &&
	test_must_fail but config -f .butmodules submodule.sub.url &&
	test_must_fail but config -f .butmodules submodule.sub.path
'

test_expect_success 'rm removes work tree of unmodified submodules' '
	but reset --hard &&
	but submodule update &&
	but rm submod &&
	test_path_is_missing submod &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual &&
	test_must_fail but config -f .butmodules submodule.sub.url &&
	test_must_fail but config -f .butmodules submodule.sub.path
'

test_expect_success 'rm removes a submodule with a trailing /' '
	but reset --hard &&
	but submodule update &&
	but rm submod/ &&
	test_path_is_missing submod &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success 'rm fails when given a file with a trailing /' '
	test_must_fail but rm empty/
'

test_expect_success 'rm succeeds when given a directory with a trailing /' '
	but rm -r frotz/
'

test_expect_success 'rm of a populated submodule with different HEAD fails unless forced' '
	but reset --hard &&
	but submodule update &&
	but -C submod checkout HEAD^ &&
	test_must_fail but rm submod &&
	test_path_is_dir submod &&
	test_path_is_file submod/.but &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.modified actual &&
	but rm -f submod &&
	test_path_is_missing submod &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual &&
	test_must_fail but config -f .butmodules submodule.sub.url &&
	test_must_fail but config -f .butmodules submodule.sub.path
'

test_expect_success 'rm --cached leaves work tree of populated submodules and .butmodules alone' '
	but reset --hard &&
	but submodule update &&
	but rm --cached submod &&
	test_path_is_dir submod &&
	test_path_is_file submod/.but &&
	but status -s -uno >actual &&
	test_cmp expect.cached actual &&
	but config -f .butmodules submodule.sub.url &&
	but config -f .butmodules submodule.sub.path
'

test_expect_success 'rm --dry-run does not touch the submodule or .butmodules' '
	but reset --hard &&
	but submodule update &&
	but rm -n submod &&
	test_path_is_file submod/.but &&
	but diff-index --exit-code HEAD
'

test_expect_success 'rm does not complain when no .butmodules file is found' '
	but reset --hard &&
	but submodule update &&
	but rm .butmodules &&
	but rm submod >actual 2>actual.err &&
	test_must_be_empty actual.err &&
	test_path_is_missing submod &&
	test_path_is_missing submod/.but &&
	but status -s -uno >actual &&
	test_cmp expect.both_deleted actual
'

test_expect_success 'rm will error out on a modified .butmodules file unless staged' '
	but reset --hard &&
	but submodule update &&
	but config -f .butmodules foo.bar true &&
	test_must_fail but rm submod >actual 2>actual.err &&
	test_file_not_empty actual.err &&
	test_path_is_dir submod &&
	test_path_is_file submod/.but &&
	but diff-files --quiet -- submod &&
	but add .butmodules &&
	but rm submod >actual 2>actual.err &&
	test_must_be_empty actual.err &&
	test_path_is_missing submod &&
	test_path_is_missing submod/.but &&
	but status -s -uno >actual &&
	test_cmp expect actual
'
test_expect_success 'rm will not error out on .butmodules file with zero stat data' '
	but reset --hard &&
	but submodule update &&
	but read-tree HEAD &&
	but rm submod &&
	test_path_is_missing submod
'

test_expect_success 'rm issues a warning when section is not found in .butmodules' '
	but reset --hard &&
	but submodule update &&
	but config -f .butmodules --remove-section submodule.sub &&
	but add .butmodules &&
	echo "warning: Could not find section in .butmodules where path=submod" >expect.err &&
	but rm submod >actual 2>actual.err &&
	test_cmp expect.err actual.err &&
	test_path_is_missing submod &&
	test_path_is_missing submod/.but &&
	but status -s -uno >actual &&
	test_cmp expect actual
'

test_expect_success 'rm of a populated submodule with modifications fails unless forced' '
	but reset --hard &&
	but submodule update &&
	echo X >submod/empty &&
	test_must_fail but rm submod &&
	test_path_is_dir submod &&
	test_path_is_file submod/.but &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.modified_inside actual &&
	but rm -f submod &&
	test_path_is_missing submod &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success 'rm of a populated submodule with untracked files fails unless forced' '
	but reset --hard &&
	but submodule update &&
	echo X >submod/untracked &&
	test_must_fail but rm submod &&
	test_path_is_dir submod &&
	test_path_is_file submod/.but &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.modified_untracked actual &&
	but rm -f submod &&
	test_path_is_missing submod &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success 'setup submodule conflict' '
	but reset --hard &&
	but submodule update &&
	but checkout -b branch1 &&
	echo 1 >nitfol &&
	but add nitfol &&
	but cummit -m "added nitfol 1" &&
	but checkout -b branch2 main &&
	echo 2 >nitfol &&
	but add nitfol &&
	but cummit -m "added nitfol 2" &&
	but checkout -b conflict1 main &&
	but -C submod fetch &&
	but -C submod checkout branch1 &&
	but add submod &&
	but cummit -m "submod 1" &&
	but checkout -b conflict2 main &&
	but -C submod checkout branch2 &&
	but add submod &&
	but cummit -m "submod 2"
'

cat >expect.conflict <<EOF
UU submod
EOF

test_expect_success 'rm removes work tree of unmodified conflicted submodule' '
	but checkout conflict1 &&
	but reset --hard &&
	but submodule update &&
	test_must_fail but merge conflict2 &&
	but rm submod &&
	test_path_is_missing submod &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success 'rm of a conflicted populated submodule with different HEAD fails unless forced' '
	but checkout conflict1 &&
	but reset --hard &&
	but submodule update &&
	but -C submod checkout HEAD^ &&
	test_must_fail but merge conflict2 &&
	test_must_fail but rm submod &&
	test_path_is_dir submod &&
	test_path_is_file submod/.but &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.conflict actual &&
	but rm -f submod &&
	test_path_is_missing submod &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual &&
	test_must_fail but config -f .butmodules submodule.sub.url &&
	test_must_fail but config -f .butmodules submodule.sub.path
'

test_expect_success 'rm of a conflicted populated submodule with modifications fails unless forced' '
	but checkout conflict1 &&
	but reset --hard &&
	but submodule update &&
	echo X >submod/empty &&
	test_must_fail but merge conflict2 &&
	test_must_fail but rm submod &&
	test_path_is_dir submod &&
	test_path_is_file submod/.but &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.conflict actual &&
	but rm -f submod &&
	test_path_is_missing submod &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual &&
	test_must_fail but config -f .butmodules submodule.sub.url &&
	test_must_fail but config -f .butmodules submodule.sub.path
'

test_expect_success 'rm of a conflicted populated submodule with untracked files fails unless forced' '
	but checkout conflict1 &&
	but reset --hard &&
	but submodule update &&
	echo X >submod/untracked &&
	test_must_fail but merge conflict2 &&
	test_must_fail but rm submod &&
	test_path_is_dir submod &&
	test_path_is_file submod/.but &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.conflict actual &&
	but rm -f submod &&
	test_path_is_missing submod &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success 'rm of a conflicted populated submodule with a .but directory fails even when forced' '
	but checkout conflict1 &&
	but reset --hard &&
	but submodule update &&
	(
		cd submod &&
		rm .but &&
		cp -R ../.but/modules/sub .but &&
		GIT_WORK_TREE=. but config --unset core.worktree
	) &&
	test_must_fail but merge conflict2 &&
	test_must_fail but rm submod &&
	test_path_is_dir submod &&
	test_path_is_dir submod/.but &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.conflict actual &&
	test_must_fail but rm -f submod &&
	test_path_is_dir submod &&
	test_path_is_dir submod/.but &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.conflict actual &&
	but merge --abort &&
	rm -rf submod
'

test_expect_success 'rm of a conflicted unpopulated submodule succeeds' '
	but checkout conflict1 &&
	but reset --hard &&
	test_must_fail but merge conflict2 &&
	but rm submod &&
	test_path_is_missing submod &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success 'rm of a populated submodule with a .but directory migrates but dir' '
	but checkout -f main &&
	but reset --hard &&
	but submodule update &&
	(
		cd submod &&
		rm .but &&
		cp -R ../.but/modules/sub .but &&
		GIT_WORK_TREE=. but config --unset core.worktree &&
		rm -r ../.but/modules/sub
	) &&
	but rm submod 2>output.err &&
	test_path_is_missing submod &&
	test_path_is_missing submod/.but &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_file_not_empty actual &&
	test_i18ngrep Migrating output.err
'

cat >expect.deepmodified <<EOF
 M submod/subsubmod
EOF

test_expect_success 'setup subsubmodule' '
	but reset --hard &&
	but submodule update &&
	(
		cd submod &&
		hash=$(but rev-parse HEAD) &&
		but update-index --add --cacheinfo 160000 "$hash" subsubmod &&
		but config -f .butmodules submodule.sub.url ../. &&
		but config -f .butmodules submodule.sub.path subsubmod &&
		but submodule init &&
		but add .butmodules &&
		but cummit -m "add subsubmodule" &&
		but submodule update subsubmod
	) &&
	but cummit -a -m "added deep submodule"
'

test_expect_success 'rm recursively removes work tree of unmodified submodules' '
	but rm submod &&
	test_path_is_missing submod &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success 'rm of a populated nested submodule with different nested HEAD fails unless forced' '
	but reset --hard &&
	but submodule update --recursive &&
	but -C submod/subsubmod checkout HEAD^ &&
	test_must_fail but rm submod &&
	test_path_is_dir submod &&
	test_path_is_file submod/.but &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.modified_inside actual &&
	but rm -f submod &&
	test_path_is_missing submod &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success 'rm of a populated nested submodule with nested modifications fails unless forced' '
	but reset --hard &&
	but submodule update --recursive &&
	echo X >submod/subsubmod/empty &&
	test_must_fail but rm submod &&
	test_path_is_dir submod &&
	test_path_is_file submod/.but &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.modified_inside actual &&
	but rm -f submod &&
	test_path_is_missing submod &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success 'rm of a populated nested submodule with nested untracked files fails unless forced' '
	but reset --hard &&
	but submodule update --recursive &&
	echo X >submod/subsubmod/untracked &&
	test_must_fail but rm submod &&
	test_path_is_dir submod &&
	test_path_is_file submod/.but &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect.modified_untracked actual &&
	but rm -f submod &&
	test_path_is_missing submod &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_cmp expect actual
'

test_expect_success "rm absorbs submodule's nested .but directory" '
	but reset --hard &&
	but submodule update --recursive &&
	(
		cd submod/subsubmod &&
		rm .but &&
		mv ../../.but/modules/sub/modules/sub .but &&
		GIT_WORK_TREE=. but config --unset core.worktree
	) &&
	but rm submod 2>output.err &&
	test_path_is_missing submod &&
	test_path_is_missing submod/subsubmod/.but &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_file_not_empty actual &&
	test_i18ngrep Migrating output.err
'

test_expect_success 'checking out a cummit after submodule removal needs manual updates' '
	but cummit -m "submodule removal" submod .butmodules &&
	but checkout HEAD^ &&
	but submodule update &&
	but checkout -q HEAD^ &&
	but checkout -q main 2>actual &&
	test_i18ngrep "^warning: unable to rmdir '\''submod'\'':" actual &&
	but status -s submod >actual &&
	echo "?? submod/" >expected &&
	test_cmp expected actual &&
	rm -rf submod &&
	but status -s -uno --ignore-submodules=none >actual &&
	test_must_be_empty actual
'

test_expect_success 'rm of d/f when d has become a non-directory' '
	rm -rf d &&
	mkdir d &&
	>d/f &&
	but add d &&
	rm -rf d &&
	>d &&
	but rm d/f &&
	test_must_fail but rev-parse --verify :d/f &&
	test_path_is_file d
'

test_expect_success SYMLINKS 'rm of d/f when d has become a dangling symlink' '
	rm -rf d &&
	mkdir d &&
	>d/f &&
	but add d &&
	rm -rf d &&
	ln -s nonexistent d &&
	but rm d/f &&
	test_must_fail but rev-parse --verify :d/f &&
	test -h d &&
	test_path_is_missing d
'

test_expect_success 'rm of file when it has become a directory' '
	rm -rf d &&
	>d &&
	but add d &&
	rm -f d &&
	mkdir d &&
	>d/f &&
	test_must_fail but rm d &&
	but rev-parse --verify :d &&
	test_path_is_file d/f
'

test_expect_success SYMLINKS 'rm across a symlinked leading path (no index)' '
	rm -rf d e &&
	mkdir e &&
	echo content >e/f &&
	ln -s e d &&
	but add -A e d &&
	but cummit -m "symlink d to e, e/f exists" &&
	test_must_fail but rm d/f &&
	but rev-parse --verify :d &&
	but rev-parse --verify :e/f &&
	test -h d &&
	test_path_is_file e/f
'

test_expect_failure SYMLINKS 'rm across a symlinked leading path (w/ index)' '
	rm -rf d e &&
	mkdir d &&
	echo content >d/f &&
	but add -A e d &&
	but cummit -m "d/f exists" &&
	mv d e &&
	ln -s e d &&
	test_must_fail but rm d/f &&
	but rev-parse --verify :d/f &&
	test -h d &&
	test_path_is_file e/f
'

test_expect_success 'setup for testing rm messages' '
	>bar.txt &&
	>foo.txt &&
	but add bar.txt foo.txt
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
	test_must_fail but rm foo.txt bar.txt 2>actual &&
	test_cmp expect actual
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
	test_must_fail but -c advice.rmhints=false rm foo.txt bar.txt 2>actual &&
	test_cmp expect actual
'

test_expect_success 'rm file with local modification' '
	cat >expect <<-\EOF &&
	error: the following file has local modifications:
	    foo.txt
	(use --cached to keep the file, or -f to force removal)
	EOF
	but cummit -m "testing rm 3" &&
	echo content3 >foo.txt &&
	test_must_fail but rm foo.txt 2>actual &&
	test_cmp expect actual
'

test_expect_success 'rm file with local modification without hints' '
	cat >expect <<-\EOF &&
	error: the following file has local modifications:
	    bar.txt
	EOF
	echo content4 >bar.txt &&
	test_must_fail but -c advice.rmhints=false rm bar.txt 2>actual &&
	test_cmp expect actual
'

test_expect_success 'rm file with changes in the index' '
	cat >expect <<-\EOF &&
	error: the following file has changes staged in the index:
	    foo.txt
	(use --cached to keep the file, or -f to force removal)
	EOF
	but reset --hard &&
	echo content5 >foo.txt &&
	but add foo.txt &&
	test_must_fail but rm foo.txt 2>actual &&
	test_cmp expect actual
'

test_expect_success 'rm file with changes in the index without hints' '
	cat >expect <<-\EOF &&
	error: the following file has changes staged in the index:
	    foo.txt
	EOF
	test_must_fail but -c advice.rmhints=false rm foo.txt 2>actual &&
	test_cmp expect actual
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
	but add foo1.txt &&
	echo content6 >foo1.txt &&
	echo content6 >bar1.txt &&
	but add bar1.txt &&
	test_must_fail but rm bar1.txt foo1.txt 2>actual &&
	test_cmp expect actual
'

test_expect_success 'rm empty string should fail' '
	test_must_fail but rm -rf ""
'

test_done
