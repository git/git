#!/bin/sh

test_description='pull signature verification tests'
. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"

test_expect_success GPG 'create repositories with signed cummits' '
	echo 1 >a && but add a &&
	test_tick && but cummit -m initial &&
	but tag initial &&

	but clone . signed &&
	(
		cd signed &&
		echo 2 >b && but add b &&
		test_tick && but cummit -S -m "signed"
	) &&

	but clone . unsigned &&
	(
		cd unsigned &&
		echo 3 >c && but add c &&
		test_tick && but cummit -m "unsigned"
	) &&

	but clone . bad &&
	(
		cd bad &&
		echo 4 >d && but add d &&
		test_tick && but cummit -S -m "bad" &&
		but cat-file commit HEAD >raw &&
		sed -e "s/^bad/forged bad/" raw >forged &&
		but hash-object -w -t cummit forged >forged.cummit &&
		but checkout $(cat forged.cummit)
	) &&

	but clone . untrusted &&
	(
		cd untrusted &&
		echo 5 >e && but add e &&
		test_tick && but cummit -SB7227189 -m "untrusted"
	)
'

test_expect_success GPG 'pull unsigned cummit with --verify-signatures' '
	test_when_finished "but reset --hard && but checkout initial" &&
	test_must_fail but pull --ff-only --verify-signatures unsigned 2>pullerror &&
	test_i18ngrep "does not have a GPG signature" pullerror
'

test_expect_success GPG 'pull cummit with bad signature with --verify-signatures' '
	test_when_finished "but reset --hard && but checkout initial" &&
	test_must_fail but pull --ff-only --verify-signatures bad 2>pullerror &&
	test_i18ngrep "has a bad GPG signature" pullerror
'

test_expect_success GPG 'pull cummit with untrusted signature with --verify-signatures' '
	test_when_finished "but reset --hard && but checkout initial" &&
	test_must_fail but pull --ff-only --verify-signatures untrusted 2>pullerror &&
	test_i18ngrep "has an untrusted GPG signature" pullerror
'

test_expect_success GPG 'pull cummit with untrusted signature with --verify-signatures and minTrustLevel=ultimate' '
	test_when_finished "but reset --hard && but checkout initial" &&
	test_config gpg.minTrustLevel ultimate &&
	test_must_fail but pull --ff-only --verify-signatures untrusted 2>pullerror &&
	test_i18ngrep "has an untrusted GPG signature" pullerror
'

test_expect_success GPG 'pull cummit with untrusted signature with --verify-signatures and minTrustLevel=marginal' '
	test_when_finished "but reset --hard && but checkout initial" &&
	test_config gpg.minTrustLevel marginal &&
	test_must_fail but pull --ff-only --verify-signatures untrusted 2>pullerror &&
	test_i18ngrep "has an untrusted GPG signature" pullerror
'

test_expect_success GPG 'pull cummit with untrusted signature with --verify-signatures and minTrustLevel=undefined' '
	test_when_finished "but reset --hard && but checkout initial" &&
	test_config gpg.minTrustLevel undefined &&
	but pull --ff-only --verify-signatures untrusted >pulloutput &&
	test_i18ngrep "has a good GPG signature" pulloutput
'

test_expect_success GPG 'pull signed cummit with --verify-signatures' '
	test_when_finished "but reset --hard && but checkout initial" &&
	but pull --verify-signatures signed >pulloutput &&
	test_i18ngrep "has a good GPG signature" pulloutput
'

test_expect_success GPG 'pull cummit with bad signature without verification' '
	test_when_finished "but reset --hard && but checkout initial" &&
	but pull --ff-only bad 2>pullerror
'

test_expect_success GPG 'pull cummit with bad signature with --no-verify-signatures' '
	test_when_finished "but reset --hard && but checkout initial" &&
	test_config merge.verifySignatures true &&
	test_config pull.verifySignatures true &&
	but pull --ff-only --no-verify-signatures bad 2>pullerror
'

test_expect_success GPG 'pull unsigned cummit into unborn branch' '
	test_when_finished "rm -rf empty-repo" &&
	but init empty-repo &&
	test_must_fail \
		but -C empty-repo pull --verify-signatures ..  2>pullerror &&
	test_i18ngrep "does not have a GPG signature" pullerror
'

test_expect_success GPG 'pull cummit into unborn branch with bad signature and --verify-signatures' '
	test_when_finished "rm -rf empty-repo" &&
	but init empty-repo &&
	test_must_fail \
		but -C empty-repo pull --ff-only --verify-signatures ../bad 2>pullerror &&
	test_i18ngrep "has a bad GPG signature" pullerror
'

test_expect_success GPG 'pull cummit into unborn branch with untrusted signature and --verify-signatures' '
	test_when_finished "rm -rf empty-repo" &&
	but init empty-repo &&
	test_must_fail \
		but -C empty-repo pull --ff-only --verify-signatures ../untrusted 2>pullerror &&
	test_i18ngrep "has an untrusted GPG signature" pullerror
'

test_expect_success GPG 'pull cummit into unborn branch with untrusted signature and --verify-signatures and minTrustLevel=ultimate' '
	test_when_finished "rm -rf empty-repo" &&
	but init empty-repo &&
	test_config_global gpg.minTrustLevel ultimate &&
	test_must_fail \
		but -C empty-repo pull --ff-only --verify-signatures ../untrusted 2>pullerror &&
	test_i18ngrep "has an untrusted GPG signature" pullerror
'

test_expect_success GPG 'pull cummit into unborn branch with untrusted signature and --verify-signatures and minTrustLevel=marginal' '
	test_when_finished "rm -rf empty-repo" &&
	but init empty-repo &&
	test_config_global gpg.minTrustLevel marginal &&
	test_must_fail \
		but -C empty-repo pull --ff-only --verify-signatures ../untrusted 2>pullerror &&
	test_i18ngrep "has an untrusted GPG signature" pullerror
'

test_expect_success GPG 'pull cummit into unborn branch with untrusted signature and --verify-signatures and minTrustLevel=undefined' '
	test_when_finished "rm -rf empty-repo" &&
	but init empty-repo &&
	test_config_global gpg.minTrustLevel undefined &&
	but -C empty-repo pull --ff-only --verify-signatures ../untrusted >pulloutput &&
	test_i18ngrep "has a good GPG signature" pulloutput
'

test_done
