#!/bin/sh

test_description='reference transaction hooks'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	test_commit PRE &&
	PRE_OID=$(git rev-parse PRE) &&
	test_commit POST &&
	POST_OID=$(git rev-parse POST)
'

test_expect_success 'hook allows updating ref if successful' '
	git reset --hard PRE &&
	test_hook reference-transaction <<-\EOF &&
		echo "$*" >>actual
	EOF
	cat >expect <<-EOF &&
		preparing
		prepared
		committed
	EOF
	git update-ref HEAD POST &&
	test_cmp expect actual
'

test_expect_success 'hook aborts updating ref in preparing state' '
	git reset --hard PRE &&
	test_hook reference-transaction <<-\EOF &&
		if test "$1" = preparing
		then
			exit 1
		fi
	EOF
	test_must_fail git update-ref HEAD POST 2>err &&
	test_grep "in '\''preparing'\'' phase, update aborted by the reference-transaction hook" err
'

test_expect_success 'hook aborts updating ref in prepared state' '
	git reset --hard PRE &&
	test_hook reference-transaction <<-\EOF &&
		if test "$1" = prepared
		then
			exit 1
		fi
	EOF
	test_must_fail git update-ref HEAD POST 2>err &&
	test_grep "in '\''prepared'\'' phase, update aborted by the reference-transaction hook" err
'

test_expect_success 'hook gets all queued updates in prepared state' '
	test_when_finished "rm actual" &&
	git reset --hard PRE &&
	test_hook reference-transaction <<-\EOF &&
		if test "$1" = prepared
		then
			while read -r line
			do
				printf "%s\n" "$line"
			done >actual
		fi
	EOF
	cat >expect <<-EOF &&
		$ZERO_OID $POST_OID refs/heads/main
	EOF
	git update-ref HEAD POST <<-EOF &&
		update HEAD $ZERO_OID $POST_OID
		update refs/heads/main $ZERO_OID $POST_OID
	EOF
	test_cmp expect actual
'

test_expect_success 'hook gets all queued updates in committed state' '
	test_when_finished "rm actual" &&
	git reset --hard PRE &&
	test_hook reference-transaction <<-\EOF &&
		if test "$1" = committed
		then
			while read -r line
			do
				printf "%s\n" "$line"
			done >actual
		fi
	EOF
	cat >expect <<-EOF &&
		$ZERO_OID $POST_OID refs/heads/main
	EOF
	git update-ref HEAD POST &&
	test_cmp expect actual
'

test_expect_success 'hook gets both updates when renaming a branch' '
	test_when_finished "rm -f actual" &&
	git branch old PRE &&
	test_hook reference-transaction <<-\EOF &&
		echo "$1" >>actual &&
		cat >>actual
	EOF
	cat >expect <<-EOF &&
	preparing
	$PRE_OID $ZERO_OID refs/heads/old
	$ZERO_OID $PRE_OID refs/heads/new
	prepared
	$PRE_OID $ZERO_OID refs/heads/old
	$ZERO_OID $PRE_OID refs/heads/new
	committed
	$PRE_OID $ZERO_OID refs/heads/old
	$ZERO_OID $PRE_OID refs/heads/new
	EOF
	git branch -m old new &&
	test_cmp expect actual &&
	test_must_fail git rev-parse --verify refs/heads/old &&
	test_cmp_rev PRE refs/heads/new
'

test_expect_success 'hook gets destination update when copying a branch' '
	test_when_finished "rm -f actual" &&
	git branch copy-source PRE &&
	test_hook reference-transaction <<-\EOF &&
		echo "$1" >>actual &&
		cat >>actual
	EOF
	cat >expect <<-EOF &&
	preparing
	$ZERO_OID $PRE_OID refs/heads/copy-destination
	prepared
	$ZERO_OID $PRE_OID refs/heads/copy-destination
	committed
	$ZERO_OID $PRE_OID refs/heads/copy-destination
	EOF
	git branch -c copy-source copy-destination &&
	test_cmp expect actual &&
	test_cmp_rev PRE refs/heads/copy-source &&
	test_cmp_rev PRE refs/heads/copy-destination
'

test_expect_success 'hook gets overwritten values for forced rename and copy' '
	git branch force-old PRE &&
	git branch force-new POST &&
	git branch force-copy-source PRE &&
	git branch force-copy-destination POST &&
	test_hook reference-transaction <<-\EOF &&
		if test "$1" = committed
		then
			cat >>actual
		fi
	EOF
	git branch -M force-old force-new &&
	git branch -C force-copy-source force-copy-destination &&
	cat >expect <<-EOF &&
	$PRE_OID $ZERO_OID refs/heads/force-old
	$POST_OID $PRE_OID refs/heads/force-new
	$POST_OID $PRE_OID refs/heads/force-copy-destination
	EOF
	test_cmp expect actual
'

test_expect_success 'hook can abort a branch rename after preparation' '
	git branch abort-old PRE &&
	git branch abort-new POST &&
	git reflog show --format=%gs abort-old >old-log &&
	git reflog show --format=%gs abort-new >new-log &&
	test_hook reference-transaction <<-\EOF &&
		test "$1" != prepared
	EOF
	test_must_fail git branch -M abort-old abort-new &&
	test_cmp_rev PRE refs/heads/abort-old &&
	test_cmp_rev POST refs/heads/abort-new &&
	git reflog show --format=%gs abort-old >old-log-after &&
	git reflog show --format=%gs abort-new >new-log-after &&
	test_cmp old-log old-log-after &&
	test_cmp new-log new-log-after
'

test_expect_success 'hook can abort a D/F branch rename after preparation' '
	git branch df-old PRE &&
	git reflog show --format=%gs df-old >df-log &&
	test_hook reference-transaction <<-\EOF &&
		test "$1" != prepared
	EOF
	test_must_fail git branch -m df-old df-old/child &&
	test_cmp_rev PRE refs/heads/df-old &&
	test_must_fail git rev-parse --verify refs/heads/df-old/child &&
	git reflog show --format=%gs df-old >df-log-after &&
	test_cmp df-log df-log-after
'

test_expect_success 'hook can abort a reverse D/F rename after preparation' '
	git branch reverse/old PRE &&
	git reflog show --format=%gs reverse/old >reverse-log &&
	test_hook reference-transaction <<-\EOF &&
		test "$1" != prepared
	EOF
	test_must_fail git branch -m reverse/old reverse &&
	test_cmp_rev PRE refs/heads/reverse/old &&
	test_must_fail git rev-parse --verify refs/heads/reverse &&
	git reflog show --format=%gs reverse/old >reverse-log-after &&
	test_cmp reverse-log reverse-log-after
'

test_expect_success 'hook can abort a forced branch copy after preparation' '
	git branch copy-abort-old PRE &&
	git branch copy-abort-new POST &&
	git reflog show --format=%gs copy-abort-old >copy-old-log &&
	git reflog show --format=%gs copy-abort-new >copy-new-log &&
	test_hook reference-transaction <<-\EOF &&
		test "$1" != prepared
	EOF
	test_must_fail git branch -C copy-abort-old copy-abort-new &&
	test_cmp_rev PRE refs/heads/copy-abort-old &&
	test_cmp_rev POST refs/heads/copy-abort-new &&
	git reflog show --format=%gs copy-abort-old >copy-old-log-after &&
	git reflog show --format=%gs copy-abort-new >copy-new-log-after &&
	test_cmp copy-old-log copy-old-log-after &&
	test_cmp copy-new-log copy-new-log-after
'

test_expect_success 'branch rename detects an update during preparing hook' '
	git branch race-old PRE &&
	git branch race-new POST &&
	test_hook reference-transaction <<-\EOF &&
		marker=$(git rev-parse --git-path rename-race-once)
		if test "$1" = preparing && test ! -e "$marker"
		then
			>"$marker" &&
			git update-ref refs/heads/race-old POST
		fi
	EOF
	test_must_fail git branch -M race-old race-new &&
	test_cmp_rev POST refs/heads/race-old &&
	test_cmp_rev POST refs/heads/race-new
'

test_expect_success 'hook gets all queued updates in aborted state' '
	test_when_finished "rm actual" &&
	git reset --hard PRE &&
	test_hook reference-transaction <<-\EOF &&
		if test "$1" = aborted
		then
			while read -r line
			do
				printf "%s\n" "$line"
			done >actual
		fi
	EOF
	cat >expect <<-EOF &&
		$ZERO_OID $POST_OID HEAD
		$ZERO_OID $POST_OID refs/heads/main
	EOF
	git update-ref --stdin <<-EOF &&
		start
		update HEAD POST $ZERO_OID
		update refs/heads/main POST $ZERO_OID
		abort
	EOF
	test_cmp expect actual
'

test_expect_success 'interleaving hook calls succeed' '
	test_when_finished "rm -r target-repo.git" &&

	git init --bare target-repo.git &&

	test_hook -C target-repo.git reference-transaction <<-\EOF &&
		echo $0 "$@" >>actual
	EOF

	test_hook -C target-repo.git update <<-\EOF &&
		echo $0 "$@" >>actual
	EOF

	cat >expect <<-EOF &&
		hooks/update refs/tags/PRE $ZERO_OID $PRE_OID
		hooks/update refs/tags/POST $ZERO_OID $POST_OID
		hooks/reference-transaction preparing
		hooks/reference-transaction prepared
		hooks/reference-transaction committed
	EOF

	git push ./target-repo.git PRE POST &&
	test_cmp expect target-repo.git/actual
'

test_expect_success 'hook captures git-symbolic-ref updates' '
	test_when_finished "rm actual" &&

	test_hook reference-transaction <<-\EOF &&
		echo "$*" >>actual
		while read -r line
		do
			printf "%s\n" "$line"
		done >>actual
	EOF

	git symbolic-ref refs/heads/symref refs/heads/main &&

	cat >expect <<-EOF &&
	preparing
	$ZERO_OID ref:refs/heads/main refs/heads/symref
	prepared
	$ZERO_OID ref:refs/heads/main refs/heads/symref
	committed
	$ZERO_OID ref:refs/heads/main refs/heads/symref
	EOF

	test_cmp expect actual
'

test_expect_success 'hook gets all queued symref updates' '
	test_when_finished "rm actual" &&

	git update-ref refs/heads/branch $POST_OID &&
	git symbolic-ref refs/heads/symref refs/heads/main &&
	git symbolic-ref refs/heads/symrefd refs/heads/main &&
	git symbolic-ref refs/heads/symrefu refs/heads/main &&

	test_hook reference-transaction <<-\EOF &&
	echo "$*" >>actual
	while read -r line
	do
		printf "%s\n" "$line"
	done >>actual
	EOF

	# In the files backend, "delete" also triggers an additional transaction
	# update on the packed-refs backend, which constitutes additional reflog
	# entries.
	cat >expect <<-EOF &&
	preparing
	ref:refs/heads/main $ZERO_OID refs/heads/symref
	ref:refs/heads/main $ZERO_OID refs/heads/symrefd
	$ZERO_OID ref:refs/heads/main refs/heads/symrefc
	ref:refs/heads/main ref:refs/heads/branch refs/heads/symrefu
	EOF

	if test_have_prereq REFFILES
	then
		cat >>expect <<-EOF
		aborted
		$ZERO_OID $ZERO_OID refs/heads/symrefd
		EOF
	fi &&

	cat >>expect <<-EOF &&
	prepared
	ref:refs/heads/main $ZERO_OID refs/heads/symref
	ref:refs/heads/main $ZERO_OID refs/heads/symrefd
	$ZERO_OID ref:refs/heads/main refs/heads/symrefc
	ref:refs/heads/main ref:refs/heads/branch refs/heads/symrefu
	committed
	ref:refs/heads/main $ZERO_OID refs/heads/symref
	ref:refs/heads/main $ZERO_OID refs/heads/symrefd
	$ZERO_OID ref:refs/heads/main refs/heads/symrefc
	ref:refs/heads/main ref:refs/heads/branch refs/heads/symrefu
	EOF

	git update-ref --no-deref --stdin <<-EOF &&
	start
	symref-verify refs/heads/symref refs/heads/main
	symref-delete refs/heads/symrefd refs/heads/main
	symref-create refs/heads/symrefc refs/heads/main
	symref-update refs/heads/symrefu refs/heads/branch ref refs/heads/main
	prepare
	commit
	EOF
	test_cmp expect actual
'

test_done
