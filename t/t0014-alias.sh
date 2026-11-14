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

test_expect_success 'detect deprecated commands' '
	git --list-cmds=deprecated >deprecated &&
	if read deprecated1 && read deprecated2
	then
		test_set_prereq HAVE_DEPRECATED
	fi <deprecated
'

test_expect_success HAVE_DEPRECATED 'looping aliases - deprecated builtins' '
	test_config alias.$deprecated1 $deprecated2 &&
	test_config alias.$deprecated2 $deprecated1 &&
	cat >expect <<-EOF &&
	${SQ}$deprecated1${SQ} is aliased to ${SQ}$deprecated2${SQ}
	${SQ}$deprecated2${SQ} is aliased to ${SQ}$deprecated1${SQ}
	fatal: alias loop detected: expansion of ${SQ}$deprecated1${SQ} does not terminate:
	  $deprecated1 <==
	  $deprecated2 ==>
	EOF
	test_must_fail git $deprecated1 -h 2>actual &&
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

test_expect_success HAVE_DEPRECATED 'can alias-shadow via two deprecated builtins' '
	# some git(1) commands will fail... (see above)
	test_might_fail git status -h >expect &&
	test_file_not_empty expect &&
	test_might_fail git -c alias.$deprecated1=$deprecated2 \
		-c alias.$deprecated2=status $deprecated1 -h >actual &&
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

test_expect_success 'simple dotted alias syntax still works' '
	test_config alias.simple.dotted "!echo ran-simple-dotted" &&
	git simple.dotted >output &&
	test_grep "ran-simple-dotted" output
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

test_expect_success 'simple dotted aliases listed in help -a' '
	test_config alias.simple.listed "!echo test" &&
	git help -a >output &&
	test_grep "simple.listed" output
'

test_expect_success 'empty subsection treated as no subsection' '
	test_config "alias..something" "!echo foobar" &&
	git something >actual &&
	echo foobar >expect &&
	test_cmp expect actual
'

test_expect_success 'alias with leading dot via subsection syntax' '
	test_config alias.".something".command "!echo foobar" &&
	git .something >actual &&
	echo foobar >expect &&
	test_cmp expect actual
'

test_done
