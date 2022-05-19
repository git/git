#!/bin/sh

test_description='reference transaction hooks'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success setup '
	test_cummit PRE &&
	PRE_OID=$(but rev-parse PRE) &&
	test_cummit POST &&
	POST_OID=$(but rev-parse POST)
'

test_expect_success 'hook allows updating ref if successful' '
	but reset --hard PRE &&
	test_hook reference-transaction <<-\EOF &&
		echo "$*" >>actual
	EOF
	cat >expect <<-EOF &&
		prepared
		cummitted
	EOF
	but update-ref HEAD POST &&
	test_cmp expect actual
'

test_expect_success 'hook aborts updating ref in prepared state' '
	but reset --hard PRE &&
	test_hook reference-transaction <<-\EOF &&
		if test "$1" = prepared
		then
			exit 1
		fi
	EOF
	test_must_fail but update-ref HEAD POST 2>err &&
	test_i18ngrep "ref updates aborted by hook" err
'

test_expect_success 'hook gets all queued updates in prepared state' '
	test_when_finished "rm actual" &&
	but reset --hard PRE &&
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
		$ZERO_OID $POST_OID HEAD
		$ZERO_OID $POST_OID refs/heads/main
	EOF
	but update-ref HEAD POST <<-EOF &&
		update HEAD $ZERO_OID $POST_OID
		update refs/heads/main $ZERO_OID $POST_OID
	EOF
	test_cmp expect actual
'

test_expect_success 'hook gets all queued updates in cummitted state' '
	test_when_finished "rm actual" &&
	but reset --hard PRE &&
	test_hook reference-transaction <<-\EOF &&
		if test "$1" = cummitted
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
	but update-ref HEAD POST &&
	test_cmp expect actual
'

test_expect_success 'hook gets all queued updates in aborted state' '
	test_when_finished "rm actual" &&
	but reset --hard PRE &&
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
	but update-ref --stdin <<-EOF &&
		start
		update HEAD POST $ZERO_OID
		update refs/heads/main POST $ZERO_OID
		abort
	EOF
	test_cmp expect actual
'

test_expect_success 'interleaving hook calls succeed' '
	test_when_finished "rm -r target-repo.but" &&

	but init --bare target-repo.but &&

	test_hook -C target-repo.but reference-transaction <<-\EOF &&
		echo $0 "$@" >>actual
	EOF

	test_hook -C target-repo.but update <<-\EOF &&
		echo $0 "$@" >>actual
	EOF

	cat >expect <<-EOF &&
		hooks/update refs/tags/PRE $ZERO_OID $PRE_OID
		hooks/reference-transaction prepared
		hooks/reference-transaction cummitted
		hooks/update refs/tags/POST $ZERO_OID $POST_OID
		hooks/reference-transaction prepared
		hooks/reference-transaction cummitted
	EOF

	but push ./target-repo.but PRE POST &&
	test_cmp expect target-repo.but/actual
'

test_done
