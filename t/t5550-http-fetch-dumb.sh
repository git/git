#!/bin/sh

test_description='test dumb fetching over http via static file'
. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-httpd.sh
start_httpd

test_expect_success 'setup repository' '
	git config push.default matching &&
	echo content1 >file &&
	git add file &&
	git commit -m one &&
	echo content2 >file &&
	git add file &&
	git commit -m two
'

test_expect_success 'create http-accessible bare repository with loose objects' '
	cp -R .git "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	 git config core.bare true &&
	 mkdir -p hooks &&
	 write_script "hooks/post-update" <<-\EOF &&
	 exec git update-server-info
	EOF
	 hooks/post-update
	) &&
	git remote add public "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" &&
	git push public master:master
'

test_expect_success 'clone http repository' '
	git clone $HTTPD_URL/dumb/repo.git clone-tmpl &&
	cp -R clone-tmpl clone &&
	test_cmp file clone/file
'

test_expect_success 'list refs from outside any repository' '
	cat >expect <<-EOF &&
	$(git rev-parse master)	HEAD
	$(git rev-parse master)	refs/heads/master
	EOF
	nongit git ls-remote "$HTTPD_URL/dumb/repo.git" >actual &&
	test_cmp expect actual
'

test_expect_success 'create password-protected repository' '
	mkdir -p "$HTTPD_DOCUMENT_ROOT_PATH/auth/dumb/" &&
	cp -Rf "$HTTPD_DOCUMENT_ROOT_PATH/repo.git" \
	       "$HTTPD_DOCUMENT_ROOT_PATH/auth/dumb/repo.git"
'

setup_askpass_helper

test_expect_success 'cloning password-protected repository can fail' '
	set_askpass wrong &&
	test_must_fail git clone "$HTTPD_URL/auth/dumb/repo.git" clone-auth-fail &&
	expect_askpass both wrong
'

test_expect_success 'http auth can use user/pass in URL' '
	set_askpass wrong &&
	git clone "$HTTPD_URL_USER_PASS/auth/dumb/repo.git" clone-auth-none &&
	expect_askpass none
'

test_expect_success 'http auth can use just user in URL' '
	set_askpass wrong pass@host &&
	git clone "$HTTPD_URL_USER/auth/dumb/repo.git" clone-auth-pass &&
	expect_askpass pass user@host
'

test_expect_success 'http auth can request both user and pass' '
	set_askpass user@host pass@host &&
	git clone "$HTTPD_URL/auth/dumb/repo.git" clone-auth-both &&
	expect_askpass both user@host
'

test_expect_success 'http auth respects credential helper config' '
	test_config_global credential.helper "!f() {
		cat >/dev/null
		echo username=user@host
		echo password=pass@host
	}; f" &&
	set_askpass wrong &&
	git clone "$HTTPD_URL/auth/dumb/repo.git" clone-auth-helper &&
	expect_askpass none
'

test_expect_success 'http auth can get username from config' '
	test_config_global "credential.$HTTPD_URL.username" user@host &&
	set_askpass wrong pass@host &&
	git clone "$HTTPD_URL/auth/dumb/repo.git" clone-auth-user &&
	expect_askpass pass user@host
'

test_expect_success 'configured username does not override URL' '
	test_config_global "credential.$HTTPD_URL.username" wrong &&
	set_askpass wrong pass@host &&
	git clone "$HTTPD_URL_USER/auth/dumb/repo.git" clone-auth-user2 &&
	expect_askpass pass user@host
'

test_expect_success 'set up repo with http submodules' '
	git init super &&
	set_askpass user@host pass@host &&
	(
		cd super &&
		git submodule add "$HTTPD_URL/auth/dumb/repo.git" sub &&
		git commit -m "add submodule"
	)
'

test_expect_success 'cmdline credential config passes to submodule via clone' '
	set_askpass wrong pass@host &&
	test_must_fail git clone --recursive super super-clone &&
	rm -rf super-clone &&

	set_askpass wrong pass@host &&
	git -c "credential.$HTTPD_URL.username=user@host" \
		clone --recursive super super-clone &&
	expect_askpass pass user@host
'

test_expect_success 'cmdline credential config passes submodule via fetch' '
	set_askpass wrong pass@host &&
	test_must_fail git -C super-clone fetch --recurse-submodules &&

	set_askpass wrong pass@host &&
	git -C super-clone \
	    -c "credential.$HTTPD_URL.username=user@host" \
	    fetch --recurse-submodules &&
	expect_askpass pass user@host
'

test_expect_success 'cmdline credential config passes submodule update' '
	# advance the submodule HEAD so that a fetch is required
	git commit --allow-empty -m foo &&
	git push "$HTTPD_DOCUMENT_ROOT_PATH/auth/dumb/repo.git" HEAD &&
	sha1=$(git rev-parse HEAD) &&
	git -C super-clone update-index --cacheinfo 160000,$sha1,sub &&

	set_askpass wrong pass@host &&
	test_must_fail git -C super-clone submodule update &&

	set_askpass wrong pass@host &&
	git -C super-clone \
	    -c "credential.$HTTPD_URL.username=user@host" \
	    submodule update &&
	expect_askpass pass user@host
'

test_expect_success 'fetch changes via http' '
	echo content >>file &&
	git commit -a -m two &&
	git push public &&
	(cd clone && git pull) &&
	test_cmp file clone/file
'

test_expect_success 'fetch changes via manual http-fetch' '
	cp -R clone-tmpl clone2 &&

	HEAD=$(git rev-parse --verify HEAD) &&
	(cd clone2 &&
	 git http-fetch -a -w heads/master-new $HEAD $(git config remote.origin.url) &&
	 git checkout master-new &&
	 test $HEAD = $(git rev-parse --verify HEAD)) &&
	test_cmp file clone2/file
'

test_expect_success 'manual http-fetch without -a works just as well' '
	cp -R clone-tmpl clone3 &&

	HEAD=$(git rev-parse --verify HEAD) &&
	(cd clone3 &&
	 git http-fetch -w heads/master-new $HEAD $(git config remote.origin.url) &&
	 git checkout master-new &&
	 test $HEAD = $(git rev-parse --verify HEAD)) &&
	test_cmp file clone3/file
'

test_expect_success 'http remote detects correct HEAD' '
	git push public master:other &&
	(cd clone &&
	 git remote set-head origin -d &&
	 git remote set-head origin -a &&
	 git symbolic-ref refs/remotes/origin/HEAD > output &&
	 echo refs/remotes/origin/master > expect &&
	 test_cmp expect output
	)
'

test_expect_success 'fetch packed objects' '
	cp -R "$HTTPD_DOCUMENT_ROOT_PATH"/repo.git "$HTTPD_DOCUMENT_ROOT_PATH"/repo_pack.git &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/repo_pack.git &&
	 git --bare repack -a -d
	) &&
	git clone $HTTPD_URL/dumb/repo_pack.git
'

test_expect_success 'fetch notices corrupt pack' '
	cp -R "$HTTPD_DOCUMENT_ROOT_PATH"/repo_pack.git "$HTTPD_DOCUMENT_ROOT_PATH"/repo_bad1.git &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/repo_bad1.git &&
	 p=$(ls objects/pack/pack-*.pack) &&
	 chmod u+w $p &&
	 printf %0256d 0 | dd of=$p bs=256 count=1 seek=1 conv=notrunc
	) &&
	mkdir repo_bad1.git &&
	(cd repo_bad1.git &&
	 git --bare init &&
	 test_must_fail git --bare fetch $HTTPD_URL/dumb/repo_bad1.git &&
	 test 0 = $(ls objects/pack/pack-*.pack | wc -l)
	)
'

test_expect_success 'fetch notices corrupt idx' '
	cp -R "$HTTPD_DOCUMENT_ROOT_PATH"/repo_pack.git "$HTTPD_DOCUMENT_ROOT_PATH"/repo_bad2.git &&
	(cd "$HTTPD_DOCUMENT_ROOT_PATH"/repo_bad2.git &&
	 p=$(ls objects/pack/pack-*.idx) &&
	 chmod u+w $p &&
	 printf %0256d 0 | dd of=$p bs=256 count=1 seek=1 conv=notrunc
	) &&
	mkdir repo_bad2.git &&
	(cd repo_bad2.git &&
	 git --bare init &&
	 test_must_fail git --bare fetch $HTTPD_URL/dumb/repo_bad2.git &&
	 test 0 = $(ls objects/pack | wc -l)
	)
'

test_expect_success 'fetch can handle previously-fetched .idx files' '
	git checkout --orphan branch1 &&
	echo base >file &&
	git add file &&
	git commit -m base &&
	git --bare init "$HTTPD_DOCUMENT_ROOT_PATH"/repo_packed_branches.git &&
	git push "$HTTPD_DOCUMENT_ROOT_PATH"/repo_packed_branches.git branch1 &&
	git --git-dir="$HTTPD_DOCUMENT_ROOT_PATH"/repo_packed_branches.git repack -d &&
	git checkout -b branch2 branch1 &&
	echo b2 >>file &&
	git commit -a -m b2 &&
	git push "$HTTPD_DOCUMENT_ROOT_PATH"/repo_packed_branches.git branch2 &&
	git --git-dir="$HTTPD_DOCUMENT_ROOT_PATH"/repo_packed_branches.git repack -d &&
	git --bare init clone_packed_branches.git &&
	git --git-dir=clone_packed_branches.git fetch "$HTTPD_URL"/dumb/repo_packed_branches.git branch1:branch1 &&
	git --git-dir=clone_packed_branches.git fetch "$HTTPD_URL"/dumb/repo_packed_branches.git branch2:branch2
'

test_expect_success 'did not use upload-pack service' '
	test_might_fail grep '/git-upload-pack' <"$HTTPD_ROOT_PATH"/access.log >act &&
	: >exp &&
	test_cmp exp act
'

test_expect_success 'git client shows text/plain errors' '
	test_must_fail git clone "$HTTPD_URL/error/text" 2>stderr &&
	grep "this is the error message" stderr
'

test_expect_success 'git client does not show html errors' '
	test_must_fail git clone "$HTTPD_URL/error/html" 2>stderr &&
	! grep "this is the error message" stderr
'

test_expect_success 'git client shows text/plain with a charset' '
	test_must_fail git clone "$HTTPD_URL/error/charset" 2>stderr &&
	grep "this is the error message" stderr
'

test_expect_success 'http error messages are reencoded' '
	test_must_fail git clone "$HTTPD_URL/error/utf16" 2>stderr &&
	grep "this is the error message" stderr
'

test_expect_success 'reencoding is robust to whitespace oddities' '
	test_must_fail git clone "$HTTPD_URL/error/odd-spacing" 2>stderr &&
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
	git ls-remote "$HTTPD_URL/dumb/repo.git" >output 2>&1 &&
	tr -d '\015' <output |
	sort -u |
	sed -ne '/^=> Send header: Accept-Language:/ p' >actual &&
	test_cmp expect actual
}

test_expect_success 'git client sends Accept-Language based on LANGUAGE' '
	check_language "ko-KR, *;q=0.9" ko_KR.UTF-8'

test_expect_success 'git client sends Accept-Language correctly with unordinary LANGUAGE' '
	check_language "ko-KR, *;q=0.9" "ko_KR:" &&
	check_language "ko-KR, en-US;q=0.9, *;q=0.8" "ko_KR::en_US" &&
	check_language "ko-KR, *;q=0.9" ":::ko_KR" &&
	check_language "ko-KR, en-US;q=0.9, *;q=0.8" "ko_KR!!:en_US" &&
	check_language "ko-KR, ja-JP;q=0.9, *;q=0.8" "ko_KR en_US:ja_JP"'

test_expect_success 'git client sends Accept-Language with many preferred languages' '
	check_language "ko-KR, en-US;q=0.9, fr-CA;q=0.8, de;q=0.7, sr;q=0.6, \
ja;q=0.5, zh;q=0.4, sv;q=0.3, pt;q=0.2, *;q=0.1" \
		ko_KR.EUC-KR:en_US.UTF-8:fr_CA:de.UTF-8@euro:sr@latin:ja:zh:sv:pt &&
	check_language "ko-KR, en-US;q=0.99, fr-CA;q=0.98, de;q=0.97, sr;q=0.96, \
ja;q=0.95, zh;q=0.94, sv;q=0.93, pt;q=0.92, nb;q=0.91, *;q=0.90" \
		ko_KR.EUC-KR:en_US.UTF-8:fr_CA:de.UTF-8@euro:sr@latin:ja:zh:sv:pt:nb
'

test_expect_success 'git client does not send an empty Accept-Language' '
	GIT_TRACE_CURL=true LANGUAGE= git ls-remote "$HTTPD_URL/dumb/repo.git" 2>stderr &&
	! grep "^=> Send header: Accept-Language:" stderr
'

test_expect_success 'remote-http complains cleanly about malformed urls' '
	# do not actually issue "list" or other commands, as we do not
	# want to rely on what curl would actually do with such a broken
	# URL. This is just about making sure we do not segfault during
	# initialization.
	test_must_fail git remote-http http::/example.com/repo.git
'

test_expect_success 'redirects can be forbidden/allowed' '
	test_must_fail git -c http.followRedirects=false \
		clone $HTTPD_URL/dumb-redir/repo.git dumb-redir &&
	git -c http.followRedirects=true \
		clone $HTTPD_URL/dumb-redir/repo.git dumb-redir 2>stderr
'

test_expect_success 'redirects are reported to stderr' '
	# just look for a snippet of the redirected-to URL
	test_i18ngrep /dumb/ stderr
'

test_expect_success 'non-initial redirects can be forbidden' '
	test_must_fail git -c http.followRedirects=initial \
		clone $HTTPD_URL/redir-objects/repo.git redir-objects &&
	git -c http.followRedirects=true \
		clone $HTTPD_URL/redir-objects/repo.git redir-objects
'

test_expect_success 'http.followRedirects defaults to "initial"' '
	test_must_fail git clone $HTTPD_URL/redir-objects/repo.git default
'

# The goal is for a clone of the "evil" repository, which has no objects
# itself, to cause the client to fetch objects from the "victim" repository.
test_expect_success 'set up evil alternates scheme' '
	victim=$HTTPD_DOCUMENT_ROOT_PATH/victim.git &&
	git init --bare "$victim" &&
	git -C "$victim" --work-tree=. commit --allow-empty -m secret &&
	git -C "$victim" repack -ad &&
	git -C "$victim" update-server-info &&
	sha1=$(git -C "$victim" rev-parse HEAD) &&

	evil=$HTTPD_DOCUMENT_ROOT_PATH/evil.git &&
	git init --bare "$evil" &&
	# do this by hand to avoid object existence check
	printf "%s\\t%s\\n" $sha1 refs/heads/master >"$evil/info/refs"
'

# Here we'll just redirect via HTTP. In a real-world attack these would be on
# different servers, but we should reject it either way.
test_expect_success 'http-alternates is a non-initial redirect' '
	echo "$HTTPD_URL/dumb/victim.git/objects" \
		>"$evil/objects/info/http-alternates" &&
	test_must_fail git -c http.followRedirects=initial \
		clone $HTTPD_URL/dumb/evil.git evil-initial &&
	git -c http.followRedirects=true \
		clone $HTTPD_URL/dumb/evil.git evil-initial
'

# Curl supports a lot of protocols that we'd prefer not to allow
# http-alternates to use, but it's hard to test whether curl has
# accessed, say, the SMTP protocol, because we are not running an SMTP server.
# But we can check that it does not allow access to file://, which would
# otherwise allow this clone to complete.
test_expect_success 'http-alternates cannot point at funny protocols' '
	echo "file://$victim/objects" >"$evil/objects/info/http-alternates" &&
	test_must_fail git -c http.followRedirects=true \
		clone "$HTTPD_URL/dumb/evil.git" evil-file
'

test_expect_success 'http-alternates triggers not-from-user protocol check' '
	echo "$HTTPD_URL/dumb/victim.git/objects" \
		>"$evil/objects/info/http-alternates" &&
	test_config_global http.followRedirects true &&
	test_must_fail git -c protocol.http.allow=user \
		clone $HTTPD_URL/dumb/evil.git evil-user &&
	git -c protocol.http.allow=always \
		clone $HTTPD_URL/dumb/evil.git evil-user
'

test_expect_success 'can redirect through non-"info/refs?service=git-upload-pack" URL' '
	git clone "$HTTPD_URL/redir-to/dumb/repo.git"
'

test_expect_success 'print HTTP error when any intermediate redirect throws error' '
	test_must_fail git clone "$HTTPD_URL/redir-to/502" 2> stderr &&
	test_i18ngrep "unable to access.*/redir-to/502" stderr
'

test_expect_success 'fetching via http alternates works' '
	parent=$HTTPD_DOCUMENT_ROOT_PATH/alt-parent.git &&
	git init --bare "$parent" &&
	git -C "$parent" --work-tree=. commit --allow-empty -m foo &&
	git -C "$parent" update-server-info &&
	commit=$(git -C "$parent" rev-parse HEAD) &&

	child=$HTTPD_DOCUMENT_ROOT_PATH/alt-child.git &&
	git init --bare "$child" &&
	echo "../../alt-parent.git/objects" >"$child/objects/info/alternates" &&
	git -C "$child" update-ref HEAD $commit &&
	git -C "$child" update-server-info &&

	git -c http.followredirects=true clone "$HTTPD_URL/dumb/alt-child.git"
'

test_done
