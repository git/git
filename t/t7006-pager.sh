#!/bin/sh

test_description='Test automatic use of a pager.'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-pager.sh
. "$TEST_DIRECTORY"/lib-terminal.sh

test_expect_success 'setup' '
	sane_unset GIT_PAGER GIT_PAGER_IN_USE &&
	test_unconfig core.pager &&

	PAGER="cat >paginated.out" &&
	export PAGER &&

	test_commit initial
'

test_expect_success TTY 'some commands use a pager' '
	rm -f paginated.out &&
	test_terminal git log &&
	test_path_is_file paginated.out
'

test_expect_failure TTY 'pager runs from subdir' '
	echo subdir/paginated.out >expected &&
	mkdir -p subdir &&
	rm -f paginated.out subdir/paginated.out &&
	(
		cd subdir &&
		test_terminal git log
	) &&
	{
		ls paginated.out subdir/paginated.out ||
		:
	} >actual &&
	test_cmp expected actual
'

test_expect_success TTY 'LESS and LV envvars are set for pagination' '
	(
		sane_unset LESS LV &&
		PAGER="env >pager-env.out; wc" &&
		export PAGER &&

		test_terminal git log
	) &&
	grep ^LESS= pager-env.out &&
	grep ^LV= pager-env.out
'

test_expect_success !MINGW,TTY 'LESS and LV envvars set by git-sh-setup' '
	(
		sane_unset LESS LV &&
		PAGER="env >pager-env.out; wc" &&
		export PAGER &&
		PATH="$(git --exec-path):$PATH" &&
		export PATH &&
		test_terminal sh -c ". git-sh-setup && git_pager"
	) &&
	grep ^LESS= pager-env.out &&
	grep ^LV= pager-env.out
'

test_expect_success TTY 'some commands do not use a pager' '
	rm -f paginated.out &&
	test_terminal git rev-list HEAD &&
	test_path_is_missing paginated.out
'

test_expect_success 'no pager when stdout is a pipe' '
	rm -f paginated.out &&
	git log | cat &&
	test_path_is_missing paginated.out
'

test_expect_success 'no pager when stdout is a regular file' '
	rm -f paginated.out &&
	git log >file &&
	test_path_is_missing paginated.out
'

test_expect_success TTY 'git --paginate rev-list uses a pager' '
	rm -f paginated.out &&
	test_terminal git --paginate rev-list HEAD &&
	test_path_is_file paginated.out
'

test_expect_success 'no pager even with --paginate when stdout is a pipe' '
	rm -f file paginated.out &&
	git --paginate log | cat &&
	test_path_is_missing paginated.out
'

test_expect_success TTY 'no pager with --no-pager' '
	rm -f paginated.out &&
	test_terminal git --no-pager log &&
	test_path_is_missing paginated.out
'

test_expect_success TTY 'configuration can disable pager' '
	rm -f paginated.out &&
	test_unconfig pager.grep &&
	test_terminal git grep initial &&
	test_path_is_file paginated.out &&

	rm -f paginated.out &&
	test_config pager.grep false &&
	test_terminal git grep initial &&
	test_path_is_missing paginated.out
'

test_expect_success TTY 'configuration can enable pager (from subdir)' '
	rm -f paginated.out &&
	mkdir -p subdir &&
	test_config pager.bundle true &&

	git bundle create test.bundle --all &&
	rm -f paginated.out subdir/paginated.out &&
	(
		cd subdir &&
		test_terminal git bundle unbundle ../test.bundle
	) &&
	{
		test_path_is_file paginated.out ||
		test_path_is_file subdir/paginated.out
	}
'

test_expect_success TTY 'git tag -l defaults to paging' '
	rm -f paginated.out &&
	test_terminal git tag -l &&
	test_path_is_file paginated.out
'

test_expect_success TTY 'git tag -l respects pager.tag' '
	rm -f paginated.out &&
	test_terminal git -c pager.tag=false tag -l &&
	test_path_is_missing paginated.out
'

test_expect_success TTY 'git tag -l respects --no-pager' '
	rm -f paginated.out &&
	test_terminal git -c pager.tag --no-pager tag -l &&
	test_path_is_missing paginated.out
'

test_expect_success TTY 'git tag with no args defaults to paging' '
	# no args implies -l so this should page like -l
	rm -f paginated.out &&
	test_terminal git tag &&
	test_path_is_file paginated.out
'

test_expect_success TTY 'git tag with no args respects pager.tag' '
	# no args implies -l so this should page like -l
	rm -f paginated.out &&
	test_terminal git -c pager.tag=false tag &&
	test_path_is_missing paginated.out
'

test_expect_success TTY 'git tag --contains defaults to paging' '
	# --contains implies -l so this should page like -l
	rm -f paginated.out &&
	test_terminal git tag --contains &&
	test_path_is_file paginated.out
'

test_expect_success TTY 'git tag --contains respects pager.tag' '
	# --contains implies -l so this should page like -l
	rm -f paginated.out &&
	test_terminal git -c pager.tag=false tag --contains &&
	test_path_is_missing paginated.out
'

test_expect_success TTY 'git tag -a defaults to not paging' '
	test_when_finished "git tag -d newtag" &&
	rm -f paginated.out &&
	test_terminal git tag -am message newtag &&
	test_path_is_missing paginated.out
'

test_expect_success TTY 'git tag -a ignores pager.tag' '
	test_when_finished "git tag -d newtag" &&
	rm -f paginated.out &&
	test_terminal git -c pager.tag tag -am message newtag &&
	test_path_is_missing paginated.out
'

test_expect_success TTY 'git tag -a respects --paginate' '
	test_when_finished "git tag -d newtag" &&
	rm -f paginated.out &&
	test_terminal git --paginate tag -am message newtag &&
	test_path_is_file paginated.out
'

test_expect_success TTY 'git tag as alias ignores pager.tag with -a' '
	test_when_finished "git tag -d newtag" &&
	rm -f paginated.out &&
	test_terminal git -c pager.tag -c alias.t=tag t -am message newtag &&
	test_path_is_missing paginated.out
'

test_expect_success TTY 'git tag as alias respects pager.tag with -l' '
	rm -f paginated.out &&
	test_terminal git -c pager.tag=false -c alias.t=tag t -l &&
	test_path_is_missing paginated.out
'

test_expect_success TTY 'git branch defaults to paging' '
	rm -f paginated.out &&
	test_terminal git branch &&
	test_path_is_file paginated.out
'

test_expect_success TTY 'git branch respects pager.branch' '
	rm -f paginated.out &&
	test_terminal git -c pager.branch=false branch &&
	test_path_is_missing paginated.out
'

test_expect_success TTY 'git branch respects --no-pager' '
	rm -f paginated.out &&
	test_terminal git --no-pager branch &&
	test_path_is_missing paginated.out
'

test_expect_success TTY 'git branch --edit-description ignores pager.branch' '
	rm -f paginated.out editor.used &&
	write_script editor <<-\EOF &&
		echo "New description" >"$1"
		touch editor.used
	EOF
	EDITOR=./editor test_terminal git -c pager.branch branch --edit-description &&
	test_path_is_missing paginated.out &&
	test_path_is_file editor.used
'

test_expect_success TTY 'git branch --set-upstream-to ignores pager.branch' '
	rm -f paginated.out &&
	git branch other &&
	test_when_finished "git branch -D other" &&
	test_terminal git -c pager.branch branch --set-upstream-to=other &&
	test_when_finished "git branch --unset-upstream" &&
	test_path_is_missing paginated.out
'

test_expect_success TTY 'git config ignores pager.config when setting' '
	rm -f paginated.out &&
	test_terminal git -c pager.config config foo.bar bar &&
	test_path_is_missing paginated.out
'

test_expect_success TTY 'git config --edit ignores pager.config' '
	rm -f paginated.out editor.used &&
	write_script editor <<-\EOF &&
		touch editor.used
	EOF
	EDITOR=./editor test_terminal git -c pager.config config --edit &&
	test_path_is_missing paginated.out &&
	test_path_is_file editor.used
'

test_expect_success TTY 'git config --get ignores pager.config' '
	rm -f paginated.out &&
	test_terminal git -c pager.config config --get foo.bar &&
	test_path_is_missing paginated.out
'

test_expect_success TTY 'git config --get-urlmatch defaults to paging' '
	rm -f paginated.out &&
	test_terminal git -c http."https://foo.com/".bar=foo \
			  config --get-urlmatch http https://foo.com &&
	test_path_is_file paginated.out
'

test_expect_success TTY 'git config --get-all respects pager.config' '
	rm -f paginated.out &&
	test_terminal git -c pager.config=false config --get-all foo.bar &&
	test_path_is_missing paginated.out
'

test_expect_success TTY 'git config --list defaults to paging' '
	rm -f paginated.out &&
	test_terminal git config --list &&
	test_path_is_file paginated.out
'


# A colored commit log will begin with an appropriate ANSI escape
# for the first color; the text "commit" comes later.
colorful() {
	read firstline <$1
	! expr "$firstline" : "[a-zA-Z]" >/dev/null
}

test_expect_success 'tests can detect color' '
	rm -f colorful.log colorless.log &&
	git log --no-color >colorless.log &&
	git log --color >colorful.log &&
	! colorful colorless.log &&
	colorful colorful.log
'

test_expect_success 'no color when stdout is a regular file' '
	rm -f colorless.log &&
	test_config color.ui auto &&
	git log >colorless.log &&
	! colorful colorless.log
'

test_expect_success TTY 'color when writing to a pager' '
	rm -f paginated.out &&
	test_config color.ui auto &&
	test_terminal git log &&
	colorful paginated.out
'

test_expect_success TTY 'colors are suppressed by color.pager' '
	rm -f paginated.out &&
	test_config color.ui auto &&
	test_config color.pager false &&
	test_terminal git log &&
	! colorful paginated.out
'

test_expect_success 'color when writing to a file intended for a pager' '
	rm -f colorful.log &&
	test_config color.ui auto &&
	(
		TERM=vt100 &&
		GIT_PAGER_IN_USE=true &&
		export TERM GIT_PAGER_IN_USE &&
		git log >colorful.log
	) &&
	colorful colorful.log
'

test_expect_success TTY 'colors are sent to pager for external commands' '
	test_config alias.externallog "!git log" &&
	test_config color.ui auto &&
	test_terminal git -p externallog &&
	colorful paginated.out
'

# Use this helper to make it easy for the caller of your
# terminal-using function to specify whether it should fail.
# If you write
#
#	your_test() {
#		parse_args "$@"
#
#		$test_expectation "$cmd - behaves well" "
#			...
#			$full_command &&
#			...
#		"
#	}
#
# then your test can be used like this:
#
#	your_test expect_(success|failure) [test_must_fail] 'git foo'
#
parse_args() {
	test_expectation="test_$1"
	shift
	if test "$1" = test_must_fail
	then
		full_command="test_must_fail test_terminal "
		shift
	else
		full_command="test_terminal "
	fi
	cmd=$1
	full_command="$full_command $1"
}

test_default_pager() {
	parse_args "$@"

	$test_expectation SIMPLEPAGER,TTY "$cmd - default pager is used by default" "
		sane_unset PAGER GIT_PAGER &&
		test_unconfig core.pager &&
		rm -f default_pager_used &&
		cat >\$less <<-\EOF &&
		#!/bin/sh
		wc >default_pager_used
		EOF
		chmod +x \$less &&
		(
			PATH=.:\$PATH &&
			export PATH &&
			$full_command
		) &&
		test_path_is_file default_pager_used
	"
}

test_PAGER_overrides() {
	parse_args "$@"

	$test_expectation TTY "$cmd - PAGER overrides default pager" "
		sane_unset GIT_PAGER &&
		test_unconfig core.pager &&
		rm -f PAGER_used &&
		PAGER='wc >PAGER_used' &&
		export PAGER &&
		$full_command &&
		test_path_is_file PAGER_used
	"
}

test_core_pager_overrides() {
	if_local_config=
	used_if_wanted='overrides PAGER'
	test_core_pager "$@"
}

test_local_config_ignored() {
	if_local_config='! '
	used_if_wanted='is not used'
	test_core_pager "$@"
}

test_core_pager() {
	parse_args "$@"

	$test_expectation TTY "$cmd - repository-local core.pager setting $used_if_wanted" "
		sane_unset GIT_PAGER &&
		rm -f core.pager_used &&
		PAGER=wc &&
		export PAGER &&
		test_config core.pager 'wc >core.pager_used' &&
		$full_command &&
		${if_local_config}test_path_is_file core.pager_used
	"
}

test_core_pager_subdir() {
	if_local_config=
	used_if_wanted='overrides PAGER'
	test_pager_subdir_helper "$@"
}

test_no_local_config_subdir() {
	if_local_config='! '
	used_if_wanted='is not used'
	test_pager_subdir_helper "$@"
}

test_pager_subdir_helper() {
	parse_args "$@"

	$test_expectation TTY "$cmd - core.pager $used_if_wanted from subdirectory" "
		sane_unset GIT_PAGER &&
		rm -f core.pager_used &&
		rm -fr sub &&
		PAGER=wc &&
		stampname=\$(pwd)/core.pager_used &&
		export PAGER stampname &&
		test_config core.pager 'wc >\"\$stampname\"' &&
		mkdir sub &&
		(
			cd sub &&
			$full_command
		) &&
		${if_local_config}test_path_is_file core.pager_used
	"
}

test_GIT_PAGER_overrides() {
	parse_args "$@"

	$test_expectation TTY "$cmd - GIT_PAGER overrides core.pager" "
		rm -f GIT_PAGER_used &&
		test_config core.pager wc &&
		GIT_PAGER='wc >GIT_PAGER_used' &&
		export GIT_PAGER &&
		$full_command &&
		test_path_is_file GIT_PAGER_used
	"
}

test_doesnt_paginate() {
	parse_args "$@"

	$test_expectation TTY "no pager for '$cmd'" "
		rm -f GIT_PAGER_used &&
		GIT_PAGER='wc >GIT_PAGER_used' &&
		export GIT_PAGER &&
		$full_command &&
		test_path_is_missing GIT_PAGER_used
	"
}

test_pager_choices() {
	test_default_pager        expect_success "$@"
	test_PAGER_overrides      expect_success "$@"
	test_core_pager_overrides expect_success "$@"
	test_core_pager_subdir    expect_success "$@"
	test_GIT_PAGER_overrides  expect_success "$@"
}

test_expect_success 'setup: some aliases' '
	git config alias.aliasedlog log &&
	git config alias.true "!true"
'

test_pager_choices                       'git log'
test_pager_choices                       'git -p log'
test_pager_choices                       'git aliasedlog'

test_default_pager        expect_success 'git -p aliasedlog'
test_PAGER_overrides      expect_success 'git -p aliasedlog'
test_core_pager_overrides expect_success 'git -p aliasedlog'
test_core_pager_subdir    expect_success 'git -p aliasedlog'
test_GIT_PAGER_overrides  expect_success 'git -p aliasedlog'

test_default_pager        expect_success 'git -p true'
test_PAGER_overrides      expect_success 'git -p true'
test_core_pager_overrides expect_success 'git -p true'
test_core_pager_subdir    expect_success 'git -p true'
test_GIT_PAGER_overrides  expect_success 'git -p true'

test_default_pager        expect_success test_must_fail 'git -p request-pull'
test_PAGER_overrides      expect_success test_must_fail 'git -p request-pull'
test_core_pager_overrides expect_success test_must_fail 'git -p request-pull'
test_core_pager_subdir    expect_success test_must_fail 'git -p request-pull'
test_GIT_PAGER_overrides  expect_success test_must_fail 'git -p request-pull'

test_default_pager        expect_success test_must_fail 'git -p'
test_PAGER_overrides      expect_success test_must_fail 'git -p'
test_local_config_ignored expect_failure test_must_fail 'git -p'
test_GIT_PAGER_overrides  expect_success test_must_fail 'git -p'

test_expect_success TTY 'core.pager in repo config works and retains cwd' '
	sane_unset GIT_PAGER &&
	test_config core.pager "cat >cwd-retained" &&
	(
		cd sub &&
		rm -f cwd-retained &&
		test_terminal git -p rev-parse HEAD &&
		test_path_is_file cwd-retained
	)
'

test_expect_success TTY 'core.pager is found via alias in subdirectory' '
	sane_unset GIT_PAGER &&
	test_config core.pager "cat >via-alias" &&
	(
		cd sub &&
		rm -f via-alias &&
		test_terminal git -c alias.r="-p rev-parse" r HEAD &&
		test_path_is_file via-alias
	)
'

test_doesnt_paginate      expect_failure test_must_fail 'git -p nonsense'

test_pager_choices                       'git shortlog'
test_expect_success 'setup: configure shortlog not to paginate' '
	git config pager.shortlog false
'
test_doesnt_paginate      expect_success 'git shortlog'
test_no_local_config_subdir expect_success 'git shortlog'
test_default_pager        expect_success 'git -p shortlog'
test_core_pager_subdir    expect_success 'git -p shortlog'

test_core_pager_subdir    expect_success test_must_fail \
					 'git -p apply </dev/null'

test_expect_success TTY 'command-specific pager' '
	sane_unset PAGER GIT_PAGER &&
	echo "foo:initial" >expect &&
	>actual &&
	test_unconfig core.pager &&
	test_config pager.log "sed s/^/foo:/ >actual" &&
	test_terminal git log --format=%s -1 &&
	test_cmp expect actual
'

test_expect_success TTY 'command-specific pager overrides core.pager' '
	sane_unset PAGER GIT_PAGER &&
	echo "foo:initial" >expect &&
	>actual &&
	test_config core.pager "exit 1" &&
	test_config pager.log "sed s/^/foo:/ >actual" &&
	test_terminal git log --format=%s -1 &&
	test_cmp expect actual
'

test_expect_success TTY 'command-specific pager overridden by environment' '
	GIT_PAGER="sed s/^/foo:/ >actual" && export GIT_PAGER &&
	>actual &&
	echo "foo:initial" >expect &&
	test_config pager.log "exit 1" &&
	test_terminal git log --format=%s -1 &&
	test_cmp expect actual
'

test_expect_success 'setup external command' '
	cat >git-external <<-\EOF &&
	#!/bin/sh
	git "$@"
	EOF
	chmod +x git-external
'

test_expect_success TTY 'command-specific pager works for external commands' '
	sane_unset PAGER GIT_PAGER &&
	echo "foo:initial" >expect &&
	>actual &&
	test_config pager.external "sed s/^/foo:/ >actual" &&
	test_terminal git --exec-path="$(pwd)" external log --format=%s -1 &&
	test_cmp expect actual
'

test_expect_success TTY 'sub-commands of externals use their own pager' '
	sane_unset PAGER GIT_PAGER &&
	echo "foo:initial" >expect &&
	>actual &&
	test_config pager.log "sed s/^/foo:/ >actual" &&
	test_terminal git --exec-path=. external log --format=%s -1 &&
	test_cmp expect actual
'

test_expect_success TTY 'external command pagers override sub-commands' '
	sane_unset PAGER GIT_PAGER &&
	>actual &&
	test_config pager.external false &&
	test_config pager.log "sed s/^/log:/ >actual" &&
	test_terminal git --exec-path=. external log --format=%s -1 &&
	test_must_be_empty actual
'

test_expect_success 'command with underscores does not complain' '
	write_script git-under_score <<-\EOF &&
	echo ok
	EOF
	git --exec-path=. under_score >actual 2>&1 &&
	echo ok >expect &&
	test_cmp expect actual
'

test_expect_success TTY 'git tag with auto-columns ' '
	test_commit one &&
	test_commit two &&
	test_commit three &&
	test_commit four &&
	test_commit five &&
	cat >expect <<-\EOF &&
	initial  one      two      three    four     five
	EOF
	test_terminal env PAGER="cat >actual" COLUMNS=80 \
		git -c column.ui=auto tag --sort=authordate &&
	test_cmp expect actual
'

test_expect_success 'setup trace2' '
	GIT_TRACE2_BRIEF=1 &&
	export GIT_TRACE2_BRIEF
'

test_expect_success 'setup large log output' '
	perl -e "
		print \"this is a long commit message\" x 50000
	" >commit-msg &&
	git commit --allow-empty -F commit-msg
'

test_expect_success TTY 'git returns SIGPIPE on early pager exit' '
	test_when_finished "rm pager-used trace.normal" &&
	test_config core.pager ">pager-used; head -n 1; exit 0" &&
	GIT_TRACE2="$(pwd)/trace.normal" &&
	export GIT_TRACE2 &&
	test_when_finished "unset GIT_TRACE2" &&

	if test_have_prereq !MINGW
	then
		{ test_terminal git log >/dev/null; OUT=$?; } &&
		test_match_signal 13 "$OUT"
	else
		test_terminal git log
	fi &&

	grep child_exit trace.normal >child-exits &&
	test_line_count = 1 child-exits &&
	grep " code:0 " child-exits &&
	test_path_is_file pager-used
'

test_expect_success TTY 'git returns SIGPIPE on early pager non-zero exit' '
	test_when_finished "rm pager-used trace.normal" &&
	test_config core.pager ">pager-used; head -n 1; exit 1" &&
	GIT_TRACE2="$(pwd)/trace.normal" &&
	export GIT_TRACE2 &&
	test_when_finished "unset GIT_TRACE2" &&

	if test_have_prereq !MINGW
	then
		{ test_terminal git log >/dev/null; OUT=$?; } &&
		test_match_signal 13 "$OUT"
	else
		test_terminal git log
	fi &&

	grep child_exit trace.normal >child-exits &&
	test_line_count = 1 child-exits &&
	grep " code:1 " child-exits &&
	test_path_is_file pager-used
'

test_expect_success TTY 'git discards pager non-zero exit without SIGPIPE' '
	test_when_finished "rm pager-used trace.normal" &&
	test_config core.pager "wc >pager-used; exit 1" &&
	GIT_TRACE2="$(pwd)/trace.normal" &&
	export GIT_TRACE2 &&
	test_when_finished "unset GIT_TRACE2" &&

	test_terminal git log &&

	grep child_exit trace.normal >child-exits &&
	test_line_count = 1 child-exits &&
	grep " code:1 " child-exits &&
	test_path_is_file pager-used
'

test_expect_success TTY 'git skips paging nonexisting command' '
	test_when_finished "rm trace.normal" &&
	test_config core.pager "does-not-exist" &&
	GIT_TRACE2="$(pwd)/trace.normal" &&
	export GIT_TRACE2 &&
	test_when_finished "unset GIT_TRACE2" &&

	test_terminal git log &&

	grep child_exit trace.normal >child-exits &&
	test_line_count = 1 child-exits &&
	grep " code:-1 " child-exits
'

test_expect_success TTY 'git returns SIGPIPE on propagated signals from pager' '
	test_when_finished "rm pager-used trace.normal" &&
	test_config core.pager ">pager-used; exec test-tool sigchain" &&
	GIT_TRACE2="$(pwd)/trace.normal" &&
	export GIT_TRACE2 &&
	test_when_finished "unset GIT_TRACE2" &&

	if test_have_prereq !MINGW
	then
		{ test_terminal git log >/dev/null; OUT=$?; } &&
		test_match_signal 13 "$OUT"
	else
		test_terminal git log
	fi &&

	grep child_exit trace.normal >child-exits &&
	test_line_count = 1 child-exits &&
	grep " code:143 " child-exits &&
	test_path_is_file pager-used
'

test_expect_success TTY 'non-existent pager doesnt cause crash' '
	test_config pager.show invalid-pager &&
	test_terminal git show
'

test_done
