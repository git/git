#!/bin/sh

test_description='test custom script in place of pack-objects'
. ./test-lib.sh

test_expect_success 'create some history to fetch' '
	test_commit one &&
	test_commit two
'

test_expect_success 'create debugging hook script' '
	write_script .git/hook <<-\EOF
		echo >&2 "hook running"
		echo "$*" >hook.args
		cat >hook.stdin
		"$@" <hook.stdin >hook.stdout
		cat hook.stdout
	EOF
'

clear_hook_results () {
	rm -rf .git/hook.* dst.git
}

test_expect_success 'hook runs via global config' '
	clear_hook_results &&
	test_config_global uploadpack.packObjectsHook ./hook &&
	git clone --no-local . dst.git 2>stderr &&
	grep "hook running" stderr
'

test_expect_success 'hook outputs are sane' '
	# check that we recorded a usable pack
	git index-pack --stdin <.git/hook.stdout &&

	# check that we recorded args and stdin. We do not check
	# the full argument list or the exact pack contents, as it would make
	# the test brittle. So just sanity check that we could replay
	# the packing procedure.
	grep "^git" .git/hook.args &&
	$(cat .git/hook.args) <.git/hook.stdin >replay
'

test_expect_success 'hook runs from -c config' '
	clear_hook_results &&
	git clone --no-local \
	  -u "git -c uploadpack.packObjectsHook=./hook upload-pack" \
	  . dst.git 2>stderr &&
	grep "hook running" stderr
'

test_expect_success 'hook does not run from repo config' '
	clear_hook_results &&
	test_config uploadpack.packObjectsHook "./hook" &&
	git clone --no-local . dst.git 2>stderr &&
	! grep "hook running" stderr &&
	test_path_is_missing .git/hook.args &&
	test_path_is_missing .git/hook.stdin &&
	test_path_is_missing .git/hook.stdout
'

test_done
