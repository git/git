#!/bin/sh

test_description='reference transaction hooks'

. ./test-lib.sh

test_expect_success setup '
	mkdir -p .git/hooks &&
	test_commit PRE &&
	test_commit POST &&
	POST_OID=$(git rev-parse POST)
'

test_expect_success 'hook allows updating ref if successful' '
	test_when_finished "rm .git/hooks/reference-transaction" &&
	git reset --hard PRE &&
	write_script .git/hooks/reference-transaction <<-\EOF &&
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
	test_when_finished "rm .git/hooks/reference-transaction" &&
	git reset --hard PRE &&
	write_script .git/hooks/reference-transaction <<-\EOF &&
		if test "$1" = prepared
		then
			exit 1
		fi
	EOF
	test_must_fail git update-ref HEAD POST 2>err &&
	test_i18ngrep "ref updates aborted by hook" err
'

test_expect_success 'hook gets all queued updates in prepared state' '
	test_when_finished "rm .git/hooks/reference-transaction actual" &&
	git reset --hard PRE &&
	write_script .git/hooks/reference-transaction <<-\EOF &&
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
		$ZERO_OID $POST_OID refs/heads/master
	EOF
	git update-ref HEAD POST <<-EOF &&
		update HEAD $ZERO_OID $POST_OID
		update refs/heads/master $ZERO_OID $POST_OID
	EOF
	test_cmp expect actual
'

test_expect_success 'hook gets all queued updates in committed state' '
	test_when_finished "rm .git/hooks/reference-transaction actual" &&
	git reset --hard PRE &&
	write_script .git/hooks/reference-transaction <<-\EOF &&
		if test "$1" = committed
		then
			while read -r line
			do
				printf "%s\n" "$line"
			done >actual
		fi
	EOF
	cat >expect <<-EOF &&
		$ZERO_OID $POST_OID HEAD
		$ZERO_OID $POST_OID refs/heads/master
	EOF
	git update-ref HEAD POST &&
	test_cmp expect actual
'

test_expect_success 'hook gets all queued updates in aborted state' '
	test_when_finished "rm .git/hooks/reference-transaction actual" &&
	git reset --hard PRE &&
	write_script .git/hooks/reference-transaction <<-\EOF &&
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
		$ZERO_OID $POST_OID refs/heads/master
	EOF
	git update-ref --stdin <<-EOF &&
		start
		update HEAD POST $ZERO_OID
		update refs/heads/master POST $ZERO_OID
		abort
	EOF
	test_cmp expect actual
'

test_expect_success 'interleaving hook calls succeed' '
	test_when_finished "rm -r target-repo.git" &&

	git init --bare target-repo.git &&

	write_script target-repo.git/hooks/reference-transaction <<-\EOF &&
		echo $0 "$@" >>actual
	EOF

	write_script target-repo.git/hooks/update <<-\EOF &&
		echo $0 "$@" >>actual
	EOF

	cat >expect <<-EOF &&
		hooks/update refs/tags/PRE 0000000000000000000000000000000000000000 63ac8e7bcdb882293465435909f54a96de17d4f7
		hooks/reference-transaction prepared
		hooks/reference-transaction committed
		hooks/update refs/tags/POST 0000000000000000000000000000000000000000 99d53161c3a0a903b6561b9f6c0c665b3a476401
		hooks/reference-transaction prepared
		hooks/reference-transaction committed
	EOF

	git push ./target-repo.git PRE POST &&
	test_cmp expect target-repo.git/actual
'

test_done
