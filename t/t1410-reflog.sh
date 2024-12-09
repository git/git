#!/bin/sh
#
# Copyright (c) 2007 Junio C Hamano
#

test_description='Test prune and reflog expiration'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

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
		test_grep "$1" fsck.output ;;
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

test_expect_success 'reflog expire should not barf on an annotated tag' '
	test_when_finished "git tag -d v0.tag || :" &&
	git -c core.logAllRefUpdates=always \
		tag -a -m "tag name" v0.tag main &&
	git reflog expire --dry-run refs/tags/v0.tag 2>err &&
	test_grep ! "error: [Oo]bject .* not a commit" err
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
	test_grep "points nowhere" stderr &&
	test_must_fail git reflog expire does-not-exist 2>stderr &&
	test_grep "points nowhere" stderr
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

test_expect_success 'expire one of multiple worktrees' '
	git init main-wt2 &&
	(
		cd main-wt2 &&
		test_tick &&
		test_commit foo &&
		git worktree add link-wt &&
		test_tick &&
		test_commit -C link-wt foobar &&
		test_tick &&
		test-tool ref-store worktree:link-wt for-each-reflog-ent HEAD \
			>expect-link-wt &&
		git reflog expire --verbose --all --expire=$test_tick \
			--single-worktree &&
		test-tool ref-store worktree:main for-each-reflog-ent HEAD \
			>actual-main &&
		test-tool ref-store worktree:link-wt for-each-reflog-ent HEAD \
			>actual-link-wt &&
		test_must_be_empty actual-main &&
		test_cmp expect-link-wt actual-link-wt
	)
'

test_expect_success 'empty reflog' '
	test_when_finished "rm -rf empty" &&
	git init empty &&
	test_commit -C empty A &&
	test-tool ref-store main create-reflog refs/heads/foo &&
	git -C empty reflog expire --all 2>err &&
	test_must_be_empty err
'

test_expect_success 'list reflogs' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		git reflog list >actual &&
		test_must_be_empty actual &&

		test_commit A &&
		cat >expect <<-EOF &&
		HEAD
		refs/heads/main
		EOF
		git reflog list >actual &&
		test_cmp expect actual &&

		git branch b &&
		cat >expect <<-EOF &&
		HEAD
		refs/heads/b
		refs/heads/main
		EOF
		git reflog list >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'list reflogs with worktree' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_commit A &&
		git worktree add wt &&
		git -c core.logAllRefUpdates=always \
			update-ref refs/worktree/main HEAD &&
		git -c core.logAllRefUpdates=always \
			update-ref refs/worktree/per-worktree HEAD &&
		git -c core.logAllRefUpdates=always -C wt \
			update-ref refs/worktree/per-worktree HEAD &&
		git -c core.logAllRefUpdates=always -C wt \
			update-ref refs/worktree/worktree HEAD &&

		cat >expect <<-EOF &&
		HEAD
		refs/heads/main
		refs/heads/wt
		refs/worktree/main
		refs/worktree/per-worktree
		EOF
		git reflog list >actual &&
		test_cmp expect actual &&

		cat >expect <<-EOF &&
		HEAD
		refs/heads/main
		refs/heads/wt
		refs/worktree/per-worktree
		refs/worktree/worktree
		EOF
		git -C wt reflog list >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'reflog list returns error with additional args' '
	cat >expect <<-EOF &&
	error: list does not accept arguments: ${SQ}bogus${SQ}
	EOF
	test_must_fail git reflog list bogus 2>err &&
	test_cmp expect err
'

test_expect_success 'reflog for symref with unborn target can be listed' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit A &&
		git symbolic-ref HEAD refs/heads/unborn &&
		cat >expect <<-EOF &&
		HEAD
		refs/heads/main
		EOF
		git reflog list >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'reflog with invalid object ID can be listed' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit A &&
		test-tool ref-store main update-ref msg refs/heads/missing \
			$(test_oid deadbeef) "$ZERO_OID" REF_SKIP_OID_VERIFICATION &&
		cat >expect <<-EOF &&
		HEAD
		refs/heads/main
		refs/heads/missing
		EOF
		git reflog list >actual &&
		test_cmp expect actual
	)
'

test_done
