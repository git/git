#!/bin/sh

test_description='test fetching over but protocol'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-but-daemon.sh
start_but_daemon

check_verbose_connect () {
	test_i18ngrep -F "Looking up 127.0.0.1 ..." stderr &&
	test_i18ngrep -F "Connecting to 127.0.0.1 (port " stderr &&
	test_i18ngrep -F "done." stderr
}

test_expect_success 'setup repository' '
	but config push.default matching &&
	echo content >file &&
	but add file &&
	but cummit -m one
'

test_expect_success 'create but-accessible bare repository' '
	mkdir "$GIT_DAEMON_DOCUMENT_ROOT_PATH/repo.but" &&
	(cd "$GIT_DAEMON_DOCUMENT_ROOT_PATH/repo.but" &&
	 but --bare init &&
	 : >but-daemon-export-ok
	) &&
	but remote add public "$GIT_DAEMON_DOCUMENT_ROOT_PATH/repo.but" &&
	but push public main:main
'

test_expect_success 'clone but repository' '
	but clone -v "$GIT_DAEMON_URL/repo.but" clone 2>stderr &&
	check_verbose_connect &&
	test_cmp file clone/file
'

test_expect_success 'fetch changes via but protocol' '
	echo content >>file &&
	but cummit -a -m two &&
	but push public &&
	(cd clone && but pull -v) 2>stderr &&
	check_verbose_connect &&
	test_cmp file clone/file
'

test_expect_success 'no-op fetch -v stderr is as expected' '
	(cd clone && but fetch -v) 2>stderr &&
	check_verbose_connect
'

test_expect_success 'no-op fetch without "-v" is quiet' '
	(cd clone && but fetch 2>../stderr) &&
	test_must_be_empty stderr
'

test_expect_success 'remote detects correct HEAD' '
	but push public main:other &&
	(cd clone &&
	 but remote set-head -d origin &&
	 but remote set-head -a origin &&
	 but symbolic-ref refs/remotes/origin/HEAD > output &&
	 echo refs/remotes/origin/main > expect &&
	 test_cmp expect output
	)
'

test_expect_success 'prepare pack objects' '
	cp -R "$GIT_DAEMON_DOCUMENT_ROOT_PATH"/repo.but "$GIT_DAEMON_DOCUMENT_ROOT_PATH"/repo_pack.but &&
	(cd "$GIT_DAEMON_DOCUMENT_ROOT_PATH"/repo_pack.but &&
	 but --bare repack -a -d
	)
'

test_expect_success 'fetch notices corrupt pack' '
	cp -R "$GIT_DAEMON_DOCUMENT_ROOT_PATH"/repo_pack.but "$GIT_DAEMON_DOCUMENT_ROOT_PATH"/repo_bad1.but &&
	(cd "$GIT_DAEMON_DOCUMENT_ROOT_PATH"/repo_bad1.but &&
	 p=$(ls objects/pack/pack-*.pack) &&
	 chmod u+w $p &&
	 printf %0256d 0 | dd of=$p bs=256 count=1 seek=1 conv=notrunc
	) &&
	mkdir repo_bad1.but &&
	(cd repo_bad1.but &&
	 but --bare init &&
	 test_must_fail but --bare fetch "$GIT_DAEMON_URL/repo_bad1.but" &&
	 test 0 = $(ls objects/pack/pack-*.pack | wc -l)
	)
'

test_expect_success 'fetch notices corrupt idx' '
	cp -R "$GIT_DAEMON_DOCUMENT_ROOT_PATH"/repo_pack.but "$GIT_DAEMON_DOCUMENT_ROOT_PATH"/repo_bad2.but &&
	(cd "$GIT_DAEMON_DOCUMENT_ROOT_PATH"/repo_bad2.but &&
	 rm -f objects/pack/multi-pack-index &&
	 p=$(ls objects/pack/pack-*.idx) &&
	 chmod u+w $p &&
	 printf %0256d 0 | dd of=$p bs=256 count=1 seek=1 conv=notrunc
	) &&
	mkdir repo_bad2.but &&
	(cd repo_bad2.but &&
	 but --bare init &&
	 test_must_fail but --bare fetch "$GIT_DAEMON_URL/repo_bad2.but" &&
	 test 0 = $(ls objects/pack | wc -l)
	)
'

test_expect_success 'client refuses to ask for repo with newline' '
	test_must_fail but clone "$GIT_DAEMON_URL/repo$LF.but" dst 2>stderr &&
	test_i18ngrep newline.is.forbidden stderr
'

test_remote_error()
{
	do_export=YesPlease
	while test $# -gt 0
	do
		case $1 in
		-x)
			shift
			chmod -x "$GIT_DAEMON_DOCUMENT_ROOT_PATH/repo.but"
			;;
		-n)
			shift
			do_export=
			;;
		*)
			break
		esac
	done

	msg=$1
	shift
	cmd=$1
	shift
	repo=$1
	shift || error "invalid number of arguments"

	if test -x "$GIT_DAEMON_DOCUMENT_ROOT_PATH/$repo"
	then
		if test -n "$do_export"
		then
			: >"$GIT_DAEMON_DOCUMENT_ROOT_PATH/$repo/but-daemon-export-ok"
		else
			rm -f "$GIT_DAEMON_DOCUMENT_ROOT_PATH/$repo/but-daemon-export-ok"
		fi
	fi

	test_must_fail but "$cmd" "$GIT_DAEMON_URL/$repo" "$@" 2>output &&
	test_i18ngrep "fatal: remote error: $msg: /$repo" output &&
	ret=$?
	chmod +x "$GIT_DAEMON_DOCUMENT_ROOT_PATH/repo.but"
	(exit $ret)
}

msg="access denied or repository not exported"
test_expect_success 'clone non-existent' "test_remote_error    '$msg' clone nowhere.but"
test_expect_success 'push disabled'      "test_remote_error    '$msg' push  repo.but main"
test_expect_success 'read access denied' "test_remote_error -x '$msg' fetch repo.but"
test_expect_success 'not exported'       "test_remote_error -n '$msg' fetch repo.but"

stop_but_daemon
start_but_daemon --informative-errors

test_expect_success 'clone non-existent' "test_remote_error    'no such repository'      clone nowhere.but"
test_expect_success 'push disabled'      "test_remote_error    'service not enabled'     push  repo.but main"
test_expect_success 'read access denied' "test_remote_error -x 'no such repository'      fetch repo.but"
test_expect_success 'not exported'       "test_remote_error -n 'repository not exported' fetch repo.but"

stop_but_daemon
start_but_daemon --interpolated-path="$GIT_DAEMON_DOCUMENT_ROOT_PATH/%H%D"

test_expect_success 'access repo via interpolated hostname' '
	repo="$GIT_DAEMON_DOCUMENT_ROOT_PATH/localhost/interp.but" &&
	but init --bare "$repo" &&
	but push "$repo" HEAD &&
	>"$repo"/but-daemon-export-ok &&
	GIT_OVERRIDE_VIRTUAL_HOST=localhost \
		but ls-remote "$GIT_DAEMON_URL/interp.but" &&
	GIT_OVERRIDE_VIRTUAL_HOST=LOCALHOST \
		but ls-remote "$GIT_DAEMON_URL/interp.but"
'

test_expect_success 'hostname cannot break out of directory' '
	repo="$GIT_DAEMON_DOCUMENT_ROOT_PATH/../escape.but" &&
	but init --bare "$repo" &&
	but push "$repo" HEAD &&
	>"$repo"/but-daemon-export-ok &&
	test_must_fail \
		env GIT_OVERRIDE_VIRTUAL_HOST=.. \
		but ls-remote "$GIT_DAEMON_URL/escape.but"
'

test_expect_success FAKENC 'hostname interpolation works after LF-stripping' '
	{
		printf "but-upload-pack /interp.but\n\0host=localhost" | packetize_raw &&
		printf "0000"
	} >input &&
	fake_nc "$GIT_DAEMON_HOST_PORT" <input >output &&
	depacketize <output >output.raw &&

	# just pick out the value of main, which avoids any protocol
	# particulars
	perl -lne "print \$1 if m{^(\\S+) refs/heads/main}" <output.raw >actual &&
	but -C "$repo" rev-parse main >expect &&
	test_cmp expect actual
'

test_done
