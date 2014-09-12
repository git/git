#!/bin/sh

test_description='signed push'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-gpg.sh

prepare_dst () {
	rm -fr dst &&
	test_create_repo dst &&

	git push dst master:noop master:ff master:noff
}

test_expect_success setup '
	# master, ff and noff branches pointing at the same commit
	test_tick &&
	git commit --allow-empty -m initial &&

	git checkout -b noop &&
	git checkout -b ff &&
	git checkout -b noff &&

	# noop stays the same, ff advances, noff rewrites
	test_tick &&
	git commit --allow-empty --amend -m rewritten &&
	git checkout ff &&

	test_tick &&
	git commit --allow-empty -m second
'

test_expect_success 'unsigned push does not send push certificate' '
	prepare_dst &&
	mkdir -p dst/.git/hooks &&
	write_script dst/.git/hooks/post-receive <<-\EOF &&
	# discard the update list
	cat >/dev/null
	# record the push certificate
	if test -n "${GIT_PUSH_CERT-}"
	then
		git cat-file blob $GIT_PUSH_CERT >../push-cert
	fi
	EOF

	git push dst noop ff +noff &&
	! test -f dst/push-cert
'

test_expect_success 'talking with a receiver without push certificate support' '
	prepare_dst &&
	mkdir -p dst/.git/hooks &&
	git -C dst config receive.acceptpushcert no &&
	write_script dst/.git/hooks/post-receive <<-\EOF &&
	# discard the update list
	cat >/dev/null
	# record the push certificate
	if test -n "${GIT_PUSH_CERT-}"
	then
		git cat-file blob $GIT_PUSH_CERT >../push-cert
	fi
	EOF

	git push dst noop ff +noff &&
	! test -f dst/push-cert
'

test_expect_success 'push --signed fails with a receiver without push certificate support' '
	prepare_dst &&
	mkdir -p dst/.git/hooks &&
	git -C dst config receive.acceptpushcert no &&
	test_must_fail git push --signed dst noop ff +noff 2>err &&
	test_i18ngrep "the receiving end does not support" err
'

test_expect_success GPG 'signed push sends push certificate' '
	prepare_dst &&
	mkdir -p dst/.git/hooks &&
	write_script dst/.git/hooks/post-receive <<-\EOF &&
	# discard the update list
	cat >/dev/null
	# record the push certificate
	if test -n "${GIT_PUSH_CERT-}"
	then
		git cat-file blob $GIT_PUSH_CERT >../push-cert
	fi
	EOF

	git push --signed dst noop ff +noff &&
	grep "$(git rev-parse noop ff) refs/heads/ff" dst/push-cert &&
	grep "$(git rev-parse noop noff) refs/heads/noff" dst/push-cert
'

test_done
