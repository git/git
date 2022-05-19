#!/bin/sh

test_description='signed push'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-gpg.sh

prepare_dst () {
	rm -fr dst &&
	test_create_repo dst &&

	but push dst main:noop main:ff main:noff
}

test_expect_success setup '
	# main, ff and noff branches pointing at the same cummit
	test_tick &&
	but cummit --allow-empty -m initial &&

	but checkout -b noop &&
	but checkout -b ff &&
	but checkout -b noff &&

	# noop stays the same, ff advances, noff rewrites
	test_tick &&
	but cummit --allow-empty --amend -m rewritten &&
	but checkout ff &&

	test_tick &&
	but cummit --allow-empty -m second
'

test_expect_success 'unsigned push does not send push certificate' '
	prepare_dst &&
	test_hook -C dst post-receive <<-\EOF &&
	# discard the update list
	cat >/dev/null
	# record the push certificate
	if test -n "${BUT_PUSH_CERT-}"
	then
		but cat-file blob $BUT_PUSH_CERT >../push-cert
	fi
	EOF

	but push dst noop ff +noff &&
	! test -f dst/push-cert
'

test_expect_success 'talking with a receiver without push certificate support' '
	prepare_dst &&
	test_hook -C dst post-receive <<-\EOF &&
	# discard the update list
	cat >/dev/null
	# record the push certificate
	if test -n "${BUT_PUSH_CERT-}"
	then
		but cat-file blob $BUT_PUSH_CERT >../push-cert
	fi
	EOF

	but push dst noop ff +noff &&
	! test -f dst/push-cert
'

test_expect_success 'push --signed fails with a receiver without push certificate support' '
	prepare_dst &&
	test_must_fail but push --signed dst noop ff +noff 2>err &&
	test_i18ngrep "the receiving end does not support" err
'

test_expect_success 'push --signed=1 is accepted' '
	prepare_dst &&
	test_must_fail but push --signed=1 dst noop ff +noff 2>err &&
	test_i18ngrep "the receiving end does not support" err
'

test_expect_success GPG 'no certificate for a signed push with no update' '
	prepare_dst &&
	test_hook -C dst post-receive <<-\EOF &&
	if test -n "${BUT_PUSH_CERT-}"
	then
		but cat-file blob $BUT_PUSH_CERT >../push-cert
	fi
	EOF
	but push dst noop &&
	! test -f dst/push-cert
'

test_expect_success GPG 'signed push sends push certificate' '
	prepare_dst &&
	but -C dst config receive.certnonceseed sekrit &&
	test_hook -C dst post-receive <<-\EOF &&
	# discard the update list
	cat >/dev/null
	# record the push certificate
	if test -n "${BUT_PUSH_CERT-}"
	then
		but cat-file blob $BUT_PUSH_CERT >../push-cert
	fi &&

	cat >../push-cert-status <<E_O_F
	SIGNER=${BUT_PUSH_CERT_SIGNER-nobody}
	KEY=${BUT_PUSH_CERT_KEY-nokey}
	STATUS=${BUT_PUSH_CERT_STATUS-nostatus}
	NONCE_STATUS=${BUT_PUSH_CERT_NONCE_STATUS-nononcestatus}
	NONCE=${BUT_PUSH_CERT_NONCE-nononce}
	E_O_F

	EOF

	but push --signed dst noop ff +noff &&

	(
		cat <<-\EOF &&
		SIGNER=C O Mitter <cummitter@example.com>
		KEY=13B6F51ECDDE430D
		STATUS=G
		NONCE_STATUS=OK
		EOF
		sed -n -e "s/^nonce /NONCE=/p" -e "/^$/q" dst/push-cert
	) >expect &&

	noop=$(but rev-parse noop) &&
	ff=$(but rev-parse ff) &&
	noff=$(but rev-parse noff) &&
	grep "$noop $ff refs/heads/ff" dst/push-cert &&
	grep "$noop $noff refs/heads/noff" dst/push-cert &&
	test_cmp expect dst/push-cert-status
'

test_expect_success GPGSSH 'ssh signed push sends push certificate' '
	prepare_dst &&
	but -C dst config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	but -C dst config receive.certnonceseed sekrit &&
	test_hook -C dst post-receive <<-\EOF &&
	# discard the update list
	cat >/dev/null
	# record the push certificate
	if test -n "${BUT_PUSH_CERT-}"
	then
		but cat-file blob $BUT_PUSH_CERT >../push-cert
	fi &&

	cat >../push-cert-status <<E_O_F
	SIGNER=${BUT_PUSH_CERT_SIGNER-nobody}
	KEY=${BUT_PUSH_CERT_KEY-nokey}
	STATUS=${BUT_PUSH_CERT_STATUS-nostatus}
	NONCE_STATUS=${BUT_PUSH_CERT_NONCE_STATUS-nononcestatus}
	NONCE=${BUT_PUSH_CERT_NONCE-nononce}
	E_O_F

	EOF

	test_config gpg.format ssh &&
	test_config user.signingkey "${GPGSSH_KEY_PRIMARY}" &&
	FINGERPRINT=$(ssh-keygen -lf "${GPGSSH_KEY_PRIMARY}" | awk "{print \$2;}") &&
	but push --signed dst noop ff +noff &&

	(
		cat <<-\EOF &&
		SIGNER=principal with number 1
		KEY=FINGERPRINT
		STATUS=G
		NONCE_STATUS=OK
		EOF
		sed -n -e "s/^nonce /NONCE=/p" -e "/^$/q" dst/push-cert
	) | sed -e "s|FINGERPRINT|$FINGERPRINT|" >expect &&

	noop=$(but rev-parse noop) &&
	ff=$(but rev-parse ff) &&
	noff=$(but rev-parse noff) &&
	grep "$noop $ff refs/heads/ff" dst/push-cert &&
	grep "$noop $noff refs/heads/noff" dst/push-cert &&
	test_cmp expect dst/push-cert-status
'

test_expect_success GPG 'inconsistent push options in signed push not allowed' '
	# First, invoke receive-pack with dummy input to obtain its preamble.
	prepare_dst &&
	but -C dst config receive.certnonceseed sekrit &&
	but -C dst config receive.advertisepushoptions 1 &&
	printf xxxx | test_might_fail but receive-pack dst >preamble &&

	# Then, invoke push. Simulate a receive-pack that sends the preamble we
	# obtained, followed by a dummy packet.
	write_script myscript <<-\EOF &&
		cat preamble &&
		printf xxxx &&
		cat >push
	EOF
	test_might_fail but push --push-option="foo" --push-option="bar" \
		--receive-pack="\"$(pwd)/myscript\"" --signed dst --delete ff &&

	# Replay the push output on a fresh dst, checking that ff is truly
	# deleted.
	prepare_dst &&
	but -C dst config receive.certnonceseed sekrit &&
	but -C dst config receive.advertisepushoptions 1 &&
	but receive-pack dst <push &&
	test_must_fail but -C dst rev-parse ff &&

	# Tweak the push output to make the push option outside the cert
	# different, then replay it on a fresh dst, checking that ff is not
	# deleted.
	perl -pe "s/([^ ])bar/\$1baz/" push >push.tweak &&
	prepare_dst &&
	but -C dst config receive.certnonceseed sekrit &&
	but -C dst config receive.advertisepushoptions 1 &&
	but receive-pack dst <push.tweak >out &&
	but -C dst rev-parse ff &&
	grep "inconsistent push options" out
'

test_expect_success GPG 'fail without key and heed user.signingkey' '
	prepare_dst &&
	but -C dst config receive.certnonceseed sekrit &&
	test_hook -C dst post-receive <<-\EOF &&
	# discard the update list
	cat >/dev/null
	# record the push certificate
	if test -n "${BUT_PUSH_CERT-}"
	then
		but cat-file blob $BUT_PUSH_CERT >../push-cert
	fi &&

	cat >../push-cert-status <<E_O_F
	SIGNER=${BUT_PUSH_CERT_SIGNER-nobody}
	KEY=${BUT_PUSH_CERT_KEY-nokey}
	STATUS=${BUT_PUSH_CERT_STATUS-nostatus}
	NONCE_STATUS=${BUT_PUSH_CERT_NONCE_STATUS-nononcestatus}
	NONCE=${BUT_PUSH_CERT_NONCE-nononce}
	E_O_F

	EOF

	test_config user.email hasnokey@nowhere.com &&
	(
		sane_unset BUT_CUMMITTER_EMAIL &&
		test_must_fail but push --signed dst noop ff +noff
	) &&
	test_config user.signingkey $BUT_CUMMITTER_EMAIL &&
	but push --signed dst noop ff +noff &&

	(
		cat <<-\EOF &&
		SIGNER=C O Mitter <cummitter@example.com>
		KEY=13B6F51ECDDE430D
		STATUS=G
		NONCE_STATUS=OK
		EOF
		sed -n -e "s/^nonce /NONCE=/p" -e "/^$/q" dst/push-cert
	) >expect &&

	noop=$(but rev-parse noop) &&
	ff=$(but rev-parse ff) &&
	noff=$(but rev-parse noff) &&
	grep "$noop $ff refs/heads/ff" dst/push-cert &&
	grep "$noop $noff refs/heads/noff" dst/push-cert &&
	test_cmp expect dst/push-cert-status
'

test_expect_success GPGSM 'fail without key and heed user.signingkey x509' '
	test_config gpg.format x509 &&
	prepare_dst &&
	but -C dst config receive.certnonceseed sekrit &&
	test_hook -C dst post-receive <<-\EOF &&
	# discard the update list
	cat >/dev/null
	# record the push certificate
	if test -n "${BUT_PUSH_CERT-}"
	then
		but cat-file blob $BUT_PUSH_CERT >../push-cert
	fi &&

	cat >../push-cert-status <<E_O_F
	SIGNER=${BUT_PUSH_CERT_SIGNER-nobody}
	KEY=${BUT_PUSH_CERT_KEY-nokey}
	STATUS=${BUT_PUSH_CERT_STATUS-nostatus}
	NONCE_STATUS=${BUT_PUSH_CERT_NONCE_STATUS-nononcestatus}
	NONCE=${BUT_PUSH_CERT_NONCE-nononce}
	E_O_F

	EOF

	test_config user.email hasnokey@nowhere.com &&
	test_config user.signingkey "" &&
	(
		sane_unset BUT_CUMMITTER_EMAIL &&
		test_must_fail but push --signed dst noop ff +noff
	) &&
	test_config user.signingkey $BUT_CUMMITTER_EMAIL &&
	but push --signed dst noop ff +noff &&

	(
		cat <<-\EOF &&
		SIGNER=/CN=C O Mitter/O=Example/SN=C O/GN=Mitter
		KEY=
		STATUS=G
		NONCE_STATUS=OK
		EOF
		sed -n -e "s/^nonce /NONCE=/p" -e "/^$/q" dst/push-cert
	) >expect.in &&
	key=$(cat "${GNUPGHOME}/trustlist.txt" | cut -d" " -f1 | tr -d ":") &&
	sed -e "s/^KEY=/KEY=${key}/" expect.in >expect &&

	noop=$(but rev-parse noop) &&
	ff=$(but rev-parse ff) &&
	noff=$(but rev-parse noff) &&
	grep "$noop $ff refs/heads/ff" dst/push-cert &&
	grep "$noop $noff refs/heads/noff" dst/push-cert &&
	test_cmp expect dst/push-cert-status
'

test_expect_success GPGSSH 'fail without key and heed user.signingkey ssh' '
	test_config gpg.format ssh &&
	prepare_dst &&
	but -C dst config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	but -C dst config receive.certnonceseed sekrit &&
	test_hook -C dst post-receive <<-\EOF &&
	# discard the update list
	cat >/dev/null
	# record the push certificate
	if test -n "${BUT_PUSH_CERT-}"
	then
		but cat-file blob $BUT_PUSH_CERT >../push-cert
	fi &&

	cat >../push-cert-status <<E_O_F
	SIGNER=${BUT_PUSH_CERT_SIGNER-nobody}
	KEY=${BUT_PUSH_CERT_KEY-nokey}
	STATUS=${BUT_PUSH_CERT_STATUS-nostatus}
	NONCE_STATUS=${BUT_PUSH_CERT_NONCE_STATUS-nononcestatus}
	NONCE=${BUT_PUSH_CERT_NONCE-nononce}
	E_O_F

	EOF

	test_config user.email hasnokey@nowhere.com &&
	test_config gpg.format ssh &&
	test_config user.signingkey "" &&
	(
		sane_unset BUT_CUMMITTER_EMAIL &&
		test_must_fail but push --signed dst noop ff +noff
	) &&
	test_config user.signingkey "${GPGSSH_KEY_PRIMARY}" &&
	FINGERPRINT=$(ssh-keygen -lf "${GPGSSH_KEY_PRIMARY}" | awk "{print \$2;}") &&
	but push --signed dst noop ff +noff &&

	(
		cat <<-\EOF &&
		SIGNER=principal with number 1
		KEY=FINGERPRINT
		STATUS=G
		NONCE_STATUS=OK
		EOF
		sed -n -e "s/^nonce /NONCE=/p" -e "/^$/q" dst/push-cert
	) | sed -e "s|FINGERPRINT|$FINGERPRINT|" >expect &&

	noop=$(but rev-parse noop) &&
	ff=$(but rev-parse ff) &&
	noff=$(but rev-parse noff) &&
	grep "$noop $ff refs/heads/ff" dst/push-cert &&
	grep "$noop $noff refs/heads/noff" dst/push-cert &&
	test_cmp expect dst/push-cert-status
'

test_expect_success GPG 'failed atomic push does not execute GPG' '
	prepare_dst &&
	but -C dst config receive.certnonceseed sekrit &&
	write_script gpg <<-EOF &&
	# should check atomic push locally before running GPG.
	exit 1
	EOF
	test_must_fail env PATH="$TRASH_DIRECTORY:$PATH" but push \
			--signed --atomic --porcelain \
			dst noop ff noff >out 2>err &&

	test_i18ngrep ! "gpg failed to sign" err &&
	cat >expect <<-EOF &&
	To dst
	=	refs/heads/noop:refs/heads/noop	[up to date]
	!	refs/heads/ff:refs/heads/ff	[rejected] (atomic push failed)
	!	refs/heads/noff:refs/heads/noff	[rejected] (non-fast-forward)
	Done
	EOF
	test_cmp expect out
'

test_done
