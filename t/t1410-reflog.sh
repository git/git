#!/bin/sh
#
# Copyright (c) 2007 Junio C Hamano
#

test_description='Test prune and reflog expiration'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

check_have () {
	gaah= &&
	for N in "$@"
	do
		eval "o=\$$N" && but cat-file -t $o || {
			echo Gaah $N
			gaah=$N
			break
		}
	done &&
	test -z "$gaah"
}

check_fsck () {
	but fsck --full >fsck.output
	case "$1" in
	'')
		test_must_be_empty fsck.output ;;
	*)
		test_i18ngrep "$1" fsck.output ;;
	esac
}

corrupt () {
	mv .but/objects/$(test_oid_to_path $1) .but/$1
}

recover () {
	aa=$(echo $1 | cut -c 1-2)
	mkdir -p .but/objects/$aa
	mv .but/$1 .but/objects/$(test_oid_to_path $1)
}

check_dont_have () {
	gaah= &&
	for N in "$@"
	do
		eval "o=\$$N"
		but cat-file -t $o && {
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
	but add . &&

	test_tick && but cummit -m rabbit &&
	H=$(but rev-parse --verify HEAD) &&
	A=$(but rev-parse --verify HEAD:A) &&
	B=$(but rev-parse --verify HEAD:A/B) &&
	C=$(but rev-parse --verify HEAD:C) &&
	D=$(but rev-parse --verify HEAD:A/D) &&
	E=$(but rev-parse --verify HEAD:A/B/E) &&
	check_fsck &&

	test_chmod +x C &&
	but add C &&
	test_tick && but cummit -m dragon &&
	L=$(but rev-parse --verify HEAD) &&
	check_fsck &&

	rm -f C A/B/E &&
	echo snake >F &&
	echo horse >A/G &&
	but add F A/G &&
	test_tick && but cummit -a -m sheep &&
	F=$(but rev-parse --verify HEAD:F) &&
	G=$(but rev-parse --verify HEAD:A/G) &&
	I=$(but rev-parse --verify HEAD:A) &&
	J=$(but rev-parse --verify HEAD) &&
	check_fsck &&

	rm -f A/G &&
	test_tick && but cummit -a -m monkey &&
	K=$(but rev-parse --verify HEAD) &&
	check_fsck &&

	check_have A B C D E F G H I J K L &&

	but prune &&

	check_have A B C D E F G H I J K L &&

	check_fsck &&

	but reflog refs/heads/main >output &&
	test_line_count = 4 output
'

test_expect_success 'correct usage on sub-command -h' '
	test_expect_code 129 but reflog expire -h >err &&
	grep "but reflog expire" err
'

test_expect_success 'correct usage on "but reflog show -h"' '
	test_expect_code 129 but reflog show -h >err &&
	grep -F "but reflog [show]" err
'

test_expect_success 'pass through -- to sub-command' '
	test_when_finished "rm -rf repo" &&
	but init repo &&
	test_cummit -C repo message --a-file contents dash-tag &&

	but -C repo reflog show -- --does-not-exist >out &&
	test_must_be_empty out &&
	but -C repo reflog show >expect &&
	but -C repo reflog show -- --a-file >actual &&
	test_cmp expect actual
'

test_expect_success rewind '
	test_tick && but reset --hard HEAD~2 &&
	test -f C &&
	test -f A/B/E &&
	! test -f F &&
	! test -f A/G &&

	check_have A B C D E F G H I J K L &&

	but prune &&

	check_have A B C D E F G H I J K L &&

	but reflog refs/heads/main >output &&
	test_line_count = 5 output
'

test_expect_success 'corrupt and check' '

	corrupt $F &&
	check_fsck "missing blob $F"

'

test_expect_success 'reflog expire --dry-run should not touch reflog' '

	but reflog expire --dry-run \
		--expire=$(($test_tick - 10000)) \
		--expire-unreachable=$(($test_tick - 10000)) \
		--stale-fix \
		--all &&

	but reflog refs/heads/main >output &&
	test_line_count = 5 output &&

	check_fsck "missing blob $F"
'

test_expect_success 'reflog expire' '

	but reflog expire --verbose \
		--expire=$(($test_tick - 10000)) \
		--expire-unreachable=$(($test_tick - 10000)) \
		--stale-fix \
		--all &&

	but reflog refs/heads/main >output &&
	test_line_count = 2 output &&

	check_fsck "dangling cummit $K"
'

test_expect_success '--stale-fix handles missing objects generously' '
	but -c core.logAllRefUpdates=false fast-import --date-format=now <<-EOS &&
	cummit refs/heads/stale-fix
	mark :1
	cummitter Author <a@uth.or> now
	data <<EOF
	start stale fix
	EOF
	M 100644 inline file
	data <<EOF
	contents
	EOF
	cummit refs/heads/stale-fix
	cummitter Author <a@uth.or> now
	data <<EOF
	stale fix branch tip
	EOF
	from :1
	EOS

	parent_oid=$(but rev-parse stale-fix^) &&
	test_when_finished "recover $parent_oid" &&
	corrupt $parent_oid &&
	but reflog expire --stale-fix
'

test_expect_success 'prune and fsck' '

	but prune &&
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
	but cummit -m rat C &&

	echo 2 > C &&
	test_tick &&
	but cummit -m ox C &&

	echo 3 > C &&
	test_tick &&
	but cummit -m tiger C &&

	HEAD_entry_count=$(but reflog | wc -l) &&
	main_entry_count=$(but reflog show main | wc -l) &&

	test $HEAD_entry_count = 5 &&
	test $main_entry_count = 5 &&


	but reflog delete main@{1} &&
	but reflog show main > output &&
	test_line_count = $(($main_entry_count - 1)) output &&
	test $HEAD_entry_count = $(but reflog | wc -l) &&
	! grep ox < output &&

	main_entry_count=$(wc -l < output) &&

	but reflog delete HEAD@{1} &&
	test $(($HEAD_entry_count -1)) = $(but reflog | wc -l) &&
	test $main_entry_count = $(but reflog show main | wc -l) &&

	HEAD_entry_count=$(but reflog | wc -l) &&

	but reflog delete main@{07.04.2005.15:15:00.-0700} &&
	but reflog show main > output &&
	test_line_count = $(($main_entry_count - 1)) output &&
	! grep dragon < output

'

test_expect_success 'rewind2' '

	test_tick && but reset --hard HEAD~2 &&
	but reflog refs/heads/main >output &&
	test_line_count = 4 output
'

test_expect_success '--expire=never' '

	but reflog expire --verbose \
		--expire=never \
		--expire-unreachable=never \
		--all &&
	but reflog refs/heads/main >output &&
	test_line_count = 4 output
'

test_expect_success 'gc.reflogexpire=never' '
	test_config gc.reflogexpire never &&
	test_config gc.reflogexpireunreachable never &&

	but reflog expire --verbose --all >output &&
	test_line_count = 9 output &&

	but reflog refs/heads/main >output &&
	test_line_count = 4 output
'

test_expect_success 'gc.reflogexpire=false' '
	test_config gc.reflogexpire false &&
	test_config gc.reflogexpireunreachable false &&

	but reflog expire --verbose --all &&
	but reflog refs/heads/main >output &&
	test_line_count = 4 output

'

test_expect_success 'but reflog expire unknown reference' '
	test_config gc.reflogexpire never &&
	test_config gc.reflogexpireunreachable never &&

	test_must_fail but reflog expire main@{123} 2>stderr &&
	test_i18ngrep "points nowhere" stderr &&
	test_must_fail but reflog expire does-not-exist 2>stderr &&
	test_i18ngrep "points nowhere" stderr
'

test_expect_success 'checkout should not delete log for packed ref' '
	test $(but reflog main | wc -l) = 4 &&
	but branch foo &&
	but pack-refs --all &&
	but checkout foo &&
	test $(but reflog main | wc -l) = 4
'

test_expect_success 'stale dirs do not cause d/f conflicts (reflogs on)' '
	test_when_finished "but branch -d one || but branch -d one/two" &&

	but branch one/two main &&
	echo "one/two@{0} branch: Created from main" >expect &&
	but log -g --format="%gd %gs" one/two >actual &&
	test_cmp expect actual &&
	but branch -d one/two &&

	# now logs/refs/heads/one is a stale directory, but
	# we should move it out of the way to create "one" reflog
	but branch one main &&
	echo "one@{0} branch: Created from main" >expect &&
	but log -g --format="%gd %gs" one >actual &&
	test_cmp expect actual
'

test_expect_success 'stale dirs do not cause d/f conflicts (reflogs off)' '
	test_when_finished "but branch -d one || but branch -d one/two" &&

	but branch one/two main &&
	echo "one/two@{0} branch: Created from main" >expect &&
	but log -g --format="%gd %gs" one/two >actual &&
	test_cmp expect actual &&
	but branch -d one/two &&

	# same as before, but we only create a reflog for "one" if
	# it already exists, which it does not
	but -c core.logallrefupdates=false branch one main &&
	but log -g --format="%gd %gs" one >actual &&
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
	but checkout -b reflogskip &&
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
	done >.but/logs/refs/heads/reflogskip &&
	but rev-parse reflogskip@{73} >actual &&
	echo ${zf}03 >expect &&
	test_cmp expect actual
'

test_expect_success 'no segfaults for reflog containing non-cummit sha1s' '
	but update-ref --create-reflog -m "Creating ref" \
		refs/tests/tree-in-reflog HEAD &&
	but update-ref -m "Forcing tree" refs/tests/tree-in-reflog HEAD^{tree} &&
	but update-ref -m "Restoring to cummit" refs/tests/tree-in-reflog HEAD &&
	but reflog refs/tests/tree-in-reflog
'

test_expect_failure 'reflog with non-cummit entries displays all entries' '
	but reflog refs/tests/tree-in-reflog >actual &&
	test_line_count = 3 actual
'

# This test takes a lock on an individual ref; this is not supported in
# reftable.
test_expect_success REFFILES 'reflog expire operates on symref not referrent' '
	but branch --create-reflog the_symref &&
	but branch --create-reflog referrent &&
	but update-ref referrent HEAD &&
	but symbolic-ref refs/heads/the_symref refs/heads/referrent &&
	test_when_finished "rm -f .but/refs/heads/referrent.lock" &&
	touch .but/refs/heads/referrent.lock &&
	but reflog expire --expire=all the_symref
'

test_expect_success 'continue walking past root cummits' '
	but init orphanage &&
	(
		cd orphanage &&
		cat >expect <<-\EOF &&
		HEAD@{0} cummit (initial): orphan2-1
		HEAD@{1} cummit: orphan1-2
		HEAD@{2} cummit (initial): orphan1-1
		HEAD@{3} cummit (initial): initial
		EOF
		test_cummit initial &&
		but checkout --orphan orphan1 &&
		test_cummit orphan1-1 &&
		test_cummit orphan1-2 &&
		but checkout --orphan orphan2 &&
		test_cummit orphan2-1 &&
		but log -g --format="%gd %gs" >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'expire with multiple worktrees' '
	but init main-wt &&
	(
		cd main-wt &&
		test_tick &&
		test_cummit foo &&
		but  worktree add link-wt &&
		test_tick &&
		test_cummit -C link-wt foobar &&
		test_tick &&
		but reflog expire --verbose --all --expire=$test_tick &&
		test-tool ref-store worktree:link-wt for-each-reflog-ent HEAD >actual &&
		test_must_be_empty actual
	)
'

test_expect_success REFFILES 'empty reflog' '
	test_when_finished "rm -rf empty" &&
	but init empty &&
	test_cummit -C empty A &&
	>empty/.but/logs/refs/heads/foo &&
	but -C empty reflog expire --all 2>err &&
	test_must_be_empty err
'

test_done
