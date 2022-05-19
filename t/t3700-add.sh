#!/bin/sh
#
# Copyright (c) 2006 Carl D. Worth
#

test_description='Test of but add, including the -- option.'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# Test the file mode "$1" of the file "$2" in the index.
test_mode_in_index () {
	case "$(but ls-files -s "$2")" in
	"$1 "*"	$2")
		echo pass
		;;
	*)
		echo fail
		but ls-files -s "$2"
		return 1
		;;
	esac
}

test_expect_success \
    'Test of but add' \
    'touch foo && but add foo'

test_expect_success \
    'Post-check that foo is in the index' \
    'but ls-files foo | grep foo'

test_expect_success \
    'Test that "but add -- -q" works' \
    'touch -- -q && but add -- -q'

test_expect_success \
	'but add: Test that executable bit is not used if core.filemode=0' \
	'but config core.filemode 0 &&
	 echo foo >xfoo1 &&
	 chmod 755 xfoo1 &&
	 but add xfoo1 &&
	 test_mode_in_index 100644 xfoo1'

test_expect_success 'but add: filemode=0 should not get confused by symlink' '
	rm -f xfoo1 &&
	test_ln_s_add foo xfoo1 &&
	test_mode_in_index 120000 xfoo1
'

test_expect_success \
	'but update-index --add: Test that executable bit is not used...' \
	'but config core.filemode 0 &&
	 echo foo >xfoo2 &&
	 chmod 755 xfoo2 &&
	 but update-index --add xfoo2 &&
	 test_mode_in_index 100644 xfoo2'

test_expect_success 'but add: filemode=0 should not get confused by symlink' '
	rm -f xfoo2 &&
	test_ln_s_add foo xfoo2 &&
	test_mode_in_index 120000 xfoo2
'

test_expect_success \
	'but update-index --add: Test that executable bit is not used...' \
	'but config core.filemode 0 &&
	 test_ln_s_add xfoo2 xfoo3 &&	# runs but update-index --add
	 test_mode_in_index 120000 xfoo3'

test_expect_success '.butignore test setup' '
	echo "*.ig" >.butignore &&
	mkdir c.if d.ig &&
	>a.ig && >b.if &&
	>c.if/c.if && >c.if/c.ig &&
	>d.ig/d.if && >d.ig/d.ig
'

test_expect_success '.butignore is honored' '
	but add . &&
	! (but ls-files | grep "\\.ig")
'

test_expect_success 'error out when attempting to add ignored ones without -f' '
	test_must_fail but add a.?? &&
	! (but ls-files | grep "\\.ig")
'

test_expect_success 'error out when attempting to add ignored ones without -f' '
	test_must_fail but add d.?? &&
	! (but ls-files | grep "\\.ig")
'

test_expect_success 'error out when attempting to add ignored ones but add others' '
	touch a.if &&
	test_must_fail but add a.?? &&
	! (but ls-files | grep "\\.ig") &&
	(but ls-files | grep a.if)
'

test_expect_success 'add ignored ones with -f' '
	but add -f a.?? &&
	but ls-files --error-unmatch a.ig
'

test_expect_success 'add ignored ones with -f' '
	but add -f d.??/* &&
	but ls-files --error-unmatch d.ig/d.if d.ig/d.ig
'

test_expect_success 'add ignored ones with -f' '
	rm -f .but/index &&
	but add -f d.?? &&
	but ls-files --error-unmatch d.ig/d.if d.ig/d.ig
'

test_expect_success '.butignore with subdirectory' '

	rm -f .but/index &&
	mkdir -p sub/dir &&
	echo "!dir/a.*" >sub/.butignore &&
	>sub/a.ig &&
	>sub/dir/a.ig &&
	but add sub/dir &&
	but ls-files --error-unmatch sub/dir/a.ig &&
	rm -f .but/index &&
	(
		cd sub/dir &&
		but add .
	) &&
	but ls-files --error-unmatch sub/dir/a.ig
'

mkdir 1 1/2 1/3
touch 1/2/a 1/3/b 1/2/c
test_expect_success 'check correct prefix detection' '
	rm -f .but/index &&
	but add 1/2/a 1/3/b 1/2/c
'

test_expect_success 'but add with filemode=0, symlinks=0, and unmerged entries' '
	for s in 1 2 3
	do
		echo $s > stage$s &&
		echo "100755 $(but hash-object -w stage$s) $s	file" &&
		echo "120000 $(printf $s | but hash-object -w -t blob --stdin) $s	symlink" || return 1
	done | but update-index --index-info &&
	but config core.filemode 0 &&
	but config core.symlinks 0 &&
	echo new > file &&
	echo new > symlink &&
	but add file symlink &&
	but ls-files --stage | grep "^100755 .* 0	file$" &&
	but ls-files --stage | grep "^120000 .* 0	symlink$"
'

test_expect_success 'but add with filemode=0, symlinks=0 prefers stage 2 over stage 1' '
	but rm --cached -f file symlink &&
	(
		echo "100644 $(but hash-object -w stage1) 1	file" &&
		echo "100755 $(but hash-object -w stage2) 2	file" &&
		echo "100644 $(printf 1 | but hash-object -w -t blob --stdin) 1	symlink" &&
		echo "120000 $(printf 2 | but hash-object -w -t blob --stdin) 2	symlink"
	) | but update-index --index-info &&
	but config core.filemode 0 &&
	but config core.symlinks 0 &&
	echo new > file &&
	echo new > symlink &&
	but add file symlink &&
	but ls-files --stage | grep "^100755 .* 0	file$" &&
	but ls-files --stage | grep "^120000 .* 0	symlink$"
'

test_expect_success 'but add --refresh' '
	>foo && but add foo && but cummit -a -m "cummit all" &&
	test -z "$(but diff-index HEAD -- foo)" &&
	but read-tree HEAD &&
	case "$(but diff-index HEAD -- foo)" in
	:100644" "*"M	foo") echo pass;;
	*) echo fail; false;;
	esac &&
	but add --refresh -- foo &&
	test -z "$(but diff-index HEAD -- foo)"
'

test_expect_success 'but add --refresh with pathspec' '
	but reset --hard &&
	echo >foo && echo >bar && echo >baz &&
	but add foo bar baz && H=$(but rev-parse :foo) && but rm -f foo &&
	echo "100644 $H 3	foo" | but update-index --index-info &&
	test-tool chmtime -60 bar baz &&
	but add --refresh bar >actual &&
	test_must_be_empty actual &&

	but diff-files --name-only >actual &&
	! grep bar actual &&
	grep baz actual
'

test_expect_success 'but add --refresh correctly reports no match error' "
	echo \"fatal: pathspec ':(icase)nonexistent' did not match any files\" >expect &&
	test_must_fail but add --refresh ':(icase)nonexistent' 2>actual &&
	test_cmp expect actual
"

test_expect_success POSIXPERM,SANITY 'but add should fail atomically upon an unreadable file' '
	but reset --hard &&
	date >foo1 &&
	date >foo2 &&
	chmod 0 foo2 &&
	test_must_fail but add --verbose . &&
	! ( but ls-files foo1 | grep foo1 )
'

rm -f foo2

test_expect_success POSIXPERM,SANITY 'but add --ignore-errors' '
	but reset --hard &&
	date >foo1 &&
	date >foo2 &&
	chmod 0 foo2 &&
	test_must_fail but add --verbose --ignore-errors . &&
	but ls-files foo1 | grep foo1
'

rm -f foo2

test_expect_success POSIXPERM,SANITY 'but add (add.ignore-errors)' '
	but config add.ignore-errors 1 &&
	but reset --hard &&
	date >foo1 &&
	date >foo2 &&
	chmod 0 foo2 &&
	test_must_fail but add --verbose . &&
	but ls-files foo1 | grep foo1
'
rm -f foo2

test_expect_success POSIXPERM,SANITY 'but add (add.ignore-errors = false)' '
	but config add.ignore-errors 0 &&
	but reset --hard &&
	date >foo1 &&
	date >foo2 &&
	chmod 0 foo2 &&
	test_must_fail but add --verbose . &&
	! ( but ls-files foo1 | grep foo1 )
'
rm -f foo2

test_expect_success POSIXPERM,SANITY '--no-ignore-errors overrides config' '
       but config add.ignore-errors 1 &&
       but reset --hard &&
       date >foo1 &&
       date >foo2 &&
       chmod 0 foo2 &&
       test_must_fail but add --verbose --no-ignore-errors . &&
       ! ( but ls-files foo1 | grep foo1 ) &&
       but config add.ignore-errors 0
'
rm -f foo2

test_expect_success BSLASHPSPEC "but add 'fo\\[ou\\]bar' ignores foobar" '
	but reset --hard &&
	touch fo\[ou\]bar foobar &&
	but add '\''fo\[ou\]bar'\'' &&
	but ls-files fo\[ou\]bar | fgrep fo\[ou\]bar &&
	! ( but ls-files foobar | grep foobar )
'

test_expect_success 'but add to resolve conflicts on otherwise ignored path' '
	but reset --hard &&
	H=$(but rev-parse :1/2/a) &&
	(
		echo "100644 $H 1	track-this" &&
		echo "100644 $H 3	track-this"
	) | but update-index --index-info &&
	echo track-this >>.butignore &&
	echo resolved >track-this &&
	but add track-this
'

test_expect_success '"add non-existent" should fail' '
	test_must_fail but add non-existent &&
	! (but ls-files | grep "non-existent")
'

test_expect_success 'but add -A on empty repo does not error out' '
	rm -fr empty &&
	but init empty &&
	(
		cd empty &&
		but add -A . &&
		but add -A
	)
'

test_expect_success '"but add ." in empty repo' '
	rm -fr empty &&
	but init empty &&
	(
		cd empty &&
		but add .
	)
'

test_expect_success 'error on a repository with no cummits' '
	rm -fr empty &&
	but init empty &&
	test_must_fail but add empty >actual 2>&1 &&
	cat >expect <<-EOF &&
	error: '"'empty/'"' does not have a cummit checked out
	fatal: adding files failed
	EOF
	test_cmp expect actual
'

test_expect_success 'but add --dry-run of existing changed file' "
	echo new >>track-this &&
	but add --dry-run track-this >actual 2>&1 &&
	echo \"add 'track-this'\" | test_cmp - actual
"

test_expect_success 'but add --dry-run of non-existing file' "
	echo ignored-file >>.butignore &&
	test_must_fail but add --dry-run track-this ignored-file >actual 2>&1
"

test_expect_success 'but add --dry-run of an existing file output' "
	echo \"fatal: pathspec 'ignored-file' did not match any files\" >expect &&
	test_cmp expect actual
"

cat >expect.err <<\EOF
The following paths are ignored by one of your .butignore files:
ignored-file
hint: Use -f if you really want to add them.
hint: Turn this message off by running
hint: "but config advice.addIgnoredFile false"
EOF
cat >expect.out <<\EOF
add 'track-this'
EOF

test_expect_success 'but add --dry-run --ignore-missing of non-existing file' '
	test_must_fail but add --dry-run --ignore-missing track-this ignored-file >actual.out 2>actual.err
'

test_expect_success 'but add --dry-run --ignore-missing of non-existing file output' '
	test_cmp expect.out actual.out &&
	test_cmp expect.err actual.err
'

test_expect_success 'but add --dry-run --interactive should fail' '
	test_must_fail but add --dry-run --interactive
'

test_expect_success 'but add empty string should fail' '
	test_must_fail but add ""
'

test_expect_success 'but add --chmod=[+-]x stages correctly' '
	rm -f foo1 &&
	echo foo >foo1 &&
	but add --chmod=+x foo1 &&
	test_mode_in_index 100755 foo1 &&
	but add --chmod=-x foo1 &&
	test_mode_in_index 100644 foo1
'

test_expect_success POSIXPERM,SYMLINKS 'but add --chmod=+x with symlinks' '
	but config core.filemode 1 &&
	but config core.symlinks 1 &&
	rm -f foo2 &&
	echo foo >foo2 &&
	but add --chmod=+x foo2 &&
	test_mode_in_index 100755 foo2
'

test_expect_success 'but add --chmod=[+-]x changes index with already added file' '
	rm -f foo3 xfoo3 &&
	but reset --hard &&
	echo foo >foo3 &&
	but add foo3 &&
	but add --chmod=+x foo3 &&
	test_mode_in_index 100755 foo3 &&
	echo foo >xfoo3 &&
	chmod 755 xfoo3 &&
	but add xfoo3 &&
	but add --chmod=-x xfoo3 &&
	test_mode_in_index 100644 xfoo3
'

test_expect_success POSIXPERM 'but add --chmod=[+-]x does not change the working tree' '
	echo foo >foo4 &&
	but add foo4 &&
	but add --chmod=+x foo4 &&
	! test -x foo4
'

test_expect_success 'but add --chmod fails with non regular files (but updates the other paths)' '
	but reset --hard &&
	test_ln_s_add foo foo3 &&
	touch foo4 &&
	test_must_fail but add --chmod=+x foo3 foo4 2>stderr &&
	test_i18ngrep "cannot chmod +x .foo3." stderr &&
	test_mode_in_index 120000 foo3 &&
	test_mode_in_index 100755 foo4
'

test_expect_success 'but add --chmod honors --dry-run' '
	but reset --hard &&
	echo foo >foo4 &&
	but add foo4 &&
	but add --chmod=+x --dry-run foo4 &&
	test_mode_in_index 100644 foo4
'

test_expect_success 'but add --chmod --dry-run reports error for non regular files' '
	but reset --hard &&
	test_ln_s_add foo foo4 &&
	test_must_fail but add --chmod=+x --dry-run foo4 2>stderr &&
	test_i18ngrep "cannot chmod +x .foo4." stderr
'

test_expect_success 'but add --chmod --dry-run reports error for unmatched pathspec' '
	test_must_fail but add --chmod=+x --dry-run nonexistent 2>stderr &&
	test_i18ngrep "pathspec .nonexistent. did not match any files" stderr
'

test_expect_success 'no file status change if no pathspec is given' '
	>foo5 &&
	>foo6 &&
	but add foo5 foo6 &&
	but add --chmod=+x &&
	test_mode_in_index 100644 foo5 &&
	test_mode_in_index 100644 foo6
'

test_expect_success 'no file status change if no pathspec is given in subdir' '
	mkdir -p sub &&
	(
		cd sub &&
		>sub-foo1 &&
		>sub-foo2 &&
		but add . &&
		but add --chmod=+x &&
		test_mode_in_index 100644 sub-foo1 &&
		test_mode_in_index 100644 sub-foo2
	)
'

test_expect_success 'all statuses changed in folder if . is given' '
	but init repo &&
	(
		cd repo &&
		mkdir -p sub/dir &&
		touch x y z sub/a sub/dir/b &&
		but add -A &&
		but add --chmod=+x . &&
		test $(but ls-files --stage | grep ^100644 | wc -l) -eq 0 &&
		but add --chmod=-x . &&
		test $(but ls-files --stage | grep ^100755 | wc -l) -eq 0
	)
'

test_expect_success CASE_INSENSITIVE_FS 'path is case-insensitive' '
	path="$(pwd)/BLUB" &&
	touch "$path" &&
	downcased="$(echo "$path" | tr A-Z a-z)" &&
	but add "$downcased"
'

test_done
