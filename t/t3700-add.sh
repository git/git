#!/bin/sh
#
# Copyright (c) 2006 Carl D. Worth
#

test_description='Test of git add, including the -- option.'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

. $TEST_DIRECTORY/lib-unique-files.sh

# Test the file mode "$1" of the file "$2" in the index.
test_mode_in_index () {
	case "$(git ls-files -s "$2")" in
	"$1 "*"	$2")
		echo pass
		;;
	*)
		echo fail
		git ls-files -s "$2"
		return 1
		;;
	esac
}

test_expect_success \
    'Test of git add' \
    'touch foo && git add foo'

test_expect_success \
    'Post-check that foo is in the index' \
    'git ls-files foo | grep foo'

test_expect_success \
    'Test that "git add -- -q" works' \
    'touch -- -q && git add -- -q'

test_expect_success 'git add: core.fsyncobjectfiles=batch' "
	test_create_unique_files 2 4 fsync-files &&
	git -c core.fsyncobjectfiles=batch add -- ./fsync-files/ &&
	rm -f fsynced_files &&
	git ls-files --stage fsync-files/ > fsynced_files &&
	test_line_count = 8 fsynced_files &&
	awk -- '{print \$2}' fsynced_files | xargs -n1 git cat-file -e
"

test_expect_success 'git update-index: core.fsyncobjectfiles=batch' "
	test_create_unique_files 2 4 fsync-files2 &&
	find fsync-files2 ! -type d -print | xargs git -c core.fsyncobjectfiles=batch update-index --add -- &&
	rm -f fsynced_files2 &&
	git ls-files --stage fsync-files2/ > fsynced_files2 &&
	test_line_count = 8 fsynced_files2 &&
	awk -- '{print \$2}' fsynced_files2 | xargs -n1 git cat-file -e
"

test_expect_success \
	'git add: Test that executable bit is not used if core.filemode=0' \
	'git config core.filemode 0 &&
	 echo foo >xfoo1 &&
	 chmod 755 xfoo1 &&
	 git add xfoo1 &&
	 test_mode_in_index 100644 xfoo1'

test_expect_success 'git add: filemode=0 should not get confused by symlink' '
	rm -f xfoo1 &&
	test_ln_s_add foo xfoo1 &&
	test_mode_in_index 120000 xfoo1
'

test_expect_success \
	'git update-index --add: Test that executable bit is not used...' \
	'git config core.filemode 0 &&
	 echo foo >xfoo2 &&
	 chmod 755 xfoo2 &&
	 git update-index --add xfoo2 &&
	 test_mode_in_index 100644 xfoo2'

test_expect_success 'git add: filemode=0 should not get confused by symlink' '
	rm -f xfoo2 &&
	test_ln_s_add foo xfoo2 &&
	test_mode_in_index 120000 xfoo2
'

test_expect_success \
	'git update-index --add: Test that executable bit is not used...' \
	'git config core.filemode 0 &&
	 test_ln_s_add xfoo2 xfoo3 &&	# runs git update-index --add
	 test_mode_in_index 120000 xfoo3'

test_expect_success '.gitignore test setup' '
	echo "*.ig" >.gitignore &&
	mkdir c.if d.ig &&
	>a.ig && >b.if &&
	>c.if/c.if && >c.if/c.ig &&
	>d.ig/d.if && >d.ig/d.ig
'

test_expect_success '.gitignore is honored' '
	git add . &&
	! (git ls-files | grep "\\.ig")
'

test_expect_success 'error out when attempting to add ignored ones without -f' '
	test_must_fail git add a.?? &&
	! (git ls-files | grep "\\.ig")
'

test_expect_success 'error out when attempting to add ignored ones without -f' '
	test_must_fail git add d.?? &&
	! (git ls-files | grep "\\.ig")
'

test_expect_success 'error out when attempting to add ignored ones but add others' '
	touch a.if &&
	test_must_fail git add a.?? &&
	! (git ls-files | grep "\\.ig") &&
	(git ls-files | grep a.if)
'

test_expect_success 'add ignored ones with -f' '
	git add -f a.?? &&
	git ls-files --error-unmatch a.ig
'

test_expect_success 'add ignored ones with -f' '
	git add -f d.??/* &&
	git ls-files --error-unmatch d.ig/d.if d.ig/d.ig
'

test_expect_success 'add ignored ones with -f' '
	rm -f .git/index &&
	git add -f d.?? &&
	git ls-files --error-unmatch d.ig/d.if d.ig/d.ig
'

test_expect_success '.gitignore with subdirectory' '

	rm -f .git/index &&
	mkdir -p sub/dir &&
	echo "!dir/a.*" >sub/.gitignore &&
	>sub/a.ig &&
	>sub/dir/a.ig &&
	git add sub/dir &&
	git ls-files --error-unmatch sub/dir/a.ig &&
	rm -f .git/index &&
	(
		cd sub/dir &&
		git add .
	) &&
	git ls-files --error-unmatch sub/dir/a.ig
'

mkdir 1 1/2 1/3
touch 1/2/a 1/3/b 1/2/c
test_expect_success 'check correct prefix detection' '
	rm -f .git/index &&
	git add 1/2/a 1/3/b 1/2/c
'

test_expect_success 'git add with filemode=0, symlinks=0, and unmerged entries' '
	for s in 1 2 3
	do
		echo $s > stage$s &&
		echo "100755 $(git hash-object -w stage$s) $s	file" &&
		echo "120000 $(printf $s | git hash-object -w -t blob --stdin) $s	symlink" || return 1
	done | git update-index --index-info &&
	git config core.filemode 0 &&
	git config core.symlinks 0 &&
	echo new > file &&
	echo new > symlink &&
	git add file symlink &&
	git ls-files --stage | grep "^100755 .* 0	file$" &&
	git ls-files --stage | grep "^120000 .* 0	symlink$"
'

test_expect_success 'git add with filemode=0, symlinks=0 prefers stage 2 over stage 1' '
	git rm --cached -f file symlink &&
	(
		echo "100644 $(git hash-object -w stage1) 1	file" &&
		echo "100755 $(git hash-object -w stage2) 2	file" &&
		echo "100644 $(printf 1 | git hash-object -w -t blob --stdin) 1	symlink" &&
		echo "120000 $(printf 2 | git hash-object -w -t blob --stdin) 2	symlink"
	) | git update-index --index-info &&
	git config core.filemode 0 &&
	git config core.symlinks 0 &&
	echo new > file &&
	echo new > symlink &&
	git add file symlink &&
	git ls-files --stage | grep "^100755 .* 0	file$" &&
	git ls-files --stage | grep "^120000 .* 0	symlink$"
'

test_expect_success 'git add --refresh' '
	>foo && git add foo && git commit -a -m "commit all" &&
	test -z "$(git diff-index HEAD -- foo)" &&
	git read-tree HEAD &&
	case "$(git diff-index HEAD -- foo)" in
	:100644" "*"M	foo") echo pass;;
	*) echo fail; false;;
	esac &&
	git add --refresh -- foo &&
	test -z "$(git diff-index HEAD -- foo)"
'

test_expect_success 'git add --refresh with pathspec' '
	git reset --hard &&
	echo >foo && echo >bar && echo >baz &&
	git add foo bar baz && H=$(git rev-parse :foo) && git rm -f foo &&
	echo "100644 $H 3	foo" | git update-index --index-info &&
	test-tool chmtime -60 bar baz &&
	git add --refresh bar >actual &&
	test_must_be_empty actual &&

	git diff-files --name-only >actual &&
	! grep bar actual &&
	grep baz actual
'

test_expect_success 'git add --refresh correctly reports no match error' "
	echo \"fatal: pathspec ':(icase)nonexistent' did not match any files\" >expect &&
	test_must_fail git add --refresh ':(icase)nonexistent' 2>actual &&
	test_cmp expect actual
"

test_expect_success POSIXPERM,SANITY 'git add should fail atomically upon an unreadable file' '
	git reset --hard &&
	date >foo1 &&
	date >foo2 &&
	chmod 0 foo2 &&
	test_must_fail git add --verbose . &&
	! ( git ls-files foo1 | grep foo1 )
'

rm -f foo2

test_expect_success POSIXPERM,SANITY 'git add --ignore-errors' '
	git reset --hard &&
	date >foo1 &&
	date >foo2 &&
	chmod 0 foo2 &&
	test_must_fail git add --verbose --ignore-errors . &&
	git ls-files foo1 | grep foo1
'

rm -f foo2

test_expect_success POSIXPERM,SANITY 'git add (add.ignore-errors)' '
	git config add.ignore-errors 1 &&
	git reset --hard &&
	date >foo1 &&
	date >foo2 &&
	chmod 0 foo2 &&
	test_must_fail git add --verbose . &&
	git ls-files foo1 | grep foo1
'
rm -f foo2

test_expect_success POSIXPERM,SANITY 'git add (add.ignore-errors = false)' '
	git config add.ignore-errors 0 &&
	git reset --hard &&
	date >foo1 &&
	date >foo2 &&
	chmod 0 foo2 &&
	test_must_fail git add --verbose . &&
	! ( git ls-files foo1 | grep foo1 )
'
rm -f foo2

test_expect_success POSIXPERM,SANITY '--no-ignore-errors overrides config' '
       git config add.ignore-errors 1 &&
       git reset --hard &&
       date >foo1 &&
       date >foo2 &&
       chmod 0 foo2 &&
       test_must_fail git add --verbose --no-ignore-errors . &&
       ! ( git ls-files foo1 | grep foo1 ) &&
       git config add.ignore-errors 0
'
rm -f foo2

test_expect_success BSLASHPSPEC "git add 'fo\\[ou\\]bar' ignores foobar" '
	git reset --hard &&
	touch fo\[ou\]bar foobar &&
	git add '\''fo\[ou\]bar'\'' &&
	git ls-files fo\[ou\]bar | fgrep fo\[ou\]bar &&
	! ( git ls-files foobar | grep foobar )
'

test_expect_success 'git add to resolve conflicts on otherwise ignored path' '
	git reset --hard &&
	H=$(git rev-parse :1/2/a) &&
	(
		echo "100644 $H 1	track-this" &&
		echo "100644 $H 3	track-this"
	) | git update-index --index-info &&
	echo track-this >>.gitignore &&
	echo resolved >track-this &&
	git add track-this
'

test_expect_success '"add non-existent" should fail' '
	test_must_fail git add non-existent &&
	! (git ls-files | grep "non-existent")
'

test_expect_success 'git add -A on empty repo does not error out' '
	rm -fr empty &&
	git init empty &&
	(
		cd empty &&
		git add -A . &&
		git add -A
	)
'

test_expect_success '"git add ." in empty repo' '
	rm -fr empty &&
	git init empty &&
	(
		cd empty &&
		git add .
	)
'

test_expect_success 'error on a repository with no commits' '
	rm -fr empty &&
	git init empty &&
	test_must_fail git add empty >actual 2>&1 &&
	cat >expect <<-EOF &&
	error: '"'empty/'"' does not have a commit checked out
	fatal: adding files failed
	EOF
	test_cmp expect actual
'

test_expect_success 'git add --dry-run of existing changed file' "
	echo new >>track-this &&
	git add --dry-run track-this >actual 2>&1 &&
	echo \"add 'track-this'\" | test_cmp - actual
"

test_expect_success 'git add --dry-run of non-existing file' "
	echo ignored-file >>.gitignore &&
	test_must_fail git add --dry-run track-this ignored-file >actual 2>&1
"

test_expect_success 'git add --dry-run of an existing file output' "
	echo \"fatal: pathspec 'ignored-file' did not match any files\" >expect &&
	test_cmp expect actual
"

cat >expect.err <<\EOF
The following paths are ignored by one of your .gitignore files:
ignored-file
hint: Use -f if you really want to add them.
hint: Turn this message off by running
hint: "git config advice.addIgnoredFile false"
EOF
cat >expect.out <<\EOF
add 'track-this'
EOF

test_expect_success 'git add --dry-run --ignore-missing of non-existing file' '
	test_must_fail git add --dry-run --ignore-missing track-this ignored-file >actual.out 2>actual.err
'

test_expect_success 'git add --dry-run --ignore-missing of non-existing file output' '
	test_cmp expect.out actual.out &&
	test_cmp expect.err actual.err
'

test_expect_success 'git add --dry-run --interactive should fail' '
	test_must_fail git add --dry-run --interactive
'

test_expect_success 'git add empty string should fail' '
	test_must_fail git add ""
'

test_expect_success 'git add --chmod=[+-]x stages correctly' '
	rm -f foo1 &&
	echo foo >foo1 &&
	git add --chmod=+x foo1 &&
	test_mode_in_index 100755 foo1 &&
	git add --chmod=-x foo1 &&
	test_mode_in_index 100644 foo1
'

test_expect_success POSIXPERM,SYMLINKS 'git add --chmod=+x with symlinks' '
	git config core.filemode 1 &&
	git config core.symlinks 1 &&
	rm -f foo2 &&
	echo foo >foo2 &&
	git add --chmod=+x foo2 &&
	test_mode_in_index 100755 foo2
'

test_expect_success 'git add --chmod=[+-]x changes index with already added file' '
	rm -f foo3 xfoo3 &&
	git reset --hard &&
	echo foo >foo3 &&
	git add foo3 &&
	git add --chmod=+x foo3 &&
	test_mode_in_index 100755 foo3 &&
	echo foo >xfoo3 &&
	chmod 755 xfoo3 &&
	git add xfoo3 &&
	git add --chmod=-x xfoo3 &&
	test_mode_in_index 100644 xfoo3
'

test_expect_success POSIXPERM 'git add --chmod=[+-]x does not change the working tree' '
	echo foo >foo4 &&
	git add foo4 &&
	git add --chmod=+x foo4 &&
	! test -x foo4
'

test_expect_success 'git add --chmod fails with non regular files (but updates the other paths)' '
	git reset --hard &&
	test_ln_s_add foo foo3 &&
	touch foo4 &&
	test_must_fail git add --chmod=+x foo3 foo4 2>stderr &&
	test_i18ngrep "cannot chmod +x .foo3." stderr &&
	test_mode_in_index 120000 foo3 &&
	test_mode_in_index 100755 foo4
'

test_expect_success 'git add --chmod honors --dry-run' '
	git reset --hard &&
	echo foo >foo4 &&
	git add foo4 &&
	git add --chmod=+x --dry-run foo4 &&
	test_mode_in_index 100644 foo4
'

test_expect_success 'git add --chmod --dry-run reports error for non regular files' '
	git reset --hard &&
	test_ln_s_add foo foo4 &&
	test_must_fail git add --chmod=+x --dry-run foo4 2>stderr &&
	test_i18ngrep "cannot chmod +x .foo4." stderr
'

test_expect_success 'git add --chmod --dry-run reports error for unmatched pathspec' '
	test_must_fail git add --chmod=+x --dry-run nonexistent 2>stderr &&
	test_i18ngrep "pathspec .nonexistent. did not match any files" stderr
'

test_expect_success 'no file status change if no pathspec is given' '
	>foo5 &&
	>foo6 &&
	git add foo5 foo6 &&
	git add --chmod=+x &&
	test_mode_in_index 100644 foo5 &&
	test_mode_in_index 100644 foo6
'

test_expect_success 'no file status change if no pathspec is given in subdir' '
	mkdir -p sub &&
	(
		cd sub &&
		>sub-foo1 &&
		>sub-foo2 &&
		git add . &&
		git add --chmod=+x &&
		test_mode_in_index 100644 sub-foo1 &&
		test_mode_in_index 100644 sub-foo2
	)
'

test_expect_success 'all statuses changed in folder if . is given' '
	git init repo &&
	(
		cd repo &&
		mkdir -p sub/dir &&
		touch x y z sub/a sub/dir/b &&
		git add -A &&
		git add --chmod=+x . &&
		test $(git ls-files --stage | grep ^100644 | wc -l) -eq 0 &&
		git add --chmod=-x . &&
		test $(git ls-files --stage | grep ^100755 | wc -l) -eq 0
	)
'

test_expect_success CASE_INSENSITIVE_FS 'path is case-insensitive' '
	path="$(pwd)/BLUB" &&
	touch "$path" &&
	downcased="$(echo "$path" | tr A-Z a-z)" &&
	git add "$downcased"
'

test_expect_success MINGW 'can add files via NTFS junctions' '
	test_when_finished "cmd //c rmdir junction && rm -rf target" &&
	test_create_repo target &&
	cmd //c "mklink /j junction target" &&
	>target/via-junction &&
	git -C junction add "$(pwd)/junction/via-junction" &&
	echo via-junction >expect &&
	git -C target diff --cached --name-only >actual &&
	test_cmp expect actual
'

test_done
