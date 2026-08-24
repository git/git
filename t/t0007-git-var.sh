#!/bin/sh

test_description='basic sanity checks for git var'

. ./test-lib.sh

sane_unset_all_editors () {
	sane_unset GIT_EDITOR &&
	sane_unset VISUAL &&
	sane_unset EDITOR
}

test_expect_success 'get GIT_AUTHOR_IDENT' '
	test_tick &&
	echo "$GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> $GIT_AUTHOR_DATE" >expect &&
	git var GIT_AUTHOR_IDENT >actual &&
	test_cmp expect actual
'

test_expect_success 'get GIT_COMMITTER_IDENT' '
	test_tick &&
	echo "$GIT_COMMITTER_NAME <$GIT_COMMITTER_EMAIL> $GIT_COMMITTER_DATE" >expect &&
	git var GIT_COMMITTER_IDENT >actual &&
	test_cmp expect actual
'

test_expect_success !FAIL_PREREQS,!AUTOIDENT 'requested identities are strict' '
	(
		sane_unset GIT_COMMITTER_NAME &&
		sane_unset GIT_COMMITTER_EMAIL &&
		test_must_fail git var GIT_COMMITTER_IDENT
	)
'

test_expect_success 'get GIT_DEFAULT_BRANCH without configuration' '
	(
		sane_unset GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME &&
		git init defbranch &&
		git -C defbranch symbolic-ref --short HEAD >expect &&
		git var GIT_DEFAULT_BRANCH >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get GIT_DEFAULT_BRANCH with configuration' '
	test_config init.defaultbranch foo &&
	(
		sane_unset GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME &&
		echo foo >expect &&
		git var GIT_DEFAULT_BRANCH >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get GIT_EDITOR without configuration' '
	(
		sane_unset_all_editors &&
		test_expect_code 1 git var GIT_EDITOR >out &&
		test_must_be_empty out
	)
'

test_expect_success 'get GIT_EDITOR with configuration' '
	test_config core.editor foo &&
	(
		sane_unset_all_editors &&
		echo foo >expect &&
		git var GIT_EDITOR >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get GIT_EDITOR with environment variable GIT_EDITOR' '
	(
		sane_unset_all_editors &&
		echo bar >expect &&
		GIT_EDITOR=bar git var GIT_EDITOR >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get GIT_EDITOR with environment variable EDITOR' '
	(
		sane_unset_all_editors &&
		echo bar >expect &&
		EDITOR=bar git var GIT_EDITOR >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get GIT_EDITOR with configuration and environment variable GIT_EDITOR' '
	test_config core.editor foo &&
	(
		sane_unset_all_editors &&
		echo bar >expect &&
		GIT_EDITOR=bar git var GIT_EDITOR >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get GIT_EDITOR with configuration and environment variable EDITOR' '
	test_config core.editor foo &&
	(
		sane_unset_all_editors &&
		echo foo >expect &&
		EDITOR=bar git var GIT_EDITOR >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get GIT_SEQUENCE_EDITOR without configuration' '
	(
		sane_unset GIT_SEQUENCE_EDITOR &&
		git var GIT_EDITOR >expect &&
		git var GIT_SEQUENCE_EDITOR >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get GIT_SEQUENCE_EDITOR with configuration' '
	test_config sequence.editor foo &&
	(
		sane_unset GIT_SEQUENCE_EDITOR &&
		echo foo >expect &&
		git var GIT_SEQUENCE_EDITOR >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get GIT_SEQUENCE_EDITOR with environment variable' '
	(
		sane_unset GIT_SEQUENCE_EDITOR &&
		echo bar >expect &&
		GIT_SEQUENCE_EDITOR=bar git var GIT_SEQUENCE_EDITOR >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'get GIT_SEQUENCE_EDITOR with configuration and environment variable' '
	test_config sequence.editor foo &&
	(
		sane_unset GIT_SEQUENCE_EDITOR &&
		echo bar >expect &&
		GIT_SEQUENCE_EDITOR=bar git var GIT_SEQUENCE_EDITOR >actual &&
		test_cmp expect actual
	)
'

test_expect_success POSIXPERM 'GIT_SHELL_PATH points to a valid executable' '
	shellpath=$(git var GIT_SHELL_PATH) &&
	test_path_is_executable "$shellpath"
'

# We know in this environment that our shell will be one of a few fixed values
# that all end in "sh".
test_expect_success MINGW 'GIT_SHELL_PATH points to a suitable shell' '
	shellpath=$(git var GIT_SHELL_PATH) &&
	case "$shellpath" in
	[A-Z]:/*/sh.exe) test -f "$shellpath";;
	*) return 1;;
	esac
'

test_expect_success 'GIT_ATTR_SYSTEM produces expected output' '
	test_must_fail env GIT_ATTR_NOSYSTEM=1 git var GIT_ATTR_SYSTEM &&
	(
		sane_unset GIT_ATTR_NOSYSTEM &&
		systempath=$(git var GIT_ATTR_SYSTEM) &&
		test "$systempath" != ""
	)
'

test_expect_success 'GIT_ATTR_GLOBAL points to the correct location' '
	TRASHDIR="$(test-tool path-utils normalize_path_copy "$(pwd)")" &&
	globalpath=$(XDG_CONFIG_HOME="$TRASHDIR/.config" git var GIT_ATTR_GLOBAL) &&
	test "$globalpath" = "$TRASHDIR/.config/git/attributes" &&
	(
		sane_unset XDG_CONFIG_HOME &&
		globalpath=$(HOME="$TRASHDIR" git var GIT_ATTR_GLOBAL) &&
		test "$globalpath" = "$TRASHDIR/.config/git/attributes"
	)
'

test_expect_success 'GIT_CONFIG_SYSTEM points to the correct location' '
	TRASHDIR="$(test-tool path-utils normalize_path_copy "$(pwd)")" &&
	test_must_fail env GIT_CONFIG_NOSYSTEM=1 git var GIT_CONFIG_SYSTEM &&
	(
		sane_unset GIT_CONFIG_NOSYSTEM &&
		systempath=$(git var GIT_CONFIG_SYSTEM) &&
		test "$systempath" != "" &&
		systempath=$(GIT_CONFIG_SYSTEM=/dev/null git var GIT_CONFIG_SYSTEM) &&
		if test_have_prereq MINGW
		then
			test "$systempath" = "nul"
		else
			test "$systempath" = "/dev/null"
		fi &&
		systempath=$(GIT_CONFIG_SYSTEM="$TRASHDIR/gitconfig" git var GIT_CONFIG_SYSTEM) &&
		test "$systempath" = "$TRASHDIR/gitconfig"
	)
'

test_expect_success 'GIT_CONFIG_GLOBAL points to the correct location' '
	TRASHDIR="$(test-tool path-utils normalize_path_copy "$(pwd)")" &&
	HOME="$TRASHDIR" XDG_CONFIG_HOME="$TRASHDIR/foo" git var GIT_CONFIG_GLOBAL >actual &&
	echo "$TRASHDIR/foo/git/config" >expected &&
	echo "$TRASHDIR/.gitconfig" >>expected &&
	test_cmp expected actual &&
	(
		sane_unset XDG_CONFIG_HOME &&
		HOME="$TRASHDIR" git var GIT_CONFIG_GLOBAL >actual &&
		echo "$TRASHDIR/.config/git/config" >expected &&
		echo "$TRASHDIR/.gitconfig" >>expected &&
		test_cmp expected actual &&
		globalpath=$(GIT_CONFIG_GLOBAL=/dev/null git var GIT_CONFIG_GLOBAL) &&
		if test_have_prereq MINGW
		then
			test "$globalpath" = "nul"
		else
			test "$globalpath" = "/dev/null"
		fi &&
		globalpath=$(GIT_CONFIG_GLOBAL="$TRASHDIR/gitconfig" git var GIT_CONFIG_GLOBAL) &&
		test "$globalpath" = "$TRASHDIR/gitconfig"
	)
'

# For git var -l, we check only a representative variable;
# testing the whole output would make our test too brittle with
# respect to unrelated changes in the test suite's environment.
test_expect_success 'git var -l lists variables' '
	git var -l >actual &&
	echo "$GIT_AUTHOR_NAME <$GIT_AUTHOR_EMAIL> $GIT_AUTHOR_DATE" >expect &&
	sed -n s/GIT_AUTHOR_IDENT=//p <actual >actual.author &&
	test_cmp expect actual.author
'

test_expect_success 'git var -l lists config' '
	git var -l >actual &&
	echo false >expect &&
	sed -n s/core\\.bare=//p <actual >actual.bare &&
	test_cmp expect actual.bare
'

test_expect_success 'git var -l lists multiple global configs' '
	TRASHDIR="$(test-tool path-utils normalize_path_copy "$(pwd)")" &&
	HOME="$TRASHDIR" XDG_CONFIG_HOME="$TRASHDIR/foo" git var -l >actual &&
	grep "^GIT_CONFIG_GLOBAL=" actual >filtered &&
	echo "GIT_CONFIG_GLOBAL=$TRASHDIR/foo/git/config" >expected &&
	echo "GIT_CONFIG_GLOBAL=$TRASHDIR/.gitconfig" >>expected &&
	test_cmp expected filtered
'

test_expect_success 'git var -l does not split multiline editors' '
	(
		GIT_EDITOR="!f() {
			echo Hello!
		}; f" &&
		export GIT_EDITOR &&
		echo "GIT_EDITOR=$GIT_EDITOR" >expected &&
		git var -l >var &&
		sed -n -e "/^GIT_EDITOR/,\$p" var | head -n 3 >actual &&
		test_cmp expected actual
	)
'

test_expect_success 'listing and asking for variables are exclusive' '
	test_must_fail git var -l GIT_COMMITTER_IDENT
'

test_expect_success '`git var -l` works even without HOME' '
	(
		XDG_CONFIG_HOME= &&
		export XDG_CONFIG_HOME &&
		unset HOME &&
		git var -l
	)
'

test_expect_success 'get author identity components' '
	test_tick &&
	echo "$GIT_AUTHOR_NAME" >expect.name &&
	echo "$GIT_AUTHOR_EMAIL" >expect.email &&
	echo "$GIT_AUTHOR_DATE" >expect.date &&
	git var GIT_AUTHOR_NAME >actual.name &&
	git var GIT_AUTHOR_EMAIL >actual.email &&
	git var GIT_AUTHOR_DATE >actual.date &&
	test_cmp expect.name actual.name &&
	test_cmp expect.email actual.email &&
	test_cmp expect.date actual.date
'

test_expect_success 'get committer identity components' '
	test_tick &&
	echo "$GIT_COMMITTER_NAME" >expect.name &&
	echo "$GIT_COMMITTER_EMAIL" >expect.email &&
	echo "$GIT_COMMITTER_DATE" >expect.date &&
	git var GIT_COMMITTER_NAME >actual.name &&
	git var GIT_COMMITTER_EMAIL >actual.email &&
	git var GIT_COMMITTER_DATE >actual.date &&
	test_cmp expect.name actual.name &&
	test_cmp expect.email actual.email &&
	test_cmp expect.date actual.date
'

test_expect_success 'get multiple variables' '
	test_tick &&
	cat >expect <<-EOF &&
	$GIT_AUTHOR_NAME
	$GIT_AUTHOR_EMAIL
	$GIT_COMMITTER_NAME
	$GIT_COMMITTER_EMAIL
	EOF
	git var GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_COMMITTER_NAME GIT_COMMITTER_EMAIL >actual &&
	test_cmp expect actual
'

test_expect_success 'get multiple variables with -z' '
	test_tick &&
	printf "%sQ%sQ" "$GIT_AUTHOR_NAME" "$GIT_AUTHOR_EMAIL" >expect &&
	git var -z GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL >actual.raw &&
	nul_to_q <actual.raw >actual &&
	test_cmp expect actual
'

test_expect_success 'get multi-valued variable with -z' '
	TRASHDIR="$(test-tool path-utils normalize_path_copy "$(pwd)")" &&
	HOME="$TRASHDIR" XDG_CONFIG_HOME="$TRASHDIR/foo" git var -z GIT_CONFIG_GLOBAL >actual.raw &&
	printf "%sQ%sQ" "$TRASHDIR/foo/git/config" "$TRASHDIR/.gitconfig" >expect &&
	nul_to_q <actual.raw >actual &&
	test_cmp expect actual
'

test_expect_success 'git var -l -z' '
	git var -l -z >actual &&
	tr "\0" "\n" <actual >actual.lines &&
	echo "$GIT_AUTHOR_NAME" >expect &&
	sed -n "/^GIT_AUTHOR_NAME$/{n;p;}" actual.lines >actual.author &&
	test_cmp expect actual.author &&
	echo false >expect &&
	sed -n "/^core\.bare$/{n;p;}" actual.lines >actual.bare &&
	test_cmp expect actual.bare
'

test_expect_success 'get GIT_SIGNING_KEY with user.signingkey configured' '
	test_config user.signingkey "TEST_KEY_ID" &&
	echo "TEST_KEY_ID" >expect &&
	git var GIT_SIGNING_KEY >actual &&
	test_cmp expect actual
'

test_expect_success 'get GIT_SIGNING_KEY fails when unset' '
	test_config user.signingkey "" &&
	test_must_fail git var GIT_SIGNING_KEY
'

test_expect_success 'git var -l lists new variables' '
	git var -l >actual &&
	test_grep "^GIT_AUTHOR_NAME=" actual &&
	test_grep "^GIT_AUTHOR_EMAIL=" actual &&
	test_grep "^GIT_AUTHOR_DATE=" actual &&
	test_grep "^GIT_COMMITTER_NAME=" actual &&
	test_grep "^GIT_COMMITTER_EMAIL=" actual &&
	test_grep "^GIT_COMMITTER_DATE=" actual
'

test_expect_success 'git var -l lists GIT_SIGNING_KEY when configured' '
	test_config user.signingkey "TEST_KEY_ID" &&
	git var -l >actual &&
	test_grep "^GIT_SIGNING_KEY=TEST_KEY_ID" actual
'

test_expect_success 'options must precede variable arguments' '
	test_must_fail git var GIT_AUTHOR_NAME -z
'

test_expect_success 'get multiple variables with unset variable outputs blank record' '
	test_config user.signingkey "" &&
	cat >expect <<-EOF &&
	$GIT_AUTHOR_NAME

	$GIT_COMMITTER_NAME
	EOF
	git var GIT_AUTHOR_NAME GIT_SIGNING_KEY GIT_COMMITTER_NAME >actual &&
	test_cmp expect actual
'

test_expect_success 'get multiple variables with -z and unset variable' '
	test_config user.signingkey "" &&
	printf "%sQ%sQ" "$GIT_AUTHOR_NAME" "Q$GIT_COMMITTER_NAME" >expect &&
	git var -z GIT_AUTHOR_NAME GIT_SIGNING_KEY GIT_COMMITTER_NAME >actual.raw &&
	nul_to_q <actual.raw >actual &&
	test_cmp expect actual
'

test_expect_success 'get multiple variables including multi-valued variable with -z' '
	TRASHDIR="$(test-tool path-utils normalize_path_copy "$(pwd)")" &&
	printf "%sQ%sQ%sQQ%sQ" "$GIT_AUTHOR_NAME" \
		"$TRASHDIR/foo/git/config" "$TRASHDIR/.gitconfig" \
		"$GIT_AUTHOR_EMAIL" >expect &&
	HOME="$TRASHDIR" XDG_CONFIG_HOME="$TRASHDIR/foo" \
		git var -z GIT_AUTHOR_NAME GIT_CONFIG_GLOBAL GIT_AUTHOR_EMAIL >actual.raw &&
	nul_to_q <actual.raw >actual &&
	test_cmp expect actual
'

test_done
