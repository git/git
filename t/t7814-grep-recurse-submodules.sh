#!/bin/sh

test_description='Test grep recurse-submodules feature

This test verifies the recurse-submodules feature correctly greps across
submodules.
'

. ./test-lib.sh

BUT_TEST_FATAL_REGISTER_SUBMODULE_ODB=1
export BUT_TEST_FATAL_REGISTER_SUBMODULE_ODB

test_expect_success 'setup directory structure and submodule' '
	echo "(1|2)d(3|4)" >a &&
	mkdir b &&
	echo "(3|4)" >b/b &&
	but add a b &&
	but cummit -m "add a and b" &&
	test_tick &&
	but init submodule &&
	echo "(1|2)d(3|4)" >submodule/a &&
	but -C submodule add a &&
	but -C submodule cummit -m "add a" &&
	but submodule add ./submodule &&
	but cummit -m "added submodule" &&
	test_tick
'

test_expect_success 'grep correctly finds patterns in a submodule' '
	cat >expect <<-\EOF &&
	a:(1|2)d(3|4)
	b/b:(3|4)
	submodule/a:(1|2)d(3|4)
	EOF

	but grep -e "(3|4)" --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success 'grep finds patterns in a submodule via config' '
	test_config submodule.recurse true &&
	# expect from previous test
	but grep -e "(3|4)" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --no-recurse-submodules overrides config' '
	test_config submodule.recurse true &&
	cat >expect <<-\EOF &&
	a:(1|2)d(3|4)
	b/b:(3|4)
	EOF

	but grep -e "(3|4)" --no-recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success 'grep and basic pathspecs' '
	cat >expect <<-\EOF &&
	submodule/a:(1|2)d(3|4)
	EOF

	but grep -e. --recurse-submodules -- submodule >actual &&
	test_cmp expect actual
'

test_expect_success 'grep and nested submodules' '
	but init submodule/sub &&
	echo "(1|2)d(3|4)" >submodule/sub/a &&
	but -C submodule/sub add a &&
	but -C submodule/sub cummit -m "add a" &&
	test_tick &&
	but -C submodule submodule add ./sub &&
	but -C submodule add sub &&
	but -C submodule cummit -m "added sub" &&
	test_tick &&
	but add submodule &&
	but cummit -m "updated submodule" &&
	test_tick &&

	cat >expect <<-\EOF &&
	a:(1|2)d(3|4)
	b/b:(3|4)
	submodule/a:(1|2)d(3|4)
	submodule/sub/a:(1|2)d(3|4)
	EOF

	but grep -e "(3|4)" --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success 'grep and multiple patterns' '
	cat >expect <<-\EOF &&
	a:(1|2)d(3|4)
	submodule/a:(1|2)d(3|4)
	submodule/sub/a:(1|2)d(3|4)
	EOF

	but grep -e "(3|4)" --and -e "(1|2)" --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success 'grep and multiple patterns' '
	cat >expect <<-\EOF &&
	b/b:(3|4)
	EOF

	but grep -e "(3|4)" --and --not -e "(1|2)" --recurse-submodules >actual &&
	test_cmp expect actual
'

test_expect_success 'basic grep tree' '
	cat >expect <<-\EOF &&
	HEAD:a:(1|2)d(3|4)
	HEAD:b/b:(3|4)
	HEAD:submodule/a:(1|2)d(3|4)
	HEAD:submodule/sub/a:(1|2)d(3|4)
	EOF

	but grep -e "(3|4)" --recurse-submodules HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'grep tree HEAD^' '
	cat >expect <<-\EOF &&
	HEAD^:a:(1|2)d(3|4)
	HEAD^:b/b:(3|4)
	HEAD^:submodule/a:(1|2)d(3|4)
	EOF

	but grep -e "(3|4)" --recurse-submodules HEAD^ >actual &&
	test_cmp expect actual
'

test_expect_success 'grep tree HEAD^^' '
	cat >expect <<-\EOF &&
	HEAD^^:a:(1|2)d(3|4)
	HEAD^^:b/b:(3|4)
	EOF

	but grep -e "(3|4)" --recurse-submodules HEAD^^ >actual &&
	test_cmp expect actual
'

test_expect_success 'grep tree and pathspecs' '
	cat >expect <<-\EOF &&
	HEAD:submodule/a:(1|2)d(3|4)
	HEAD:submodule/sub/a:(1|2)d(3|4)
	EOF

	but grep -e "(3|4)" --recurse-submodules HEAD -- submodule >actual &&
	test_cmp expect actual
'

test_expect_success 'grep tree and pathspecs' '
	cat >expect <<-\EOF &&
	HEAD:submodule/a:(1|2)d(3|4)
	HEAD:submodule/sub/a:(1|2)d(3|4)
	EOF

	but grep -e "(3|4)" --recurse-submodules HEAD -- "submodule*a" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep tree and more pathspecs' '
	cat >expect <<-\EOF &&
	HEAD:submodule/a:(1|2)d(3|4)
	EOF

	but grep -e "(3|4)" --recurse-submodules HEAD -- "submodul?/a" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep tree and more pathspecs' '
	cat >expect <<-\EOF &&
	HEAD:submodule/sub/a:(1|2)d(3|4)
	EOF

	but grep -e "(3|4)" --recurse-submodules HEAD -- "submodul*/sub/a" >actual &&
	test_cmp expect actual
'

test_expect_success !MINGW 'grep recurse submodule colon in name' '
	but init parent &&
	test_when_finished "rm -rf parent" &&
	echo "(1|2)d(3|4)" >"parent/fi:le" &&
	but -C parent add "fi:le" &&
	but -C parent cummit -m "add fi:le" &&
	test_tick &&

	but init "su:b" &&
	test_when_finished "rm -rf su:b" &&
	echo "(1|2)d(3|4)" >"su:b/fi:le" &&
	but -C "su:b" add "fi:le" &&
	but -C "su:b" cummit -m "add fi:le" &&
	test_tick &&

	but -C parent submodule add "../su:b" "su:b" &&
	but -C parent cummit -m "add submodule" &&
	test_tick &&

	cat >expect <<-\EOF &&
	fi:le:(1|2)d(3|4)
	su:b/fi:le:(1|2)d(3|4)
	EOF
	but -C parent grep -e "(1|2)d(3|4)" --recurse-submodules >actual &&
	test_cmp expect actual &&

	cat >expect <<-\EOF &&
	HEAD:fi:le:(1|2)d(3|4)
	HEAD:su:b/fi:le:(1|2)d(3|4)
	EOF
	but -C parent grep -e "(1|2)d(3|4)" --recurse-submodules HEAD >actual &&
	test_cmp expect actual
'

test_expect_success 'grep history with moved submoules' '
	but init parent &&
	test_when_finished "rm -rf parent" &&
	echo "(1|2)d(3|4)" >parent/file &&
	but -C parent add file &&
	but -C parent cummit -m "add file" &&
	test_tick &&

	but init sub &&
	test_when_finished "rm -rf sub" &&
	echo "(1|2)d(3|4)" >sub/file &&
	but -C sub add file &&
	but -C sub cummit -m "add file" &&
	test_tick &&

	but -C parent submodule add ../sub dir/sub &&
	but -C parent cummit -m "add submodule" &&
	test_tick &&

	cat >expect <<-\EOF &&
	dir/sub/file:(1|2)d(3|4)
	file:(1|2)d(3|4)
	EOF
	but -C parent grep -e "(1|2)d(3|4)" --recurse-submodules >actual &&
	test_cmp expect actual &&

	but -C parent mv dir/sub sub-moved &&
	but -C parent cummit -m "moved submodule" &&
	test_tick &&

	cat >expect <<-\EOF &&
	file:(1|2)d(3|4)
	sub-moved/file:(1|2)d(3|4)
	EOF
	but -C parent grep -e "(1|2)d(3|4)" --recurse-submodules >actual &&
	test_cmp expect actual &&

	cat >expect <<-\EOF &&
	HEAD^:dir/sub/file:(1|2)d(3|4)
	HEAD^:file:(1|2)d(3|4)
	EOF
	but -C parent grep -e "(1|2)d(3|4)" --recurse-submodules HEAD^ >actual &&
	test_cmp expect actual
'

test_expect_success 'grep using relative path' '
	test_when_finished "rm -rf parent sub" &&
	but init sub &&
	echo "(1|2)d(3|4)" >sub/file &&
	but -C sub add file &&
	but -C sub cummit -m "add file" &&
	test_tick &&

	but init parent &&
	echo "(1|2)d(3|4)" >parent/file &&
	but -C parent add file &&
	mkdir parent/src &&
	echo "(1|2)d(3|4)" >parent/src/file2 &&
	but -C parent add src/file2 &&
	but -C parent submodule add ../sub &&
	but -C parent cummit -m "add files and submodule" &&
	test_tick &&

	# From top works
	cat >expect <<-\EOF &&
	file:(1|2)d(3|4)
	src/file2:(1|2)d(3|4)
	sub/file:(1|2)d(3|4)
	EOF
	but -C parent grep --recurse-submodules -e "(1|2)d(3|4)" >actual &&
	test_cmp expect actual &&

	# Relative path to top
	cat >expect <<-\EOF &&
	../file:(1|2)d(3|4)
	file2:(1|2)d(3|4)
	../sub/file:(1|2)d(3|4)
	EOF
	but -C parent/src grep --recurse-submodules -e "(1|2)d(3|4)" -- .. >actual &&
	test_cmp expect actual &&

	# Relative path to submodule
	cat >expect <<-\EOF &&
	../sub/file:(1|2)d(3|4)
	EOF
	but -C parent/src grep --recurse-submodules -e "(1|2)d(3|4)" -- ../sub >actual &&
	test_cmp expect actual
'

test_expect_success 'grep from a subdir' '
	test_when_finished "rm -rf parent sub" &&
	but init sub &&
	echo "(1|2)d(3|4)" >sub/file &&
	but -C sub add file &&
	but -C sub cummit -m "add file" &&
	test_tick &&

	but init parent &&
	mkdir parent/src &&
	echo "(1|2)d(3|4)" >parent/src/file &&
	but -C parent add src/file &&
	but -C parent submodule add ../sub src/sub &&
	but -C parent submodule add ../sub sub &&
	but -C parent cummit -m "add files and submodules" &&
	test_tick &&

	# Verify grep from root works
	cat >expect <<-\EOF &&
	src/file:(1|2)d(3|4)
	src/sub/file:(1|2)d(3|4)
	sub/file:(1|2)d(3|4)
	EOF
	but -C parent grep --recurse-submodules -e "(1|2)d(3|4)" >actual &&
	test_cmp expect actual &&

	# Verify grep from a subdir works
	cat >expect <<-\EOF &&
	file:(1|2)d(3|4)
	sub/file:(1|2)d(3|4)
	EOF
	but -C parent/src grep --recurse-submodules -e "(1|2)d(3|4)" >actual &&
	test_cmp expect actual
'

test_incompatible_with_recurse_submodules ()
{
	test_expect_success "--recurse-submodules and $1 are incompatible" "
		test_must_fail but grep -e. --recurse-submodules $1 2>actual &&
		test_i18ngrep 'not supported with --recurse-submodules' actual
	"
}

test_incompatible_with_recurse_submodules --untracked

test_expect_success 'grep --recurse-submodules --no-index ignores --recurse-submodules' '
	but grep --recurse-submodules --no-index -e "^(.|.)[\d]" >actual &&
	cat >expect <<-\EOF &&
	a:(1|2)d(3|4)
	submodule/a:(1|2)d(3|4)
	submodule/sub/a:(1|2)d(3|4)
	EOF
	test_cmp expect actual
'

test_expect_success 'grep --recurse-submodules should pass the pattern type along' '
	# Fixed
	test_must_fail but grep -F --recurse-submodules -e "(.|.)[\d]" &&
	test_must_fail but -c grep.patternType=fixed grep --recurse-submodules -e "(.|.)[\d]" &&

	# Basic
	but grep -G --recurse-submodules -e "(.|.)[\d]" >actual &&
	cat >expect <<-\EOF &&
	a:(1|2)d(3|4)
	submodule/a:(1|2)d(3|4)
	submodule/sub/a:(1|2)d(3|4)
	EOF
	test_cmp expect actual &&
	but -c grep.patternType=basic grep --recurse-submodules -e "(.|.)[\d]" >actual &&
	test_cmp expect actual &&

	# Extended
	but grep -E --recurse-submodules -e "(.|.)[\d]" >actual &&
	cat >expect <<-\EOF &&
	.butmodules:[submodule "submodule"]
	.butmodules:	path = submodule
	.butmodules:	url = ./submodule
	a:(1|2)d(3|4)
	submodule/.butmodules:[submodule "sub"]
	submodule/a:(1|2)d(3|4)
	submodule/sub/a:(1|2)d(3|4)
	EOF
	test_cmp expect actual &&
	but -c grep.patternType=extended grep --recurse-submodules -e "(.|.)[\d]" >actual &&
	test_cmp expect actual &&
	but -c grep.extendedRegexp=true grep --recurse-submodules -e "(.|.)[\d]" >actual &&
	test_cmp expect actual &&

	# Perl
	if test_have_prereq PCRE
	then
		but grep -P --recurse-submodules -e "(.|.)[\d]" >actual &&
		cat >expect <<-\EOF &&
		a:(1|2)d(3|4)
		b/b:(3|4)
		submodule/a:(1|2)d(3|4)
		submodule/sub/a:(1|2)d(3|4)
		EOF
		test_cmp expect actual &&
		but -c grep.patternType=perl grep --recurse-submodules -e "(.|.)[\d]" >actual &&
		test_cmp expect actual
	fi
'

test_expect_success 'grep --recurse-submodules with submodules without .butmodules in the working tree' '
	test_when_finished "but -C submodule checkout .butmodules" &&
	rm submodule/.butmodules &&
	but grep --recurse-submodules -e "(.|.)[\d]" >actual &&
	cat >expect <<-\EOF &&
	a:(1|2)d(3|4)
	submodule/a:(1|2)d(3|4)
	submodule/sub/a:(1|2)d(3|4)
	EOF
	test_cmp expect actual
'

reset_and_clean () {
	but reset --hard &&
	but clean -fd &&
	but submodule foreach --recursive 'but reset --hard' &&
	but submodule foreach --recursive 'but clean -fd'
}

test_expect_success 'grep --recurse-submodules without --cached considers worktree modifications' '
	reset_and_clean &&
	echo "A modified line in submodule" >>submodule/a &&
	echo "submodule/a:A modified line in submodule" >expect &&
	but grep --recurse-submodules "A modified line in submodule" >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --recurse-submodules with --cached ignores worktree modifications' '
	reset_and_clean &&
	echo "A modified line in submodule" >>submodule/a &&
	test_must_fail but grep --recurse-submodules --cached "A modified line in submodule" >actual 2>&1 &&
	test_must_be_empty actual
'

test_expect_failure 'grep --textconv: superproject .butattributes does not affect submodules' '
	reset_and_clean &&
	test_config_global diff.d2x.textconv "sed -e \"s/d/x/\"" &&
	echo "a diff=d2x" >.butattributes &&

	cat >expect <<-\EOF &&
	a:(1|2)x(3|4)
	EOF
	but grep --textconv --recurse-submodules x >actual &&
	test_cmp expect actual
'

test_expect_failure 'grep --textconv: superproject .butattributes (from index) does not affect submodules' '
	reset_and_clean &&
	test_config_global diff.d2x.textconv "sed -e \"s/d/x/\"" &&
	echo "a diff=d2x" >.butattributes &&
	but add .butattributes &&
	rm .butattributes &&

	cat >expect <<-\EOF &&
	a:(1|2)x(3|4)
	EOF
	but grep --textconv --recurse-submodules x >actual &&
	test_cmp expect actual
'

test_expect_failure 'grep --textconv: superproject .but/info/attributes does not affect submodules' '
	reset_and_clean &&
	test_config_global diff.d2x.textconv "sed -e \"s/d/x/\"" &&
	super_attr="$(but rev-parse --but-path info/attributes)" &&
	test_when_finished "rm -f \"$super_attr\"" &&
	echo "a diff=d2x" >"$super_attr" &&

	cat >expect <<-\EOF &&
	a:(1|2)x(3|4)
	EOF
	but grep --textconv --recurse-submodules x >actual &&
	test_cmp expect actual
'

# Note: what currently prevents this test from passing is not that the
# .butattributes file from "./submodule" is being ignored, but that it is being
# propagated to the nested "./submodule/sub" files.
#
test_expect_failure 'grep --textconv correctly reads submodule .butattributes' '
	reset_and_clean &&
	test_config_global diff.d2x.textconv "sed -e \"s/d/x/\"" &&
	echo "a diff=d2x" >submodule/.butattributes &&

	cat >expect <<-\EOF &&
	submodule/a:(1|2)x(3|4)
	EOF
	but grep --textconv --recurse-submodules x >actual &&
	test_cmp expect actual
'

test_expect_failure 'grep --textconv correctly reads submodule .butattributes (from index)' '
	reset_and_clean &&
	test_config_global diff.d2x.textconv "sed -e \"s/d/x/\"" &&
	echo "a diff=d2x" >submodule/.butattributes &&
	but -C submodule add .butattributes &&
	rm submodule/.butattributes &&

	cat >expect <<-\EOF &&
	submodule/a:(1|2)x(3|4)
	EOF
	but grep --textconv --recurse-submodules x >actual &&
	test_cmp expect actual
'

test_expect_failure 'grep --textconv correctly reads submodule .but/info/attributes' '
	reset_and_clean &&
	test_config_global diff.d2x.textconv "sed -e \"s/d/x/\"" &&

	submodule_attr="$(but -C submodule rev-parse --path-format=absolute --but-path info/attributes)" &&
	test_when_finished "rm -f \"$submodule_attr\"" &&
	echo "a diff=d2x" >"$submodule_attr" &&

	cat >expect <<-\EOF &&
	submodule/a:(1|2)x(3|4)
	EOF
	but grep --textconv --recurse-submodules x >actual &&
	test_cmp expect actual
'

test_expect_failure 'grep saves textconv cache in the appropriate repository' '
	reset_and_clean &&
	test_config_global diff.d2x_cached.textconv "sed -e \"s/d/x/\"" &&
	test_config_global diff.d2x_cached.cachetextconv true &&
	echo "a diff=d2x_cached" >submodule/.butattributes &&

	# We only read/write to the textconv cache when grepping from an OID,
	# as the working tree file might have modifications.
	but grep --textconv --cached --recurse-submodules x &&

	super_textconv_cache="$(but rev-parse --but-path refs/notes/textconv/d2x_cached)" &&
	sub_textconv_cache="$(but -C submodule rev-parse \
			--path-format=absolute --but-path refs/notes/textconv/d2x_cached)" &&
	test_path_is_missing "$super_textconv_cache" &&
	test_path_is_file "$sub_textconv_cache"
'

test_expect_success 'grep partially-cloned submodule' '
	# Set up clean superproject and submodule for partial cloning.
	but init super &&
	but init super/sub &&
	(
		cd super &&
		test_cummit --no-tag "Add file in superproject" \
			super-file "Some content for super-file" &&
		test_cummit -C sub --no-tag "Add file in submodule" \
			sub-file "Some content for sub-file" &&
		but submodule add ./sub &&
		but cummit -m "Add other as submodule sub" &&
		test_tick &&
		test_cummit -C sub --no-tag --append "Update file in submodule" \
			sub-file "Some more content for sub-file" &&
		but add sub &&
		but cummit -m "Update submodule" &&
		test_tick &&
		but config --local uploadpack.allowfilter 1 &&
		but config --local uploadpack.allowanysha1inwant 1 &&
		but -C sub config --local uploadpack.allowfilter 1 &&
		but -C sub config --local uploadpack.allowanysha1inwant 1
	) &&
	# Clone the superproject & submodule, then make sure we can lazy-fetch submodule objects.
	but clone --filter=blob:none --also-filter-submodules \
		--recurse-submodules "file://$(pwd)/super" partial &&
	(
		cd partial &&
		cat >expect <<-\EOF &&
		HEAD^:sub/sub-file:Some content for sub-file
		HEAD^:super-file:Some content for super-file
		EOF

		BUT_TRACE2_EVENT="$(pwd)/trace2.log" but grep -e content \
			--recurse-submodules HEAD^ >actual &&
		test_cmp expect actual &&
		# Verify that we actually fetched data from the promisor remote:
		grep \"category\":\"promisor\",\"key\":\"fetch_count\",\"value\":\"1\" trace2.log
	)
'

test_done
