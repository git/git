#!/bin/sh
#
# Copyright (c) 2008 Clemens Buchacher <drizzd@aon.at>
#

test_description='test smart pushing over http via http-backend'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

ROOT_PATH="$PWD"
. "$TEST_DIRECTORY"/lib-gpg.sh
. "$TEST_DIRECTORY"/lib-httpd.sh
. "$TEST_DIRECTORY"/lib-terminal.sh
start_httpd

test_expect_success 'setup remote repository' '
	cd "$ROOT_PATH" &&
	mkdir test_repo &&
	cd test_repo &&
	but init &&
	: >path1 &&
	but add path1 &&
	test_tick &&
	but cummit -m initial &&
	cd - &&
	but clone --bare test_repo test_repo.but &&
	cd test_repo.but &&
	but config http.receivepack true &&
	but config core.logallrefupdates true &&
	ORIG_HEAD=$(but rev-parse --verify HEAD) &&
	cd - &&
	mv test_repo.but "$HTTPD_DOCUMENT_ROOT_PATH"
'

setup_askpass_helper

cat >exp <<EOF
GET  /smart/test_repo.but/info/refs?service=but-upload-pack HTTP/1.1 200
POST /smart/test_repo.but/but-upload-pack HTTP/1.1 200
EOF
test_expect_success 'no empty path components' '
	# Clear the log, so that it does not affect the "used receive-pack
	# service" test which reads the log too.
	test_when_finished ">\"\$HTTPD_ROOT_PATH\"/access.log" &&

	# In the URL, add a trailing slash, and see if but appends yet another
	# slash.
	cd "$ROOT_PATH" &&
	but clone $HTTPD_URL/smart/test_repo.but/ test_repo_clone &&

	# NEEDSWORK: If the overspecification of the expected result is reduced, we
	# might be able to run this test in all protocol versions.
	if test "$BUT_TEST_PROTOCOL_VERSION" = 0
	then
		check_access_log exp
	fi
'

test_expect_success 'clone remote repository' '
	rm -rf test_repo_clone &&
	but clone $HTTPD_URL/smart/test_repo.but test_repo_clone &&
	(
		cd test_repo_clone && but config push.default matching
	)
'

test_expect_success 'push to remote repository (standard)' '
	cd "$ROOT_PATH"/test_repo_clone &&
	: >path2 &&
	but add path2 &&
	test_tick &&
	but cummit -m path2 &&
	HEAD=$(but rev-parse --verify HEAD) &&
	BUT_TRACE_CURL=true but push -v -v 2>err &&
	! grep "Expect: 100-continue" err &&
	grep "POST but-receive-pack ([0-9]* bytes)" err &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.but &&
	 test $HEAD = $(but rev-parse --verify HEAD))
'

test_expect_success 'push already up-to-date' '
	but push
'

test_expect_success 'create and delete remote branch' '
	cd "$ROOT_PATH"/test_repo_clone &&
	but checkout -b dev &&
	: >path3 &&
	but add path3 &&
	test_tick &&
	but cummit -m dev &&
	but push origin dev &&
	but push origin :dev &&
	test_must_fail but show-ref --verify refs/remotes/origin/dev
'

test_expect_success 'setup rejected update hook' '
	test_hook --setup -C "$HTTPD_DOCUMENT_ROOT_PATH/test_repo.but" update <<-\EOF &&
	exit 1
	EOF

	cat >exp <<-EOF
	remote: error: hook declined to update refs/heads/dev2
	To http://127.0.0.1:$LIB_HTTPD_PORT/smart/test_repo.but
	 ! [remote rejected] dev2 -> dev2 (hook declined)
	error: failed to push some refs to '\''http://127.0.0.1:$LIB_HTTPD_PORT/smart/test_repo.but'\''
	EOF
'

test_expect_success 'rejected update prints status' '
	cd "$ROOT_PATH"/test_repo_clone &&
	but checkout -b dev2 &&
	: >path4 &&
	but add path4 &&
	test_tick &&
	but cummit -m dev2 &&
	test_must_fail but push origin dev2 2>act &&
	sed -e "/^remote: /s/ *$//" <act >cmp &&
	test_cmp exp cmp
'
rm -f "$HTTPD_DOCUMENT_ROOT_PATH/test_repo.but/hooks/update"

cat >exp <<EOF
GET  /smart/test_repo.but/info/refs?service=but-upload-pack HTTP/1.1 200
POST /smart/test_repo.but/but-upload-pack HTTP/1.1 200
GET  /smart/test_repo.but/info/refs?service=but-receive-pack HTTP/1.1 200
POST /smart/test_repo.but/but-receive-pack HTTP/1.1 200
GET  /smart/test_repo.but/info/refs?service=but-receive-pack HTTP/1.1 200
GET  /smart/test_repo.but/info/refs?service=but-receive-pack HTTP/1.1 200
POST /smart/test_repo.but/but-receive-pack HTTP/1.1 200
GET  /smart/test_repo.but/info/refs?service=but-receive-pack HTTP/1.1 200
POST /smart/test_repo.but/but-receive-pack HTTP/1.1 200
GET  /smart/test_repo.but/info/refs?service=but-receive-pack HTTP/1.1 200
POST /smart/test_repo.but/but-receive-pack HTTP/1.1 200
EOF
test_expect_success 'used receive-pack service' '
	# NEEDSWORK: If the overspecification of the expected result is reduced, we
	# might be able to run this test in all protocol versions.
	if test "$BUT_TEST_PROTOCOL_VERSION" = 0
	then
		check_access_log exp
	fi
'

test_http_push_nonff "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.but \
	"$ROOT_PATH"/test_repo_clone main 		success

test_expect_success 'push fails for non-fast-forward refs unmatched by remote helper' '
	# create a dissimilarly-named remote ref so that but is unable to match the
	# two refs (viz. local, remote) unless an explicit refspec is provided.
	but push origin main:niam &&

	echo "change changed" > path2 &&
	but cummit -a -m path2 --amend &&

	# push main too; this ensures there is at least one '"'push'"' command to
	# the remote helper and triggers interaction with the helper.
	test_must_fail but push -v origin +main main:niam >output 2>&1'

test_expect_success 'push fails for non-fast-forward refs unmatched by remote helper: remote output' '
	grep "^ + [a-f0-9]*\.\.\.[a-f0-9]* *main -> main (forced update)$" output &&
	grep "^ ! \[rejected\] *main -> niam (non-fast-forward)$" output
'

test_expect_success 'push fails for non-fast-forward refs unmatched by remote helper: our output' '
	test_i18ngrep "Updates were rejected because" \
		output
'

test_expect_success 'push (chunked)' '
	but checkout main &&
	test_cummit cummit path3 &&
	HEAD=$(but rev-parse --verify HEAD) &&
	test_config http.postbuffer 4 &&
	but push -v -v origin $BRANCH 2>err &&
	grep "POST but-receive-pack (chunked)" err &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.but &&
	 test $HEAD = $(but rev-parse --verify HEAD))
'

## References of remote: atomic1(1)            main(2) collateral(2) other(2)
## References of local :            atomic2(2) main(1) collateral(3) other(2) collateral1(3) atomic(1)
## Atomic push         :                       main(1) collateral(3)                         atomic(1)
test_expect_success 'push --atomic also prevents branch creation, reports collateral' '
	# Setup upstream repo - empty for now
	d=$HTTPD_DOCUMENT_ROOT_PATH/atomic-branches.but &&
	but init --bare "$d" &&
	test_config -C "$d" http.receivepack true &&
	up="$HTTPD_URL"/smart/atomic-branches.but &&

	# Tell "$up" about three branches for now
	test_cummit atomic1 &&
	test_cummit atomic2 &&
	but branch collateral &&
	but branch other &&
	but push "$up" atomic1 main collateral other &&
	but tag -d atomic1 &&

	# collateral is a valid push, but should be failed by atomic push
	but checkout collateral &&
	test_cummit collateral1 &&

	# Make main incompatible with upstream to provoke atomic
	but checkout main &&
	but reset --hard HEAD^ &&

	# Add a new branch which should be failed by atomic push. This is a
	# regression case.
	but branch atomic &&

	# --atomic should cause entire push to be rejected
	test_must_fail but push --atomic "$up" main atomic collateral 2>output &&

	# the new branch should not have been created upstream
	test_must_fail but -C "$d" show-ref --verify refs/heads/atomic &&

	# upstream should still reflect atomic2, the last thing we pushed
	# successfully
	but rev-parse atomic2 >expected &&
	# on main...
	but -C "$d" rev-parse refs/heads/main >actual &&
	test_cmp expected actual &&
	# ...and collateral.
	but -C "$d" rev-parse refs/heads/collateral >actual &&
	test_cmp expected actual &&

	# the failed refs should be indicated to the user
	grep "^ ! .*rejected.* main -> main" output &&

	# the collateral failure refs should be indicated to the user
	grep "^ ! .*rejected.* atomic -> atomic .*atomic push failed" output &&
	grep "^ ! .*rejected.* collateral -> collateral .*atomic push failed" output &&

	# never report what we do not push
	! grep "^ ! .*rejected.* atomic1 " output &&
	! grep "^ ! .*rejected.* other " output
'

test_expect_success 'push --atomic fails on server-side errors' '
	# Use previously set up repository
	d=$HTTPD_DOCUMENT_ROOT_PATH/atomic-branches.but &&
	test_config -C "$d" http.receivepack true &&
	up="$HTTPD_URL"/smart/atomic-branches.but &&

	# break ref updates for other on the remote site
	mkdir "$d/refs/heads/other.lock" &&

	# add the new cummit to other
	but branch -f other collateral &&

	# --atomic should cause entire push to be rejected
	test_must_fail but push --atomic "$up" atomic other 2>output  &&

	# the new branch should not have been created upstream
	test_must_fail but -C "$d" show-ref --verify refs/heads/atomic &&

	# upstream should still reflect atomic2, the last thing we pushed
	# successfully
	but rev-parse atomic2 >expected &&
	# ...to other.
	but -C "$d" rev-parse refs/heads/other >actual &&
	test_cmp expected actual &&

	# the new branch should not have been created upstream
	test_must_fail but -C "$d" show-ref --verify refs/heads/atomic &&

	# the failed refs should be indicated to the user
	grep "^ ! .*rejected.* other -> other .*atomic transaction failed" output &&

	# the collateral failure refs should be indicated to the user
	grep "^ ! .*rejected.* atomic -> atomic .*atomic transaction failed" output
'

test_expect_success 'push --all can push to empty repo' '
	d=$HTTPD_DOCUMENT_ROOT_PATH/empty-all.but &&
	but init --bare "$d" &&
	but --but-dir="$d" config http.receivepack true &&
	but push --all "$HTTPD_URL"/smart/empty-all.but
'

test_expect_success 'push --mirror can push to empty repo' '
	d=$HTTPD_DOCUMENT_ROOT_PATH/empty-mirror.but &&
	but init --bare "$d" &&
	but --but-dir="$d" config http.receivepack true &&
	but push --mirror "$HTTPD_URL"/smart/empty-mirror.but
'

test_expect_success 'push --all to repo with alternates' '
	s=$HTTPD_DOCUMENT_ROOT_PATH/test_repo.but &&
	d=$HTTPD_DOCUMENT_ROOT_PATH/alternates-all.but &&
	but clone --bare --shared "$s" "$d" &&
	but --but-dir="$d" config http.receivepack true &&
	but --but-dir="$d" repack -adl &&
	but push --all "$HTTPD_URL"/smart/alternates-all.but
'

test_expect_success 'push --mirror to repo with alternates' '
	s=$HTTPD_DOCUMENT_ROOT_PATH/test_repo.but &&
	d=$HTTPD_DOCUMENT_ROOT_PATH/alternates-mirror.but &&
	but clone --bare --shared "$s" "$d" &&
	but --but-dir="$d" config http.receivepack true &&
	but --but-dir="$d" repack -adl &&
	but push --mirror "$HTTPD_URL"/smart/alternates-mirror.but
'

test_expect_success TTY 'push shows progress when stderr is a tty' '
	cd "$ROOT_PATH"/test_repo_clone &&
	test_cummit noisy &&
	test_terminal but push >output 2>&1 &&
	test_i18ngrep "^Writing objects" output
'

test_expect_success TTY 'push --quiet silences status and progress' '
	cd "$ROOT_PATH"/test_repo_clone &&
	test_cummit quiet &&
	test_terminal but push --quiet >output 2>&1 &&
	test_must_be_empty output
'

test_expect_success TTY 'push --no-progress silences progress but not status' '
	cd "$ROOT_PATH"/test_repo_clone &&
	test_cummit no-progress &&
	test_terminal but push --no-progress >output 2>&1 &&
	test_i18ngrep "^To http" output &&
	test_i18ngrep ! "^Writing objects" output
'

test_expect_success 'push --progress shows progress to non-tty' '
	cd "$ROOT_PATH"/test_repo_clone &&
	test_cummit progress &&
	but push --progress >output 2>&1 &&
	test_i18ngrep "^To http" output &&
	test_i18ngrep "^Writing objects" output
'

test_expect_success 'http push gives sane defaults to reflog' '
	cd "$ROOT_PATH"/test_repo_clone &&
	test_cummit reflog-test &&
	but push "$HTTPD_URL"/smart/test_repo.but &&
	but --but-dir="$HTTPD_DOCUMENT_ROOT_PATH/test_repo.but" \
		log -g -1 --format="%gn <%ge>" >actual &&
	echo "anonymous <anonymous@http.127.0.0.1>" >expect &&
	test_cmp expect actual
'

test_expect_success 'http push respects BUT_CUMMITTER_* in reflog' '
	cd "$ROOT_PATH"/test_repo_clone &&
	test_cummit custom-reflog-test &&
	but push "$HTTPD_URL"/smart_custom_env/test_repo.but &&
	but --but-dir="$HTTPD_DOCUMENT_ROOT_PATH/test_repo.but" \
		log -g -1 --format="%gn <%ge>" >actual &&
	echo "Custom User <custom@example.com>" >expect &&
	test_cmp expect actual
'

test_expect_success 'push over smart http with auth' '
	cd "$ROOT_PATH/test_repo_clone" &&
	echo push-auth-test >expect &&
	test_cummit push-auth-test &&
	set_askpass user@host pass@host &&
	but push "$HTTPD_URL"/auth/smart/test_repo.but &&
	but --but-dir="$HTTPD_DOCUMENT_ROOT_PATH/test_repo.but" \
		log -1 --format=%s >actual &&
	expect_askpass both user@host &&
	test_cmp expect actual
'

test_expect_success 'push to auth-only-for-push repo' '
	cd "$ROOT_PATH/test_repo_clone" &&
	echo push-half-auth >expect &&
	test_cummit push-half-auth &&
	set_askpass user@host pass@host &&
	but push "$HTTPD_URL"/auth-push/smart/test_repo.but &&
	but --but-dir="$HTTPD_DOCUMENT_ROOT_PATH/test_repo.but" \
		log -1 --format=%s >actual &&
	expect_askpass both user@host &&
	test_cmp expect actual
'

test_expect_success 'create repo without http.receivepack set' '
	cd "$ROOT_PATH" &&
	but init half-auth &&
	(
		cd half-auth &&
		test_cummit one
	) &&
	but clone --bare half-auth "$HTTPD_DOCUMENT_ROOT_PATH/half-auth.but"
'

test_expect_success 'clone via half-auth-complete does not need password' '
	cd "$ROOT_PATH" &&
	set_askpass wrong &&
	but clone "$HTTPD_URL"/half-auth-complete/smart/half-auth.but \
		half-auth-clone &&
	expect_askpass none
'

test_expect_success 'push into half-auth-complete requires password' '
	cd "$ROOT_PATH/half-auth-clone" &&
	echo two >expect &&
	test_cummit two &&
	set_askpass user@host pass@host &&
	but push "$HTTPD_URL/half-auth-complete/smart/half-auth.but" &&
	but --but-dir="$HTTPD_DOCUMENT_ROOT_PATH/half-auth.but" \
		log -1 --format=%s >actual &&
	expect_askpass both user@host &&
	test_cmp expect actual
'

test_expect_success CMDLINE_LIMIT 'push 2000 tags over http' '
	sha1=$(but rev-parse HEAD) &&
	test_seq 2000 |
	  sort |
	  sed "s|.*|$sha1 refs/tags/really-long-tag-name-&|" \
	  >.but/packed-refs &&
	run_with_limited_cmdline but push --mirror
'

test_expect_success GPG 'push with post-receive to inspect certificate' '
	test_hook -C "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.but post-receive <<-\EOF &&
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
	(
		cd "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.but &&
		but config receive.certnonceseed sekrit &&
		but config receive.certnonceslop 30
	) &&
	cd "$ROOT_PATH/test_repo_clone" &&
	test_cummit cert-test &&
	but push --signed "$HTTPD_URL/smart/test_repo.but" &&
	(
		cd "$HTTPD_DOCUMENT_ROOT_PATH" &&
		cat <<-\EOF &&
		SIGNER=C O Mitter <cummitter@example.com>
		KEY=13B6F51ECDDE430D
		STATUS=G
		NONCE_STATUS=OK
		EOF
		sed -n -e "s/^nonce /NONCE=/p" -e "/^$/q" push-cert
	) >expect &&
	test_cmp expect "$HTTPD_DOCUMENT_ROOT_PATH/push-cert-status"
'

test_expect_success 'push status output scrubs password' '
	cd "$ROOT_PATH/test_repo_clone" &&
	but push --porcelain \
		"$HTTPD_URL_USER_PASS/smart/test_repo.but" \
		+HEAD:scrub >status &&
	# should have been scrubbed down to vanilla URL
	grep "^To $HTTPD_URL/smart/test_repo.but" status
'

test_expect_success 'clone/fetch scrubs password from reflogs' '
	cd "$ROOT_PATH" &&
	but clone "$HTTPD_URL_USER_PASS/smart/test_repo.but" \
		reflog-test &&
	cd reflog-test &&
	test_cummit prepare-for-force-fetch &&
	but switch -c away &&
	but fetch "$HTTPD_URL_USER_PASS/smart/test_repo.but" \
		+main:main &&
	# should have been scrubbed down to vanilla URL
	but log -g main >reflog &&
	grep "$HTTPD_URL" reflog &&
	! grep "$HTTPD_URL_USER_PASS" reflog
'

test_expect_success 'Non-ASCII branch name can be used with --force-with-lease' '
	cd "$ROOT_PATH" &&
	but clone "$HTTPD_URL_USER_PASS/smart/test_repo.but" non-ascii &&
	cd non-ascii &&
	but checkout -b rama-de-árbol &&
	test_cummit F &&
	but push --force-with-lease origin rama-de-árbol &&
	but ls-remote origin refs/heads/rama-de-árbol >actual &&
	but ls-remote . refs/heads/rama-de-árbol >expect &&
	test_cmp expect actual &&
	but push --delete --force-with-lease origin rama-de-árbol &&
	but ls-remote origin refs/heads/rama-de-árbol >actual &&
	test_must_be_empty actual
'

test_expect_success 'colorize errors/hints' '
	cd "$ROOT_PATH"/test_repo_clone &&
	test_must_fail but -c color.transport=always -c color.advice=always \
		-c color.push=always \
		push origin origin/main^:main 2>act &&
	test_decode_color <act >decoded &&
	test_i18ngrep "<RED>.*rejected.*<RESET>" decoded &&
	test_i18ngrep "<RED>error: failed to push some refs" decoded &&
	test_i18ngrep "<YELLOW>hint: " decoded &&
	test_i18ngrep ! "^hint: " decoded
'

test_expect_success 'report error server does not provide ref status' '
	but init "$HTTPD_DOCUMENT_ROOT_PATH/no_report" &&
	but -C "$HTTPD_DOCUMENT_ROOT_PATH/no_report" config http.receivepack true &&
	test_must_fail but push --porcelain \
		$HTTPD_URL_USER_PASS/smart/no_report \
		HEAD:refs/tags/will-fail >actual &&
	test_must_fail but -C "$HTTPD_DOCUMENT_ROOT_PATH/no_report" \
		rev-parse --verify refs/tags/will-fail &&
	cat >expect <<-EOF &&
	To $HTTPD_URL/smart/no_report
	!	HEAD:refs/tags/will-fail	[remote failure] (remote failed to report status)
	Done
	EOF
	test_cmp expect actual
'

test_done
