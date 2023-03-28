#!/bin/sh
#
# Copyright (c) 2007 Junio C Hamano
#

test_description='Test prune and reflog expiration'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

check_have () {
	gaah= &&
	for N in "$@"
	do
		eval "o=\$$N" && git cat-file -t $o || {
			echo Gaah $N
			gaah=$N
			break
		}
	done &&
	test -z "$gaah"
}

check_fsck () {
	git fsck --full >fsck.output
	case "$1" in
	'')
		test_must_be_empty fsck.output ;;
	*)
		test_i18ngrep "$1" fsck.output ;;
	esac
}

corrupt () {
	mv .git/objects/$(test_oid_to_path $1) .git/$1
}

recover () {
	aa=$(echo $1 | cut -c 1-2)
	mkdir -p .git/objects/$aa
	mv .git/$1 .git/objects/$(test_oid_to_path $1)
}

check_dont_have () {
	gaah= &&
	for N in "$@"
	do
		eval "o=\$$N"
		git cat-file -t $o && {
			echo Gaah $N
			gaah=$N
			break
		}
	done
	test -z "$gaah"
}

test_expect_success setup '
	mkdir -p A/B &&
	echo rat >C &&
	echo ox >A/D &&
	echo tiger >A/B/E &&
	git add . &&

	test_tick && git commit -m rabbit &&
	H=$(git rev-parse --verify HEAD) &&
	A=$(git rev-parse --verify HEAD:A) &&
	B=$(git rev-parse --verify HEAD:A/B) &&
	C=$(git rev-parse --verify HEAD:C) &&
	D=$(git rev-parse --verify HEAD:A/D) &&
	E=$(git rev-parse --verify HEAD:A/B/E) &&
	check_fsck &&

	test_chmod +x C &&
	git add C &&
	test_tick && git commit -m dragon &&
	L=$(git rev-parse --verify HEAD) &&
	check_fsck &&

	rm -f C A/B/E &&
	echo snake >F &&
	echo horse >A/G &&
	git add F A/G &&
	test_tick && git commit -a -m sheep &&
	F=$(git rev-parse --verify HEAD:F) &&
	G=$(git rev-parse --verify HEAD:A/G) &&
	I=$(git rev-parse --verify HEAD:A) &&
	J=$(git rev-parse --verify HEAD) &&
	check_fsck &&

	rm -f A/G &&
	test_tick && git commit -a -m monkey &&
	K=$(git rev-parse --verify HEAD) &&
	check_fsck &&

	check_have A B C D E F G H I J K L &&

	git prune &&

	check_have A B C D E F G H I J K L &&

	check_fsck &&

	git reflog refs/heads/main >output &&
	test_line_count = 4 output
'

test_expect_success 'correct usage on sub-command -h' '
	test_expect_code 129 git reflog expire -h >err &&
	grep "git reflog expire" err
'

test_expect_success 'correct usage on "git reflog show -h"' '
	test_expect_code 129 git reflog show -h >err &&
	grep -F "git reflog [show]" err
'

test_expect_success 'pass through -- to sub-command' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_commit -C repo message --a-file contents dash-tag &&

	git -C repo reflog show -- --does-not-exist >out &&
	test_must_be_empty out &&
	git -C repo reflog show >expect &&
	git -C repo reflog show -- --a-file >actual &&
	test_cmp expect actual
'

test_expect_success rewind '
	test_tick && git reset --hard HEAD~2 &&
	test -f C &&
	test -f A/B/E &&
	! test -f F &&
	! test -f A/G &&

	check_have A B C D E F G H I J K L &&

	git prune &&

	check_have A B C D E F G H I J K L &&

	git reflog refs/heads/main >output &&
	test_line_count = 5 output
'

test_expect_success 'corrupt and check' '

	corrupt $F &&
	check_fsck "missing blob $F"

'

test_expect_success 'reflog expire --dry-run should not touch reflog' '

	git reflog expire --dry-run \
		--expire=$(($test_tick - 10000)) \
		--expire-unreachable=$(($test_tick - 10000)) \
		--stale-fix \
		--all &&

	git reflog refs/heads/main >output &&
	test_line_count = 5 output &&

	check_fsck "missing blob $F"
'

test_expect_success 'reflog expire' '

	git reflog expire --verbose \
		--expire=$(($test_tick - 10000)) \
		--expire-unreachable=$(($test_tick - 10000)) \
		--stale-fix \
		--all &&

	git reflog refs/heads/main >output &&
	test_line_count = 2 output &&

	check_fsck "dangling commit $K"
'

test_expect_success '--stale-fix handles missing objects generously' '
	git -c core.logAllRefUpdates=false fast-import --date-format=now <<-EOS &&
	commit refs/heads/stale-fix
	mark :1
	committer Author <a@uth.or> now
	data <<EOF
	start stale fix
	EOF
	M 100644 inline file
	data <<EOF
	contents
	EOF
	commit refs/heads/stale-fix
	committer Author <a@uth.or> now
	data <<EOF
	stale fix branch tip
	EOF
	from :1
	EOS

	parent_oid=$(git rev-parse stale-fix^) &&
	test_when_finished "recover $parent_oid" &&
	corrupt $parent_oid &&
	git reflog expire --stale-fix
'

test_expect_success 'prune and fsck' '

	git prune &&
	check_fsck &&

	check_have A B C D E H L &&
	check_dont_have F G I J K

'

test_expect_success 'recover and check' '

	recover $F &&
	check_fsck "dangling blob $F"

'

test_expect_success 'delete' '
	echo 1 > C &&
	test_tick &&
	git commit -m rat C &&

	echo 2 > C &&
	test_tick &&
	git commit -m ox C &&

	echo 3 > C &&
	test_tick &&
	git commit -m tiger C &&

	HEAD_entry_count=$(git reflog | wc -l) &&
	main_entry_count=$(git reflog show main | wc -l) &&

	test $HEAD_entry_count = 5 &&
	test $main_entry_count = 5 &&


	git reflog delete main@{1} &&
	git reflog show main > output &&
	test_line_count = $(($main_entry_count - 1)) output &&
	test $HEAD_entry_count = $(git reflog | wc -l) &&
	! grep ox < output &&

	main_entry_count=$(wc -l < output) &&

	git reflog delete HEAD@{1} &&
	test $(($HEAD_entry_count -1)) = $(git reflog | wc -l) &&
	test $main_entry_count = $(git reflog show main | wc -l) &&

	HEAD_entry_count=$(git reflog | wc -l) &&

	git reflog delete main@{07.04.2005.15:15:00.-0700} &&
	git reflog show main > output &&
	test_line_count = $(($main_entry_count - 1)) output &&
	! grep dragon < output

'

test_expect_success 'rewind2' '

	test_tick && git reset --hard HEAD~2 &&
	git reflog refs/heads/main >output &&
	test_line_count = 4 output
'

test_expect_success '--expire=never' '

	git reflog expire --verbose \
		--expire=never \
		--expire-unreachable=never \
		--all &&
	git reflog refs/heads/main >output &&
	test_line_count = 4 output
'

test_expect_success 'gc.reflogexpire=never' '
	test_config gc.reflogexpire never &&
	test_config gc.reflogexpireunreachable never &&

	git reflog expire --verbose --all >output &&
	test_line_count = 9 output &&

	git reflog refs/heads/main >output &&
	test_line_count = 4 output
'

test_expect_success 'gc.reflogexpire=false' '
	test_config gc.reflogexpire false &&
	test_config gc.reflogexpireunreachable false &&

	git reflog expire --verbose --all &&
	git reflog refs/heads/main >output &&
	test_line_count = 4 output

'

test_expect_success 'git reflog expire unknown reference' '
	test_config gc.reflogexpire never &&
	test_config gc.reflogexpireunreachable never &&

	test_must_fail git reflog expire main@{123} 2>stderr &&
	test_i18ngrep "points nowhere" stderr &&
	test_must_fail git reflog expire does-not-exist 2>stderr &&
	test_i18ngrep "points nowhere" stderr
'

test_expect_success 'checkout should not delete log for packed ref' '
	test $(git reflog main | wc -l) = 4 &&
	git branch foo &&
	git pack-refs --all &&
	git checkout foo &&
	test $(git reflog main | wc -l) = 4
'

test_expect_success 'stale dirs do not cause d/f conflicts (reflogs on)' '
	test_when_finished "git branch -d one || git branch -d one/two" &&

	git branch one/two main &&
	echo "one/two@{0} branch: Created from main" >expect &&
	git log -g --format="%gd %gs" one/two >actual &&
	test_cmp expect actual &&
	git branch -d one/two &&

	# now logs/refs/heads/one is a stale directory, but
	# we should move it out of the way to create "one" reflog
	git branch one main &&
	echo "one@{0} branch: Created from main" >expect &&
	git log -g --format="%gd %gs" one >actual &&
	test_cmp expect actual
'

test_expect_success 'stale dirs do not cause d/f conflicts (reflogs off)' '
	test_when_finished "git branch -d one || git branch -d one/two" &&

	git branch one/two main &&
	echo "one/two@{0} branch: Created from main" >expect &&
	git log -g --format="%gd %gs" one/two >actual &&
	test_cmp expect actual &&
	git branch -d one/two &&

	# same as before, but we only create a reflog for "one" if
	# it already exists, which it does not
	git -c core.logallrefupdates=false branch one main &&
	git log -g --format="%gd %gs" one >actual &&
	test_must_be_empty actual
'

# Triggering the bug detected by this test requires a newline to fall
# exactly BUFSIZ-1 bytes from the end of the file. We don't know
# what that value is, since it's platform dependent. However, if
# we choose some value N, we also catch any D which divides N evenly
# (since we will read backwards in chunks of D). So we choose 8K,
# which catches glibc (with an 8K BUFSIZ) and *BSD (1K).
#
# Each line is 114 characters, so we need 75 to still have a few before the
# last 8K. The 89-character padding on the final entry lines up our
# newline exactly.
test_expect_success REFFILES,SHA1 'parsing reverse reflogs at BUFSIZ boundaries' '
	git checkout -b reflogskip &&
	zf=$(test_oid zero_2) &&
	ident="abc <xyz> 0000000001 +0000" &&
	for i in $(test_seq 1 75); do
		printf "$zf%02d $zf%02d %s\t" $i $(($i+1)) "$ident" &&
		if test $i = 75; then
			for j in $(test_seq 1 89); do
				printf X || return 1
			done
		else
			printf X
		fi &&
		printf "\n" || return 1
	done >.git/logs/refs/heads/reflogskip &&
	git rev-parse reflogskip@{73} >actual &&
	echo ${zf}03 >expect &&
	test_cmp expect actual
'

test_expect_success 'no segfaults for reflog containing non-commit sha1s' '
	git update-ref --create-reflog -m "Creating ref" \
		refs/tests/tree-in-reflog HEAD &&
	git update-ref -m "Forcing tree" refs/tests/tree-in-reflog HEAD^{tree} &&
	git update-ref -m "Restoring to commit" refs/tests/tree-in-reflog HEAD &&
	git reflog refs/tests/tree-in-reflog
'

test_expect_failure 'reflog with non-commit entries displays all entries' '
	git reflog refs/tests/tree-in-reflog >actual &&
	test_line_count = 3 actual
'

# This test takes a lock on an individual ref; this is not supported in
# reftable.
test_expect_success REFFILES 'reflog expire operates on symref not referrent' '
	git branch --create-reflog the_symref &&
	git branch --create-reflog referrent &&
	git update-ref referrent HEAD &&
	git symbolic-ref refs/heads/the_symref refs/heads/referrent &&
	test_when_finished "rm -f .git/refs/heads/referrent.lock" &&
	touch .git/refs/heads/referrent.lock &&
	git reflog expire --expire=all the_symref
'

test_expect_success 'continue walking past root commits' '
	git init orphanage &&
	(
		cd orphanage &&
		cat >expect <<-\EOF &&
		HEAD@{0} commit (initial): orphan2-1
		HEAD@{1} commit: orphan1-2
		HEAD@{2} commit (initial): orphan1-1
		HEAD@{3} commit (initial): initial
		EOF
		test_commit initial &&
		git checkout --orphan orphan1 &&
		test_commit orphan1-1 &&
		test_commit orphan1-2 &&
		git checkout --orphan orphan2 &&
		test_commit orphan2-1 &&
		git log -g --format="%gd %gs" >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'expire with multiple worktrees' '
	git init main-wt &&
	(
		cd main-wt &&
		test_tick &&
		test_commit foo &&
		git  worktree add link-wt &&
		test_tick &&
		test_commit -C link-wt foobar &&
		test_tick &&
		git reflog expire --verbose --all --expire=$test_tick &&
		test-tool ref-store worktree:link-wt for-each-reflog-ent HEAD >actual &&
		test_must_be_empty actual
	)
'

test_expect_success REFFILES 'empty reflog' '
	test_when_finished "rm -rf empty" &&
	git init empty &&
	test_commit -C empty A &&
	>empty/.git/logs/refs/heads/foo &&
	git -C empty reflog expire --all 2>err &&
	test_must_be_empty err
'

test_done
