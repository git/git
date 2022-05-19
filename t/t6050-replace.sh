#!/bin/sh
#
# Copyright (c) 2008 Christian Couder
#
test_description='Tests replace refs functionality'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"

add_and_cummit_file ()
{
    _file="$1"
    _msg="$2"

    but add $_file || return $?
    test_tick || return $?
    but cummit --quiet -m "$_file: $_msg"
}

cummit_buffer_contains_parents ()
{
    but cat-file cummit "$1" >payload &&
    sed -n -e '/^$/q' -e '/^parent /p' <payload >actual &&
    shift &&
    for _parent
    do
	echo "parent $_parent"
    done >expected &&
    test_cmp expected actual
}

cummit_peeling_shows_parents ()
{
    _parent_number=1
    _cummit="$1"
    shift &&
    for _parent
    do
	_found=$(but rev-parse --verify $_cummit^$_parent_number) || return 1
	test "$_found" = "$_parent" || return 1
	_parent_number=$(( $_parent_number + 1 ))
    done &&
    test_must_fail but rev-parse --verify $_cummit^$_parent_number 2>err &&
    test_i18ngrep "Needed a single revision" err
}

commit_has_parents ()
{
    cummit_buffer_contains_parents "$@" &&
    cummit_peeling_shows_parents "$@"
}

HASH1=
HASH2=
HASH3=
HASH4=
HASH5=
HASH6=
HASH7=

test_expect_success 'set up buggy branch' '
     echo "line 1" >>hello &&
     echo "line 2" >>hello &&
     echo "line 3" >>hello &&
     echo "line 4" >>hello &&
     add_and_cummit_file hello "4 lines" &&
     HASH1=$(but rev-parse --verify HEAD) &&
     echo "line BUG" >>hello &&
     echo "line 6" >>hello &&
     echo "line 7" >>hello &&
     echo "line 8" >>hello &&
     add_and_cummit_file hello "4 more lines with a BUG" &&
     HASH2=$(but rev-parse --verify HEAD) &&
     echo "line 9" >>hello &&
     echo "line 10" >>hello &&
     add_and_cummit_file hello "2 more lines" &&
     HASH3=$(but rev-parse --verify HEAD) &&
     echo "line 11" >>hello &&
     add_and_cummit_file hello "1 more line" &&
     HASH4=$(but rev-parse --verify HEAD) &&
     sed -e "s/BUG/5/" hello >hello.new &&
     mv hello.new hello &&
     add_and_cummit_file hello "BUG fixed" &&
     HASH5=$(but rev-parse --verify HEAD) &&
     echo "line 12" >>hello &&
     echo "line 13" >>hello &&
     add_and_cummit_file hello "2 more lines" &&
     HASH6=$(but rev-parse --verify HEAD) &&
     echo "line 14" >>hello &&
     echo "line 15" >>hello &&
     echo "line 16" >>hello &&
     add_and_cummit_file hello "again 3 more lines" &&
     HASH7=$(but rev-parse --verify HEAD)
'

test_expect_success 'replace the author' '
     but cat-file cummit $HASH2 | grep "author A U Thor" &&
     R=$(but cat-file cummit $HASH2 | sed -e "s/A U/O/" | but hash-object -t cummit --stdin -w) &&
     but cat-file cummit $R | grep "author O Thor" &&
     but update-ref refs/replace/$HASH2 $R &&
     but show HEAD~5 | grep "O Thor" &&
     but show $HASH2 | grep "O Thor"
'

test_expect_success 'test --no-replace-objects option' '
     but cat-file cummit $HASH2 | grep "author O Thor" &&
     but --no-replace-objects cat-file cummit $HASH2 | grep "author A U Thor" &&
     but show $HASH2 | grep "O Thor" &&
     but --no-replace-objects show $HASH2 | grep "A U Thor"
'

test_expect_success 'test GIT_NO_REPLACE_OBJECTS env variable' '
     GIT_NO_REPLACE_OBJECTS=1 but cat-file cummit $HASH2 | grep "author A U Thor" &&
     GIT_NO_REPLACE_OBJECTS=1 but show $HASH2 | grep "A U Thor"
'

test_expect_success 'test core.usereplacerefs config option' '
	test_config core.usereplacerefs false &&
	but cat-file cummit $HASH2 | grep "author A U Thor" &&
	but show $HASH2 | grep "A U Thor"
'

cat >tag.sig <<EOF
object $HASH2
type cummit
tag mytag
tagger T A Gger <> 0 +0000

EOF

test_expect_success 'tag replaced cummit' '
     but update-ref refs/tags/mytag $(but mktag <tag.sig)
'

test_expect_success '"but fsck" works' '
     but fsck main >fsck_main.out &&
     test_i18ngrep "dangling cummit $R" fsck_main.out &&
     test_i18ngrep "dangling tag $(but show-ref -s refs/tags/mytag)" fsck_main.out &&
     test -z "$(but fsck)"
'

test_expect_success 'repack, clone and fetch work' '
     but repack -a -d &&
     but clone --no-hardlinks . clone_dir &&
     (
	  cd clone_dir &&
	  but show HEAD~5 | grep "A U Thor" &&
	  but show $HASH2 | grep "A U Thor" &&
	  but cat-file cummit $R &&
	  but repack -a -d &&
	  test_must_fail but cat-file cummit $R &&
	  but fetch ../ "refs/replace/*:refs/replace/*" &&
	  but show HEAD~5 | grep "O Thor" &&
	  but show $HASH2 | grep "O Thor" &&
	  but cat-file cummit $R
     )
'

test_expect_success '"but replace" listing and deleting' '
     test "$HASH2" = "$(but replace -l)" &&
     test "$HASH2" = "$(but replace)" &&
     aa=${HASH2%??????????????????????????????????????} &&
     test "$HASH2" = "$(but replace --list "$aa*")" &&
     test_must_fail but replace -d $R &&
     test_must_fail but replace --delete &&
     test_must_fail but replace -l -d $HASH2 &&
     but replace -d $HASH2 &&
     but show $HASH2 | grep "A U Thor" &&
     test -z "$(but replace -l)"
'

test_expect_success '"but replace" replacing' '
     but replace $HASH2 $R &&
     but show $HASH2 | grep "O Thor" &&
     test_must_fail but replace $HASH2 $R &&
     but replace -f $HASH2 $R &&
     test_must_fail but replace -f &&
     test "$HASH2" = "$(but replace)"
'

test_expect_success '"but replace" resolves sha1' '
     SHORTHASH2=$(but rev-parse --short=8 $HASH2) &&
     but replace -d $SHORTHASH2 &&
     but replace $SHORTHASH2 $R &&
     but show $HASH2 | grep "O Thor" &&
     test_must_fail but replace $HASH2 $R &&
     but replace -f $HASH2 $R &&
     test_must_fail but replace --force &&
     test "$HASH2" = "$(but replace)"
'

# This creates a side branch where the bug in H2
# does not appear because P2 is created by applying
# H2 and squashing H5 into it.
# P3, P4 and P6 are created by cherry-picking H3, H4
# and H6 respectively.
#
# At this point, we should have the following:
#
#    P2--P3--P4--P6
#   /
# H1-H2-H3-H4-H5-H6-H7
#
# Then we replace H6 with P6.
#
test_expect_success 'create parallel branch without the bug' '
     but replace -d $HASH2 &&
     but show $HASH2 | grep "A U Thor" &&
     but checkout $HASH1 &&
     but cherry-pick $HASH2 &&
     but show $HASH5 | but apply &&
     but cummit --amend -m "hello: 4 more lines WITHOUT the bug" hello &&
     PARA2=$(but rev-parse --verify HEAD) &&
     but cherry-pick $HASH3 &&
     PARA3=$(but rev-parse --verify HEAD) &&
     but cherry-pick $HASH4 &&
     PARA4=$(but rev-parse --verify HEAD) &&
     but cherry-pick $HASH6 &&
     PARA6=$(but rev-parse --verify HEAD) &&
     but replace $HASH6 $PARA6 &&
     but checkout main &&
     cur=$(but rev-parse --verify HEAD) &&
     test "$cur" = "$HASH7" &&
     but log --pretty=oneline | grep $PARA2 &&
     but remote add cloned ./clone_dir
'

test_expect_success 'push to cloned repo' '
     but push cloned $HASH6^:refs/heads/parallel &&
     (
	  cd clone_dir &&
	  but checkout parallel &&
	  but log --pretty=oneline | grep $PARA2
     )
'

test_expect_success 'push branch with replacement' '
     but cat-file cummit $PARA3 | grep "author A U Thor" &&
     S=$(but cat-file cummit $PARA3 | sed -e "s/A U/O/" | but hash-object -t cummit --stdin -w) &&
     but cat-file cummit $S | grep "author O Thor" &&
     but replace $PARA3 $S &&
     but show $HASH6~2 | grep "O Thor" &&
     but show $PARA3 | grep "O Thor" &&
     but push cloned $HASH6^:refs/heads/parallel2 &&
     (
	  cd clone_dir &&
	  but checkout parallel2 &&
	  but log --pretty=oneline | grep $PARA3 &&
	  but show $PARA3 | grep "A U Thor"
     )
'

test_expect_success 'fetch branch with replacement' '
     but branch tofetch $HASH6 &&
     (
	  cd clone_dir &&
	  but fetch origin refs/heads/tofetch:refs/heads/parallel3 &&
	  but log --pretty=oneline parallel3 >output.txt &&
	  ! grep $PARA3 output.txt &&
	  but show $PARA3 >para3.txt &&
	  grep "A U Thor" para3.txt &&
	  but fetch origin "refs/replace/*:refs/replace/*" &&
	  but log --pretty=oneline parallel3 >output.txt &&
	  grep $PARA3 output.txt &&
	  but show $PARA3 >para3.txt &&
	  grep "O Thor" para3.txt
     )
'

test_expect_success 'bisect and replacements' '
     but bisect start $HASH7 $HASH1 &&
     test "$PARA3" = "$(but rev-parse --verify HEAD)" &&
     but bisect reset &&
     GIT_NO_REPLACE_OBJECTS=1 but bisect start $HASH7 $HASH1 &&
     test "$HASH4" = "$(but rev-parse --verify HEAD)" &&
     but bisect reset &&
     but --no-replace-objects bisect start $HASH7 $HASH1 &&
     test "$HASH4" = "$(but rev-parse --verify HEAD)" &&
     but bisect reset
'

test_expect_success 'index-pack and replacements' '
	but --no-replace-objects rev-list --objects HEAD |
	but --no-replace-objects pack-objects test- &&
	but index-pack test-*.pack
'

test_expect_success 'not just cummits' '
	echo replaced >file &&
	but add file &&
	REPLACED=$(but rev-parse :file) &&
	mv file file.replaced &&

	echo original >file &&
	but add file &&
	ORIGINAL=$(but rev-parse :file) &&
	but update-ref refs/replace/$ORIGINAL $REPLACED &&
	mv file file.original &&

	but checkout file &&
	test_cmp file.replaced file
'

test_expect_success 'replaced and replacement objects must be of the same type' '
	test_must_fail but replace mytag $HASH1 &&
	test_must_fail but replace HEAD^{tree} HEAD~1 &&
	BLOB=$(but rev-parse :file) &&
	test_must_fail but replace HEAD^ $BLOB
'

test_expect_success '-f option bypasses the type check' '
	but replace -f mytag $HASH1 &&
	but replace --force HEAD^{tree} HEAD~1 &&
	but replace -f HEAD^ $BLOB
'

test_expect_success 'but cat-file --batch works on replace objects' '
	but replace | grep $PARA3 &&
	echo $PARA3 | but cat-file --batch
'

test_expect_success 'test --format bogus' '
	test_must_fail but replace --format bogus >/dev/null 2>&1
'

test_expect_success 'test --format short' '
	but replace --format=short >actual &&
	but replace >expected &&
	test_cmp expected actual
'

test_expect_success 'test --format medium' '
	H1=$(but --no-replace-objects rev-parse HEAD~1) &&
	HT=$(but --no-replace-objects rev-parse HEAD^{tree}) &&
	MYTAG=$(but --no-replace-objects rev-parse mytag) &&
	{
		echo "$H1 -> $BLOB" &&
		echo "$BLOB -> $REPLACED" &&
		echo "$HT -> $H1" &&
		echo "$PARA3 -> $S" &&
		echo "$MYTAG -> $HASH1"
	} | sort >expected &&
	but replace -l --format medium | sort >actual &&
	test_cmp expected actual
'

test_expect_success 'test --format long' '
	{
		echo "$H1 (cummit) -> $BLOB (blob)" &&
		echo "$BLOB (blob) -> $REPLACED (blob)" &&
		echo "$HT (tree) -> $H1 (cummit)" &&
		echo "$PARA3 (cummit) -> $S (cummit)" &&
		echo "$MYTAG (tag) -> $HASH1 (cummit)"
	} | sort >expected &&
	but replace --format=long | sort >actual &&
	test_cmp expected actual
'

test_expect_success 'setup fake editors' '
	write_script fakeeditor <<-\EOF &&
		sed -e "s/A U Thor/A fake Thor/" "$1" >"$1.new"
		mv "$1.new" "$1"
	EOF
	write_script failingfakeeditor <<-\EOF
		./fakeeditor "$@"
		false
	EOF
'

test_expect_success '--edit with and without already replaced object' '
	test_must_fail env GIT_EDITOR=./fakeeditor but replace --edit "$PARA3" &&
	GIT_EDITOR=./fakeeditor but replace --force --edit "$PARA3" &&
	but replace -l | grep "$PARA3" &&
	but cat-file cummit "$PARA3" | grep "A fake Thor" &&
	but replace -d "$PARA3" &&
	GIT_EDITOR=./fakeeditor but replace --edit "$PARA3" &&
	but replace -l | grep "$PARA3" &&
	but cat-file cummit "$PARA3" | grep "A fake Thor"
'

test_expect_success '--edit and change nothing or command failed' '
	but replace -d "$PARA3" &&
	test_must_fail env GIT_EDITOR=true but replace --edit "$PARA3" &&
	test_must_fail env GIT_EDITOR="./failingfakeeditor" but replace --edit "$PARA3" &&
	GIT_EDITOR=./fakeeditor but replace --edit "$PARA3" &&
	but replace -l | grep "$PARA3" &&
	but cat-file cummit "$PARA3" | grep "A fake Thor"
'

test_expect_success 'replace ref cleanup' '
	test -n "$(but replace)" &&
	but replace -d $(but replace) &&
	test -z "$(but replace)"
'

test_expect_success '--graft with and without already replaced object' '
	but log --oneline >log &&
	test_line_count = 7 log &&
	but replace --graft $HASH5 &&
	but log --oneline >log &&
	test_line_count = 3 log &&
	commit_has_parents $HASH5 &&
	test_must_fail but replace --graft $HASH5 $HASH4 $HASH3 &&
	but replace --force -g $HASH5 $HASH4 $HASH3 &&
	commit_has_parents $HASH5 $HASH4 $HASH3 &&
	but replace -d $HASH5
'

test_expect_success '--graft using a tag as the new parent' '
	but tag new_parent $HASH5 &&
	but replace --graft $HASH7 new_parent &&
	commit_has_parents $HASH7 $HASH5 &&
	but replace -d $HASH7 &&
	but tag -a -m "annotated new parent tag" annotated_new_parent $HASH5 &&
	but replace --graft $HASH7 annotated_new_parent &&
	commit_has_parents $HASH7 $HASH5 &&
	but replace -d $HASH7
'

test_expect_success '--graft using a tag as the replaced object' '
	but tag replaced_object $HASH7 &&
	but replace --graft replaced_object $HASH5 &&
	commit_has_parents $HASH7 $HASH5 &&
	but replace -d $HASH7 &&
	but tag -a -m "annotated replaced object tag" annotated_replaced_object $HASH7 &&
	but replace --graft annotated_replaced_object $HASH5 &&
	commit_has_parents $HASH7 $HASH5 &&
	but replace -d $HASH7
'

test_expect_success GPG 'set up a signed cummit' '
	echo "line 17" >>hello &&
	echo "line 18" >>hello &&
	but add hello &&
	test_tick &&
	but cummit --quiet -S -m "hello: 2 more lines in a signed cummit" &&
	HASH8=$(but rev-parse --verify HEAD) &&
	but verify-cummit $HASH8
'

test_expect_success GPG '--graft with a signed cummit' '
	but cat-file cummit $HASH8 >orig &&
	but replace --graft $HASH8 &&
	but cat-file cummit $HASH8 >repl &&
	commit_has_parents $HASH8 &&
	test_must_fail but verify-cummit $HASH8 &&
	sed -n -e "/^tree /p" -e "/^author /p" -e "/^cummitter /p" orig >expected &&
	echo >>expected &&
	sed -e "/^$/q" repl >actual &&
	test_cmp expected actual &&
	but replace -d $HASH8
'

test_expect_success GPG 'set up a merge cummit with a mergetag' '
	but reset --hard HEAD &&
	but checkout -b test_branch HEAD~2 &&
	echo "line 1 from test branch" >>hello &&
	echo "line 2 from test branch" >>hello &&
	but add hello &&
	test_tick &&
	but cummit -m "hello: 2 more lines from a test branch" &&
	HASH9=$(but rev-parse --verify HEAD) &&
	but tag -s -m "tag for testing with a mergetag" test_tag HEAD &&
	but checkout main &&
	but merge -s ours test_tag &&
	HASH10=$(but rev-parse --verify HEAD) &&
	but cat-file cummit $HASH10 | grep "^mergetag object"
'

test_expect_success GPG '--graft on a cummit with a mergetag' '
	test_must_fail but replace --graft $HASH10 $HASH8^1 &&
	but replace --graft $HASH10 $HASH8^1 $HASH9 &&
	but replace -d $HASH10
'

test_expect_success '--convert-graft-file' '
	but checkout -b with-graft-file &&
	test_cummit root2 &&
	but reset --hard root2^ &&
	test_cummit root1 &&
	test_cummit after-root1 &&
	test_tick &&
	but merge -m merge-root2 root2 &&

	: add and convert graft file &&
	printf "%s\n%s %s\n\n# comment\n%s\n" \
		$(but rev-parse HEAD^^ HEAD^ HEAD^^ HEAD^2) \
		>.but/info/grafts &&
	but status 2>stderr &&
	test_i18ngrep "hint:.*grafts is deprecated" stderr &&
	but replace --convert-graft-file 2>stderr &&
	test_i18ngrep ! "hint:.*grafts is deprecated" stderr &&
	test_path_is_missing .but/info/grafts &&

	: verify that the history is now "grafted" &&
	but rev-list HEAD >out &&
	test_line_count = 4 out &&

	: create invalid graft file and verify that it is not deleted &&
	test_when_finished "rm -f .but/info/grafts" &&
	echo $EMPTY_BLOB $EMPTY_TREE >.but/info/grafts &&
	test_must_fail but replace --convert-graft-file 2>err &&
	test_i18ngrep "$EMPTY_BLOB $EMPTY_TREE" err &&
	test_i18ngrep "$EMPTY_BLOB $EMPTY_TREE" .but/info/grafts
'

test_done
