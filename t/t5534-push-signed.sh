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
	test_must_fail git push --signed dst noop ff +noff 2>err &&
	test_i18ngrep "the receiving end does not support" err
'

test_expect_success GPG 'no certificate for a signed push with no update' '
	prepare_dst &&
	mkdir -p dst/.git/hooks &&
	write_script dst/.git/hooks/post-receive <<-\EOF &&
	if test -n "${GIT_PUSH_CERT-}"
	then
		git cat-file blob $GIT_PUSH_CERT >../push-cert
	fi
	EOF
	git push dst noop &&
	! test -f dst/push-cert
'

test_expect_success GPG 'signed push sends push certificate' '
	prepare_dst &&
	mkdir -p dst/.git/hooks &&
	git -C dst config receive.certnonceseed sekrit &&
	write_script dst/.git/hooks/post-receive <<-\EOF &&
	# discard the update list
	cat >/dev/null
	# record the push certificate
	if test -n "${GIT_PUSH_CERT-}"
	then
		git cat-file blob $GIT_PUSH_CERT >../push-cert
	fi &&

	cat >../push-cert-status <<E_O_F
	SIGNER=${GIT_PUSH_CERT_SIGNER-nobody}
	KEY=${GIT_PUSH_CERT_KEY-nokey}
	STATUS=${GIT_PUSH_CERT_STATUS-nostatus}
	NONCE_STATUS=${GIT_PUSH_CERT_NONCE_STATUS-nononcestatus}
	NONCE=${GIT_PUSH_CERT_NONCE-nononce}
	E_O_F

	EOF

	git push --signed dst noop ff +noff &&

	(
		cat <<-\EOF &&
		SIGNER=C O Mitter <committer@example.com>
		KEY=13B6F51ECDDE430D
		STATUS=G
		NONCE_STATUS=OK
		EOF
		sed -n -e "s/^nonce /NONCE=/p" -e "/^$/q" dst/push-cert
	) >expect &&

	grep "$(git rev-parse noop ff) refs/heads/ff" dst/push-cert &&
	grep "$(git rev-parse noop noff) refs/heads/noff" dst/push-cert &&
	test_cmp expect dst/push-cert-status
'

test_expect_success GPG 'fail without key and heed user.signingkey' '
	prepare_dst &&
	mkdir -p dst/.git/hooks &&
	git -C dst config receive.certnonceseed sekrit &&
	write_script dst/.git/hooks/post-receive <<-\EOF &&
	# discard the update list
	cat >/dev/null
	# record the push certificate
	if test -n "${GIT_PUSH_CERT-}"
	then
		git cat-file blob $GIT_PUSH_CERT >../push-cert
	fi &&

	cat >../push-cert-status <<E_O_F
	SIGNER=${GIT_PUSH_CERT_SIGNER-nobody}
	KEY=${GIT_PUSH_CERT_KEY-nokey}
	STATUS=${GIT_PUSH_CERT_STATUS-nostatus}
	NONCE_STATUS=${GIT_PUSH_CERT_NONCE_STATUS-nononcestatus}
	NONCE=${GIT_PUSH_CERT_NONCE-nononce}
	E_O_F

	EOF

	unset GIT_COMMITTER_EMAIL &&
	git config user.email hasnokey@nowhere.com &&
	test_must_fail git push --signed dst noop ff +noff &&
	git config user.signingkey committer@example.com &&
	git push --signed dst noop ff +noff &&

	(
		cat <<-\EOF &&
		SIGNER=C O Mitter <committer@example.com>
		KEY=13B6F51ECDDE430D
		STATUS=G
		NONCE_STATUS=OK
		EOF
		sed -n -e "s/^nonce /NONCE=/p" -e "/^$/q" dst/push-cert
	) >expect &&

	grep "$(git rev-parse noop ff) refs/heads/ff" dst/push-cert &&
	grep "$(git rev-parse noop noff) refs/heads/noff" dst/push-cert &&
	test_cmp expect dst/push-cert-status
'

test_done
