#!/bin/sh

test_description='checkout handling of ambiguous (branch/tag) refs'
. ./test-lib.sh

test_expect_success 'setup ambiguous refs' '
	test_commit branch file &&
	git branch ambiguity &&
	git branch vagueness &&
	test_commit tag file &&
	git tag ambiguity &&
	git tag vagueness HEAD:file &&
	test_commit other file
'

test_expect_success 'checkout ambiguous ref succeeds' '
	git checkout ambiguity >stdout 2>stderr
'

test_expect_success 'checkout produces ambiguity warning' '
	grep "warning.*ambiguous" stderr
'

test_expect_success 'checkout chooses branch over tag' '
	echo refs/heads/ambiguity >expect &&
	git symbolic-ref HEAD >actual &&
	test_cmp expect actual &&
	echo branch >expect &&
	test_cmp expect file
'

test_expect_success 'checkout reports switch to branch' '
	test_i18ngrep "Switched to branch" stderr &&
	test_i18ngrep ! "^HEAD is now at" stderr
'

test_expect_success 'checkout vague ref succeeds' '
	git checkout vagueness >stdout 2>stderr &&
	test_set_prereq VAGUENESS_SUCCESS
'

test_expect_success VAGUENESS_SUCCESS 'checkout produces ambiguity warning' '
	grep "warning.*ambiguous" stderr
'

test_expect_success VAGUENESS_SUCCESS 'checkout chooses branch over tag' '
	echo refs/heads/vagueness >expect &&
	git symbolic-ref HEAD >actual &&
	test_cmp expect actual &&
	echo branch >expect &&
	test_cmp expect file
'

test_expect_success VAGUENESS_SUCCESS 'checkout reports switch to branch' '
	test_i18ngrep "Switched to branch" stderr &&
	test_i18ngrep ! "^HEAD is now at" stderr
'

test_expect_success 'wildcard ambiguation, paths win' '
	git init ambi &&
	(
		cd ambi &&
		echo a >a.c &&
		git add a.c &&
		echo b >a.c &&
		git checkout "*.c" &&
		echo a >expect &&
		test_cmp expect a.c
	)
'

test_expect_success !MINGW 'wildcard ambiguation, refs lose' '
	git init ambi2 &&
	(
		cd ambi2 &&
		echo a >"*.c" &&
		git add . &&
		test_must_fail git show :"*.c" &&
		git show :"*.c" -- >actual &&
		echo a >expect &&
		test_cmp expect actual
	)
'

test_done
