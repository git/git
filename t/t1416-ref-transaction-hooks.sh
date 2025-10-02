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
		prepared
		committed
	EOF
	git update-ref HEAD POST &&
	test_cmp expect actual
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
	test_grep "ref updates aborted by hook" err
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
	if test_have_prereq REFFILES
	then
		cat >expect <<-EOF
		aborted
		$ZERO_OID $ZERO_OID refs/heads/symrefd
		EOF
	else
		>expect
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
