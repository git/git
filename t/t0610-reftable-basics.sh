#!/bin/sh
#
# Copyright (c) 2020 Google LLC
#

test_description='reftable basics'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME
GIT_TEST_DEFAULT_REF_FORMAT=reftable
export GIT_TEST_DEFAULT_REF_FORMAT

. ./test-lib.sh

INVALID_OID=$(test_oid 001)

test_expect_success 'init: creates basic reftable structures' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_path_is_dir repo/.git/reftable &&
	test_path_is_file repo/.git/reftable/tables.list &&
	echo reftable >expect &&
	git -C repo rev-parse --show-ref-format >actual &&
	test_cmp expect actual
'

test_expect_success 'init: sha256 object format via environment variable' '
	test_when_finished "rm -rf repo" &&
	GIT_DEFAULT_HASH=sha256 git init repo &&
	cat >expect <<-EOF &&
	sha256
	reftable
	EOF
	git -C repo rev-parse --show-object-format --show-ref-format >actual &&
	test_cmp expect actual
'

test_expect_success 'init: sha256 object format via option' '
	test_when_finished "rm -rf repo" &&
	git init --object-format=sha256 repo &&
	cat >expect <<-EOF &&
	sha256
	reftable
	EOF
	git -C repo rev-parse --show-object-format --show-ref-format >actual &&
	test_cmp expect actual
'

test_expect_success 'init: reinitializing reftable backend succeeds' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_commit -C repo A &&

	git -C repo for-each-ref >expect &&
	git init --ref-format=reftable repo &&
	git -C repo for-each-ref >actual &&
	test_cmp expect actual
'

test_expect_success 'init: reinitializing files with reftable backend fails' '
	test_when_finished "rm -rf repo" &&
	git init --ref-format=files repo &&
	test_commit -C repo file &&

	cp repo/.git/HEAD expect &&
	test_must_fail git init --ref-format=reftable repo &&
	test_cmp expect repo/.git/HEAD
'

test_expect_success 'init: reinitializing reftable with files backend fails' '
	test_when_finished "rm -rf repo" &&
	git init --ref-format=reftable repo &&
	test_commit -C repo file &&

	cp repo/.git/HEAD expect &&
	test_must_fail git init --ref-format=files repo &&
	test_cmp expect repo/.git/HEAD
'

test_expect_perms () {
	local perms="$1" &&
	local file="$2" &&
	local actual="$(ls -l "$file")" &&

	case "$actual" in
	$perms*)
		: happy
		;;
	*)
		echo "$(basename $2) is not $perms but $actual"
		false
		;;
	esac
}

test_expect_reftable_perms () {
	local umask="$1"
	local shared="$2"
	local expect="$3"

	test_expect_success POSIXPERM "init: honors --shared=$shared with umask $umask" '
		test_when_finished "rm -rf repo" &&
		(
			umask $umask &&
			git init --shared=$shared repo
		) &&
		test_expect_perms "$expect" repo/.git/reftable/tables.list &&
		for table in repo/.git/reftable/*.ref
		do
			test_expect_perms "$expect" "$table" ||
			return 1
		done
	'

	test_expect_success POSIXPERM "pack-refs: honors --shared=$shared with umask $umask" '
		test_when_finished "rm -rf repo" &&
		(
			umask $umask &&
			git init --shared=$shared repo &&
			test_commit -C repo A &&
			test_line_count = 2 repo/.git/reftable/tables.list &&
			git -C repo pack-refs
		) &&
		test_expect_perms "$expect" repo/.git/reftable/tables.list &&
		for table in repo/.git/reftable/*.ref
		do
			test_expect_perms "$expect" "$table" ||
			return 1
		done
	'
}

test_expect_reftable_perms 002 umask "-rw-rw-r--"
test_expect_reftable_perms 022 umask "-rw-r--r--"
test_expect_reftable_perms 027 umask "-rw-r-----"

test_expect_reftable_perms 002 group "-rw-rw-r--"
test_expect_reftable_perms 022 group "-rw-rw-r--"
test_expect_reftable_perms 027 group "-rw-rw----"

test_expect_reftable_perms 002 world "-rw-rw-r--"
test_expect_reftable_perms 022 world "-rw-rw-r--"
test_expect_reftable_perms 027 world "-rw-rw-r--"

test_expect_success 'clone: can clone reftable repository' '
	test_when_finished "rm -rf repo clone" &&
	git init repo &&
	test_commit -C repo message1 file1 &&

	git clone repo cloned &&
	echo reftable >expect &&
	git -C cloned rev-parse --show-ref-format >actual &&
	test_cmp expect actual &&
	test_path_is_file cloned/file1
'

test_expect_success 'clone: can clone reffiles into reftable repository' '
	test_when_finished "rm -rf reffiles reftable" &&
	git init --ref-format=files reffiles &&
	test_commit -C reffiles A &&
	git clone --ref-format=reftable ./reffiles reftable &&

	git -C reffiles rev-parse HEAD >expect &&
	git -C reftable rev-parse HEAD >actual &&
	test_cmp expect actual &&

	git -C reftable rev-parse --show-ref-format >actual &&
	echo reftable >expect &&
	test_cmp expect actual &&

	git -C reffiles rev-parse --show-ref-format >actual &&
	echo files >expect &&
	test_cmp expect actual
'

test_expect_success 'clone: can clone reftable into reffiles repository' '
	test_when_finished "rm -rf reffiles reftable" &&
	git init --ref-format=reftable reftable &&
	test_commit -C reftable A &&
	git clone --ref-format=files ./reftable reffiles &&

	git -C reftable rev-parse HEAD >expect &&
	git -C reffiles rev-parse HEAD >actual &&
	test_cmp expect actual &&

	git -C reftable rev-parse --show-ref-format >actual &&
	echo reftable >expect &&
	test_cmp expect actual &&

	git -C reffiles rev-parse --show-ref-format >actual &&
	echo files >expect &&
	test_cmp expect actual
'

test_expect_success 'ref transaction: corrupted tables cause failure' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit file1 &&
		for f in .git/reftable/*.ref
		do
			: >"$f" || return 1
		done &&
		test_must_fail git update-ref refs/heads/main HEAD
	)
'

test_expect_success 'ref transaction: corrupted tables.list cause failure' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit file1 &&
		echo garbage >.git/reftable/tables.list &&
		test_must_fail git update-ref refs/heads/main HEAD
	)
'

test_expect_success 'ref transaction: refuses to write ref causing F/D conflict' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_commit -C repo file &&
	test_must_fail git -C repo update-ref refs/heads/main/forbidden
'

test_expect_success 'ref transaction: deleting ref with invalid name fails' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_commit -C repo file &&
	test_must_fail git -C repo update-ref -d ../../my-private-file
'

test_expect_success 'ref transaction: can skip object ID verification' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_must_fail test-tool -C repo ref-store main update-ref msg refs/heads/branch $INVALID_OID $ZERO_OID 0 &&
	test-tool -C repo ref-store main update-ref msg refs/heads/branch $INVALID_OID $ZERO_OID REF_SKIP_OID_VERIFICATION
'

test_expect_success 'ref transaction: updating same ref multiple times fails' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_commit -C repo A &&
	cat >updates <<-EOF &&
	update refs/heads/main $A
	update refs/heads/main $A
	EOF
	cat >expect <<-EOF &&
	fatal: multiple updates for ref ${SQ}refs/heads/main${SQ} not allowed
	EOF
	test_must_fail git -C repo update-ref --stdin <updates 2>err &&
	test_cmp expect err
'

test_expect_success 'ref transaction: can delete symbolic self-reference with git-symbolic-ref(1)' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	git -C repo symbolic-ref refs/heads/self refs/heads/self &&
	git -C repo symbolic-ref -d refs/heads/self
'

test_expect_success 'ref transaction: deleting symbolic self-reference without --no-deref fails' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	git -C repo symbolic-ref refs/heads/self refs/heads/self &&
	cat >expect <<-EOF &&
	error: multiple updates for ${SQ}refs/heads/self${SQ} (including one via symref ${SQ}refs/heads/self${SQ}) are not allowed
	EOF
	test_must_fail git -C repo update-ref -d refs/heads/self 2>err &&
	test_cmp expect err
'

test_expect_success 'ref transaction: deleting symbolic self-reference with --no-deref succeeds' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	git -C repo symbolic-ref refs/heads/self refs/heads/self &&
	git -C repo update-ref -d --no-deref refs/heads/self
'

test_expect_success 'ref transaction: creating symbolic ref fails with F/D conflict' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_commit -C repo A &&
	cat >expect <<-EOF &&
	error: ${SQ}refs/heads/main${SQ} exists; cannot create ${SQ}refs/heads${SQ}
	EOF
	test_must_fail git -C repo symbolic-ref refs/heads refs/heads/foo 2>err &&
	test_cmp expect err
'

test_expect_success 'ref transaction: ref deletion' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit file &&
		HEAD_OID=$(git show-ref -s --verify HEAD) &&
		cat >expect <<-EOF &&
		$HEAD_OID refs/heads/main
		$HEAD_OID refs/tags/file
		EOF
		git show-ref >actual &&
		test_cmp expect actual &&

		test_must_fail git update-ref -d refs/tags/file $INVALID_OID &&
		git show-ref >actual &&
		test_cmp expect actual &&

		git update-ref -d refs/tags/file $HEAD_OID &&
		echo "$HEAD_OID refs/heads/main" >expect &&
		git show-ref >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'ref transaction: writes cause auto-compaction' '
	test_when_finished "rm -rf repo" &&

	git init repo &&
	test_line_count = 1 repo/.git/reftable/tables.list &&

	test_commit -C repo --no-tag A &&
	test_line_count = 1 repo/.git/reftable/tables.list &&

	test_commit -C repo --no-tag B &&
	test_line_count = 1 repo/.git/reftable/tables.list
'

test_expect_success 'ref transaction: env var disables compaction' '
	test_when_finished "rm -rf repo" &&

	git init repo &&
	test_commit -C repo A &&

	start=$(wc -l <repo/.git/reftable/tables.list) &&
	iterations=5 &&
	expected=$((start + iterations)) &&

	for i in $(test_seq $iterations)
	do
		GIT_TEST_REFTABLE_AUTOCOMPACTION=false \
		git -C repo update-ref branch-$i HEAD || return 1
	done &&
	test_line_count = $expected repo/.git/reftable/tables.list &&

	git -C repo update-ref foo HEAD &&
	test_line_count -lt $expected repo/.git/reftable/tables.list
'

test_expect_success 'ref transaction: alternating table sizes are compacted' '
	test_when_finished "rm -rf repo" &&

	git init repo &&
	test_commit -C repo A &&
	for i in $(test_seq 5)
	do
		git -C repo branch -f foo &&
		git -C repo branch -d foo || return 1
	done &&
	test_line_count = 2 repo/.git/reftable/tables.list
'

check_fsync_events () {
	local trace="$1" &&
	shift &&

	cat >expect &&
	sed -n \
		-e '/^{"event":"counter",.*"category":"fsync",/ {
			s/.*"category":"fsync",//;
			s/}$//;
			p;
		}' \
		<"$trace" >actual &&
	test_cmp expect actual
}

test_expect_success 'ref transaction: writes are synced' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_commit -C repo initial &&

	GIT_TRACE2_EVENT="$(pwd)/trace2.txt" \
	GIT_TEST_FSYNC=true \
		git -C repo -c core.fsync=reference \
		-c core.fsyncMethod=fsync update-ref refs/heads/branch HEAD &&
	check_fsync_events trace2.txt <<-EOF
	"name":"hardware-flush","count":4
	EOF
'

test_expect_success 'ref transaction: empty transaction in empty repo' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_commit -C repo --no-tag A &&
	git -C repo update-ref -d refs/heads/main &&
	test-tool -C repo ref-store main delete-refs REF_NO_DEREF msg HEAD &&
	git -C repo update-ref --stdin <<-EOF
	prepare
	commit
	EOF
'

test_expect_success 'ref transaction: fails gracefully when auto compaction fails' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_commit A &&
		for i in $(test_seq 10)
		do
			git branch branch-$i &&
			for table in .git/reftable/*.ref
			do
				touch "$table.lock" || exit 1
			done ||
			exit 1
		done &&
		test_line_count = 10 .git/reftable/tables.list
	)
'

test_expect_success 'ref transaction: timeout acquiring tables.list lock' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		>.git/reftable/tables.list.lock &&
		test_must_fail git update-ref refs/heads/branch HEAD 2>err &&
		test_grep "cannot lock references" err
	)
'

test_expect_success 'ref transaction: retry acquiring tables.list lock' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		LOCK=.git/reftable/tables.list.lock &&
		>$LOCK &&
		{
			( sleep 1 && rm -f $LOCK ) &
		} &&
		git -c reftable.lockTimeout=5000 update-ref refs/heads/branch HEAD
	)
'

# This test fails most of the time on Cygwin systems. The root cause is
# that Windows does not allow us to rename the "tables.list.lock" file into
# place when "tables.list" is open for reading by a concurrent process. We have
# worked around that in our MinGW-based rename emulation, but the Cygwin
# emulation seems to be insufficient.
test_expect_success !CYGWIN 'ref transaction: many concurrent writers' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		# Set a high timeout. While a couple of seconds should be
		# plenty, using the address sanitizer will significantly slow
		# us down here. So we are aiming way higher than you would ever
		# think is necessary just to keep us from flaking. We could
		# also lock indefinitely by passing -1, but that could
		# potentially block CI jobs indefinitely if there was a bug
		# here.
		git config set reftable.lockTimeout 300000 &&
		test_commit --no-tag initial &&

		head=$(git rev-parse HEAD) &&
		for i in $(test_seq 100)
		do
			printf "%s commit\trefs/heads/branch-%s\n" "$head" "$i" ||
			return 1
		done >expect &&
		printf "%s commit\trefs/heads/main\n" "$head" >>expect &&

		for i in $(test_seq 100)
		do
			{ git update-ref refs/heads/branch-$i HEAD& } ||
			return 1
		done &&

		wait &&
		git for-each-ref --sort=v:refname >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'pack-refs: compacts tables' '
	test_when_finished "rm -rf repo" &&
	git init repo &&

	test_commit -C repo A &&
	ls -1 repo/.git/reftable >table-files &&
	test_line_count = 3 table-files &&
	test_line_count = 2 repo/.git/reftable/tables.list &&

	git -C repo pack-refs &&
	ls -1 repo/.git/reftable >table-files &&
	test_line_count = 2 table-files &&
	test_line_count = 1 repo/.git/reftable/tables.list
'

test_expect_success 'pack-refs: compaction raises locking errors' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_commit -C repo A &&
	touch repo/.git/reftable/tables.list.lock &&
	cat >expect <<-EOF &&
	error: unable to compact stack: data is locked
	EOF
	test_must_fail git -C repo pack-refs 2>err &&
	test_cmp expect err
'

for command in pack-refs gc "maintenance run --task=pack-refs"
do
test_expect_success "$command: auto compaction" '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_commit A &&

		# We need a bit of setup to ensure that git-gc(1) actually
		# triggers, and that it does not write anything to the refdb.
		git config gc.auto 1 &&
		git config gc.autoDetach 0 &&
		git config gc.reflogExpire never &&
		git config gc.reflogExpireUnreachable never &&
		test_oid blob17_1 | git hash-object -w --stdin &&

		# The tables should have been auto-compacted, and thus auto
		# compaction should not have to do anything.
		ls -1 .git/reftable >tables-expect &&
		test_line_count = 3 tables-expect &&
		git $command --auto &&
		ls -1 .git/reftable >tables-actual &&
		test_cmp tables-expect tables-actual &&

		test_oid blob17_2 | git hash-object -w --stdin &&

		# Lock all tables, write some refs. Auto-compaction will be
		# unable to compact tables and thus fails gracefully,
		# compacting only those tables which are not locked.
		ls .git/reftable/*.ref | sort |
		while read table
		do
			touch "$table.lock" &&
			basename "$table" >>tables.expect || exit 1
		done &&
		test_line_count = 2 .git/reftable/tables.list &&
		git branch B &&
		git branch C &&

		# The new tables are auto-compacted, but the locked tables are
		# left intact.
		test_line_count = 3 .git/reftable/tables.list &&
		head -n 2 .git/reftable/tables.list >tables.head &&
		test_cmp tables.expect tables.head &&

		rm .git/reftable/*.lock &&
		git $command --auto &&
		test_line_count = 1 .git/reftable/tables.list
	)
'
done

test_expect_success 'pack-refs: prunes stale tables' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	touch repo/.git/reftable/stale-table.ref &&
	git -C repo pack-refs &&
	test_path_is_missing repo/.git/reftable/stable-ref.ref
'

test_expect_success 'pack-refs: does not prune non-table files' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	touch repo/.git/reftable/garbage &&
	git -C repo pack-refs &&
	test_path_is_file repo/.git/reftable/garbage
'

test_expect_success 'packed-refs: writes are synced' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_commit -C repo initial &&
	test_line_count = 2 table-files &&

	: >trace2.txt &&
	GIT_TRACE2_EVENT="$(pwd)/trace2.txt" \
	GIT_TEST_FSYNC=true \
		git -C repo -c core.fsync=reference \
		-c core.fsyncMethod=fsync pack-refs &&
	check_fsync_events trace2.txt <<-EOF
	"name":"hardware-flush","count":2
	EOF
'

test_expect_success 'ref iterator: bogus names are flagged' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit --no-tag file &&
		test-tool ref-store main update-ref msg "refs/heads/bogus..name" $(git rev-parse HEAD) $ZERO_OID REF_SKIP_REFNAME_VERIFICATION &&

		cat >expect <<-EOF &&
		$ZERO_OID refs/heads/bogus..name 0xc
		$(git rev-parse HEAD) refs/heads/main 0x0
		EOF
		test-tool ref-store main for-each-ref "" >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'ref iterator: missing object IDs are not flagged' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test-tool ref-store main update-ref msg "refs/heads/broken-hash" $INVALID_OID $ZERO_OID REF_SKIP_OID_VERIFICATION &&

		cat >expect <<-EOF &&
		$INVALID_OID refs/heads/broken-hash 0x0
		EOF
		test-tool ref-store main for-each-ref "" >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'basic: commit and list refs' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_commit -C repo file &&
	test_write_lines refs/heads/main refs/tags/file >expect &&
	git -C repo for-each-ref --format="%(refname)" >actual &&
	test_cmp actual expect
'

test_expect_success 'basic: can write large commit message' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	perl -e "
		print \"this is a long commit message\" x 50000
	" >commit-msg &&
	git -C repo commit --allow-empty --file=../commit-msg
'

test_expect_success 'basic: show-ref fails with empty repository' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_must_fail git -C repo show-ref >actual &&
	test_must_be_empty actual
'

test_expect_success 'basic: can check out unborn branch' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	git -C repo checkout -b main
'

test_expect_success 'basic: peeled tags are stored' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	test_commit -C repo file &&
	git -C repo tag -m "annotated tag" test_tag HEAD &&
	for ref in refs/heads/main refs/tags/file refs/tags/test_tag refs/tags/test_tag^{}
	do
		echo "$(git -C repo rev-parse "$ref") $ref" || return 1
	done >expect &&
	git -C repo show-ref -d >actual &&
	test_cmp expect actual
'

test_expect_success 'basic: for-each-ref can print symrefs' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit file &&
		git branch &&
		git symbolic-ref refs/heads/sym refs/heads/main &&
		cat >expected <<-EOF &&
		refs/heads/main
		EOF
		git for-each-ref --format="%(symref)" refs/heads/sym >actual &&
		test_cmp expected actual
	)
'

test_expect_success 'basic: notes' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		write_script fake_editor <<-\EOF &&
		echo "$MSG" >"$1"
		echo "$MSG" >&2
		EOF

		test_commit 1st &&
		test_commit 2nd &&
		GIT_EDITOR=./fake_editor MSG=b4 git notes add &&
		GIT_EDITOR=./fake_editor MSG=b3 git notes edit &&
		echo b4 >expect &&
		git notes --ref commits@{1} show >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'basic: stash' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit file &&
		git stash list >expect &&
		test_line_count = 0 expect &&

		echo hoi >>file.t &&
		git stash push -m stashed &&
		git stash list >expect &&
		test_line_count = 1 expect &&

		git stash clear &&
		git stash list >expect &&
		test_line_count = 0 expect
	)
'

test_expect_success 'basic: cherry-pick' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit message1 file1 &&
		test_commit message2 file2 &&
		git branch source &&
		git checkout HEAD^ &&
		test_commit message3 file3 &&
		git cherry-pick source &&
		test_path_is_file file2
	)
'

test_expect_success 'basic: rebase' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit message1 file1 &&
		test_commit message2 file2 &&
		git branch source &&
		git checkout HEAD^ &&
		test_commit message3 file3 &&
		git rebase source &&
		test_path_is_file file2
	)
'

test_expect_success 'reflog: can delete separate reflog entries' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_commit file &&
		test_commit file2 &&
		test_commit file3 &&
		test_commit file4 &&
		git reflog >actual &&
		grep file3 actual &&

		git reflog delete HEAD@{1} &&
		git reflog >actual &&
		! grep file3 actual
	)
'

test_expect_success 'reflog: can switch to previous branch' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit file1 &&
		git checkout -b branch1 &&
		test_commit file2 &&
		git checkout -b branch2 &&
		git switch - &&
		git rev-parse --symbolic-full-name HEAD >actual &&
		echo refs/heads/branch1 >expect &&
		test_cmp actual expect
	)
'

test_expect_success 'reflog: copying branch writes reflog entry' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit file1 &&
		test_commit file2 &&
		oid=$(git rev-parse --short HEAD) &&
		git branch src &&
		cat >expect <<-EOF &&
		${oid} dst@{0}: Branch: copied refs/heads/src to refs/heads/dst
		${oid} dst@{1}: branch: Created from main
		EOF
		git branch -c src dst &&
		git reflog dst >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'reflog: renaming branch writes reflog entry' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		git symbolic-ref HEAD refs/heads/before &&
		test_commit file &&
		git show-ref >expected.refs &&
		sed s/before/after/g <expected.refs >expected &&
		git branch -M after &&
		git show-ref >actual &&
		test_cmp expected actual &&
		echo refs/heads/after >expected &&
		git symbolic-ref HEAD >actual &&
		test_cmp expected actual
	)
'

test_expect_success 'reflog: can store empty logs' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_must_fail test-tool ref-store main reflog-exists refs/heads/branch &&
		test-tool ref-store main create-reflog refs/heads/branch &&
		test-tool ref-store main reflog-exists refs/heads/branch &&
		test-tool ref-store main for-each-reflog-ent-reverse refs/heads/branch >actual &&
		test_must_be_empty actual
	)
'

test_expect_success 'reflog: expiry empties reflog' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		test_commit initial &&
		git checkout -b branch &&
		test_commit fileA &&
		test_commit fileB &&

		cat >expect <<-EOF &&
		commit: fileB
		commit: fileA
		branch: Created from HEAD
		EOF
		git reflog show --format="%gs" refs/heads/branch >actual &&
		test_cmp expect actual &&

		git reflog expire branch --expire=all &&
		git reflog show --format="%gs" refs/heads/branch >actual &&
		test_must_be_empty actual &&
		test-tool ref-store main reflog-exists refs/heads/branch
	)
'

test_expect_success 'reflog: can be deleted' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit initial &&
		test-tool ref-store main reflog-exists refs/heads/main &&
		test-tool ref-store main delete-reflog refs/heads/main &&
		test_must_fail test-tool ref-store main reflog-exists refs/heads/main
	)
'

test_expect_success 'reflog: garbage collection deletes reflog entries' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&

		for count in $(test_seq 1 10)
		do
			test_commit "number $count" file.t $count number-$count ||
			return 1
		done &&
		git reflog refs/heads/main >actual &&
		test_line_count = 10 actual &&
		grep "commit (initial): number 1" actual &&
		grep "commit: number 10" actual &&

		git gc &&
		git reflog refs/heads/main >actual &&
		test_line_count = 0 actual
	)
'

test_expect_success 'reflog: updates via HEAD update HEAD reflog' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit main-one &&
		git checkout -b new-branch &&
		test_commit new-one &&
		test_commit new-two &&

		echo new-one >expect &&
		git log -1 --format=%s HEAD@{1} >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'branch: copying branch with D/F conflict' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit A &&
		git branch branch &&
		cat >expect <<-EOF &&
		error: ${SQ}refs/heads/branch${SQ} exists; cannot create ${SQ}refs/heads/branch/moved${SQ}
		fatal: branch copy failed
		EOF
		test_must_fail git branch -c branch branch/moved 2>err &&
		test_cmp expect err
	)
'

test_expect_success 'branch: moving branch with D/F conflict' '
	test_when_finished "rm -rf repo" &&
	git init repo &&
	(
		cd repo &&
		test_commit A &&
		git branch branch &&
		git branch conflict &&
		cat >expect <<-EOF &&
		error: ${SQ}refs/heads/conflict${SQ} exists; cannot create ${SQ}refs/heads/conflict/moved${SQ}
		fatal: branch rename failed
		EOF
		test_must_fail git branch -m branch conflict/moved 2>err &&
		test_cmp expect err
	)
'

test_expect_success 'worktree: adding worktree creates separate stack' '
	test_when_finished "rm -rf repo worktree" &&
	git init repo &&
	test_commit -C repo A &&

	git -C repo worktree add ../worktree &&
	test_path_is_file repo/.git/worktrees/worktree/refs/heads &&
	echo "ref: refs/heads/.invalid" >expect &&
	test_cmp expect repo/.git/worktrees/worktree/HEAD &&
	test_path_is_dir repo/.git/worktrees/worktree/reftable &&
	test_path_is_file repo/.git/worktrees/worktree/reftable/tables.list
'

test_expect_success 'worktree: pack-refs in main repo packs main refs' '
	test_when_finished "rm -rf repo worktree" &&
	git init repo &&
	test_commit -C repo A &&

	GIT_TEST_REFTABLE_AUTOCOMPACTION=false \
	git -C repo worktree add ../worktree &&
	GIT_TEST_REFTABLE_AUTOCOMPACTION=false \
	git -C worktree update-ref refs/worktree/per-worktree HEAD &&

	test_line_count = 4 repo/.git/worktrees/worktree/reftable/tables.list &&
	test_line_count = 3 repo/.git/reftable/tables.list &&
	git -C repo pack-refs &&
	test_line_count = 4 repo/.git/worktrees/worktree/reftable/tables.list &&
	test_line_count = 1 repo/.git/reftable/tables.list
'

test_expect_success 'worktree: pack-refs in worktree packs worktree refs' '
	test_when_finished "rm -rf repo worktree" &&
	git init repo &&
	test_commit -C repo A &&

	GIT_TEST_REFTABLE_AUTOCOMPACTION=false \
	git -C repo worktree add ../worktree &&
	GIT_TEST_REFTABLE_AUTOCOMPACTION=false \
	git -C worktree update-ref refs/worktree/per-worktree HEAD &&

	test_line_count = 4 repo/.git/worktrees/worktree/reftable/tables.list &&
	test_line_count = 3 repo/.git/reftable/tables.list &&
	git -C worktree pack-refs &&
	test_line_count = 1 repo/.git/worktrees/worktree/reftable/tables.list &&
	test_line_count = 3 repo/.git/reftable/tables.list
'

test_expect_success 'worktree: creating shared ref updates main stack' '
	test_when_finished "rm -rf repo worktree" &&
	git init repo &&
	test_commit -C repo A &&

	git -C repo worktree add ../worktree &&
	git -C repo pack-refs &&
	git -C worktree pack-refs &&
	test_line_count = 1 repo/.git/worktrees/worktree/reftable/tables.list &&
	test_line_count = 1 repo/.git/reftable/tables.list &&

	GIT_TEST_REFTABLE_AUTOCOMPACTION=false \
	git -C worktree update-ref refs/heads/shared HEAD &&
	test_line_count = 1 repo/.git/worktrees/worktree/reftable/tables.list &&
	test_line_count = 2 repo/.git/reftable/tables.list
'

test_expect_success 'worktree: creating per-worktree ref updates worktree stack' '
	test_when_finished "rm -rf repo worktree" &&
	git init repo &&
	test_commit -C repo A &&

	git -C repo worktree add ../worktree &&
	git -C repo pack-refs &&
	git -C worktree pack-refs &&
	test_line_count = 1 repo/.git/worktrees/worktree/reftable/tables.list &&
	test_line_count = 1 repo/.git/reftable/tables.list &&

	git -C worktree update-ref refs/bisect/per-worktree HEAD &&
	test_line_count = 2 repo/.git/worktrees/worktree/reftable/tables.list &&
	test_line_count = 1 repo/.git/reftable/tables.list
'

test_expect_success 'worktree: creating per-worktree ref from main repo' '
	test_when_finished "rm -rf repo worktree" &&
	git init repo &&
	test_commit -C repo A &&

	git -C repo worktree add ../worktree &&
	git -C repo pack-refs &&
	git -C worktree pack-refs &&
	test_line_count = 1 repo/.git/worktrees/worktree/reftable/tables.list &&
	test_line_count = 1 repo/.git/reftable/tables.list &&

	git -C repo update-ref worktrees/worktree/refs/bisect/per-worktree HEAD &&
	test_line_count = 2 repo/.git/worktrees/worktree/reftable/tables.list &&
	test_line_count = 1 repo/.git/reftable/tables.list
'

test_expect_success 'worktree: creating per-worktree ref from second worktree' '
	test_when_finished "rm -rf repo wt1 wt2" &&
	git init repo &&
	test_commit -C repo A &&

	git -C repo worktree add ../wt1 &&
	git -C repo worktree add ../wt2 &&
	git -C repo pack-refs &&
	git -C wt1 pack-refs &&
	git -C wt2 pack-refs &&
	test_line_count = 1 repo/.git/worktrees/wt1/reftable/tables.list &&
	test_line_count = 1 repo/.git/worktrees/wt2/reftable/tables.list &&
	test_line_count = 1 repo/.git/reftable/tables.list &&

	git -C wt1 update-ref worktrees/wt2/refs/bisect/per-worktree HEAD &&
	test_line_count = 1 repo/.git/worktrees/wt1/reftable/tables.list &&
	test_line_count = 2 repo/.git/worktrees/wt2/reftable/tables.list &&
	test_line_count = 1 repo/.git/reftable/tables.list
'

test_expect_success 'worktree: can create shared and per-worktree ref in one transaction' '
	test_when_finished "rm -rf repo worktree" &&
	git init repo &&
	test_commit -C repo A &&

	git -C repo worktree add ../worktree &&
	git -C repo pack-refs &&
	git -C worktree pack-refs &&
	test_line_count = 1 repo/.git/worktrees/worktree/reftable/tables.list &&
	test_line_count = 1 repo/.git/reftable/tables.list &&

	cat >stdin <<-EOF &&
	create worktrees/worktree/refs/bisect/per-worktree HEAD
	create refs/branches/shared HEAD
	EOF
	git -C repo update-ref --stdin <stdin &&
	test_line_count = 2 repo/.git/worktrees/worktree/reftable/tables.list &&
	test_line_count = 2 repo/.git/reftable/tables.list
'

test_expect_success 'worktree: can access common refs' '
	test_when_finished "rm -rf repo worktree" &&
	git init repo &&
	test_commit -C repo file1 &&
	git -C repo branch branch1 &&
	git -C repo worktree add ../worktree &&

	echo refs/heads/worktree >expect &&
	git -C worktree symbolic-ref HEAD >actual &&
	test_cmp expect actual &&
	git -C worktree checkout branch1
'

test_expect_success 'worktree: adds worktree with detached HEAD' '
	test_when_finished "rm -rf repo worktree" &&

	git init repo &&
	test_commit -C repo A &&
	git -C repo rev-parse main >expect &&

	git -C repo worktree add --detach ../worktree main &&
	git -C worktree rev-parse HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'fetch: accessing FETCH_HEAD special ref works' '
	test_when_finished "rm -rf repo sub" &&

	git init sub &&
	test_commit -C sub two &&
	git -C sub rev-parse HEAD >expect &&

	git init repo &&
	test_commit -C repo one &&
	git -C repo fetch ../sub &&
	git -C repo rev-parse FETCH_HEAD >actual &&
	test_cmp expect actual
'

test_done
