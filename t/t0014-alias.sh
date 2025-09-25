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

test_done
