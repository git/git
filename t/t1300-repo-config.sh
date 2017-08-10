#!/bin/sh
#
# Copyright (c) 2005 Johannes Schindelin
#

test_description='Test git config in different settings'

. ./test-lib.sh

test_expect_success 'clear default config' '
	rm -f .git/config
'

cat > expect << EOF
[core]
	penguin = little blue
EOF
test_expect_success 'initial' '
	git config core.penguin "little blue" &&
	test_cmp expect .git/config
'

cat > expect << EOF
[core]
	penguin = little blue
	Movie = BadPhysics
EOF
test_expect_success 'mixed case' '
	git config Core.Movie BadPhysics &&
	test_cmp expect .git/config
'

cat > expect << EOF
[core]
	penguin = little blue
	Movie = BadPhysics
[Cores]
	WhatEver = Second
EOF
test_expect_success 'similar section' '
	git config Cores.WhatEver Second &&
	test_cmp expect .git/config
'

cat > expect << EOF
[core]
	penguin = little blue
	Movie = BadPhysics
	UPPERCASE = true
[Cores]
	WhatEver = Second
EOF
test_expect_success 'uppercase section' '
	git config CORE.UPPERCASE true &&
	test_cmp expect .git/config
'

test_expect_success 'replace with non-match' '
	git config core.penguin kingpin !blue
'

test_expect_success 'replace with non-match (actually matching)' '
	git config core.penguin "very blue" !kingpin
'

cat > expect << EOF
[core]
	penguin = very blue
	Movie = BadPhysics
	UPPERCASE = true
	penguin = kingpin
[Cores]
	WhatEver = Second
EOF

test_expect_success 'non-match result' 'test_cmp expect .git/config'

test_expect_success 'find mixed-case key by canonical name' '
	echo Second >expect &&
	git config cores.whatever >actual &&
	test_cmp expect actual
'

test_expect_success 'find mixed-case key by non-canonical name' '
	echo Second >expect &&
	git config CoReS.WhAtEvEr >actual &&
	test_cmp expect actual
'

test_expect_success 'subsections are not canonicalized by git-config' '
	cat >>.git/config <<-\EOF &&
	[section.SubSection]
	key = one
	[section "SubSection"]
	key = two
	EOF
	echo one >expect &&
	git config section.subsection.key >actual &&
	test_cmp expect actual &&
	echo two >expect &&
	git config section.SubSection.key >actual &&
	test_cmp expect actual
'

cat > .git/config <<\EOF
[alpha]
bar = foo
[beta]
baz = multiple \
lines
EOF

test_expect_success 'unset with cont. lines' '
	git config --unset beta.baz
'

cat > expect <<\EOF
[alpha]
bar = foo
[beta]
EOF

test_expect_success 'unset with cont. lines is correct' 'test_cmp expect .git/config'

cat > .git/config << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
		haha   ="beta" # last silly comment
haha = hello
	haha = bello
[nextSection] noNewline = ouch
EOF

cp .git/config .git/config2

test_expect_success 'multiple unset' '
	git config --unset-all beta.haha
'

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
[nextSection] noNewline = ouch
EOF

test_expect_success 'multiple unset is correct' '
	test_cmp expect .git/config
'

cp .git/config2 .git/config

test_expect_success '--replace-all missing value' '
	test_must_fail git config --replace-all beta.haha &&
	test_cmp .git/config2 .git/config
'

rm .git/config2

test_expect_success '--replace-all' '
	git config --replace-all beta.haha gamma
'

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
	haha = gamma
[nextSection] noNewline = ouch
EOF

test_expect_success 'all replaced' '
	test_cmp expect .git/config
'

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
	haha = alpha
[nextSection] noNewline = ouch
EOF
test_expect_success 'really mean test' '
	git config beta.haha alpha &&
	test_cmp expect .git/config
'

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
	haha = alpha
[nextSection]
	nonewline = wow
EOF
test_expect_success 'really really mean test' '
	git config nextsection.nonewline wow &&
	test_cmp expect .git/config
'

test_expect_success 'get value' '
	echo alpha >expect &&
	git config beta.haha >actual &&
	test_cmp expect actual
'

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
[nextSection]
	nonewline = wow
EOF
test_expect_success 'unset' '
	git config --unset beta.haha &&
	test_cmp expect .git/config
'

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
[nextSection]
	nonewline = wow
	NoNewLine = wow2 for me
EOF
test_expect_success 'multivar' '
	git config nextsection.NoNewLine "wow2 for me" "for me$" &&
	test_cmp expect .git/config
'

test_expect_success 'non-match' '
	git config --get nextsection.nonewline !for
'

test_expect_success 'non-match value' '
	echo wow >expect &&
	git config --get nextsection.nonewline !for >actual &&
	test_cmp expect actual
'

test_expect_success 'multi-valued get returns final one' '
	echo "wow2 for me" >expect &&
	git config --get nextsection.nonewline >actual &&
	test_cmp expect actual
'

test_expect_success 'multi-valued get-all returns all' '
	cat >expect <<-\EOF &&
	wow
	wow2 for me
	EOF
	git config --get-all nextsection.nonewline >actual &&
	test_cmp expect actual
'

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
[nextSection]
	nonewline = wow3
	NoNewLine = wow2 for me
EOF
test_expect_success 'multivar replace' '
	git config nextsection.nonewline "wow3" "wow$" &&
	test_cmp expect .git/config
'

test_expect_success 'ambiguous unset' '
	test_must_fail git config --unset nextsection.nonewline
'

test_expect_success 'invalid unset' '
	test_must_fail git config --unset somesection.nonewline
'

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
[nextSection]
	NoNewLine = wow2 for me
EOF

test_expect_success 'multivar unset' '
	git config --unset nextsection.nonewline "wow3$" &&
	test_cmp expect .git/config
'

test_expect_success 'invalid key' 'test_must_fail git config inval.2key blabla'

test_expect_success 'correct key' 'git config 123456.a123 987'

test_expect_success 'hierarchical section' '
	git config Version.1.2.3eX.Alpha beta
'

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
[nextSection]
	NoNewLine = wow2 for me
[123456]
	a123 = 987
[Version "1.2.3eX"]
	Alpha = beta
EOF

test_expect_success 'hierarchical section value' '
	test_cmp expect .git/config
'

cat > expect << EOF
beta.noindent=sillyValue
nextsection.nonewline=wow2 for me
123456.a123=987
version.1.2.3eX.alpha=beta
EOF

test_expect_success 'working --list' '
	git config --list > output &&
	test_cmp expect output
'
cat > expect << EOF
EOF

test_expect_success '--list without repo produces empty output' '
	git --git-dir=nonexistent config --list >output &&
	test_cmp expect output
'

cat > expect << EOF
beta.noindent
nextsection.nonewline
123456.a123
version.1.2.3eX.alpha
EOF

test_expect_success '--name-only --list' '
	git config --name-only --list >output &&
	test_cmp expect output
'

cat > expect << EOF
beta.noindent sillyValue
nextsection.nonewline wow2 for me
EOF

test_expect_success '--get-regexp' '
	git config --get-regexp in >output &&
	test_cmp expect output
'

cat > expect << EOF
beta.noindent
nextsection.nonewline
EOF

test_expect_success '--name-only --get-regexp' '
	git config --name-only --get-regexp in >output &&
	test_cmp expect output
'

cat > expect << EOF
wow2 for me
wow4 for you
EOF

test_expect_success '--add' '
	git config --add nextsection.nonewline "wow4 for you" &&
	git config --get-all nextsection.nonewline > output &&
	test_cmp expect output
'

cat > .git/config << EOF
[novalue]
	variable
[emptyvalue]
	variable =
EOF

test_expect_success 'get variable with no value' '
	git config --get novalue.variable ^$
'

test_expect_success 'get variable with empty value' '
	git config --get emptyvalue.variable ^$
'

echo novalue.variable > expect

test_expect_success 'get-regexp variable with no value' '
	git config --get-regexp novalue > output &&
	test_cmp expect output
'

echo 'novalue.variable true' > expect

test_expect_success 'get-regexp --bool variable with no value' '
	git config --bool --get-regexp novalue > output &&
	test_cmp expect output
'

echo 'emptyvalue.variable ' > expect

test_expect_success 'get-regexp variable with empty value' '
	git config --get-regexp emptyvalue > output &&
	test_cmp expect output
'

echo true > expect

test_expect_success 'get bool variable with no value' '
	git config --bool novalue.variable > output &&
	test_cmp expect output
'

echo false > expect

test_expect_success 'get bool variable with empty value' '
	git config --bool emptyvalue.variable > output &&
	test_cmp expect output
'

test_expect_success 'no arguments, but no crash' '
	test_must_fail git config >output 2>&1 &&
	test_i18ngrep usage output
'

cat > .git/config << EOF
[a.b]
	c = d
EOF

cat > expect << EOF
[a.b]
	c = d
[a]
	x = y
EOF

test_expect_success 'new section is partial match of another' '
	git config a.x y &&
	test_cmp expect .git/config
'

cat > expect << EOF
[a.b]
	c = d
[a]
	x = y
	b = c
[b]
	x = y
EOF

test_expect_success 'new variable inserts into proper section' '
	git config b.x y &&
	git config a.b c &&
	test_cmp expect .git/config
'

test_expect_success 'alternative --file (non-existing file should fail)' '
	test_must_fail git config --file non-existing-config -l
'

cat > other-config << EOF
[ein]
	bahn = strasse
EOF

cat > expect << EOF
ein.bahn=strasse
EOF

test_expect_success 'alternative GIT_CONFIG' '
	GIT_CONFIG=other-config git config --list >output &&
	test_cmp expect output
'

test_expect_success 'alternative GIT_CONFIG (--file)' '
	git config --file other-config --list >output &&
	test_cmp expect output
'

test_expect_success 'alternative GIT_CONFIG (--file=-)' '
	git config --file - --list <other-config >output &&
	test_cmp expect output
'

test_expect_success 'setting a value in stdin is an error' '
	test_must_fail git config --file - some.value foo
'

test_expect_success 'editing stdin is an error' '
	test_must_fail git config --file - --edit
'

test_expect_success 'refer config from subdirectory' '
	mkdir x &&
	(
		cd x &&
		echo strasse >expect &&
		git config --get --file ../other-config ein.bahn >actual &&
		test_cmp expect actual
	)

'

test_expect_success 'refer config from subdirectory via --file' '
	(
		cd x &&
		git config --file=../other-config --get ein.bahn >actual &&
		test_cmp expect actual
	)
'

cat > expect << EOF
[ein]
	bahn = strasse
[anwohner]
	park = ausweis
EOF

test_expect_success '--set in alternative file' '
	git config --file=other-config anwohner.park ausweis &&
	test_cmp expect other-config
'

cat > .git/config << EOF
# Hallo
	#Bello
[branch "eins"]
	x = 1
[branch.eins]
	y = 1
	[branch "1 234 blabl/a"]
weird
EOF

test_expect_success 'rename section' '
	git config --rename-section branch.eins branch.zwei
'

cat > expect << EOF
# Hallo
	#Bello
[branch "zwei"]
	x = 1
[branch "zwei"]
	y = 1
	[branch "1 234 blabl/a"]
weird
EOF

test_expect_success 'rename succeeded' '
	test_cmp expect .git/config
'

test_expect_success 'rename non-existing section' '
	test_must_fail git config --rename-section \
		branch."world domination" branch.drei
'

test_expect_success 'rename succeeded' '
	test_cmp expect .git/config
'

test_expect_success 'rename another section' '
	git config --rename-section branch."1 234 blabl/a" branch.drei
'

cat > expect << EOF
# Hallo
	#Bello
[branch "zwei"]
	x = 1
[branch "zwei"]
	y = 1
[branch "drei"]
weird
EOF

test_expect_success 'rename succeeded' '
	test_cmp expect .git/config
'

cat >> .git/config << EOF
[branch "vier"] z = 1
EOF

test_expect_success 'rename a section with a var on the same line' '
	git config --rename-section branch.vier branch.zwei
'

cat > expect << EOF
# Hallo
	#Bello
[branch "zwei"]
	x = 1
[branch "zwei"]
	y = 1
[branch "drei"]
weird
[branch "zwei"]
	z = 1
EOF

test_expect_success 'rename succeeded' '
	test_cmp expect .git/config
'

test_expect_success 'renaming empty section name is rejected' '
	test_must_fail git config --rename-section branch.zwei ""
'

test_expect_success 'renaming to bogus section is rejected' '
	test_must_fail git config --rename-section branch.zwei "bogus name"
'

cat >> .git/config << EOF
  [branch "zwei"] a = 1 [branch "vier"]
EOF

test_expect_success 'remove section' '
	git config --remove-section branch.zwei
'

cat > expect << EOF
# Hallo
	#Bello
[branch "drei"]
weird
EOF

test_expect_success 'section was removed properly' '
	test_cmp expect .git/config
'

cat > expect << EOF
[gitcvs]
	enabled = true
	dbname = %Ggitcvs2.%a.%m.sqlite
[gitcvs "ext"]
	dbname = %Ggitcvs1.%a.%m.sqlite
EOF

test_expect_success 'section ending' '
	rm -f .git/config &&
	git config gitcvs.enabled true &&
	git config gitcvs.ext.dbname %Ggitcvs1.%a.%m.sqlite &&
	git config gitcvs.dbname %Ggitcvs2.%a.%m.sqlite &&
	test_cmp expect .git/config

'

test_expect_success numbers '
	git config kilo.gram 1k &&
	git config mega.ton 1m &&
	echo 1024 >expect &&
	echo 1048576 >>expect &&
	git config --int --get kilo.gram >actual &&
	git config --int --get mega.ton >>actual &&
	test_cmp expect actual
'

test_expect_success '--int is at least 64 bits' '
	git config giga.watts 121g &&
	echo 129922760704 >expect &&
	git config --int --get giga.watts >actual &&
	test_cmp expect actual
'

test_expect_success 'invalid unit' '
	git config aninvalid.unit "1auto" &&
	echo 1auto >expect &&
	git config aninvalid.unit >actual &&
	test_cmp expect actual &&
	test_must_fail git config --int --get aninvalid.unit 2>actual &&
	test_i18ngrep "bad numeric config value .1auto. for .aninvalid.unit. in file .git/config: invalid unit" actual
'

test_expect_success 'line number is reported correctly' '
	printf "[bool]\n\tvar\n" >invalid &&
	test_must_fail git config -f invalid --path bool.var 2>actual &&
	test_i18ngrep "line 2" actual
'

test_expect_success 'invalid stdin config' '
	echo "[broken" | test_must_fail git config --list --file - >output 2>&1 &&
	test_i18ngrep "bad config line 1 in standard input" output
'

cat > expect << EOF
true
false
true
false
true
false
true
false
EOF

test_expect_success bool '

	git config bool.true1 01 &&
	git config bool.true2 -1 &&
	git config bool.true3 YeS &&
	git config bool.true4 true &&
	git config bool.false1 000 &&
	git config bool.false2 "" &&
	git config bool.false3 nO &&
	git config bool.false4 FALSE &&
	rm -f result &&
	for i in 1 2 3 4
	do
	    git config --bool --get bool.true$i >>result
	    git config --bool --get bool.false$i >>result
        done &&
	test_cmp expect result'

test_expect_success 'invalid bool (--get)' '

	git config bool.nobool foobar &&
	test_must_fail git config --bool --get bool.nobool'

test_expect_success 'invalid bool (set)' '

	test_must_fail git config --bool bool.nobool foobar'

cat > expect <<\EOF
[bool]
	true1 = true
	true2 = true
	true3 = true
	true4 = true
	false1 = false
	false2 = false
	false3 = false
	false4 = false
EOF

test_expect_success 'set --bool' '

	rm -f .git/config &&
	git config --bool bool.true1 01 &&
	git config --bool bool.true2 -1 &&
	git config --bool bool.true3 YeS &&
	git config --bool bool.true4 true &&
	git config --bool bool.false1 000 &&
	git config --bool bool.false2 "" &&
	git config --bool bool.false3 nO &&
	git config --bool bool.false4 FALSE &&
	test_cmp expect .git/config'

cat > expect <<\EOF
[int]
	val1 = 1
	val2 = -1
	val3 = 5242880
EOF

test_expect_success 'set --int' '

	rm -f .git/config &&
	git config --int int.val1 01 &&
	git config --int int.val2 -1 &&
	git config --int int.val3 5m &&
	test_cmp expect .git/config
'

test_expect_success 'get --bool-or-int' '
	cat >.git/config <<-\EOF &&
	[bool]
	true1
	true2 = true
	false = false
	[int]
	int1 = 0
	int2 = 1
	int3 = -1
	EOF
	cat >expect <<-\EOF &&
	true
	true
	false
	0
	1
	-1
	EOF
	{
		git config --bool-or-int bool.true1 &&
		git config --bool-or-int bool.true2 &&
		git config --bool-or-int bool.false &&
		git config --bool-or-int int.int1 &&
		git config --bool-or-int int.int2 &&
		git config --bool-or-int int.int3
	} >actual &&
	test_cmp expect actual
'

cat >expect <<\EOF
[bool]
	true1 = true
	false1 = false
	true2 = true
	false2 = false
[int]
	int1 = 0
	int2 = 1
	int3 = -1
EOF

test_expect_success 'set --bool-or-int' '
	rm -f .git/config &&
	git config --bool-or-int bool.true1 true &&
	git config --bool-or-int bool.false1 false &&
	git config --bool-or-int bool.true2 yes &&
	git config --bool-or-int bool.false2 no &&
	git config --bool-or-int int.int1 0 &&
	git config --bool-or-int int.int2 1 &&
	git config --bool-or-int int.int3 -1 &&
	test_cmp expect .git/config
'

cat >expect <<\EOF
[path]
	home = ~/
	normal = /dev/null
	trailingtilde = foo~
EOF

test_expect_success !MINGW 'set --path' '
	rm -f .git/config &&
	git config --path path.home "~/" &&
	git config --path path.normal "/dev/null" &&
	git config --path path.trailingtilde "foo~" &&
	test_cmp expect .git/config'

if test_have_prereq !MINGW && test "${HOME+set}"
then
	test_set_prereq HOMEVAR
fi

cat >expect <<EOF
$HOME/
/dev/null
foo~
EOF

test_expect_success HOMEVAR 'get --path' '
	git config --get --path path.home > result &&
	git config --get --path path.normal >> result &&
	git config --get --path path.trailingtilde >> result &&
	test_cmp expect result
'

cat >expect <<\EOF
/dev/null
foo~
EOF

test_expect_success !MINGW 'get --path copes with unset $HOME' '
	(
		unset HOME;
		test_must_fail git config --get --path path.home \
			>result 2>msg &&
		git config --get --path path.normal >>result &&
		git config --get --path path.trailingtilde >>result
	) &&
	test_i18ngrep "[Ff]ailed to expand.*~/" msg &&
	test_cmp expect result
'

test_expect_success 'get --path barfs on boolean variable' '
	echo "[path]bool" >.git/config &&
	test_must_fail git config --get --path path.bool
'

cat > expect << EOF
[quote]
	leading = " test"
	ending = "test "
	semicolon = "test;test"
	hash = "test#test"
EOF
test_expect_success 'quoting' '
	rm -f .git/config &&
	git config quote.leading " test" &&
	git config quote.ending "test " &&
	git config quote.semicolon "test;test" &&
	git config quote.hash "test#test" &&
	test_cmp expect .git/config
'

test_expect_success 'key with newline' '
	test_must_fail git config "key.with
newline" 123'

test_expect_success 'value with newline' 'git config key.sub value.with\\\
newline'

cat > .git/config <<\EOF
[section]
	; comment \
	continued = cont\
inued
	noncont   = not continued ; \
	quotecont = "cont;\
inued"
EOF

cat > expect <<\EOF
section.continued=continued
section.noncont=not continued
section.quotecont=cont;inued
EOF

test_expect_success 'value continued on next line' '
	git config --list > result &&
	test_cmp result expect
'

cat > .git/config <<\EOF
[section "sub=section"]
	val1 = foo=bar
	val2 = foo\nbar
	val3 = \n\n
	val4 =
	val5
EOF

cat > expect <<\EOF
section.sub=section.val1
foo=barQsection.sub=section.val2
foo
barQsection.sub=section.val3


Qsection.sub=section.val4
Qsection.sub=section.val5Q
EOF
test_expect_success '--null --list' '
	git config --null --list >result.raw &&
	nul_to_q <result.raw >result &&
	echo >>result &&
	test_cmp expect result
'

test_expect_success '--null --get-regexp' '
	git config --null --get-regexp "val[0-9]" >result.raw &&
	nul_to_q <result.raw >result &&
	echo >>result &&
	test_cmp expect result
'

test_expect_success 'inner whitespace kept verbatim' '
	git config section.val "foo 	  bar" &&
	echo "foo 	  bar" >expect &&
	git config section.val >actual &&
	test_cmp expect actual
'

test_expect_success SYMLINKS 'symlinked configuration' '
	ln -s notyet myconfig &&
	git config --file=myconfig test.frotz nitfol &&
	test -h myconfig &&
	test -f notyet &&
	test "z$(git config --file=notyet test.frotz)" = znitfol &&
	git config --file=myconfig test.xyzzy rezrov &&
	test -h myconfig &&
	test -f notyet &&
	cat >expect <<-\EOF &&
	nitfol
	rezrov
	EOF
	{
		git config --file=notyet test.frotz &&
		git config --file=notyet test.xyzzy
	} >actual &&
	test_cmp expect actual
'

test_expect_success 'nonexistent configuration' '
	test_must_fail git config --file=doesnotexist --list &&
	test_must_fail git config --file=doesnotexist test.xyzzy
'

test_expect_success SYMLINKS 'symlink to nonexistent configuration' '
	ln -s doesnotexist linktonada &&
	ln -s linktonada linktolinktonada &&
	test_must_fail git config --file=linktonada --list &&
	test_must_fail git config --file=linktolinktonada --list
'

test_expect_success 'check split_cmdline return' "
	git config alias.split-cmdline-fix 'echo \"' &&
	test_must_fail git split-cmdline-fix &&
	echo foo > foo &&
	git add foo &&
	git commit -m 'initial commit' &&
	git config branch.master.mergeoptions 'echo \"' &&
	test_must_fail git merge master
"

test_expect_success 'git -c "key=value" support' '
	cat >expect <<-\EOF &&
	value
	value
	true
	EOF
	{
		git -c core.name=value config core.name &&
		git -c foo.CamelCase=value config foo.camelcase &&
		git -c foo.flag config --bool foo.flag
	} >actual &&
	test_cmp expect actual &&
	test_must_fail git -c name=value config core.name
'

# We just need a type-specifier here that cares about the
# distinction internally between a NULL boolean and a real
# string (because most of git's internal parsers do care).
# Using "--path" works, but we do not otherwise care about
# its semantics.
test_expect_success 'git -c can represent empty string' '
	echo >expect &&
	git -c foo.empty= config --path foo.empty >actual &&
	test_cmp expect actual
'

test_expect_success 'key sanity-checking' '
	test_must_fail git config foo=bar &&
	test_must_fail git config foo=.bar &&
	test_must_fail git config foo.ba=r &&
	test_must_fail git config foo.1bar &&
	test_must_fail git config foo."ba
				z".bar &&
	test_must_fail git config . false &&
	test_must_fail git config .foo false &&
	test_must_fail git config foo. false &&
	test_must_fail git config .foo. false &&
	git config foo.bar true &&
	git config foo."ba =z".bar false
'

test_expect_success 'git -c works with aliases of builtins' '
	git config alias.checkconfig "-c foo.check=bar config foo.check" &&
	echo bar >expect &&
	git checkconfig >actual &&
	test_cmp expect actual
'

test_expect_success 'aliases can be CamelCased' '
	test_config alias.CamelCased "rev-parse HEAD" &&
	git CamelCased >out &&
	git rev-parse HEAD >expect &&
	test_cmp expect out
'

test_expect_success 'git -c does not split values on equals' '
	echo "value with = in it" >expect &&
	git -c core.foo="value with = in it" config core.foo >actual &&
	test_cmp expect actual
'

test_expect_success 'git -c dies on bogus config' '
	test_must_fail git -c core.bare=foo rev-parse
'

test_expect_success 'git -c complains about empty key' '
	test_must_fail git -c "=foo" rev-parse
'

test_expect_success 'git -c complains about empty key and value' '
	test_must_fail git -c "" rev-parse
'

test_expect_success 'multiple git -c appends config' '
	test_config alias.x "!git -c x.two=2 config --get-regexp ^x\.*" &&
	cat >expect <<-\EOF &&
	x.one 1
	x.two 2
	EOF
	git -c x.one=1 x >actual &&
	test_cmp expect actual
'

test_expect_success 'last one wins: two level vars' '

	# sec.var and sec.VAR are the same variable, as the first
	# and the last level of a configuration variable name is
	# case insensitive.

	echo VAL >expect &&

	git -c sec.var=val -c sec.VAR=VAL config --get sec.var >actual &&
	test_cmp expect actual &&
	git -c SEC.var=val -c sec.var=VAL config --get sec.var >actual &&
	test_cmp expect actual &&

	git -c sec.var=val -c sec.VAR=VAL config --get SEC.var >actual &&
	test_cmp expect actual &&
	git -c SEC.var=val -c sec.var=VAL config --get sec.VAR >actual &&
	test_cmp expect actual
'

test_expect_success 'last one wins: three level vars' '

	# v.a.r and v.A.r are not the same variable, as the middle
	# level of a three-level configuration variable name is
	# case sensitive.

	echo val >expect &&
	git -c v.a.r=val -c v.A.r=VAL config --get v.a.r >actual &&
	test_cmp expect actual &&
	git -c v.a.r=val -c v.A.r=VAL config --get V.a.R >actual &&
	test_cmp expect actual &&

	# v.a.r and V.a.R are the same variable, as the first
	# and the last level of a configuration variable name is
	# case insensitive.

	echo VAL >expect &&
	git -c v.a.r=val -c v.a.R=VAL config --get v.a.r >actual &&
	test_cmp expect actual &&
	git -c v.a.r=val -c V.a.r=VAL config --get v.a.r >actual &&
	test_cmp expect actual &&
	git -c v.a.r=val -c v.a.R=VAL config --get V.a.R >actual &&
	test_cmp expect actual &&
	git -c v.a.r=val -c V.a.r=VAL config --get V.a.R >actual &&
	test_cmp expect actual
'

for VAR in a .a a. a.0b a."b c". a."b c".0d
do
	test_expect_success "git -c $VAR=VAL rejects invalid '$VAR'" '
		test_must_fail git -c "$VAR=VAL" config -l
	'
done

for VAR in a.b a."b c".d
do
	test_expect_success "git -c $VAR=VAL works with valid '$VAR'" '
		echo VAL >expect &&
		git -c "$VAR=VAL" config --get "$VAR" >actual &&
		test_cmp expect actual
	'
done

test_expect_success 'git -c is not confused by empty environment' '
	GIT_CONFIG_PARAMETERS="" git -c x.one=1 config --list
'

test_expect_success 'git config --edit works' '
	git config -f tmp test.value no &&
	echo test.value=yes >expect &&
	GIT_EDITOR="echo [test]value=yes >" git config -f tmp --edit &&
	git config -f tmp --list >actual &&
	test_cmp expect actual
'

test_expect_success 'git config --edit respects core.editor' '
	git config -f tmp test.value no &&
	echo test.value=yes >expect &&
	test_config core.editor "echo [test]value=yes >" &&
	git config -f tmp --edit &&
	git config -f tmp --list >actual &&
	test_cmp expect actual
'

# malformed configuration files
test_expect_success 'barf on syntax error' '
	cat >.git/config <<-\EOF &&
	# broken section line
	[section]
	key garbage
	EOF
	test_must_fail git config --get section.key >actual 2>error &&
	test_i18ngrep " line 3 " error
'

test_expect_success 'barf on incomplete section header' '
	cat >.git/config <<-\EOF &&
	# broken section line
	[section
	key = value
	EOF
	test_must_fail git config --get section.key >actual 2>error &&
	test_i18ngrep " line 2 " error
'

test_expect_success 'barf on incomplete string' '
	cat >.git/config <<-\EOF &&
	# broken section line
	[section]
	key = "value string
	EOF
	test_must_fail git config --get section.key >actual 2>error &&
	test_i18ngrep " line 3 " error
'

test_expect_success 'urlmatch' '
	cat >.git/config <<-\EOF &&
	[http]
		sslVerify
	[http "https://weak.example.com"]
		sslVerify = false
		cookieFile = /tmp/cookie.txt
	EOF

	test_expect_code 1 git config --bool --get-urlmatch doesnt.exist https://good.example.com >actual &&
	test_must_be_empty actual &&

	echo true >expect &&
	git config --bool --get-urlmatch http.SSLverify https://good.example.com >actual &&
	test_cmp expect actual &&

	echo false >expect &&
	git config --bool --get-urlmatch http.sslverify https://weak.example.com >actual &&
	test_cmp expect actual &&

	{
		echo http.cookiefile /tmp/cookie.txt &&
		echo http.sslverify false
	} >expect &&
	git config --get-urlmatch HTTP https://weak.example.com >actual &&
	test_cmp expect actual
'

test_expect_success 'urlmatch favors more specific URLs' '
	cat >.git/config <<-\EOF &&
	[http "https://example.com/"]
		cookieFile = /tmp/root.txt
	[http "https://example.com/subdirectory"]
		cookieFile = /tmp/subdirectory.txt
	[http "https://user@example.com/"]
		cookieFile = /tmp/user.txt
	[http "https://averylonguser@example.com/"]
		cookieFile = /tmp/averylonguser.txt
	[http "https://preceding.example.com"]
		cookieFile = /tmp/preceding.txt
	[http "https://*.example.com"]
		cookieFile = /tmp/wildcard.txt
	[http "https://*.example.com/wildcardwithsubdomain"]
		cookieFile = /tmp/wildcardwithsubdomain.txt
	[http "https://trailing.example.com"]
		cookieFile = /tmp/trailing.txt
	[http "https://user@*.example.com/"]
		cookieFile = /tmp/wildcardwithuser.txt
	[http "https://sub.example.com/"]
		cookieFile = /tmp/sub.txt
	EOF

	echo http.cookiefile /tmp/root.txt >expect &&
	git config --get-urlmatch HTTP https://example.com >actual &&
	test_cmp expect actual &&

	echo http.cookiefile /tmp/subdirectory.txt >expect &&
	git config --get-urlmatch HTTP https://example.com/subdirectory >actual &&
	test_cmp expect actual &&

	echo http.cookiefile /tmp/subdirectory.txt >expect &&
	git config --get-urlmatch HTTP https://example.com/subdirectory/nested >actual &&
	test_cmp expect actual &&

	echo http.cookiefile /tmp/user.txt >expect &&
	git config --get-urlmatch HTTP https://user@example.com/ >actual &&
	test_cmp expect actual &&

	echo http.cookiefile /tmp/subdirectory.txt >expect &&
	git config --get-urlmatch HTTP https://averylonguser@example.com/subdirectory >actual &&
	test_cmp expect actual &&

	echo http.cookiefile /tmp/preceding.txt >expect &&
	git config --get-urlmatch HTTP https://preceding.example.com >actual &&
	test_cmp expect actual &&

	echo http.cookiefile /tmp/wildcard.txt >expect &&
	git config --get-urlmatch HTTP https://wildcard.example.com >actual &&
	test_cmp expect actual &&

	echo http.cookiefile /tmp/sub.txt >expect &&
	git config --get-urlmatch HTTP https://sub.example.com/wildcardwithsubdomain >actual &&
	test_cmp expect actual &&

	echo http.cookiefile /tmp/trailing.txt >expect &&
	git config --get-urlmatch HTTP https://trailing.example.com >actual &&
	test_cmp expect actual &&

	echo http.cookiefile /tmp/sub.txt >expect &&
	git config --get-urlmatch HTTP https://user@sub.example.com >actual &&
	test_cmp expect actual
'

test_expect_success 'urlmatch with wildcard' '
	cat >.git/config <<-\EOF &&
	[http]
		sslVerify
	[http "https://*.example.com"]
		sslVerify = false
		cookieFile = /tmp/cookie.txt
	EOF

	test_expect_code 1 git config --bool --get-urlmatch doesnt.exist https://good.example.com >actual &&
	test_must_be_empty actual &&

	echo true >expect &&
	git config --bool --get-urlmatch http.SSLverify https://example.com >actual &&
	test_cmp expect actual &&

	echo true >expect &&
	git config --bool --get-urlmatch http.SSLverify https://good-example.com >actual &&
	test_cmp expect actual &&

	echo true >expect &&
	git config --bool --get-urlmatch http.sslverify https://deep.nested.example.com >actual &&
	test_cmp expect actual &&

	echo false >expect &&
	git config --bool --get-urlmatch http.sslverify https://good.example.com >actual &&
	test_cmp expect actual &&

	{
		echo http.cookiefile /tmp/cookie.txt &&
		echo http.sslverify false
	} >expect &&
	git config --get-urlmatch HTTP https://good.example.com >actual &&
	test_cmp expect actual &&

	echo http.sslverify >expect &&
	git config --get-urlmatch HTTP https://more.example.com.au >actual &&
	test_cmp expect actual
'

# good section hygiene
test_expect_failure 'unsetting the last key in a section removes header' '
	cat >.git/config <<-\EOF &&
	# some generic comment on the configuration file itself
	# a comment specific to this "section" section.
	[section]
	# some intervening lines
	# that should also be dropped

	key = value
	# please be careful when you update the above variable
	EOF

	cat >expect <<-\EOF &&
	# some generic comment on the configuration file itself
	EOF

	git config --unset section.key &&
	test_cmp expect .git/config
'

test_expect_failure 'adding a key into an empty section reuses header' '
	cat >.git/config <<-\EOF &&
	[section]
	EOF

	q_to_tab >expect <<-\EOF &&
	[section]
	Qkey = value
	EOF

	git config section.key value &&
	test_cmp expect .git/config
'

test_expect_success POSIXPERM,PERL 'preserves existing permissions' '
	chmod 0600 .git/config &&
	git config imap.pass Hunter2 &&
	perl -e \
	  "die q(badset) if ((stat(q(.git/config)))[2] & 07777) != 0600" &&
	git config --rename-section imap pop &&
	perl -e \
	  "die q(badrename) if ((stat(q(.git/config)))[2] & 07777) != 0600"
'

! test_have_prereq MINGW ||
HOME="$(pwd)" # convert to Windows path

test_expect_success 'set up --show-origin tests' '
	INCLUDE_DIR="$HOME/include" &&
	mkdir -p "$INCLUDE_DIR" &&
	cat >"$INCLUDE_DIR"/absolute.include <<-\EOF &&
		[user]
			absolute = include
	EOF
	cat >"$INCLUDE_DIR"/relative.include <<-\EOF &&
		[user]
			relative = include
	EOF
	cat >"$HOME"/.gitconfig <<-EOF &&
		[user]
			global = true
			override = global
		[include]
			path = "$INCLUDE_DIR/absolute.include"
	EOF
	cat >.git/config <<-\EOF
		[user]
			local = true
			override = local
		[include]
			path = ../include/relative.include
	EOF
'

test_expect_success '--show-origin with --list' '
	cat >expect <<-EOF &&
		file:$HOME/.gitconfig	user.global=true
		file:$HOME/.gitconfig	user.override=global
		file:$HOME/.gitconfig	include.path=$INCLUDE_DIR/absolute.include
		file:$INCLUDE_DIR/absolute.include	user.absolute=include
		file:.git/config	user.local=true
		file:.git/config	user.override=local
		file:.git/config	include.path=../include/relative.include
		file:.git/../include/relative.include	user.relative=include
		command line:	user.cmdline=true
	EOF
	git -c user.cmdline=true config --list --show-origin >output &&
	test_cmp expect output
'

test_expect_success '--show-origin with --list --null' '
	cat >expect <<-EOF &&
		file:$HOME/.gitconfigQuser.global
		trueQfile:$HOME/.gitconfigQuser.override
		globalQfile:$HOME/.gitconfigQinclude.path
		$INCLUDE_DIR/absolute.includeQfile:$INCLUDE_DIR/absolute.includeQuser.absolute
		includeQfile:.git/configQuser.local
		trueQfile:.git/configQuser.override
		localQfile:.git/configQinclude.path
		../include/relative.includeQfile:.git/../include/relative.includeQuser.relative
		includeQcommand line:Quser.cmdline
		trueQ
	EOF
	git -c user.cmdline=true config --null --list --show-origin >output.raw &&
	nul_to_q <output.raw >output &&
	# The here-doc above adds a newline that the --null output would not
	# include. Add it here to make the two comparable.
	echo >>output &&
	test_cmp expect output
'

test_expect_success '--show-origin with single file' '
	cat >expect <<-\EOF &&
		file:.git/config	user.local=true
		file:.git/config	user.override=local
		file:.git/config	include.path=../include/relative.include
	EOF
	git config --local --list --show-origin >output &&
	test_cmp expect output
'

test_expect_success '--show-origin with --get-regexp' '
	cat >expect <<-EOF &&
		file:$HOME/.gitconfig	user.global true
		file:.git/config	user.local true
	EOF
	git config --show-origin --get-regexp "user\.[g|l].*" >output &&
	test_cmp expect output
'

test_expect_success '--show-origin getting a single key' '
	cat >expect <<-\EOF &&
		file:.git/config	local
	EOF
	git config --show-origin user.override >output &&
	test_cmp expect output
'

test_expect_success !MINGW 'set up custom config file' '
	CUSTOM_CONFIG_FILE="file\" (dq) and spaces.conf" &&
	cat >"$CUSTOM_CONFIG_FILE" <<-\EOF
		[user]
			custom = true
	EOF
'

test_expect_success !MINGW '--show-origin escape special file name characters' '
	cat >expect <<-\EOF &&
		file:"file\" (dq) and spaces.conf"	user.custom=true
	EOF
	git config --file "$CUSTOM_CONFIG_FILE" --show-origin --list >output &&
	test_cmp expect output
'

test_expect_success !MINGW '--show-origin stdin' '
	cat >expect <<-\EOF &&
		standard input:	user.custom=true
	EOF
	git config --file - --show-origin --list <"$CUSTOM_CONFIG_FILE" >output &&
	test_cmp expect output
'

test_expect_success '--show-origin stdin with file include' '
	cat >"$INCLUDE_DIR"/stdin.include <<-EOF &&
		[user]
			stdin = include
	EOF
	cat >expect <<-EOF &&
		file:$INCLUDE_DIR/stdin.include	include
	EOF
	echo "[include]path=\"$INCLUDE_DIR\"/stdin.include" \
		| git config --show-origin --includes --file - user.stdin >output &&
	test_cmp expect output
'

test_expect_success !MINGW '--show-origin blob' '
	cat >expect <<-\EOF &&
		blob:a9d9f9e555b5c6f07cbe09d3f06fe3df11e09c08	user.custom=true
	EOF
	blob=$(git hash-object -w "$CUSTOM_CONFIG_FILE") &&
	git config --blob=$blob --show-origin --list >output &&
	test_cmp expect output
'

test_expect_success !MINGW '--show-origin blob ref' '
	cat >expect <<-\EOF &&
		blob:"master:file\" (dq) and spaces.conf"	user.custom=true
	EOF
	git add "$CUSTOM_CONFIG_FILE" &&
	git commit -m "new config file" &&
	git config --blob=master:"$CUSTOM_CONFIG_FILE" --show-origin --list >output &&
	test_cmp expect output
'

test_expect_success '--local requires a repo' '
	# we expect 128 to ensure that we do not simply
	# fail to find anything and return code "1"
	test_expect_code 128 nongit git config --local foo.bar
'

test_done
