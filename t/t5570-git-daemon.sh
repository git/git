#!/bin/sh

test_description='test fetching over git protocol'
GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-git-daemon.sh

test_expect_success 'daemon rejects invalid --init-timeout values' '
	for arg in "3a" "-3"
	do
		test_must_fail git daemon --init-timeout="$arg" 2>err &&
		test_grep "fatal: invalid init-timeout ${SQ}$arg${SQ}, expecting a non-negative integer" err ||
		return 1
	done
'

test_expect_success 'daemon rejects invalid --timeout values' '
	for arg in "3a" "-3"
	do
		test_must_fail git daemon --timeout="$arg" 2>err &&
		test_grep "fatal: invalid timeout ${SQ}$arg${SQ}, expecting a non-negative integer" err ||
		return 1
	done
'

test_expect_success 'daemon rejects invalid --max-connections values' '
	arg='3a' &&
	test_must_fail git daemon --max-connections=3a 2>err &&
	test_grep "fatal: invalid max-connections ${SQ}$arg${SQ}, expecting an integer" err
'

start_git_daemon

check_verbose_connect () {
	test_grep -F "Looking up 127.0.0.1 ..." stderr &&
	test_grep -F "Connecting to 127.0.0.1 (port " stderr &&
	test_grep -F "done." stderr
}

test_expect_success 'setup repository' '
	git config push.default matching &&
	echo content >file &&
	git add file &&
	git commit -m one
'

test_expect_success 'create git-accessible bare repository' '
	mkdir "$GIT_DAEMON_DOCUMENT_ROOT_PATH/repo.git" &&
	(cd "$GIT_DAEMON_DOCUMENT_ROOT_PATH/repo.git" &&
	 git --bare init &&
	 : >git-daemon-export-ok
	) &&
	git remote add public "$GIT_DAEMON_DOCUMENT_ROOT_PATH/repo.git" &&
	git push public main:main
'

test_expect_success 'clone git repository' '
	git clone -v "$GIT_DAEMON_URL/repo.git" clone 2>stderr &&
	check_verbose_connect &&
	test_cmp file clone/file
'

test_expect_success 'fetch changes via git protocol' '
	echo content >>file &&
	git commit -a -m two &&
	git push public &&
	(cd clone && git pull -v) 2>stderr &&
	check_verbose_connect &&
	test_cmp file clone/file
'

test_expect_success 'no-op fetch -v stderr is as expected' '
	(cd clone && git fetch -v) 2>stderr &&
	check_verbose_connect
'

test_expect_success 'no-op fetch without "-v" is quiet' '
	(cd clone && git fetch 2>../stderr) &&
	test_must_be_empty stderr
'

test_expect_success 'remote detects correct HEAD' '
	git push public main:other &&
	(cd clone &&
	 git remote set-head -d origin &&
	 git remote set-head -a origin &&
	 git symbolic-ref refs/remotes/origin/HEAD > output &&
	 echo refs/remotes/origin/main > expect &&
	 test_cmp expect output
	)
'

test_expect_success 'prepare pack objects' '
	cp -R "$GIT_DAEMON_DOCUMENT_ROOT_PATH"/repo.git "$GIT_DAEMON_DOCUMENT_ROOT_PATH"/repo_pack.git &&
	(cd "$GIT_DAEMON_DOCUMENT_ROOT_PATH"/repo_pack.git &&
	 git --bare repack -a -d
	)
'

test_expect_success 'fetch notices corrupt pack' '
	cp -R "$GIT_DAEMON_DOCUMENT_ROOT_PATH"/repo_pack.git "$GIT_DAEMON_DOCUMENT_ROOT_PATH"/repo_bad1.git &&
	(cd "$GIT_DAEMON_DOCUMENT_ROOT_PATH"/repo_bad1.git &&
	 p=$(ls objects/pack/pack-*.pack) &&
	 chmod u+w $p &&
	 printf %0256d 0 | dd of=$p bs=256 count=1 seek=1 conv=notrunc
	) &&
	mkdir repo_bad1.git &&
	(cd repo_bad1.git &&
	 git --bare init &&
	 test_must_fail git --bare fetch "$GIT_DAEMON_URL/repo_bad1.git" &&
	 test 0 = $(ls objects/pack/pack-*.pack | wc -l)
	)
'

test_expect_success 'fetch notices corrupt idx' '
	cp -R "$GIT_DAEMON_DOCUMENT_ROOT_PATH"/repo_pack.git "$GIT_DAEMON_DOCUMENT_ROOT_PATH"/repo_bad2.git &&
	(cd "$GIT_DAEMON_DOCUMENT_ROOT_PATH"/repo_bad2.git &&
	 rm -f objects/pack/multi-pack-index &&
	 p=$(ls objects/pack/pack-*.idx) &&
	 chmod u+w $p &&
	 printf %0256d 0 | dd of=$p bs=256 count=1 seek=1 conv=notrunc
	) &&
	mkdir repo_bad2.git &&
	(cd repo_bad2.git &&
	 git --bare init &&
	 test_must_fail git --bare fetch "$GIT_DAEMON_URL/repo_bad2.git" &&
	 test 0 = $(ls objects/pack | wc -l)
	)
'

test_expect_success 'client refuses to ask for repo with newline' '
	test_must_fail git clone "$GIT_DAEMON_URL/repo$LF.git" dst 2>stderr &&
	test_grep newline.is.forbidden stderr
'

test_remote_error()
{
	do_export=YesPlease
	while test $# -gt 0
	do
		case $1 in
		-x)
			shift
			chmod -x "$GIT_DAEMON_DOCUMENT_ROOT_PATH/repo.git"
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
			: >"$GIT_DAEMON_DOCUMENT_ROOT_PATH/$repo/git-daemon-export-ok"
		else
			rm -f "$GIT_DAEMON_DOCUMENT_ROOT_PATH/$repo/git-daemon-export-ok"
		fi
	fi

	test_must_fail git "$cmd" "$GIT_DAEMON_URL/$repo" "$@" 2>output &&
	test_grep "fatal: remote error: $msg: /$repo" output &&
	ret=$?
	chmod +x "$GIT_DAEMON_DOCUMENT_ROOT_PATH/repo.git"
	(exit $ret)
}

msg="access denied or repository not exported"
test_expect_success 'clone non-existent' "test_remote_error    '$msg' clone nowhere.git"
test_expect_success 'push disabled'      "test_remote_error    '$msg' push  repo.git main"
test_expect_success 'read access denied' "test_remote_error -x '$msg' fetch repo.git"
test_expect_success 'not exported'       "test_remote_error -n '$msg' fetch repo.git"

stop_git_daemon
start_git_daemon --informative-errors

test_expect_success 'clone non-existent' "test_remote_error    'no such repository'      clone nowhere.git"
test_expect_success 'push disabled'      "test_remote_error    'service not enabled'     push  repo.git main"
test_expect_success 'read access denied' "test_remote_error -x 'no such repository'      fetch repo.git"
test_expect_success 'not exported'       "test_remote_error -n 'repository not exported' fetch repo.git"

stop_git_daemon
start_git_daemon --interpolated-path="$GIT_DAEMON_DOCUMENT_ROOT_PATH/%H%D"

test_expect_success 'access repo via interpolated hostname' '
	repo="$GIT_DAEMON_DOCUMENT_ROOT_PATH/localhost/interp.git" &&
	git init --bare "$repo" &&
	git push "$repo" HEAD &&
	>"$repo"/git-daemon-export-ok &&
	GIT_OVERRIDE_VIRTUAL_HOST=localhost \
		git ls-remote "$GIT_DAEMON_URL/interp.git" &&
	GIT_OVERRIDE_VIRTUAL_HOST=LOCALHOST \
		git ls-remote "$GIT_DAEMON_URL/interp.git"
'

test_expect_success 'hostname cannot break out of directory' '
	repo="$GIT_DAEMON_DOCUMENT_ROOT_PATH/../escape.git" &&
	git init --bare "$repo" &&
	git push "$repo" HEAD &&
	>"$repo"/git-daemon-export-ok &&
	test_must_fail \
		env GIT_OVERRIDE_VIRTUAL_HOST=.. \
		git ls-remote "$GIT_DAEMON_URL/escape.git"
'

test_expect_success FAKENC 'hostname interpolation works after LF-stripping' '
	{
		printf "git-upload-pack /interp.git\n\0host=localhost" | packetize_raw &&
		printf "0000"
	} >input &&
	fake_nc "$GIT_DAEMON_HOST_PORT" <input >output &&
	depacketize <output >output.raw &&

	# just pick out the value of main, which avoids any protocol
	# particulars
	perl -lne "print \$1 if m{^(\\S+) refs/heads/main}" <output.raw >actual &&
	git -C "$repo" rev-parse main >expect &&
	test_cmp expect actual
'

test_done
