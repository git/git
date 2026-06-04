#!/bin/sh
#
# Copyright (c) 2008 Clemens Buchacher <drizzd@aon.at>
#

test_description='test smart pushing over http via http-backend'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

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

test_expect_success 'clone remote repository' '
	rm -rf test_repo_clone &&
	git clone $HTTPD_URL/smart/test_repo.git test_repo_clone &&
	(
		cd test_repo_clone && git config push.default matching
	)
'

test_expect_success 'push to remote repository (standard)' '
	# Clear the log, so that the "used receive-pack service" test below
	# sees just what we did here.
	>"$HTTPD_ROOT_PATH"/access.log &&

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

test_expect_success 'used receive-pack service' '
	cat >exp <<-\EOF &&
	GET  /smart/test_repo.git/info/refs?service=git-receive-pack HTTP/1.1 200
	POST /smart/test_repo.git/git-receive-pack HTTP/1.1 200
	EOF

	check_access_log exp
'

test_expect_success 'push to remote repository (standard) with sending Accept-Language' '
	cat >exp <<-\EOF &&
	=> Send header: Accept-Language: ko-KR, *;q=0.9
	=> Send header: Accept-Language: ko-KR, *;q=0.9
	EOF

	cd "$ROOT_PATH"/test_repo_clone &&
	: >path_lang &&
	git add path_lang &&
	test_tick &&
	git commit -m path_lang &&
	HEAD=$(git rev-parse --verify HEAD) &&
	GIT_TRACE_CURL=true LANGUAGE="ko_KR.UTF-8" git push -v -v 2>err &&
	! grep "Expect: 100-continue" err &&

	grep "=> Send header: Accept-Language:" err >err.language &&
	test_cmp exp err.language
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

test_expect_success 'setup rejected update hook' '
	test_hook --setup -C "$HTTPD_DOCUMENT_ROOT_PATH/test_repo.git" update <<-\EOF &&
	exit 1
	EOF

	cat >exp <<-EOF
	remote: error: hook declined to update refs/heads/dev2
	To http://127.0.0.1:$LIB_HTTPD_PORT/smart/test_repo.git
	 ! [remote rejected] dev2 -> dev2 (hook declined)
	error: failed to push some refs to '\''http://127.0.0.1:$LIB_HTTPD_PORT/smart/test_repo.git'\''
	EOF
'

test_expect_success 'rejected update prints status' '
	cd "$ROOT_PATH"/test_repo_clone &&
	git checkout -b dev2 &&
	: >path4 &&
	git add path4 &&
	test_tick &&
	git commit -m dev2 &&
	test_must_fail git push origin dev2 2>act &&
	sed -e "/^remote: /s/ *$//" <act >cmp &&
	test_cmp exp cmp
'
rm -f "$HTTPD_DOCUMENT_ROOT_PATH/test_repo.git/hooks/update"

test_http_push_nonff "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.git \
	"$ROOT_PATH"/test_repo_clone main 		success

test_expect_success 'push fails for non-fast-forward refs unmatched by remote helper' '
	# create a dissimilarly-named remote ref so that git is unable to match the
	# two refs (viz. local, remote) unless an explicit refspec is provided.
	git push origin main:niam &&

	echo "change changed" > path2 &&
	git commit -a -m path2 --amend &&

	# push main too; this ensures there is at least one '"'push'"' command to
	# the remote helper and triggers interaction with the helper.
	test_must_fail git push -v origin +main main:niam >output 2>&1'

test_expect_success 'push fails for non-fast-forward refs unmatched by remote helper: remote output' '
	grep "^ + [a-f0-9]*\.\.\.[a-f0-9]* *main -> main (forced update)$" output &&
	grep "^ ! \[rejected\] *main -> niam (non-fast-forward)$" output
'

test_expect_success 'push fails for non-fast-forward refs unmatched by remote helper: our output' '
	test_grep "Updates were rejected because" \
		output
'

test_expect_success 'push (chunked)' '
	git checkout main &&
	test_commit commit path3 &&
	HEAD=$(git rev-parse --verify HEAD) &&
	test_config http.postbuffer 4 &&
	git push -v -v origin $BRANCH 2>err &&
	grep "POST git-receive-pack (chunked)" err &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.git &&
	 test $HEAD = $(git rev-parse --verify HEAD))
'

## References of remote: atomic1(1)            main(2) collateral(2) other(2)
## References of local :            atomic2(2) main(1) collateral(3) other(2) collateral1(3) atomic(1)
## Atomic push         :                       main(1) collateral(3)                         atomic(1)
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
	git push "$up" atomic1 main collateral other &&
	git tag -d atomic1 &&

	# collateral is a valid push, but should be failed by atomic push
	git checkout collateral &&
	test_commit collateral1 &&

	# Make main incompatible with upstream to provoke atomic
	git checkout main &&
	git reset --hard HEAD^ &&

	# Add a new branch which should be failed by atomic push. This is a
	# regression case.
	git branch atomic &&

	# --atomic should cause entire push to be rejected
	test_must_fail git push --atomic "$up" main atomic collateral 2>output &&

	# the new branch should not have been created upstream
	test_must_fail git -C "$d" show-ref --verify refs/heads/atomic &&

	# upstream should still reflect atomic2, the last thing we pushed
	# successfully
	git rev-parse atomic2 >expected &&
	# on main...
	git -C "$d" rev-parse refs/heads/main >actual &&
	test_cmp expected actual &&
	# ...and collateral.
	git -C "$d" rev-parse refs/heads/collateral >actual &&
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
	d=$HTTPD_DOCUMENT_ROOT_PATH/atomic-branches.git &&
	test_config -C "$d" http.receivepack true &&
	up="$HTTPD_URL"/smart/atomic-branches.git &&

	# Create d/f conflict to break ref updates for other on the remote site.
	git -C "$d" update-ref -d refs/heads/other &&
	git -C "$d" update-ref refs/heads/other/conflict HEAD &&

	# add the new commit to other
	git branch -f other collateral &&

	# --atomic should cause entire push to be rejected
	test_must_fail git push --atomic "$up" atomic other 2>output  &&

	# The atomic and other branches should not be created upstream.
	test_must_fail git -C "$d" show-ref --verify refs/heads/atomic &&
	test_must_fail git -C "$d" show-ref --verify refs/heads/other &&

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
	test_grep "^Writing objects" output
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
	test_grep "^To http" output &&
	test_grep ! "^Writing objects" output
'

test_expect_success 'push --progress shows progress to non-tty' '
	cd "$ROOT_PATH"/test_repo_clone &&
	test_commit progress &&
	git push --progress >output 2>&1 &&
	test_grep "^To http" output &&
	test_grep "^Writing objects" output
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
	expect_askpass both user%40host &&
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
	expect_askpass both user%40host &&
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
	expect_askpass both user%40host &&
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
	test_hook -C "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.git post-receive <<-\EOF &&
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
	(
		cd "$HTTPD_DOCUMENT_ROOT_PATH"/test_repo.git &&
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

test_expect_success 'clone/fetch scrubs password from reflogs' '
	cd "$ROOT_PATH" &&
	git clone "$HTTPD_URL_USER_PASS/smart/test_repo.git" \
		reflog-test &&
	cd reflog-test &&
	test_commit prepare-for-force-fetch &&
	git switch -c away &&
	git fetch "$HTTPD_URL_USER_PASS/smart/test_repo.git" \
		+main:main &&
	# should have been scrubbed down to vanilla URL
	git log -g main >reflog &&
	grep "$HTTPD_URL" reflog &&
	! grep "$HTTPD_URL_USER_PASS" reflog
'

test_expect_success 'Non-ASCII branch name can be used with --force-with-lease' '
	cd "$ROOT_PATH" &&
	git clone "$HTTPD_URL_USER_PASS/smart/test_repo.git" non-ascii &&
	cd non-ascii &&
	git checkout -b rama-de-árbol &&
	test_commit F &&
	git push --force-with-lease origin rama-de-árbol &&
	git ls-remote origin refs/heads/rama-de-árbol >actual &&
	git ls-remote . refs/heads/rama-de-árbol >expect &&
	test_cmp expect actual &&
	git push --delete --force-with-lease origin rama-de-árbol &&
	git ls-remote origin refs/heads/rama-de-árbol >actual &&
	test_must_be_empty actual
'

test_expect_success 'colorize errors/hints' '
	cd "$ROOT_PATH"/test_repo_clone &&
	test_must_fail git -c color.transport=always -c color.advice=always \
		-c color.push=always \
		push origin origin/main^:main 2>act &&
	test_decode_color <act >decoded &&
	test_grep "<RED>.*rejected.*<RESET>" decoded &&
	test_grep "<RED>error: failed to push some refs" decoded &&
	test_grep "<YELLOW>hint: " decoded &&
	test_grep ! "^hint: " decoded
'

test_expect_success 'report error server does not provide ref status' '
	git init "$HTTPD_DOCUMENT_ROOT_PATH/no_report" &&
	git -C "$HTTPD_DOCUMENT_ROOT_PATH/no_report" config http.receivepack true &&
	test_must_fail git push --porcelain \
		$HTTPD_URL_USER_PASS/smart/no_report \
		HEAD:refs/tags/will-fail >actual &&
	test_must_fail git -C "$HTTPD_DOCUMENT_ROOT_PATH/no_report" \
		rev-parse --verify refs/tags/will-fail &&
	cat >expect <<-EOF &&
	To $HTTPD_URL/smart/no_report
	!	HEAD:refs/tags/will-fail	[remote failure] (remote failed to report status)
	Done
	EOF
	test_cmp expect actual
'

test_done
