#!/bin/sh

test_description='Test git config-set API in different settings'

. ./test-lib.sh

# 'check_config get_* section.key value' verifies that the entry for
# section.key is 'value'
check_config () {
	if test "$1" = expect_code
	then
		expect_code="$2" && shift && shift
	else
		expect_code=0
	fi &&
	op=$1 key=$2 && shift && shift &&
	if test $# != 0
	then
		printf "%s\n" "$@"
	fi >expect &&
	test_expect_code $expect_code test-tool config "$op" "$key" >actual &&
	test_cmp expect actual
}

test_expect_success 'setup default config' '
	cat >.git/config <<-\EOF
	[case]
		penguin = very blue
		Movie = BadPhysics
		UPPERCASE = true
		MixedCase = true
		my =
		foo
		baz = sam
	[Cores]
		WhatEver = Second
		baz = bar
	[cores]
		baz = bat
	[CORES]
		baz = ball
	[my "Foo bAr"]
		hi = mixed-case
	[my "FOO BAR"]
		hi = upper-case
	[my "foo bar"]
		hi = lower-case
	[case]
		baz = bat
		baz = hask
	[lamb]
		chop = 65
		head = none
	[goat]
		legs = 4
		head = true
		skin = false
		nose = 1
		horns
	[value]
		less
	EOF
'

test_expect_success 'get value for a simple key' '
	check_config get_value case.penguin "very blue"
'

test_expect_success 'get value for a key with value as an empty string' '
	check_config get_value case.my ""
'

test_expect_success 'get value for a key with value as NULL' '
	check_config get_value case.foo "(NULL)"
'

test_expect_success 'upper case key' '
	check_config get_value case.UPPERCASE "true" &&
	check_config get_value case.uppercase "true"
'

test_expect_success 'mixed case key' '
	check_config get_value case.MixedCase "true" &&
	check_config get_value case.MIXEDCASE "true" &&
	check_config get_value case.mixedcase "true"
'

test_expect_success 'key and value with mixed case' '
	check_config get_value case.Movie "BadPhysics"
'

test_expect_success 'key with case sensitive subsection' '
	check_config get_value "my.Foo bAr.hi" "mixed-case" &&
	check_config get_value "my.FOO BAR.hi" "upper-case" &&
	check_config get_value "my.foo bar.hi" "lower-case"
'

test_expect_success 'key with case insensitive section header' '
	check_config get_value cores.baz "ball" &&
	check_config get_value Cores.baz "ball" &&
	check_config get_value CORES.baz "ball" &&
	check_config get_value coreS.baz "ball"
'

test_expect_success 'key with case insensitive section header & variable' '
	check_config get_value CORES.BAZ "ball" &&
	check_config get_value cores.baz "ball" &&
	check_config get_value cores.BaZ "ball" &&
	check_config get_value cOreS.bAz "ball"
'

test_expect_success 'find value with misspelled key' '
	check_config expect_code 1 get_value "my.fOo Bar.hi" "Value not found for \"my.fOo Bar.hi\""
'

test_expect_success 'find value with the highest priority' '
	check_config get_value case.baz "hask"
'

test_expect_success 'return value for an existing key' '
	test-tool config get lamb.chop >out 2>err &&
	test_must_be_empty out &&
	test_must_be_empty err
'

test_expect_success 'return value for value-less key' '
	test-tool config get value.less >out 2>err &&
	test_must_be_empty out &&
	test_must_be_empty err
'

test_expect_success 'return value for a missing key' '
	cat >expect <<-\EOF &&
	Value not found for "missing.key"
	EOF
	test_expect_code 1 test-tool config get missing.key >actual 2>err &&
	test_cmp actual expect &&
	test_must_be_empty err
'

test_expect_success 'return value for a bad key: CONFIG_INVALID_KEY' '
	cat >expect <<-\EOF &&
	Key "fails.iskeychar.-" is invalid
	EOF
	test_expect_code 1 test-tool config get fails.iskeychar.- >actual 2>err &&
	test_cmp actual expect &&
	test_must_be_empty out
'

test_expect_success 'return value for a bad key: CONFIG_NO_SECTION_OR_NAME' '
	cat >expect <<-\EOF &&
	Key "keynosection" has no section
	EOF
	test_expect_code 1 test-tool config get keynosection >actual 2>err &&
	test_cmp actual expect &&
	test_must_be_empty out
'

test_expect_success 'find integer value for a key' '
	check_config get_int lamb.chop 65
'

test_expect_success 'parse integer value during iteration' '
	check_config git_config_int lamb.chop 65
'

test_expect_success 'find string value for a key' '
	check_config get_string case.baz hask &&
	check_config expect_code 1 get_string case.ba "Value not found for \"case.ba\""
'

test_expect_success 'check line error when NULL string is queried' '
	test_expect_code 128 test-tool config get_string case.foo 2>result &&
	test_grep "fatal: .*case\.foo.*\.git/config.*line 7" result
'

test_expect_success 'find integer if value is non parse-able' '
	check_config expect_code 128 get_int lamb.head
'

test_expect_success 'non parse-able integer value during iteration' '
	check_config expect_code 128 git_config_int lamb.head 2>result &&
	grep "fatal: bad numeric config value .* in file \.git/config" result
'

test_expect_success 'find bool value for the entered key' '
	check_config get_bool goat.head 1 &&
	check_config get_bool goat.skin 0 &&
	check_config get_bool goat.nose 1 &&
	check_config get_bool goat.horns 1 &&
	check_config get_bool goat.legs 1
'

test_expect_success 'find multiple values' '
	check_config get_value_multi case.baz sam bat hask
'

test_NULL_in_multi () {
	local op="$1" &&
	local file="$2" &&

	test_expect_success "$op: NULL value in config${file:+ in $file}" '
		config="$file" &&
		if test -z "$config"
		then
			config=.git/config &&
			test_when_finished "mv $config.old $config" &&
			mv "$config" "$config".old
		fi &&

		# Value-less in the middle of a list
		cat >"$config" <<-\EOF &&
		[a]key=x
		[a]key
		[a]key=y
		EOF
		case "$op" in
		*_multi)
			cat >expect <<-\EOF
			x
			(NULL)
			y
			EOF
			;;
		*)
			cat >expect <<-\EOF
			y
			EOF
			;;
		esac &&
		test-tool config "$op" a.key $file >actual &&
		test_cmp expect actual &&

		# Value-less at the end of a least
		cat >"$config" <<-\EOF &&
		[a]key=x
		[a]key=y
		[a]key
		EOF
		case "$op" in
		*_multi)
			cat >expect <<-\EOF
			x
			y
			(NULL)
			EOF
			;;
		*)
			cat >expect <<-\EOF
			(NULL)
			EOF
			;;
		esac &&
		test-tool config "$op" a.key $file >actual &&
		test_cmp expect actual
	'
}

test_NULL_in_multi "get_value_multi"
test_NULL_in_multi "configset_get_value" "my.config"
test_NULL_in_multi "configset_get_value_multi" "my.config"

test_expect_success 'find value from a configset' '
	cat >config2 <<-\EOF &&
	[case]
		baz = lama
	[my]
		new = silk
	[case]
		baz = ball
	EOF
	echo silk >expect &&
	test-tool config configset_get_value my.new config2 .git/config >actual &&
	test_cmp expect actual
'

test_expect_success 'find value with highest priority from a configset' '
	echo hask >expect &&
	test-tool config configset_get_value case.baz config2 .git/config >actual &&
	test_cmp expect actual
'

test_expect_success 'find value_list for a key from a configset' '
	cat >expect <<-\EOF &&
	lama
	ball
	sam
	bat
	hask
	EOF
	test-tool config configset_get_value_multi case.baz config2 .git/config >actual &&
	test_cmp expect actual
'

test_expect_success 'proper error on non-existent files' '
	echo "Error (-1) reading configuration file non-existent-file." >expect &&
	test_expect_code 2 test-tool config configset_get_value foo.bar non-existent-file 2>actual &&
	test_cmp expect actual
'

test_expect_success 'proper error on directory "files"' '
	echo "Error (-1) reading configuration file a-directory." >expect &&
	mkdir a-directory &&
	test_expect_code 2 test-tool config configset_get_value foo.bar a-directory 2>output &&
	grep "^warning:" output &&
	grep "^Error" output >actual &&
	test_cmp expect actual
'

test_expect_success POSIXPERM,SANITY 'proper error on non-accessible files' '
	chmod -r .git/config &&
	test_when_finished "chmod +r .git/config" &&
	echo "Error (-1) reading configuration file .git/config." >expect &&
	test_expect_code 2 test-tool config configset_get_value foo.bar .git/config 2>output &&
	grep "^warning:" output &&
	grep "^Error" output >actual &&
	test_cmp expect actual
'

test_expect_success 'proper error on error in default config files' '
	cp .git/config .git/config.old &&
	test_when_finished "mv .git/config.old .git/config" &&
	echo "[" >>.git/config &&
	echo "fatal: bad config line 36 in file .git/config" >expect &&
	test_expect_code 128 test-tool config get_value foo.bar 2>actual &&
	test_cmp expect actual
'

test_expect_success 'proper error on error in custom config files' '
	echo "[" >>syntax-error &&
	echo "fatal: bad config line 1 in file syntax-error" >expect &&
	test_expect_code 128 test-tool config configset_get_value foo.bar syntax-error 2>actual &&
	test_cmp expect actual
'

test_expect_success 'check line errors for malformed values' '
	mv .git/config .git/config.old &&
	test_when_finished "mv .git/config.old .git/config" &&
	cat >.git/config <<-\EOF &&
	[alias]
		br
	EOF
	test_expect_code 128 git br 2>result &&
	test_grep "missing value for .alias\.br" result &&
	test_grep "fatal: .*\.git/config" result &&
	test_grep "fatal: .*line 2" result
'

test_expect_success 'error on modifying repo config without repo' '
	nongit test_must_fail git config a.b c 2>err &&
	test_grep "not in a git directory" err
'

cmdline_config="'foo.bar=from-cmdline'"
test_expect_success 'iteration shows correct origins' '
	printf "[ignore]\n\tthis = please\n[foo]bar = from-repo\n" >.git/config &&
	printf "[foo]\n\tbar = from-home\n" >.gitconfig &&
	if test_have_prereq MINGW
	then
		# Use Windows path (i.e. *not* $HOME)
		HOME_GITCONFIG=$(pwd)/.gitconfig
	else
		# Do not get fooled by symbolic links, i.e. $HOME != $(pwd)
		HOME_GITCONFIG=$HOME/.gitconfig
	fi &&
	cat >expect <<-EOF &&
	key=foo.bar
	value=from-home
	origin=file
	name=$HOME_GITCONFIG
	lno=2
	scope=global

	key=ignore.this
	value=please
	origin=file
	name=.git/config
	lno=2
	scope=local

	key=foo.bar
	value=from-repo
	origin=file
	name=.git/config
	lno=3
	scope=local

	key=foo.bar
	value=from-cmdline
	origin=command line
	name=
	lno=-1
	scope=command
	EOF
	GIT_CONFIG_PARAMETERS=$cmdline_config test-tool config iterate >actual &&
	test_cmp expect actual
'

test_done
