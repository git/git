#!/bin/sh

test_description='test dumb fetching over http via static file'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

if test_have_prereq !REFFILES
then
	skip_all='skipping test; dumb HTTP protocol not supported with reftable.'
	test_done
fi

. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success 'setup repository' '
	but config push.default matching &&
	echo content1 >file &&
	but add file &&
	but cummit -m one &&
	echo content2 >file &&
	but add file &&
	but cummit -m two
'

setup_post_update_server_info_hook () {
	test_hook --setup -C "$1" post-update <<-\EOF &&
	exec but update-server-info
	EOF
	but -C "$1" update-server-info
}

test_expect_success 'create http-accessible bare repository with loose objects' '
	cp -R .but "$HTTPD_DOCUMENT_ROOT_PATH/repo.but" &&
	but -C "$HTTPD_DOCUMENT_ROOT_PATH/repo.but" config core.bare true &&
	setup_post_update_server_info_hook "$HTTPD_DOCUMENT_ROOT_PATH/repo.but" &&
	but remote add public "$HTTPD_DOCUMENT_ROOT_PATH/repo.but" &&
	but push public main:main
'

test_expect_success 'clone http repository' '
	but clone $HTTPD_URL/dumb/repo.but clone-tmpl &&
	cp -R clone-tmpl clone &&
	test_cmp file clone/file
'

test_expect_success 'list refs from outside any repository' '
	cat >expect <<-EOF &&
	$(but rev-parse main)	HEAD
	$(but rev-parse main)	refs/heads/main
	EOF
	nonbut but ls-remote "$HTTPD_URL/dumb/repo.but" >actual &&
	test_cmp expect actual
'

test_expect_success 'create password-protected repository' '
	mkdir -p "$HTTPD_DOCUMENT_ROOT_PATH/auth/dumb/" &&
	cp -Rf "$HTTPD_DOCUMENT_ROOT_PATH/repo.but" \
	       "$HTTPD_DOCUMENT_ROOT_PATH/auth/dumb/repo.but"
'

test_expect_success 'create empty remote repository' '
	but init --bare "$HTTPD_DOCUMENT_ROOT_PATH/empty.but" &&
	setup_post_update_server_info_hook "$HTTPD_DOCUMENT_ROOT_PATH/empty.but"
'

test_expect_success 'empty dumb HTTP repository has default hash algorithm' '
	test_when_finished "rm -fr clone-empty" &&
	but clone $HTTPD_URL/dumb/empty.but clone-empty &&
	but -C clone-empty rev-parse --show-object-format >empty-format &&
	test "$(cat empty-format)" = "$(test_oid algo)"
'

setup_askpass_helper

test_expect_success 'cloning password-protected repository can fail' '
	set_askpass wrong &&
	test_must_fail but clone "$HTTPD_URL/auth/dumb/repo.but" clone-auth-fail &&
	expect_askpass both wrong
'

test_expect_success 'http auth can use user/pass in URL' '
	set_askpass wrong &&
	but clone "$HTTPD_URL_USER_PASS/auth/dumb/repo.but" clone-auth-none &&
	expect_askpass none
'

test_expect_success 'http auth can use just user in URL' '
	set_askpass wrong pass@host &&
	but clone "$HTTPD_URL_USER/auth/dumb/repo.but" clone-auth-pass &&
	expect_askpass pass user@host
'

test_expect_success 'http auth can request both user and pass' '
	set_askpass user@host pass@host &&
	but clone "$HTTPD_URL/auth/dumb/repo.but" clone-auth-both &&
	expect_askpass both user@host
'

test_expect_success 'http auth respects credential helper config' '
	test_config_global credential.helper "!f() {
		cat >/dev/null
		echo username=user@host
		echo password=pass@host
	}; f" &&
	set_askpass wrong &&
	but clone "$HTTPD_URL/auth/dumb/repo.but" clone-auth-helper &&
	expect_askpass none
'

test_expect_success 'http auth can get username from config' '
	test_config_global "credential.$HTTPD_URL.username" user@host &&
	set_askpass wrong pass@host &&
	but clone "$HTTPD_URL/auth/dumb/repo.but" clone-auth-user &&
	expect_askpass pass user@host
'

test_expect_success 'configured username does not override URL' '
	test_config_global "credential.$HTTPD_URL.username" wrong &&
	set_askpass wrong pass@host &&
	but clone "$HTTPD_URL_USER/auth/dumb/repo.but" clone-auth-user2 &&
	expect_askpass pass user@host
'

test_expect_success 'set up repo with http submodules' '
	but init super &&
	set_askpass user@host pass@host &&
	(
		cd super &&
		but submodule add "$HTTPD_URL/auth/dumb/repo.but" sub &&
		but cummit -m "add submodule"
	)
'

test_expect_success 'cmdline credential config passes to submodule via clone' '
	set_askpass wrong pass@host &&
	test_must_fail but clone --recursive super super-clone &&
	rm -rf super-clone &&

	set_askpass wrong pass@host &&
	but -c "credential.$HTTPD_URL.username=user@host" \
		clone --recursive super super-clone &&
	expect_askpass pass user@host
'

test_expect_success 'cmdline credential config passes submodule via fetch' '
	set_askpass wrong pass@host &&
	test_must_fail but -C super-clone fetch --recurse-submodules &&

	set_askpass wrong pass@host &&
	but -C super-clone \
	    -c "credential.$HTTPD_URL.username=user@host" \
	    fetch --recurse-submodules &&
	expect_askpass pass user@host
'

test_expect_success 'cmdline credential config passes submodule update' '
	# advance the submodule HEAD so that a fetch is required
	but cummit --allow-empty -m foo &&
	but push "$HTTPD_DOCUMENT_ROOT_PATH/auth/dumb/repo.but" HEAD &&
	sha1=$(but rev-parse HEAD) &&
	but -C super-clone update-index --cacheinfo 160000,$sha1,sub &&

	set_askpass wrong pass@host &&
	test_must_fail but -C super-clone submodule update &&

	set_askpass wrong pass@host &&
	but -C super-clone \
	    -c "credential.$HTTPD_URL.username=user@host" \
	    submodule update &&
	expect_askpass pass user@host
'

test_expect_success 'fetch changes via http' '
	echo content >>file &&
	but cummit -a -m two &&
	but push public &&
	(cd clone && but pull) &&
	test_cmp file clone/file
'

test_expect_success 'fetch changes via manual http-fetch' '
	cp -R clone-tmpl clone2 &&

	HEAD=$(but rev-parse --verify HEAD) &&
	(cd clone2 &&
	 but http-fetch -a -w heads/main-new $HEAD $(but config remote.origin.url) &&
	 but checkout main-new &&
	 test $HEAD = $(but rev-parse --verify HEAD)) &&
	test_cmp file clone2/file
'

test_expect_success 'manual http-fetch without -a works just as well' '
	cp -R clone-tmpl clone3 &&

	HEAD=$(but rev-parse --verify HEAD) &&
	(cd clone3 &&
	 but http-fetch -w heads/main-new $HEAD $(but config remote.origin.url) &&
	 but checkout main-new &&
	 test $HEAD = $(but rev-parse --verify HEAD)) &&
	test_cmp file clone3/file
'

test_expect_success 'http remote detects correct HEAD' '
	but push public main:other &&
	(cd clone &&
	 but remote set-head origin -d &&
	 but remote set-head origin -a &&
	 but symbolic-ref refs/remotes/origin/HEAD > output &&
	 echo refs/remotes/origin/main > expect &&
	 test_cmp expect output
	)
'

test_expect_success 'fetch packed objects' '
	cp -R "$HTTPD_DOCUMENT_ROOT_PATH"/repo.but "$HTTPD_DOCUMENT_ROOT_PATH"/repo_pack.but &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/repo_pack.but &&
	 but --bare repack -a -d
	) &&
	but clone $HTTPD_URL/dumb/repo_pack.but
'

test_expect_success 'http-fetch --packfile' '
	# Arbitrary hash. Use rev-parse so that we get one of the correct
	# length.
	ARBITRARY=$(but -C "$HTTPD_DOCUMENT_ROOT_PATH"/repo_pack.but rev-parse HEAD) &&

	but init packfileclient &&
	p=$(cd "$HTTPD_DOCUMENT_ROOT_PATH"/repo_pack.but && ls objects/pack/pack-*.pack) &&
	but -C packfileclient http-fetch --packfile=$ARBITRARY \
		--index-pack-arg=index-pack --index-pack-arg=--stdin \
		--index-pack-arg=--keep \
		"$HTTPD_URL"/dumb/repo_pack.but/$p >out &&

	grep "^keep.[0-9a-f]\{16,\}$" out &&
	cut -c6- out >packhash &&

	# Ensure that the expected files are generated
	test -e "packfileclient/.but/objects/pack/pack-$(cat packhash).pack" &&
	test -e "packfileclient/.but/objects/pack/pack-$(cat packhash).idx" &&
	test -e "packfileclient/.but/objects/pack/pack-$(cat packhash).keep" &&

	# Ensure that it has the HEAD of repo_pack, at least
	HASH=$(but -C "$HTTPD_DOCUMENT_ROOT_PATH"/repo_pack.but rev-parse HEAD) &&
	but -C packfileclient cat-file -e "$HASH"
'

test_expect_success 'fetch notices corrupt pack' '
	cp -R "$HTTPD_DOCUMENT_ROOT_PATH"/repo_pack.but "$HTTPD_DOCUMENT_ROOT_PATH"/repo_bad1.but &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/repo_bad1.but &&
	 p=$(ls objects/pack/pack-*.pack) &&
	 chmod u+w $p &&
	 printf %0256d 0 | dd of=$p bs=256 count=1 seek=1 conv=notrunc
	) &&
	mkdir repo_bad1.but &&
	(cd repo_bad1.but &&
	 but --bare init &&
	 test_must_fail but --bare fetch $HTTPD_URL/dumb/repo_bad1.but &&
	 test 0 = $(ls objects/pack/pack-*.pack | wc -l)
	)
'

test_expect_success 'http-fetch --packfile with corrupt pack' '
	rm -rf packfileclient &&
	but init packfileclient &&
	p=$(cd "$HTTPD_DOCUMENT_ROOT_PATH"/repo_bad1.but && ls objects/pack/pack-*.pack) &&
	test_must_fail but -C packfileclient http-fetch --packfile \
		"$HTTPD_URL"/dumb/repo_bad1.but/$p
'

test_expect_success 'fetch notices corrupt idx' '
	cp -R "$HTTPD_DOCUMENT_ROOT_PATH"/repo_pack.but "$HTTPD_DOCUMENT_ROOT_PATH"/repo_bad2.but &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/repo_bad2.but &&
	 p=$(ls objects/pack/pack-*.idx) &&
	 chmod u+w $p &&
	 printf %0256d 0 | dd of=$p bs=256 count=1 seek=1 conv=notrunc
	) &&
	mkdir repo_bad2.but &&
	(cd repo_bad2.but &&
	 but --bare init &&
	 test_must_fail but --bare fetch $HTTPD_URL/dumb/repo_bad2.but &&
	 test 0 = $(ls objects/pack | wc -l)
	)
'

test_expect_success 'fetch can handle previously-fetched .idx files' '
	but checkout --orphan branch1 &&
	echo base >file &&
	but add file &&
	but cummit -m base &&
	but --bare init "$HTTPD_DOCUMENT_ROOT_PATH"/repo_packed_branches.but &&
	but push "$HTTPD_DOCUMENT_ROOT_PATH"/repo_packed_branches.but branch1 &&
	but --but-dir="$HTTPD_DOCUMENT_ROOT_PATH"/repo_packed_branches.but repack -d &&
	but checkout -b branch2 branch1 &&
	echo b2 >>file &&
	but cummit -a -m b2 &&
	but push "$HTTPD_DOCUMENT_ROOT_PATH"/repo_packed_branches.but branch2 &&
	but --but-dir="$HTTPD_DOCUMENT_ROOT_PATH"/repo_packed_branches.but repack -d &&
	but --bare init clone_packed_branches.but &&
	but --but-dir=clone_packed_branches.but fetch "$HTTPD_URL"/dumb/repo_packed_branches.but branch1:branch1 &&
	but --but-dir=clone_packed_branches.but fetch "$HTTPD_URL"/dumb/repo_packed_branches.but branch2:branch2
'

test_expect_success 'did not use upload-pack service' '
	! grep "/but-upload-pack" "$HTTPD_ROOT_PATH/access.log"
'

test_expect_success 'but client shows text/plain errors' '
	test_must_fail but clone "$HTTPD_URL/error/text" 2>stderr &&
	grep "this is the error message" stderr
'

test_expect_success 'but client does not show html errors' '
	test_must_fail but clone "$HTTPD_URL/error/html" 2>stderr &&
	! grep "this is the error message" stderr
'

test_expect_success 'but client shows text/plain with a charset' '
	test_must_fail but clone "$HTTPD_URL/error/charset" 2>stderr &&
	grep "this is the error message" stderr
'

test_expect_success 'http error messages are reencoded' '
	test_must_fail but clone "$HTTPD_URL/error/utf16" 2>stderr &&
	grep "this is the error message" stderr
'

test_expect_success 'reencoding is robust to whitespace oddities' '
	test_must_fail but clone "$HTTPD_URL/error/odd-spacing" 2>stderr &&
	grep "this is the error message" stderr
'

check_language () {
	case "$2" in
	'')
		>expect
		;;
	?*)
		echo "=> Send header: Accept-Language: $1" >expect
		;;
	esac &&
	GIT_TRACE_CURL=true \
	LANGUAGE=$2 \
	but ls-remote "$HTTPD_URL/dumb/repo.but" >output 2>&1 &&
	tr -d '\015' <output |
	sort -u |
	sed -ne '/^=> Send header: Accept-Language:/ p' >actual &&
	test_cmp expect actual
}

test_expect_success 'but client sends Accept-Language based on LANGUAGE' '
	check_language "ko-KR, *;q=0.9" ko_KR.UTF-8'

test_expect_success 'but client sends Accept-Language correctly with unordinary LANGUAGE' '
	check_language "ko-KR, *;q=0.9" "ko_KR:" &&
	check_language "ko-KR, en-US;q=0.9, *;q=0.8" "ko_KR::en_US" &&
	check_language "ko-KR, *;q=0.9" ":::ko_KR" &&
	check_language "ko-KR, en-US;q=0.9, *;q=0.8" "ko_KR!!:en_US" &&
	check_language "ko-KR, ja-JP;q=0.9, *;q=0.8" "ko_KR en_US:ja_JP"'

test_expect_success 'but client sends Accept-Language with many preferred languages' '
	check_language "ko-KR, en-US;q=0.9, fr-CA;q=0.8, de;q=0.7, sr;q=0.6, \
ja;q=0.5, zh;q=0.4, sv;q=0.3, pt;q=0.2, *;q=0.1" \
		ko_KR.EUC-KR:en_US.UTF-8:fr_CA:de.UTF-8@euro:sr@latin:ja:zh:sv:pt &&
	check_language "ko-KR, en-US;q=0.99, fr-CA;q=0.98, de;q=0.97, sr;q=0.96, \
ja;q=0.95, zh;q=0.94, sv;q=0.93, pt;q=0.92, nb;q=0.91, *;q=0.90" \
		ko_KR.EUC-KR:en_US.UTF-8:fr_CA:de.UTF-8@euro:sr@latin:ja:zh:sv:pt:nb
'

test_expect_success 'but client does not send an empty Accept-Language' '
	GIT_TRACE_CURL=true LANGUAGE= but ls-remote "$HTTPD_URL/dumb/repo.but" 2>stderr &&
	! grep "^=> Send header: Accept-Language:" stderr
'

test_expect_success 'remote-http complains cleanly about malformed urls' '
	test_must_fail but remote-http http::/example.com/repo.but 2>stderr &&
	test_i18ngrep "url has no scheme" stderr
'

# NEEDSWORK: Writing commands to but-remote-curl can race against the latter
# erroring out, producing SIGPIPE. Remove "ok=sigpipe" once transport-helper has
# learned to handle early remote helper failures more cleanly.
test_expect_success 'remote-http complains cleanly about empty scheme' '
	test_must_fail ok=sigpipe but ls-remote \
		http::${HTTPD_URL#http}/dumb/repo.but 2>stderr &&
	test_i18ngrep "url has no scheme" stderr
'

test_expect_success 'redirects can be forbidden/allowed' '
	test_must_fail but -c http.followRedirects=false \
		clone $HTTPD_URL/dumb-redir/repo.but dumb-redir &&
	but -c http.followRedirects=true \
		clone $HTTPD_URL/dumb-redir/repo.but dumb-redir 2>stderr
'

test_expect_success 'redirects are reported to stderr' '
	# just look for a snippet of the redirected-to URL
	test_i18ngrep /dumb/ stderr
'

test_expect_success 'non-initial redirects can be forbidden' '
	test_must_fail but -c http.followRedirects=initial \
		clone $HTTPD_URL/redir-objects/repo.but redir-objects &&
	but -c http.followRedirects=true \
		clone $HTTPD_URL/redir-objects/repo.but redir-objects
'

test_expect_success 'http.followRedirects defaults to "initial"' '
	test_must_fail but clone $HTTPD_URL/redir-objects/repo.but default
'

# The goal is for a clone of the "evil" repository, which has no objects
# itself, to cause the client to fetch objects from the "victim" repository.
test_expect_success 'set up evil alternates scheme' '
	victim=$HTTPD_DOCUMENT_ROOT_PATH/victim.but &&
	but init --bare "$victim" &&
	but -C "$victim" --work-tree=. cummit --allow-empty -m secret &&
	but -C "$victim" repack -ad &&
	but -C "$victim" update-server-info &&
	sha1=$(but -C "$victim" rev-parse HEAD) &&

	evil=$HTTPD_DOCUMENT_ROOT_PATH/evil.but &&
	but init --bare "$evil" &&
	# do this by hand to avoid object existence check
	printf "%s\\t%s\\n" $sha1 refs/heads/main >"$evil/info/refs"
'

# Here we'll just redirect via HTTP. In a real-world attack these would be on
# different servers, but we should reject it either way.
test_expect_success 'http-alternates is a non-initial redirect' '
	echo "$HTTPD_URL/dumb/victim.but/objects" \
		>"$evil/objects/info/http-alternates" &&
	test_must_fail but -c http.followRedirects=initial \
		clone $HTTPD_URL/dumb/evil.but evil-initial &&
	but -c http.followRedirects=true \
		clone $HTTPD_URL/dumb/evil.but evil-initial
'

# Curl supports a lot of protocols that we'd prefer not to allow
# http-alternates to use, but it's hard to test whether curl has
# accessed, say, the SMTP protocol, because we are not running an SMTP server.
# But we can check that it does not allow access to file://, which would
# otherwise allow this clone to complete.
test_expect_success 'http-alternates cannot point at funny protocols' '
	echo "file://$victim/objects" >"$evil/objects/info/http-alternates" &&
	test_must_fail but -c http.followRedirects=true \
		clone "$HTTPD_URL/dumb/evil.but" evil-file
'

test_expect_success 'http-alternates triggers not-from-user protocol check' '
	echo "$HTTPD_URL/dumb/victim.but/objects" \
		>"$evil/objects/info/http-alternates" &&
	test_config_global http.followRedirects true &&
	test_must_fail but -c protocol.http.allow=user \
		clone $HTTPD_URL/dumb/evil.but evil-user &&
	but -c protocol.http.allow=always \
		clone $HTTPD_URL/dumb/evil.but evil-user
'

test_expect_success 'can redirect through non-"info/refs?service=but-upload-pack" URL' '
	but clone "$HTTPD_URL/redir-to/dumb/repo.but"
'

test_expect_success 'print HTTP error when any intermediate redirect throws error' '
	test_must_fail but clone "$HTTPD_URL/redir-to/502" 2> stderr &&
	test_i18ngrep "unable to access.*/redir-to/502" stderr
'

test_expect_success 'fetching via http alternates works' '
	parent=$HTTPD_DOCUMENT_ROOT_PATH/alt-parent.but &&
	but init --bare "$parent" &&
	but -C "$parent" --work-tree=. cummit --allow-empty -m foo &&
	but -C "$parent" update-server-info &&
	cummit=$(but -C "$parent" rev-parse HEAD) &&

	child=$HTTPD_DOCUMENT_ROOT_PATH/alt-child.but &&
	but init --bare "$child" &&
	echo "../../alt-parent.but/objects" >"$child/objects/info/alternates" &&
	but -C "$child" update-ref HEAD $cummit &&
	but -C "$child" update-server-info &&

	but -c http.followredirects=true clone "$HTTPD_URL/dumb/alt-child.but"
'

test_done
