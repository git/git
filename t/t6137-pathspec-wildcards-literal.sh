#!/bin/sh
test_description='test wildcards and literals with git add/commit (subshell style)'

. ./test-lib.sh

test_have_prereq FUNNYNAMES || {
	skip_all='skipping: needs FUNNYNAMES (non-Windows only)'
	test_done
}

prepare_test_files () {
	for f in "*" "**" "?" "[abc]" "a" "f*" "f**" "f?z" "foo*bar" "hello?world" "hello_world"
	do
		>"$f" || return
	done
}

test_expect_success 'add wildcard *' '
	git init test-asterisk &&
	(
		cd test-asterisk &&
		prepare_test_files &&
		git add "*" &&
		cat >expect <<-EOF &&
		*
		**
		?
		[abc]
		a
		f*
		f**
		f?z
		foo*bar
		hello?world
		hello_world
		EOF
		git ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'add literal \*' '
	git init test-asterisk-literal &&
	(
		cd test-asterisk-literal &&
		prepare_test_files &&
		git add "\*" &&
		cat >expect <<-EOF &&
		*
		EOF
		git ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'add wildcard **' '
	git init test-dstar &&
	(
		cd test-dstar &&
		prepare_test_files &&
		git add "**" &&
		cat >expect <<-EOF &&
		*
		**
		?
		[abc]
		a
		f*
		f**
		f?z
		foo*bar
		hello?world
		hello_world
		EOF
		git ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'add wildcard ?' '
	git init test-qmark &&
	(
		cd test-qmark &&
		prepare_test_files &&
		git add "?" &&
		cat >expect <<-\EOF | sort &&
		*
		?
		a
		EOF
		git ls-files | sort >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'add wildcard [abc]' '
	git init test-brackets &&
	(
		cd test-brackets &&
		prepare_test_files &&
		git add "[abc]" &&
		cat >expect <<-\EOF | sort &&
		[abc]
		a
		EOF
		git ls-files | sort >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'add wildcard f*' '
	git init test-f-wild &&
	(
		cd test-f-wild &&
		prepare_test_files &&
		git add "f*" &&
		cat >expect <<-\EOF | sort &&
		f*
		f**
		f?z
		foo*bar
		EOF
		git ls-files | sort >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'add literal f\*' '
	git init test-f-lit &&
	(
		cd test-f-lit &&
		prepare_test_files &&
		git add "f\*" &&
		cat >expect <<-\EOF &&
		f*
		EOF
		git ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'add wildcard f**' '
	git init test-fdstar &&
	(
		cd test-fdstar &&
		prepare_test_files &&
		git add "f**" &&
		cat >expect <<-\EOF | sort &&
		f*
		f**
		f?z
		foo*bar
		EOF
		git ls-files | sort >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'add literal f\*\*' '
	git init test-fdstar-lit &&
	(
		cd test-fdstar-lit &&
		prepare_test_files &&
		git add "f\*\*" &&
		cat >expect <<-\EOF &&
		f**
		EOF
		git ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'add wildcard f?z' '
	git init test-fqz &&
	(
		cd test-fqz &&
		prepare_test_files &&
		git add "f?z" &&
		cat >expect <<-\EOF &&
		f?z
		EOF
		git ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'add literal \? literal' '
	git init test-q-lit &&
	(
		cd test-q-lit &&
		prepare_test_files &&
		git add "\?" &&
		cat >expect <<-\EOF &&
		?
		EOF
		git ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'add wildcard foo*bar' '
	git init test-foobar &&
	(
		cd test-foobar &&
		prepare_test_files &&
		git add "foo*bar" &&
		cat >expect <<-\EOF &&
		foo*bar
		EOF
		git ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'add wildcard hello?world' '
	git init test-hellowild &&
	(
		cd test-hellowild &&
		prepare_test_files &&
		git add "hello?world" &&
		cat >expect <<-\EOF &&
		hello?world
		hello_world
		EOF
		git ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'add literal hello\?world' '
	git init test-hellolit &&
	(
		cd test-hellolit &&
		prepare_test_files &&
		git add "hello\?world" &&
		cat >expect <<-\EOF &&
		hello?world
		EOF
		git ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'add literal [abc]' '
	git init test-brackets-lit &&
	(
		cd test-brackets-lit &&
		prepare_test_files &&
		git add "\[abc\]" &&
		cat >expect <<-\EOF &&
		[abc]
		EOF
		git ls-files >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'commit: wildcard *' '
	git init test-c-asterisk &&
	(
		cd test-c-asterisk &&
		prepare_test_files &&
		git add . &&
		git commit -m "c1" -- "*" &&
		cat >expect <<-EOF &&
		*
		**
		?
		[abc]
		a
		f*
		f**
		f?z
		foo*bar
		hello?world
		hello_world
		EOF
		git ls-tree -r --name-only HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'commit: literal *' '
	git init test-c-asterisk-lit &&
	(
		cd test-c-asterisk-lit &&
		prepare_test_files &&
		git add . &&
		git commit -m "c2" -- "\*" &&
		cat >expect <<-EOF &&
		*
		EOF
		git ls-tree -r --name-only HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'commit: wildcard f*' '
	git init test-c-fwild &&
	(
		cd test-c-fwild &&
		prepare_test_files &&
		git add . &&
		git commit -m "c3" -- "f*" &&
		cat >expect <<-EOF &&
		f*
		f**
		f?z
		foo*bar
		EOF
		git ls-tree -r --name-only HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'commit: literal f\*' '
	git init test-c-flit &&
	(
		cd test-c-flit &&
		prepare_test_files &&
		git add . &&
		git commit -m "c4" -- "f\*" &&
		cat >expect <<-EOF &&
		f*
		EOF
		git ls-tree -r --name-only HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'commit: wildcard pathspec limits commit' '
	git init test-c-pathlimit &&
	(
		cd test-c-pathlimit &&
		prepare_test_files &&
		git add . &&
		git commit -m "c5" -- "f**" &&
		cat >expect <<-EOF &&
		f*
		f**
		f?z
		foo*bar
		EOF
		git ls-tree -r --name-only HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'commit: literal f\*\*' '
	git init test-c-fdstar-lit &&
	(
		cd test-c-fdstar-lit &&
		prepare_test_files &&
		git add . &&
		git commit -m "c6" -- "f\*\*" &&
		cat >expect <<-EOF &&
		f**
		EOF
		git ls-tree -r --name-only HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'commit: wildcard ?' '
	git init test-c-qwild &&
	(
		cd test-c-qwild &&
		prepare_test_files &&
		git add . &&
		git commit -m "c7" -- "?" &&
		cat >expect <<-EOF &&
		*
		?
		a
		EOF
		git ls-tree -r --name-only HEAD | sort >actual &&
		sort expect >expect.sorted &&
		test_cmp expect.sorted actual
	)
'

test_expect_success 'commit: literal \?' '
	git init test-c-qlit &&
	(
		cd test-c-qlit &&
		prepare_test_files &&
		git add . &&
		git commit -m "c8" -- "\?" &&
		cat >expect <<-EOF &&
		?
		EOF
		git ls-tree -r --name-only HEAD >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'commit: wildcard hello?world' '
	git init test-c-hellowild &&
	(
		cd test-c-hellowild &&
		prepare_test_files &&
		git add . &&
		git commit -m "c9" -- "hello?world"  &&
		cat >expect <<-EOF &&
		hello?world
		hello_world
		EOF
		git ls-tree -r --name-only HEAD | sort >actual &&
		sort expect >expect.sorted &&
		test_cmp expect.sorted actual
	)
'

test_expect_success 'commit: literal hello\?world' '
	git init test-c-hellolit &&
	(
		cd test-c-hellolit &&
		prepare_test_files &&
		git add . &&
		git commit -m "c10" -- "hello\?world" &&
		cat >expect <<-EOF &&
		hello?world
		EOF
		git ls-tree -r --name-only HEAD >actual &&
		test_cmp expect actual
	)
'

test_done
