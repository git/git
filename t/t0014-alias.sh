#!/bin/sh

test_description='git command aliasing'

. ./test-lib.sh

test_expect_success 'nested aliases - internal execution' '
	git config alias.nested-internal-1 nested-internal-2 &&
	git config alias.nested-internal-2 status &&
	git nested-internal-1 >output &&
	test_grep "^On branch " output
'

test_expect_success 'nested aliases - mixed execution' '
	git config alias.nested-external-1 nested-external-2 &&
	git config alias.nested-external-2 "!git nested-external-3" &&
	git config alias.nested-external-3 status &&
	git nested-external-1 >output &&
	test_grep "^On branch " output
'

test_expect_success 'looping aliases - internal execution' '
	git config alias.loop-internal-1 loop-internal-2 &&
	git config alias.loop-internal-2 loop-internal-3 &&
	git config alias.loop-internal-3 loop-internal-2 &&
	test_must_fail git loop-internal-1 2>output &&
	test_grep "^fatal: alias loop detected: expansion of" output
'

test_expect_success 'looping aliases - deprecated builtins' '
	test_config alias.whatchanged pack-redundant &&
	test_config alias.pack-redundant whatchanged &&
	cat >expect <<-EOF &&
	${SQ}whatchanged${SQ} is aliased to ${SQ}pack-redundant${SQ}
	${SQ}pack-redundant${SQ} is aliased to ${SQ}whatchanged${SQ}
	fatal: alias loop detected: expansion of ${SQ}whatchanged${SQ} does not terminate:
	  whatchanged <==
	  pack-redundant ==>
	EOF
	test_must_fail git whatchanged -h 2>actual &&
	test_cmp expect actual
'

# This test is disabled until external loops are fixed, because would block
# the test suite for a full minute.
#
#test_expect_failure 'looping aliases - mixed execution' '
#	git config alias.loop-mixed-1 loop-mixed-2 &&
#	git config alias.loop-mixed-2 "!git loop-mixed-1" &&
#	test_must_fail git loop-mixed-1 2>output &&
#	test_grep "^fatal: alias loop detected: expansion of" output
#'

test_expect_success 'run-command formats empty args properly' '
    test_must_fail env GIT_TRACE=1 git frotz a "" b " " c 2>actual.raw &&
    sed -ne "/run_command:/s/.*trace: run_command: //p" actual.raw >actual &&
    echo "git-frotz a '\'''\'' b '\'' '\'' c" >expect &&
    test_cmp expect actual
'

test_expect_success 'tracing a shell alias with arguments shows trace of prepared command' '
	cat >expect <<-EOF &&
	trace: start_command: SHELL -c ${SQ}echo \$* "\$@"${SQ} ${SQ}echo \$*${SQ} arg
	EOF
	git config alias.echo "!echo \$*" &&
	env GIT_TRACE=1 git echo arg 2>output &&
	# redact platform differences
	sed -n -e "s/^\(trace: start_command:\) .* -c /\1 SHELL -c /p" output >actual &&
	test_cmp expect actual
'

can_alias_deprecated_builtin () {
	cmd="$1" &&
	# some git(1) commands will fail for `-h` (the case for
	# git-status as of 2025-09-07)
	test_might_fail git status -h >expect &&
	test_file_not_empty expect &&
	test_might_fail git -c alias."$cmd"=status "$cmd" -h >actual &&
	test_cmp expect actual
}

test_expect_success 'can alias-shadow deprecated builtins' '
	for cmd in $(git --list-cmds=deprecated)
	do
		can_alias_deprecated_builtin "$cmd" || return 1
	done
'

test_expect_success 'can alias-shadow via two deprecated builtins' '
	# some git(1) commands will fail... (see above)
	test_might_fail git status -h >expect &&
	test_file_not_empty expect &&
	test_might_fail git -c alias.whatchanged=pack-redundant \
		-c alias.pack-redundant=status whatchanged -h >actual &&
	test_cmp expect actual
'

cannot_alias_regular_builtin () {
	cmd="$1" &&
	# some git(1) commands will fail... (see above)
	test_might_fail git "$cmd" -h >expect &&
	test_file_not_empty expect &&
	test_might_fail git -c alias."$cmd"=status "$cmd" -h >actual &&
	test_cmp expect actual
}

test_expect_success 'cannot alias-shadow a sample of regular builtins' '
	for cmd in grep check-ref-format interpret-trailers \
		checkout-index fast-import diagnose rev-list prune
	do
		cannot_alias_regular_builtin "$cmd" || return 1
	done
'

test_expect_success 'alias without value reports error' '
	test_when_finished "git config --unset alias.noval" &&
	cat >>.git/config <<-\EOF &&
	[alias]
		noval
	EOF
	test_must_fail git noval 2>error &&
	test_grep "alias.noval" error
'

test_expect_success 'subsection syntax works' '
	test_config alias.testnew.command "!echo ran-subsection" &&
	git testnew >output &&
	test_grep "ran-subsection" output
'

test_expect_success 'subsection syntax only accepts command key' '
	test_config alias.invalid.notcommand value &&
	test_must_fail git invalid 2>error &&
	test_grep -i "not a git command" error
'

test_expect_success 'subsection syntax requires value for command' '
	test_when_finished "git config --remove-section alias.noval" &&
	cat >>.git/config <<-\EOF &&
	[alias "noval"]
		command
	EOF
	test_must_fail git noval 2>error &&
	test_grep "alias.noval.command" error
'

test_expect_success 'simple syntax is case-insensitive' '
	test_config alias.LegacyCase "!echo ran-legacy" &&
	git legacycase >output &&
	test_grep "ran-legacy" output
'

test_expect_success 'subsection syntax is case-sensitive' '
	test_config alias.SubCase.command "!echo ran-upper" &&
	test_config alias.subcase.command "!echo ran-lower" &&
	git SubCase >upper.out &&
	git subcase >lower.out &&
	test_grep "ran-upper" upper.out &&
	test_grep "ran-lower" lower.out
'

test_expect_success 'UTF-8 alias with Swedish characters' '
	test_config alias."förgrena".command "!echo ran-swedish" &&
	git förgrena >output &&
	test_grep "ran-swedish" output
'

test_expect_success 'UTF-8 alias with CJK characters' '
	test_config alias."分支".command "!echo ran-cjk" &&
	git 分支 >output &&
	test_grep "ran-cjk" output
'

test_expect_success 'alias with spaces in name' '
	test_config alias."test name".command "!echo ran-spaces" &&
	git "test name" >output &&
	test_grep "ran-spaces" output
'

test_expect_success 'subsection aliases listed in help -a' '
	test_config alias."förgrena".command "!echo test" &&
	git help -a >output &&
	test_grep "förgrena" output
'

test_done
