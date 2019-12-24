#!/bin/sh
#
# Copyright (c) 2008 Clemens Buchacher <drizzd@aon.at>
#

test_description='test smart pushing over http via http-backend'
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
	git init &&
	: >path1 &&
	git add path1 &&
	test_tick &&
	git commit -m initial &&
	cd - &&
	git clone --bare test_repo test_repo.git &&
	cd test_repo.git &&
	git config http.receivepack true &&
	git config core.logallrefupdates true &&
	ORIG_HEAD=$(git rev-parse --verify HEAD) &&
	cd - &&
	mv test_repo.git "$HTTPD_DOCUMENT_ROOT_PATH"
'

setup_askpass_helper

cat >exp <<EOF
GET  /smart/test_repo.git/info/refs?service=git-upload-pack HTTP/1.1 200
POST /smart/test_repo.git/git-upload-pack HTTP/1.1 200
EOF
test_expect_success 'no empty path components' '
	# Clear the log, so that it does not affect the "used receive-pack
	# service" test which reads the log too.
	test_when_finished ">\"\$HTTPD_ROOT_PATH\"/access.log" &&

	# In the URL, add a trailing slash, and see if git appends yet another
	# slash.
	cd "$ROOT_PATH" &&
	git clone $HTTPD_URL/smart/test_repo.git/ test_repo_clone &&

	# NEEDSWORK: If the overspecification of the expected result is reduced, we
	# might be able to run this test in all protocol versions.
	if test "$GIT_TEST_PROTOCOL_VERSION" = 0
	then
		check_access_log exp
	fi
'

test_expect_success 'clone remote repository' '
	rm -rf test_repo_clone &&
	git clone $HTTPD_URL/smart/test_repo.git test_repo_clone &&
	(
		cd test_repo_clone && git config push.default matching
	)
'

test_expect_success 'push to remote repository (standard)' '
	cd "$ROOT_PATH"/test_repo_clone &&
	: >path2 &&
	git add path2 &&
	test_tick &&
	git commit -m path2 &&
	HEAD=$(git rev-parse --verify HEAD) &&
	GIT_TRACE_CURL=true git push -v -v 2>err &&
	! grep "Expect: 100-continue" err &&
	grep "POST git-receive-pack ([0-9]* bytes)" err &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.git &&
	 test $HEAD = $(git rev-parse --verify HEAD))
'

test_expect_success 'push already up-to-date' '
	git push
'

test_expect_success 'create and delete remote branch' '
	cd "$ROOT_PATH"/test_repo_clone &&
	git checkout -b dev &&
	: >path3 &&
	git add path3 &&
	test_tick &&
	git commit -m dev &&
	git push origin dev &&
	git push origin :dev &&
	test_must_fail git show-ref --verify refs/remotes/origin/dev
'

cat >"$HTTPD_DOCUMENT_ROOT_PATH/test_repo.git/hooks/update" <<EOF
#!/bin/sh
exit 1
EOF
chmod a+x "$HTTPD_DOCUMENT_ROOT_PATH/test_repo.git/hooks/update"

cat >exp <<EOF
remote: error: hook declined to update refs/heads/dev2
To http://127.0.0.1:$LIB_HTTPD_PORT/smart/test_repo.git
 ! [remote rejected] dev2 -> dev2 (hook declined)
error: failed to push some refs to 'http://127.0.0.1:$LIB_HTTPD_PORT/smart/test_repo.git'
EOF

test_expect_success 'rejected update prints status' '
	cd "$ROOT_PATH"/test_repo_clone &&
	git checkout -b dev2 &&
	: >path4 &&
	git add path4 &&
	test_tick &&
	git commit -m dev2 &&
	test_must_fail git push origin dev2 2>act &&
	sed -e "/^remote: /s/ *$//" <act >cmp &&
	test_i18ncmp exp cmp
'
rm -f "$HTTPD_DOCUMENT_ROOT_PATH/test_repo.git/hooks/update"

cat >exp <<EOF
GET  /smart/test_repo.git/info/refs?service=git-upload-pack HTTP/1.1 200
POST /smart/test_repo.git/git-upload-pack HTTP/1.1 200
GET  /smart/test_repo.git/info/refs?service=git-receive-pack HTTP/1.1 200
POST /smart/test_repo.git/git-receive-pack HTTP/1.1 200
GET  /smart/test_repo.git/info/refs?service=git-receive-pack HTTP/1.1 200
GET  /smart/test_repo.git/info/refs?service=git-receive-pack HTTP/1.1 200
POST /smart/test_repo.git/git-receive-pack HTTP/1.1 200
GET  /smart/test_repo.git/info/refs?service=git-receive-pack HTTP/1.1 200
POST /smart/test_repo.git/git-receive-pack HTTP/1.1 200
GET  /smart/test_repo.git/info/refs?service=git-receive-pack HTTP/1.1 200
POST /smart/test_repo.git/git-receive-pack HTTP/1.1 200
EOF
test_expect_success 'used receive-pack service' '
	# NEEDSWORK: If the overspecification of the expected result is reduced, we
	# might be able to run this test in all protocol versions.
	if test "$GIT_TEST_PROTOCOL_VERSION" = 0
	then
		check_access_log exp
	fi
'

test_http_push_nonff "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.git \
	"$ROOT_PATH"/test_repo_clone master 		success

test_expect_success 'push fails for non-fast-forward refs unmatched by remote helper' '
	# create a dissimilarly-named remote ref so that git is unable to match the
	# two refs (viz. local, remote) unless an explicit refspec is provided.
	git push origin master:retsam &&

	echo "change changed" > path2 &&
	git commit -a -m path2 --amend &&

	# push master too; this ensures there is at least one '"'push'"' command to
	# the remote helper and triggers interaction with the helper.
	test_must_fail git push -v origin +master master:retsam >output 2>&1'

test_expect_success 'push fails for non-fast-forward refs unmatched by remote helper: remote output' '
	grep "^ + [a-f0-9]*\.\.\.[a-f0-9]* *master -> master (forced update)$" output &&
	grep "^ ! \[rejected\] *master -> retsam (non-fast-forward)$" output
'

test_expect_success 'push fails for non-fast-forward refs unmatched by remote helper: our output' '
	test_i18ngrep "Updates were rejected because" \
		output
'

test_expect_success 'push (chunked)' '
	git checkout master &&
	test_commit commit path3 &&
	HEAD=$(git rev-parse --verify HEAD) &&
	test_config http.postbuffer 4 &&
	git push -v -v origin $BRANCH 2>err &&
	grep "POST git-receive-pack (chunked)" err &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.git &&
	 test $HEAD = $(git rev-parse --verify HEAD))
'

test_expect_success 'push --atomic also prevents branch creation, reports collateral' '
	# Setup upstream repo - empty for now
	d=$HTTPD_DOCUMENT_ROOT_PATH/atomic-branches.git &&
	git init --bare "$d" &&
	test_config -C "$d" http.receivepack true &&
	up="$HTTPD_URL"/smart/atomic-branches.git &&

	# Tell "$up" about three branches for now
	test_commit atomic1 &&
	test_commit atomic2 &&
	git branch collateral &&
	git branch other &&
	git push "$up" master collateral other &&

	# collateral is a valid push, but should be failed by atomic push
	git checkout collateral &&
	test_commit collateral1 &&

	# Make master incompatible with upstream to provoke atomic
	git checkout master &&
	git reset --hard HEAD^ &&

	# Add a new branch which should be failed by atomic push. This is a
	# regression case.
	git branch atomic &&

	# --atomic should cause entire push to be rejected
	test_must_fail git push --atomic "$up" master atomic collateral 2>output &&

	# the new branch should not have been created upstream
	test_must_fail git -C "$d" show-ref --verify refs/heads/atomic &&

	# upstream should still reflect atomic2, the last thing we pushed
	# successfully
	git rev-parse atomic2 >expected &&
	# on master...
	git -C "$d" rev-parse refs/heads/master >actual &&
	test_cmp expected actual &&
	# ...and collateral.
	git -C "$d" rev-parse refs/heads/collateral >actual &&
	test_cmp expected actual &&

	# the failed refs should be indicated to the user
	grep "^ ! .*rejected.* master -> master" output &&

	# the collateral failure refs should be indicated to the user
	grep "^ ! .*rejected.* atomic -> atomic .*atomic push failed" output &&
	grep "^ ! .*rejected.* collateral -> collateral .*atomic push failed" output
'

test_expect_success 'push --atomic fails on server-side errors' '
	# Use previously set up repository
	d=$HTTPD_DOCUMENT_ROOT_PATH/atomic-branches.git &&
	test_config -C "$d" http.receivepack true &&
	up="$HTTPD_URL"/smart/atomic-branches.git &&

	# break ref updates for other on the remote site
	mkdir "$d/refs/heads/other.lock" &&

	# add the new commit to other
	git branch -f other collateral &&

	# --atomic should cause entire push to be rejected
	test_must_fail git push --atomic "$up" atomic other 2>output  &&

	# the new branch should not have been created upstream
	test_must_fail git -C "$d" show-ref --verify refs/heads/atomic &&

	# upstream should still reflect atomic2, the last thing we pushed
	# successfully
	git rev-parse atomic2 >expected &&
	# ...to other.
	git -C "$d" rev-parse refs/heads/other >actual &&
	test_cmp expected actual &&

	# the new branch should not have been created upstream
	test_must_fail git -C "$d" show-ref --verify refs/heads/atomic &&

	# the failed refs should be indicated to the user
	grep "^ ! .*rejected.* other -> other .*atomic transaction failed" output &&

	# the collateral failure refs should be indicated to the user
	grep "^ ! .*rejected.* atomic -> atomic .*atomic transaction failed" output
'

test_expect_success 'push --all can push to empty repo' '
	d=$HTTPD_DOCUMENT_ROOT_PATH/empty-all.git &&
	git init --bare "$d" &&
	git --git-dir="$d" config http.receivepack true &&
	git push --all "$HTTPD_URL"/smart/empty-all.git
'

test_expect_success 'push --mirror can push to empty repo' '
	d=$HTTPD_DOCUMENT_ROOT_PATH/empty-mirror.git &&
	git init --bare "$d" &&
	git --git-dir="$d" config http.receivepack true &&
	git push --mirror "$HTTPD_URL"/smart/empty-mirror.git
'

test_expect_success 'push --all to repo with alternates' '
	s=$HTTPD_DOCUMENT_ROOT_PATH/test_repo.git &&
	d=$HTTPD_DOCUMENT_ROOT_PATH/alternates-all.git &&
	git clone --bare --shared "$s" "$d" &&
	git --git-dir="$d" config http.receivepack true &&
	git --git-dir="$d" repack -adl &&
	git push --all "$HTTPD_URL"/smart/alternates-all.git
'

test_expect_success 'push --mirror to repo with alternates' '
	s=$HTTPD_DOCUMENT_ROOT_PATH/test_repo.git &&
	d=$HTTPD_DOCUMENT_ROOT_PATH/alternates-mirror.git &&
	git clone --bare --shared "$s" "$d" &&
	git --git-dir="$d" config http.receivepack true &&
	git --git-dir="$d" repack -adl &&
	git push --mirror "$HTTPD_URL"/smart/alternates-mirror.git
'

test_expect_success TTY 'push shows progress when stderr is a tty' '
	cd "$ROOT_PATH"/test_repo_clone &&
	test_commit noisy &&
	test_terminal git push >output 2>&1 &&
	test_i18ngrep "^Writing objects" output
'

test_expect_success TTY 'push --quiet silences status and progress' '
	cd "$ROOT_PATH"/test_repo_clone &&
	test_commit quiet &&
	test_terminal git push --quiet >output 2>&1 &&
	test_must_be_empty output
'

test_expect_success TTY 'push --no-progress silences progress but not status' '
	cd "$ROOT_PATH"/test_repo_clone &&
	test_commit no-progress &&
	test_terminal git push --no-progress >output 2>&1 &&
	test_i18ngrep "^To http" output &&
	test_i18ngrep ! "^Writing objects" output
'

test_expect_success 'push --progress shows progress to non-tty' '
	cd "$ROOT_PATH"/test_repo_clone &&
	test_commit progress &&
	git push --progress >output 2>&1 &&
	test_i18ngrep "^To http" output &&
	test_i18ngrep "^Writing objects" output
'

test_expect_success 'http push gives sane defaults to reflog' '
	cd "$ROOT_PATH"/test_repo_clone &&
	test_commit reflog-test &&
	git push "$HTTPD_URL"/smart/test_repo.git &&
	git --git-dir="$HTTPD_DOCUMENT_ROOT_PATH/test_repo.git" \
		log -g -1 --format="%gn <%ge>" >actual &&
	echo "anonymous <anonymous@http.127.0.0.1>" >expect &&
	test_cmp expect actual
'

test_expect_success 'http push respects GIT_COMMITTER_* in reflog' '
	cd "$ROOT_PATH"/test_repo_clone &&
	test_commit custom-reflog-test &&
	git push "$HTTPD_URL"/smart_custom_env/test_repo.git &&
	git --git-dir="$HTTPD_DOCUMENT_ROOT_PATH/test_repo.git" \
		log -g -1 --format="%gn <%ge>" >actual &&
	echo "Custom User <custom@example.com>" >expect &&
	test_cmp expect actual
'

test_expect_success 'push over smart http with auth' '
	cd "$ROOT_PATH/test_repo_clone" &&
	echo push-auth-test >expect &&
	test_commit push-auth-test &&
	set_askpass user@host pass@host &&
	git push "$HTTPD_URL"/auth/smart/test_repo.git &&
	git --git-dir="$HTTPD_DOCUMENT_ROOT_PATH/test_repo.git" \
		log -1 --format=%s >actual &&
	expect_askpass both user@host &&
	test_cmp expect actual
'

test_expect_success 'push to auth-only-for-push repo' '
	cd "$ROOT_PATH/test_repo_clone" &&
	echo push-half-auth >expect &&
	test_commit push-half-auth &&
	set_askpass user@host pass@host &&
	git push "$HTTPD_URL"/auth-push/smart/test_repo.git &&
	git --git-dir="$HTTPD_DOCUMENT_ROOT_PATH/test_repo.git" \
		log -1 --format=%s >actual &&
	expect_askpass both user@host &&
	test_cmp expect actual
'

test_expect_success 'create repo without http.receivepack set' '
	cd "$ROOT_PATH" &&
	git init half-auth &&
	(
		cd half-auth &&
		test_commit one
	) &&
	git clone --bare half-auth "$HTTPD_DOCUMENT_ROOT_PATH/half-auth.git"
'

test_expect_success 'clone via half-auth-complete does not need password' '
	cd "$ROOT_PATH" &&
	set_askpass wrong &&
	git clone "$HTTPD_URL"/half-auth-complete/smart/half-auth.git \
		half-auth-clone &&
	expect_askpass none
'

test_expect_success 'push into half-auth-complete requires password' '
	cd "$ROOT_PATH/half-auth-clone" &&
	echo two >expect &&
	test_commit two &&
	set_askpass user@host pass@host &&
	git push "$HTTPD_URL/half-auth-complete/smart/half-auth.git" &&
	git --git-dir="$HTTPD_DOCUMENT_ROOT_PATH/half-auth.git" \
		log -1 --format=%s >actual &&
	expect_askpass both user@host &&
	test_cmp expect actual
'

test_expect_success CMDLINE_LIMIT 'push 2000 tags over http' '
	sha1=$(git rev-parse HEAD) &&
	test_seq 2000 |
	  sort |
	  sed "s|.*|$sha1 refs/tags/really-long-tag-name-&|" \
	  >.git/packed-refs &&
	run_with_limited_cmdline git push --mirror
'

test_expect_success GPG 'push with post-receive to inspect certificate' '
	(
		cd "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.git &&
		mkdir -p hooks &&
		write_script hooks/post-receive <<-\EOF &&
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

		git config receive.certnonceseed sekrit &&
		git config receive.certnonceslop 30
	) &&
	cd "$ROOT_PATH/test_repo_clone" &&
	test_commit cert-test &&
	git push --signed "$HTTPD_URL/smart/test_repo.git" &&
	(
		cd "$HTTPD_DOCUMENT_ROOT_PATH" &&
		cat <<-\EOF &&
		SIGNER=C O Mitter <committer@example.com>
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
	git push --porcelain \
		"$HTTPD_URL_USER_PASS/smart/test_repo.git" \
		+HEAD:scrub >status &&
	# should have been scrubbed down to vanilla URL
	grep "^To $HTTPD_URL/smart/test_repo.git" status
'

test_expect_success 'colorize errors/hints' '
	cd "$ROOT_PATH"/test_repo_clone &&
	test_must_fail git -c color.transport=always -c color.advice=always \
		-c color.push=always \
		push origin origin/master^:master 2>act &&
	test_decode_color <act >decoded &&
	test_i18ngrep "<RED>.*rejected.*<RESET>" decoded &&
	test_i18ngrep "<RED>error: failed to push some refs" decoded &&
	test_i18ngrep "<YELLOW>hint: " decoded &&
	test_i18ngrep ! "^hint: " decoded
'

test_done
