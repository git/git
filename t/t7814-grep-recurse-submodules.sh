#!/bin/sh

test_description='Test grep recurse-submodules feature

This test verifies the recurse-submodules feature correctly greps across
submodules.
'

. ./test-lib.sh

test_expect_success 'setup directory structure and submodule' '
	echo "(1|2)d(3|4)" >a &&
	mkdir b &&
	echo "(3|4)" >b/b &&
	git add a b &&
	git commit -m "add a and b" &&
	test_tick &&
	git init submodule &&
	echo "(1|2)d(3|4)" >submodule/a &&
	git -C submodule add a &&
	git -C submodule commit -m "add a" &&
	git submodule add ./submodule &&
	git commit -m "added submodule" &&
	test_tick
'

test_expect_success 'grep correctly finds patterns in a submodule' '
	cat >expect <<-\EOF &&
	a:(1|2)d(3|4)
	b/b:(3|4)
	submodule/a:(1|2)d(3|4)
	EOF

	git grep -e "(3|4)" --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success 'grep finds patterns in a submodule via config' '
	test_config submodule.recurse true &&
	# expect from previous test
	git grep -e "(3|4)" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --no-recurse-submodules overrides config' '
	test_config submodule.recurse true &&
	cat >expect <<-\EOF &&
	a:(1|2)d(3|4)
	b/b:(3|4)
	EOF

	git grep -e "(3|4)" --no-recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success 'grep and basic pathspecs' '
	cat >expect <<-\EOF &&
	submodule/a:(1|2)d(3|4)
	EOF

	git grep -e. --recurse-submodules -- submodule >actual &&
	test_cmp expect actual
'

test_expect_success 'grep and nested submodules' '
	git init submodule/sub &&
	echo "(1|2)d(3|4)" >submodule/sub/a &&
	git -C submodule/sub add a &&
	git -C submodule/sub commit -m "add a" &&
	test_tick &&
	git -C submodule submodule add ./sub &&
	git -C submodule add sub &&
	git -C submodule commit -m "added sub" &&
	test_tick &&
	git add submodule &&
	git commit -m "updated submodule" &&
	test_tick &&

	cat >expect <<-\EOF &&
	a:(1|2)d(3|4)
	b/b:(3|4)
	submodule/a:(1|2)d(3|4)
	submodule/sub/a:(1|2)d(3|4)
	EOF

	git grep -e "(3|4)" --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success 'grep and multiple patterns' '
	cat >expect <<-\EOF &&
	a:(1|2)d(3|4)
	submodule/a:(1|2)d(3|4)
	submodule/sub/a:(1|2)d(3|4)
	EOF

	git grep -e "(3|4)" --and -e "(1|2)" --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success 'grep and multiple patterns' '
	cat >expect <<-\EOF &&
	b/b:(3|4)
	EOF

	git grep -e "(3|4)" --and --not -e "(1|2)" --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success 'basic grep tree' '
	cat >expect <<-\EOF &&
	HEAD:a:(1|2)d(3|4)
	HEAD:b/b:(3|4)
	HEAD:submodule/a:(1|2)d(3|4)
	HEAD:submodule/sub/a:(1|2)d(3|4)
	EOF

	git grep -e "(3|4)" --recurse-submodules HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'grep tree HEAD^' '
	cat >expect <<-\EOF &&
	HEAD^:a:(1|2)d(3|4)
	HEAD^:b/b:(3|4)
	HEAD^:submodule/a:(1|2)d(3|4)
	EOF

	git grep -e "(3|4)" --recurse-submodules HEAD^ >actual &&
	test_cmp expect actual
'

test_expect_success 'grep tree HEAD^^' '
	cat >expect <<-\EOF &&
	HEAD^^:a:(1|2)d(3|4)
	HEAD^^:b/b:(3|4)
	EOF

	git grep -e "(3|4)" --recurse-submodules HEAD^^ >actual &&
	test_cmp expect actual
'

test_expect_success 'grep tree and pathspecs' '
	cat >expect <<-\EOF &&
	HEAD:submodule/a:(1|2)d(3|4)
	HEAD:submodule/sub/a:(1|2)d(3|4)
	EOF

	git grep -e "(3|4)" --recurse-submodules HEAD -- submodule >actual &&
	test_cmp expect actual
'

test_expect_success 'grep tree and pathspecs' '
	cat >expect <<-\EOF &&
	HEAD:submodule/a:(1|2)d(3|4)
	HEAD:submodule/sub/a:(1|2)d(3|4)
	EOF

	git grep -e "(3|4)" --recurse-submodules HEAD -- "submodule*a" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep tree and more pathspecs' '
	cat >expect <<-\EOF &&
	HEAD:submodule/a:(1|2)d(3|4)
	EOF

	git grep -e "(3|4)" --recurse-submodules HEAD -- "submodul?/a" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep tree and more pathspecs' '
	cat >expect <<-\EOF &&
	HEAD:submodule/sub/a:(1|2)d(3|4)
	EOF

	git grep -e "(3|4)" --recurse-submodules HEAD -- "submodul*/sub/a" >actual &&
	test_cmp expect actual
'

test_expect_success !MINGW 'grep recurse submodule colon in name' '
	git init parent &&
	test_when_finished "rm -rf parent" &&
	echo "(1|2)d(3|4)" >"parent/fi:le" &&
	git -C parent add "fi:le" &&
	git -C parent commit -m "add fi:le" &&
	test_tick &&

	git init "su:b" &&
	test_when_finished "rm -rf su:b" &&
	echo "(1|2)d(3|4)" >"su:b/fi:le" &&
	git -C "su:b" add "fi:le" &&
	git -C "su:b" commit -m "add fi:le" &&
	test_tick &&

	git -C parent submodule add "../su:b" "su:b" &&
	git -C parent commit -m "add submodule" &&
	test_tick &&

	cat >expect <<-\EOF &&
	fi:le:(1|2)d(3|4)
	su:b/fi:le:(1|2)d(3|4)
	EOF
	git -C parent grep -e "(1|2)d(3|4)" --recurse-submodules >actual &&
	test_cmp expect actual &&

	cat >expect <<-\EOF &&
	HEAD:fi:le:(1|2)d(3|4)
	HEAD:su:b/fi:le:(1|2)d(3|4)
	EOF
	git -C parent grep -e "(1|2)d(3|4)" --recurse-submodules HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'grep history with moved submoules' '
	git init parent &&
	test_when_finished "rm -rf parent" &&
	echo "(1|2)d(3|4)" >parent/file &&
	git -C parent add file &&
	git -C parent commit -m "add file" &&
	test_tick &&

	git init sub &&
	test_when_finished "rm -rf sub" &&
	echo "(1|2)d(3|4)" >sub/file &&
	git -C sub add file &&
	git -C sub commit -m "add file" &&
	test_tick &&

	git -C parent submodule add ../sub dir/sub &&
	git -C parent commit -m "add submodule" &&
	test_tick &&

	cat >expect <<-\EOF &&
	dir/sub/file:(1|2)d(3|4)
	file:(1|2)d(3|4)
	EOF
	git -C parent grep -e "(1|2)d(3|4)" --recurse-submodules >actual &&
	test_cmp expect actual &&

	git -C parent mv dir/sub sub-moved &&
	git -C parent commit -m "moved submodule" &&
	test_tick &&

	cat >expect <<-\EOF &&
	file:(1|2)d(3|4)
	sub-moved/file:(1|2)d(3|4)
	EOF
	git -C parent grep -e "(1|2)d(3|4)" --recurse-submodules >actual &&
	test_cmp expect actual &&

	cat >expect <<-\EOF &&
	HEAD^:dir/sub/file:(1|2)d(3|4)
	HEAD^:file:(1|2)d(3|4)
	EOF
	git -C parent grep -e "(1|2)d(3|4)" --recurse-submodules HEAD^ >actual &&
	test_cmp expect actual
'

test_expect_success 'grep using relative path' '
	test_when_finished "rm -rf parent sub" &&
	git init sub &&
	echo "(1|2)d(3|4)" >sub/file &&
	git -C sub add file &&
	git -C sub commit -m "add file" &&
	test_tick &&

	git init parent &&
	echo "(1|2)d(3|4)" >parent/file &&
	git -C parent add file &&
	mkdir parent/src &&
	echo "(1|2)d(3|4)" >parent/src/file2 &&
	git -C parent add src/file2 &&
	git -C parent submodule add ../sub &&
	git -C parent commit -m "add files and submodule" &&
	test_tick &&

	# From top works
	cat >expect <<-\EOF &&
	file:(1|2)d(3|4)
	src/file2:(1|2)d(3|4)
	sub/file:(1|2)d(3|4)
	EOF
	git -C parent grep --recurse-submodules -e "(1|2)d(3|4)" >actual &&
	test_cmp expect actual &&

	# Relative path to top
	cat >expect <<-\EOF &&
	../file:(1|2)d(3|4)
	file2:(1|2)d(3|4)
	../sub/file:(1|2)d(3|4)
	EOF
	git -C parent/src grep --recurse-submodules -e "(1|2)d(3|4)" -- .. >actual &&
	test_cmp expect actual &&

	# Relative path to submodule
	cat >expect <<-\EOF &&
	../sub/file:(1|2)d(3|4)
	EOF
	git -C parent/src grep --recurse-submodules -e "(1|2)d(3|4)" -- ../sub >actual &&
	test_cmp expect actual
'

test_expect_success 'grep from a subdir' '
	test_when_finished "rm -rf parent sub" &&
	git init sub &&
	echo "(1|2)d(3|4)" >sub/file &&
	git -C sub add file &&
	git -C sub commit -m "add file" &&
	test_tick &&

	git init parent &&
	mkdir parent/src &&
	echo "(1|2)d(3|4)" >parent/src/file &&
	git -C parent add src/file &&
	git -C parent submodule add ../sub src/sub &&
	git -C parent submodule add ../sub sub &&
	git -C parent commit -m "add files and submodules" &&
	test_tick &&

	# Verify grep from root works
	cat >expect <<-\EOF &&
	src/file:(1|2)d(3|4)
	src/sub/file:(1|2)d(3|4)
	sub/file:(1|2)d(3|4)
	EOF
	git -C parent grep --recurse-submodules -e "(1|2)d(3|4)" >actual &&
	test_cmp expect actual &&

	# Verify grep from a subdir works
	cat >expect <<-\EOF &&
	file:(1|2)d(3|4)
	sub/file:(1|2)d(3|4)
	EOF
	git -C parent/src grep --recurse-submodules -e "(1|2)d(3|4)" >actual &&
	test_cmp expect actual
'

test_incompatible_with_recurse_submodules ()
{
	test_expect_success "--recurse-submodules and $1 are incompatible" "
		test_must_fail git grep -e. --recurse-submodules $1 2>actual &&
		test_i18ngrep 'not supported with --recurse-submodules' actual
	"
}

test_incompatible_with_recurse_submodules --untracked
test_incompatible_with_recurse_submodules --no-index

test_expect_success 'grep --recurse-submodules should pass the pattern type along' '
	# Fixed
	test_must_fail git grep -F --recurse-submodules -e "(.|.)[\d]" &&
	test_must_fail git -c grep.patternType=fixed grep --recurse-submodules -e "(.|.)[\d]" &&

	# Basic
	git grep -G --recurse-submodules -e "(.|.)[\d]" >actual &&
	cat >expect <<-\EOF &&
	a:(1|2)d(3|4)
	submodule/a:(1|2)d(3|4)
	submodule/sub/a:(1|2)d(3|4)
	EOF
	test_cmp expect actual &&
	git -c grep.patternType=basic grep --recurse-submodules -e "(.|.)[\d]" >actual &&
	test_cmp expect actual &&

	# Extended
	git grep -E --recurse-submodules -e "(.|.)[\d]" >actual &&
	cat >expect <<-\EOF &&
	.gitmodules:[submodule "submodule"]
	.gitmodules:	path = submodule
	.gitmodules:	url = ./submodule
	a:(1|2)d(3|4)
	submodule/.gitmodules:[submodule "sub"]
	submodule/a:(1|2)d(3|4)
	submodule/sub/a:(1|2)d(3|4)
	EOF
	test_cmp expect actual &&
	git -c grep.patternType=extended grep --recurse-submodules -e "(.|.)[\d]" >actual &&
	test_cmp expect actual &&
	git -c grep.extendedRegexp=true grep --recurse-submodules -e "(.|.)[\d]" >actual &&
	test_cmp expect actual &&

	# Perl
	if test_have_prereq PCRE
	then
		git grep -P --recurse-submodules -e "(.|.)[\d]" >actual &&
		cat >expect <<-\EOF &&
		a:(1|2)d(3|4)
		b/b:(3|4)
		submodule/a:(1|2)d(3|4)
		submodule/sub/a:(1|2)d(3|4)
		EOF
		test_cmp expect actual &&
		git -c grep.patternType=perl grep --recurse-submodules -e "(.|.)[\d]" >actual &&
		test_cmp expect actual
	fi
'

test_expect_success 'grep --recurse-submodules with submodules without .gitmodules in the working tree' '
	test_when_finished "git -C submodule checkout .gitmodules" &&
	rm submodule/.gitmodules &&
	git grep --recurse-submodules -e "(.|.)[\d]" >actual &&
	cat >expect <<-\EOF &&
	a:(1|2)d(3|4)
	submodule/a:(1|2)d(3|4)
	submodule/sub/a:(1|2)d(3|4)
	EOF
	test_cmp expect actual
'

reset_and_clean () {
	git reset --hard &&
	git clean -fd &&
	git submodule foreach --recursive 'git reset --hard' &&
	git submodule foreach --recursive 'git clean -fd'
}

test_expect_success 'grep --recurse-submodules without --cached considers worktree modifications' '
	reset_and_clean &&
	echo "A modified line in submodule" >>submodule/a &&
	echo "submodule/a:A modified line in submodule" >expect &&
	git grep --recurse-submodules "A modified line in submodule" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --recurse-submodules with --cached ignores worktree modifications' '
	reset_and_clean &&
	echo "A modified line in submodule" >>submodule/a &&
	test_must_fail git grep --recurse-submodules --cached "A modified line in submodule" >actual 2>&1 &&
	test_must_be_empty actual
'
test_done
