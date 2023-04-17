#!/bin/sh
#
# Copyright (c) 2005 Johannes Schindelin
#

test_description='Test git config in different settings'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'clear default config' '
	rm -f .git/config
'

cat > expect << EOF
[section]
	penguin = little blue
EOF
test_expect_success 'initial' '
	git config section.penguin "little blue" &&
	test_cmp expect .git/config
'

cat > expect << EOF
[section]
	penguin = little blue
	Movie = BadPhysics
EOF
test_expect_success 'mixed case' '
	git config Section.Movie BadPhysics &&
	test_cmp expect .git/config
'

cat > expect << EOF
[section]
	penguin = little blue
	Movie = BadPhysics
[Sections]
	WhatEver = Second
EOF
test_expect_success 'similar section' '
	git config Sections.WhatEver Second &&
	test_cmp expect .git/config
'

cat > expect << EOF
[section]
	penguin = little blue
	Movie = BadPhysics
	UPPERCASE = true
[Sections]
	WhatEver = Second
EOF
test_expect_success 'uppercase section' '
	git config SECTION.UPPERCASE true &&
	test_cmp expect .git/config
'

test_expect_success 'replace with non-match' '
	git config section.penguin kingpin !blue
'

test_expect_success 'replace with non-match (actually matching)' '
	git config section.penguin "very blue" !kingpin
'

cat > expect << EOF
[section]
	penguin = very blue
	Movie = BadPhysics
	UPPERCASE = true
	penguin = kingpin
[Sections]
	WhatEver = Second
EOF

test_expect_success 'non-match result' 'test_cmp expect .git/config'

test_expect_success 'find mixed-case key by canonical name' '
	test_cmp_config Second sections.whatever
'

test_expect_success 'find mixed-case key by non-canonical name' '
	test_cmp_config Second SeCtIoNs.WhAtEvEr
'

test_expect_success 'subsections are not canonicalized by git-config' '
	cat >>.git/config <<-\EOF &&
	[section.SubSection]
	key = one
	[section "SubSection"]
	key = two
	EOF
	test_cmp_config one section.subsection.key &&
	test_cmp_config two section.SubSection.key
'

cat > .git/config <<\EOF
[alpha]
bar = foo
[beta]
baz = multiple \
lines
foo = bar
EOF

test_expect_success 'unset with cont. lines' '
	git config --unset beta.baz
'

cat > expect <<\EOF
[alpha]
bar = foo
[beta]
foo = bar
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
	test_cmp_config alpha beta.haha
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
	test_cmp_config wow --get nextsection.nonewline !for
'

test_expect_success 'multi-valued get returns final one' '
	test_cmp_config "wow2 for me" --get nextsection.nonewline
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
test_expect_success '--list without repo produces empty output' '
	git --git-dir=nonexistent config --list >output &&
	test_must_be_empty output
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
	test_must_fail git config --file non-existing-config -l &&
	test_must_fail git config --file non-existing-config test.xyzzy
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
	test_cmp_config -C x strasse --file=../other-config --get ein.bahn
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
	echo  >expect &&
	test_cmp_config 129922760704 --int --get giga.watts
'

test_expect_success 'invalid unit' '
	git config aninvalid.unit "1auto" &&
	test_cmp_config 1auto aninvalid.unit &&
	test_must_fail git config --int --get aninvalid.unit 2>actual &&
	test_i18ngrep "bad numeric config value .1auto. for .aninvalid.unit. in file .git/config: invalid unit" actual
'

test_expect_success 'invalid unit boolean' '
	git config commit.gpgsign "1true" &&
	test_cmp_config 1true commit.gpgsign &&
	test_must_fail git config --bool --get commit.gpgsign 2>actual &&
	test_i18ngrep "bad boolean config value .1true. for .commit.gpgsign." actual
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
	    git config --bool --get bool.true$i >>result &&
	    git config --bool --get bool.false$i >>result || return 1
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
		sane_unset HOME &&
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

test_expect_success 'get --expiry-date' '
	rel="3.weeks.5.days.00:00" &&
	rel_out="$rel ->" &&
	cat >.git/config <<-\EOF &&
	[date]
	valid1 = "3.weeks.5.days 00:00"
	valid2 = "Fri Jun 4 15:46:55 2010"
	valid3 = "2017/11/11 11:11:11PM"
	valid4 = "2017/11/10 09:08:07 PM"
	valid5 = "never"
	invalid1 = "abc"
	EOF
	cat >expect <<-EOF &&
	$(test-tool date timestamp $rel)
	1275666415
	1510441871
	1510348087
	0
	EOF
	: "work around heredoc parsing bug fixed in dash 0.5.7 (in ec2c84d)" &&
	{
		echo "$rel_out $(git config --expiry-date date.valid1)" &&
		git config --expiry-date date.valid2 &&
		git config --expiry-date date.valid3 &&
		git config --expiry-date date.valid4 &&
		git config --expiry-date date.valid5
	} >actual &&
	test_cmp expect actual &&
	test_must_fail git config --expiry-date date.invalid1
'

test_expect_success 'get --type=color' '
	rm .git/config &&
	git config foo.color "red" &&
	git config --get --type=color foo.color >actual.raw &&
	test_decode_color <actual.raw >actual &&
	echo "<RED>" >expect &&
	test_cmp expect actual
'

cat >expect << EOF
[foo]
	color = red
EOF

test_expect_success 'set --type=color' '
	rm .git/config &&
	git config --type=color foo.color "red" &&
	test_cmp expect .git/config
'

test_expect_success 'get --type=color barfs on non-color' '
	echo "[foo]bar=not-a-color" >.git/config &&
	test_must_fail git config --get --type=color foo.bar
'

test_expect_success 'set --type=color barfs on non-color' '
	test_must_fail git config --type=color foo.color "not-a-color" 2>error &&
	test_i18ngrep "cannot parse color" error
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
	test_cmp expect result
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
	test_cmp_config "foo 	  bar" section.val
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
	git config branch.main.mergeoptions 'echo \"' &&
	test_must_fail git merge main
"

test_expect_success 'git -c "key=value" support' '
	cat >expect <<-\EOF &&
	value
	value
	true
	EOF
	{
		git -c section.name=value config section.name &&
		git -c foo.CamelCase=value config foo.camelcase &&
		git -c foo.flag config --bool foo.flag
	} >actual &&
	test_cmp expect actual &&
	test_must_fail git -c name=value config section.name
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
	git -c section.foo="value with = in it" config section.foo >actual &&
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

test_expect_success 'old-fashioned settings are case insensitive' '
	test_when_finished "rm -f testConfig testConfig_expect testConfig_actual" &&

	cat >testConfig_actual <<-EOF &&
	[V.A]
	r = value1
	EOF
	q_to_tab >testConfig_expect <<-EOF &&
	[V.A]
	Qr = value2
	EOF
	git config -f testConfig_actual "v.a.r" value2 &&
	test_cmp testConfig_expect testConfig_actual &&

	cat >testConfig_actual <<-EOF &&
	[V.A]
	r = value1
	EOF
	q_to_tab >testConfig_expect <<-EOF &&
	[V.A]
	QR = value2
	EOF
	git config -f testConfig_actual "V.a.R" value2 &&
	test_cmp testConfig_expect testConfig_actual &&

	cat >testConfig_actual <<-EOF &&
	[V.A]
	r = value1
	EOF
	q_to_tab >testConfig_expect <<-EOF &&
	[V.A]
	r = value1
	Qr = value2
	EOF
	git config -f testConfig_actual "V.A.r" value2 &&
	test_cmp testConfig_expect testConfig_actual &&

	cat >testConfig_actual <<-EOF &&
	[V.A]
	r = value1
	EOF
	q_to_tab >testConfig_expect <<-EOF &&
	[V.A]
	r = value1
	Qr = value2
	EOF
	git config -f testConfig_actual "v.A.r" value2 &&
	test_cmp testConfig_expect testConfig_actual
'

test_expect_success 'setting different case sensitive subsections ' '
	test_when_finished "rm -f testConfig testConfig_expect testConfig_actual" &&

	cat >testConfig_actual <<-EOF &&
	[V "A"]
	R = v1
	[K "E"]
	Y = v1
	[a "b"]
	c = v1
	[d "e"]
	f = v1
	EOF
	q_to_tab >testConfig_expect <<-EOF &&
	[V "A"]
	Qr = v2
	[K "E"]
	Qy = v2
	[a "b"]
	Qc = v2
	[d "e"]
	f = v1
	[d "E"]
	Qf = v2
	EOF
	# exact match
	git config -f testConfig_actual a.b.c v2 &&
	# match section and subsection, key is cased differently.
	git config -f testConfig_actual K.E.y v2 &&
	# section and key are matched case insensitive, but subsection needs
	# to match; When writing out new values only the key is adjusted
	git config -f testConfig_actual v.A.r v2 &&
	# subsection is not matched:
	git config -f testConfig_actual d.E.f v2 &&
	test_cmp testConfig_expect testConfig_actual
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

test_expect_success 'GIT_CONFIG_PARAMETERS handles old-style entries' '
	v="${SQ}key.one=foo${SQ}" &&
	v="$v  ${SQ}key.two=bar${SQ}" &&
	v="$v ${SQ}key.ambiguous=section.whatever=value${SQ}" &&
	GIT_CONFIG_PARAMETERS=$v git config --get-regexp "key.*" >actual &&
	cat >expect <<-EOF &&
	key.one foo
	key.two bar
	key.ambiguous section.whatever=value
	EOF
	test_cmp expect actual
'

test_expect_success 'GIT_CONFIG_PARAMETERS handles new-style entries' '
	v="${SQ}key.one${SQ}=${SQ}foo${SQ}" &&
	v="$v  ${SQ}key.two${SQ}=${SQ}bar${SQ}" &&
	v="$v ${SQ}key.ambiguous=section.whatever${SQ}=${SQ}value${SQ}" &&
	GIT_CONFIG_PARAMETERS=$v git config --get-regexp "key.*" >actual &&
	cat >expect <<-EOF &&
	key.one foo
	key.two bar
	key.ambiguous=section.whatever value
	EOF
	test_cmp expect actual
'

test_expect_success 'old and new-style entries can mix' '
	v="${SQ}key.oldone=oldfoo${SQ}" &&
	v="$v ${SQ}key.newone${SQ}=${SQ}newfoo${SQ}" &&
	v="$v ${SQ}key.oldtwo=oldbar${SQ}" &&
	v="$v ${SQ}key.newtwo${SQ}=${SQ}newbar${SQ}" &&
	GIT_CONFIG_PARAMETERS=$v git config --get-regexp "key.*" >actual &&
	cat >expect <<-EOF &&
	key.oldone oldfoo
	key.newone newfoo
	key.oldtwo oldbar
	key.newtwo newbar
	EOF
	test_cmp expect actual
'

test_expect_success 'old and new bools with ambiguous subsection' '
	v="${SQ}key.with=equals.oldbool${SQ}" &&
	v="$v ${SQ}key.with=equals.newbool${SQ}=" &&
	GIT_CONFIG_PARAMETERS=$v git config --get-regexp "key.*" >actual &&
	cat >expect <<-EOF &&
	key.with equals.oldbool
	key.with=equals.newbool
	EOF
	test_cmp expect actual
'

test_expect_success 'detect bogus GIT_CONFIG_PARAMETERS' '
	cat >expect <<-\EOF &&
	env.one one
	env.two two
	EOF
	GIT_CONFIG_PARAMETERS="${SQ}env.one=one${SQ} ${SQ}env.two=two${SQ}" \
		git config --get-regexp "env.*" >actual &&
	test_cmp expect actual &&

	cat >expect <<-EOF &&
	env.one one${SQ}
	env.two two
	EOF
	GIT_CONFIG_PARAMETERS="${SQ}env.one=one${SQ}\\$SQ$SQ$SQ ${SQ}env.two=two${SQ}" \
		git config --get-regexp "env.*" >actual &&
	test_cmp expect actual &&

	test_must_fail env \
		GIT_CONFIG_PARAMETERS="${SQ}env.one=one${SQ}\\$SQ ${SQ}env.two=two${SQ}" \
		git config --get-regexp "env.*"
'

test_expect_success 'git --config-env=key=envvar support' '
	cat >expect <<-\EOF &&
	value
	value
	value
	value
	false
	false
	EOF
	{
		ENVVAR=value git --config-env=core.name=ENVVAR config core.name &&
		ENVVAR=value git --config-env core.name=ENVVAR config core.name &&
		ENVVAR=value git --config-env=foo.CamelCase=ENVVAR config foo.camelcase &&
		ENVVAR=value git --config-env foo.CamelCase=ENVVAR config foo.camelcase &&
		ENVVAR= git --config-env=foo.flag=ENVVAR config --bool foo.flag &&
		ENVVAR= git --config-env foo.flag=ENVVAR config --bool foo.flag
	} >actual &&
	test_cmp expect actual
'

test_expect_success 'git --config-env with missing value' '
	test_must_fail env ENVVAR=value git --config-env 2>error &&
	grep "no config key given for --config-env" error &&
	test_must_fail env ENVVAR=value git --config-env config core.name 2>error &&
	grep "invalid config format: config" error
'

test_expect_success 'git --config-env fails with invalid parameters' '
	test_must_fail git --config-env=foo.flag config --bool foo.flag 2>error &&
	test_i18ngrep "invalid config format: foo.flag" error &&
	test_must_fail git --config-env=foo.flag= config --bool foo.flag 2>error &&
	test_i18ngrep "missing environment variable name for configuration ${SQ}foo.flag${SQ}" error &&
	sane_unset NONEXISTENT &&
	test_must_fail git --config-env=foo.flag=NONEXISTENT config --bool foo.flag 2>error &&
	test_i18ngrep "missing environment variable ${SQ}NONEXISTENT${SQ} for configuration ${SQ}foo.flag${SQ}" error
'

test_expect_success 'git -c and --config-env work together' '
	cat >expect <<-\EOF &&
	bar.cmd cmd-value
	bar.env env-value
	EOF
	ENVVAR=env-value git \
		-c bar.cmd=cmd-value \
		--config-env=bar.env=ENVVAR \
		config --get-regexp "^bar.*" >actual &&
	test_cmp expect actual
'

test_expect_success 'git -c and --config-env override each other' '
	cat >expect <<-\EOF &&
	env
	cmd
	EOF
	{
		ENVVAR=env git -c bar.bar=cmd --config-env=bar.bar=ENVVAR config bar.bar &&
		ENVVAR=env git --config-env=bar.bar=ENVVAR -c bar.bar=cmd config bar.bar
	} >actual &&
	test_cmp expect actual
'

test_expect_success '--config-env handles keys with equals' '
	echo value=with=equals >expect &&
	ENVVAR=value=with=equals git \
		--config-env=section.subsection=with=equals.key=ENVVAR \
		config section.subsection=with=equals.key >actual &&
	test_cmp expect actual
'

test_expect_success 'git config handles environment config pairs' '
	GIT_CONFIG_COUNT=2 \
		GIT_CONFIG_KEY_0="pair.one" GIT_CONFIG_VALUE_0="foo" \
		GIT_CONFIG_KEY_1="pair.two" GIT_CONFIG_VALUE_1="bar" \
		git config --get-regexp "pair.*" >actual &&
	cat >expect <<-EOF &&
	pair.one foo
	pair.two bar
	EOF
	test_cmp expect actual
'

test_expect_success 'git config ignores pairs without count' '
	test_must_fail env GIT_CONFIG_KEY_0="pair.one" GIT_CONFIG_VALUE_0="value" \
		git config pair.one 2>error &&
	test_must_be_empty error
'

test_expect_success 'git config ignores pairs with zero count' '
	test_must_fail env \
		GIT_CONFIG_COUNT=0 \
		GIT_CONFIG_KEY_0="pair.one" GIT_CONFIG_VALUE_0="value" \
		git config pair.one
'

test_expect_success 'git config ignores pairs exceeding count' '
	GIT_CONFIG_COUNT=1 \
		GIT_CONFIG_KEY_0="pair.one" GIT_CONFIG_VALUE_0="value" \
		GIT_CONFIG_KEY_1="pair.two" GIT_CONFIG_VALUE_1="value" \
		git config --get-regexp "pair.*" >actual &&
	cat >expect <<-EOF &&
	pair.one value
	EOF
	test_cmp expect actual
'

test_expect_success 'git config ignores pairs with zero count' '
	test_must_fail env \
		GIT_CONFIG_COUNT=0 GIT_CONFIG_KEY_0="pair.one" GIT_CONFIG_VALUE_0="value" \
		git config pair.one >error &&
	test_must_be_empty error
'

test_expect_success 'git config ignores pairs with empty count' '
	test_must_fail env \
		GIT_CONFIG_COUNT= GIT_CONFIG_KEY_0="pair.one" GIT_CONFIG_VALUE_0="value" \
		git config pair.one >error &&
	test_must_be_empty error
'

test_expect_success 'git config fails with invalid count' '
	test_must_fail env GIT_CONFIG_COUNT=10a git config --list 2>error &&
	test_i18ngrep "bogus count" error &&
	test_must_fail env GIT_CONFIG_COUNT=9999999999999999 git config --list 2>error &&
	test_i18ngrep "too many entries" error
'

test_expect_success 'git config fails with missing config key' '
	test_must_fail env GIT_CONFIG_COUNT=1 GIT_CONFIG_VALUE_0="value" \
		git config --list 2>error &&
	test_i18ngrep "missing config key" error
'

test_expect_success 'git config fails with missing config value' '
	test_must_fail env GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0="pair.one" \
		git config --list 2>error &&
	test_i18ngrep "missing config value" error
'

test_expect_success 'git config fails with invalid config pair key' '
	test_must_fail env GIT_CONFIG_COUNT=1 \
		GIT_CONFIG_KEY_0= GIT_CONFIG_VALUE_0=value \
		git config --list &&
	test_must_fail env GIT_CONFIG_COUNT=1 \
		GIT_CONFIG_KEY_0=missing-section GIT_CONFIG_VALUE_0=value \
		git config --list
'

test_expect_success 'environment overrides config file' '
	test_when_finished "rm -f .git/config" &&
	cat >.git/config <<-EOF &&
	[pair]
	one = value
	EOF
	GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=pair.one GIT_CONFIG_VALUE_0=override \
		git config pair.one >actual &&
	cat >expect <<-EOF &&
	override
	EOF
	test_cmp expect actual
'

test_expect_success 'GIT_CONFIG_PARAMETERS overrides environment config' '
	GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=pair.one GIT_CONFIG_VALUE_0=value \
		GIT_CONFIG_PARAMETERS="${SQ}pair.one=override${SQ}" \
		git config pair.one >actual &&
	cat >expect <<-EOF &&
	override
	EOF
	test_cmp expect actual
'

test_expect_success 'command line overrides environment config' '
	GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=pair.one GIT_CONFIG_VALUE_0=value \
		git -c pair.one=override config pair.one >actual &&
	cat >expect <<-EOF &&
	override
	EOF
	test_cmp expect actual
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
	# broken key=value
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
	# broken value string
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
	[http "https://*.example.*"]
		cookieFile = /tmp/multiwildcard.txt
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
	test_cmp expect actual &&

	echo http.cookiefile /tmp/multiwildcard.txt >expect &&
	git config --get-urlmatch HTTP https://wildcard.example.org >actual &&
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
test_expect_success '--unset last key removes section (except if commented)' '
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
	# a comment specific to this "section" section.
	[section]
	# some intervening lines
	# that should also be dropped

	# please be careful when you update the above variable
	EOF

	git config --unset section.key &&
	test_cmp expect .git/config &&

	cat >.git/config <<-\EOF &&
	[section]
	key = value
	[next-section]
	EOF

	cat >expect <<-\EOF &&
	[next-section]
	EOF

	git config --unset section.key &&
	test_cmp expect .git/config &&

	q_to_tab >.git/config <<-\EOF &&
	[one]
	Qkey = "multiline \
	QQ# with comment"
	[two]
	key = true
	EOF
	git config --unset two.key &&
	! grep two .git/config &&

	q_to_tab >.git/config <<-\EOF &&
	[one]
	Qkey = "multiline \
	QQ# with comment"
	[one]
	key = true
	EOF
	git config --unset-all one.key &&
	test_line_count = 0 .git/config &&

	q_to_tab >.git/config <<-\EOF &&
	[one]
	Qkey = true
	Q# a comment not at the start
	[two]
	Qkey = true
	EOF
	git config --unset two.key &&
	grep two .git/config &&

	q_to_tab >.git/config <<-\EOF &&
	[one]
	Qkey = not [two "subsection"]
	[two "subsection"]
	[two "subsection"]
	Qkey = true
	[TWO "subsection"]
	[one]
	EOF
	git config --unset two.subsection.key &&
	test "not [two subsection]" = "$(git config one.key)" &&
	test_line_count = 3 .git/config
'

test_expect_success '--unset-all removes section if empty & uncommented' '
	cat >.git/config <<-\EOF &&
	[section]
	key = value1
	key = value2
	EOF

	git config --unset-all section.key &&
	test_line_count = 0 .git/config
'

test_expect_success 'adding a key into an empty section reuses header' '
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
	command line:	user.environ=true
	command line:	user.cmdline=true
	EOF
	GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=user.environ GIT_CONFIG_VALUE_0=true\
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

test_expect_success 'set up custom config file' '
	CUSTOM_CONFIG_FILE="custom.conf" &&
	cat >"$CUSTOM_CONFIG_FILE" <<-\EOF
	[user]
		custom = true
	EOF
'

test_expect_success !MINGW 'set up custom config file with special name characters' '
	WEIRDLY_NAMED_FILE="file\" (dq) and spaces.conf" &&
	cp "$CUSTOM_CONFIG_FILE" "$WEIRDLY_NAMED_FILE"
'

test_expect_success !MINGW '--show-origin escape special file name characters' '
	cat >expect <<-\EOF &&
	file:"file\" (dq) and spaces.conf"	user.custom=true
	EOF
	git config --file "$WEIRDLY_NAMED_FILE" --show-origin --list >output &&
	test_cmp expect output
'

test_expect_success '--show-origin stdin' '
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
	echo "[include]path=\"$INCLUDE_DIR\"/stdin.include" |
	git config --show-origin --includes --file - user.stdin >output &&

	test_cmp expect output
'

test_expect_success '--show-origin blob' '
	blob=$(git hash-object -w "$CUSTOM_CONFIG_FILE") &&
	cat >expect <<-EOF &&
	blob:$blob	user.custom=true
	EOF
	git config --blob=$blob --show-origin --list >output &&
	test_cmp expect output
'

test_expect_success '--show-origin blob ref' '
	cat >expect <<-\EOF &&
	blob:main:custom.conf	user.custom=true
	EOF
	git add "$CUSTOM_CONFIG_FILE" &&
	git commit -m "new config file" &&
	git config --blob=main:"$CUSTOM_CONFIG_FILE" --show-origin --list >output &&
	test_cmp expect output
'

test_expect_success '--show-scope with --list' '
	cat >expect <<-EOF &&
	global	user.global=true
	global	user.override=global
	global	include.path=$INCLUDE_DIR/absolute.include
	global	user.absolute=include
	local	user.local=true
	local	user.override=local
	local	include.path=../include/relative.include
	local	user.relative=include
	local	core.repositoryformatversion=1
	local	extensions.worktreeconfig=true
	worktree	user.worktree=true
	command	user.cmdline=true
	EOF
	git worktree add wt1 &&
	# We need these to test for worktree scope, but outside of this
	# test, this is just noise
	test_config core.repositoryformatversion 1 &&
	test_config extensions.worktreeConfig true &&
	git config --worktree user.worktree true &&
	git -c user.cmdline=true config --list --show-scope >output &&
	test_cmp expect output
'

test_expect_success !MINGW '--show-scope with --blob' '
	blob=$(git hash-object -w "$CUSTOM_CONFIG_FILE") &&
	cat >expect <<-EOF &&
	command	user.custom=true
	EOF
	git config --blob=$blob --show-scope --list >output &&
	test_cmp expect output
'

test_expect_success '--show-scope with --local' '
	cat >expect <<-\EOF &&
	local	user.local=true
	local	user.override=local
	local	include.path=../include/relative.include
	EOF
	git config --local --list --show-scope >output &&
	test_cmp expect output
'

test_expect_success '--show-scope getting a single value' '
	cat >expect <<-\EOF &&
	local	true
	EOF
	git config --show-scope --get user.local >output &&
	test_cmp expect output
'

test_expect_success '--show-scope with --show-origin' '
	cat >expect <<-EOF &&
	global	file:$HOME/.gitconfig	user.global=true
	global	file:$HOME/.gitconfig	user.override=global
	global	file:$HOME/.gitconfig	include.path=$INCLUDE_DIR/absolute.include
	global	file:$INCLUDE_DIR/absolute.include	user.absolute=include
	local	file:.git/config	user.local=true
	local	file:.git/config	user.override=local
	local	file:.git/config	include.path=../include/relative.include
	local	file:.git/../include/relative.include	user.relative=include
	command	command line:	user.cmdline=true
	EOF
	git -c user.cmdline=true config --list --show-origin --show-scope >output &&
	test_cmp expect output
'

test_expect_success 'override global and system config' '
	test_when_finished rm -f \"\$HOME\"/.gitconfig &&
	cat >"$HOME"/.gitconfig <<-EOF &&
	[home]
		config = true
	EOF

	test_when_finished rm -rf \"\$HOME\"/.config/git &&
	mkdir -p "$HOME"/.config/git &&
	cat >"$HOME"/.config/git/config <<-EOF &&
	[xdg]
		config = true
	EOF
	cat >.git/config <<-EOF &&
	[local]
		config = true
	EOF
	cat >custom-global-config <<-EOF &&
	[global]
		config = true
	EOF
	cat >custom-system-config <<-EOF &&
	[system]
		config = true
	EOF

	cat >expect <<-EOF &&
	global	xdg.config=true
	global	home.config=true
	local	local.config=true
	EOF
	git config --show-scope --list >output &&
	test_cmp expect output &&

	cat >expect <<-EOF &&
	system	system.config=true
	global	global.config=true
	local	local.config=true
	EOF
	GIT_CONFIG_NOSYSTEM=false GIT_CONFIG_SYSTEM=custom-system-config GIT_CONFIG_GLOBAL=custom-global-config \
		git config --show-scope --list >output &&
	test_cmp expect output &&

	cat >expect <<-EOF &&
	local	local.config=true
	EOF
	GIT_CONFIG_NOSYSTEM=false GIT_CONFIG_SYSTEM=/dev/null GIT_CONFIG_GLOBAL=/dev/null \
		git config --show-scope --list >output &&
	test_cmp expect output
'

test_expect_success 'override global and system config with missing file' '
	test_must_fail env GIT_CONFIG_GLOBAL=does-not-exist GIT_CONFIG_SYSTEM=/dev/null git config --global --list &&
	test_must_fail env GIT_CONFIG_GLOBAL=/dev/null GIT_CONFIG_SYSTEM=does-not-exist git config --system --list &&
	GIT_CONFIG_GLOBAL=does-not-exist GIT_CONFIG_SYSTEM=does-not-exist git version
'

test_expect_success 'system override has no effect with GIT_CONFIG_NOSYSTEM' '
	# `git config --system` has different semantics compared to other
	# commands as it ignores GIT_CONFIG_NOSYSTEM. We thus test whether the
	# variable has an effect via a different proxy.
	cat >alias-config <<-EOF &&
	[alias]
		hello-world = !echo "hello world"
	EOF
	test_must_fail env GIT_CONFIG_NOSYSTEM=true GIT_CONFIG_SYSTEM=alias-config \
		git hello-world &&
	GIT_CONFIG_NOSYSTEM=false GIT_CONFIG_SYSTEM=alias-config \
		git hello-world >actual &&
	echo "hello world" >expect &&
	test_cmp expect actual
'

test_expect_success 'write to overridden global and system config' '
	cat >expect <<EOF &&
[config]
	key = value
EOF

	GIT_CONFIG_GLOBAL=write-to-global git config --global config.key value &&
	test_cmp expect write-to-global &&

	GIT_CONFIG_SYSTEM=write-to-system git config --system config.key value &&
	test_cmp expect write-to-system
'

for opt in --local --worktree
do
	test_expect_success "$opt requires a repo" '
		# we expect 128 to ensure that we do not simply
		# fail to find anything and return code "1"
		test_expect_code 128 nongit git config $opt foo.bar
	'
done

cat >.git/config <<-\EOF &&
[section]
foo = true
number = 10
big = 1M
EOF

test_expect_success 'identical modern --type specifiers are allowed' '
	test_cmp_config 1048576 --type=int --type=int section.big
'

test_expect_success 'identical legacy --type specifiers are allowed' '
	test_cmp_config 1048576 --int --int section.big
'

test_expect_success 'identical mixed --type specifiers are allowed' '
	test_cmp_config 1048576 --int --type=int section.big
'

test_expect_success 'non-identical modern --type specifiers are not allowed' '
	test_must_fail git config --type=int --type=bool section.big 2>error &&
	test_i18ngrep "only one type at a time" error
'

test_expect_success 'non-identical legacy --type specifiers are not allowed' '
	test_must_fail git config --int --bool section.big 2>error &&
	test_i18ngrep "only one type at a time" error
'

test_expect_success 'non-identical mixed --type specifiers are not allowed' '
	test_must_fail git config --type=int --bool section.big 2>error &&
	test_i18ngrep "only one type at a time" error
'

test_expect_success '--type allows valid type specifiers' '
	test_cmp_config true  --type=bool section.foo
'

test_expect_success '--no-type unsets type specifiers' '
	test_cmp_config 10 --type=bool --no-type section.number
'

test_expect_success 'unset type specifiers may be reset to conflicting ones' '
	test_cmp_config 1048576 --type=bool --no-type --type=int section.big
'

test_expect_success '--type rejects unknown specifiers' '
	test_must_fail git config --type=nonsense section.foo 2>error &&
	test_i18ngrep "unrecognized --type argument" error
'

test_expect_success '--type=int requires at least one digit' '
	test_must_fail git config --type int --default m some.key >out 2>error &&
	grep "bad numeric config value" error &&
	test_must_be_empty out
'

test_expect_success '--replace-all does not invent newlines' '
	q_to_tab >.git/config <<-\EOF &&
	[abc]key
	QkeepSection
	[xyz]
	Qkey = 1
	[abc]
	Qkey = a
	EOF
	q_to_tab >expect <<-\EOF &&
	[abc]
	QkeepSection
	[xyz]
	Qkey = 1
	[abc]
	Qkey = b
	EOF
	git config --replace-all abc.key b &&
	test_cmp expect .git/config
'

test_expect_success 'set all config with value-pattern' '
	test_when_finished rm -f config initial &&
	git config --file=initial abc.key one &&

	# no match => add new entry
	cp initial config &&
	git config --file=config abc.key two a+ &&
	git config --file=config --list >actual &&
	cat >expect <<-\EOF &&
	abc.key=one
	abc.key=two
	EOF
	test_cmp expect actual &&

	# multiple matches => failure
	test_must_fail git config --file=config abc.key three o+ 2>err &&
	test_i18ngrep "has multiple values" err &&

	# multiple values, no match => add
	git config --file=config abc.key three a+ &&
	git config --file=config --list >actual &&
	cat >expect <<-\EOF &&
	abc.key=one
	abc.key=two
	abc.key=three
	EOF
	test_cmp expect actual &&

	# single match => replace
	git config --file=config abc.key four h+ &&
	git config --file=config --list >actual &&
	cat >expect <<-\EOF &&
	abc.key=one
	abc.key=two
	abc.key=four
	EOF
	test_cmp expect actual
'

test_expect_success '--replace-all and value-pattern' '
	test_when_finished rm -f config &&
	git config --file=config --add abc.key one &&
	git config --file=config --add abc.key two &&
	git config --file=config --add abc.key three &&
	git config --file=config --replace-all abc.key four "o+" &&
	git config --file=config --list >actual &&
	cat >expect <<-\EOF &&
	abc.key=four
	abc.key=three
	EOF
	test_cmp expect actual
'

test_expect_success 'refuse --fixed-value for incompatible actions' '
	test_when_finished rm -f config &&
	git config --file=config dev.null bogus &&

	# These modes do not allow --fixed-value at all
	test_must_fail git config --file=config --fixed-value --add dev.null bogus &&
	test_must_fail git config --file=config --fixed-value --get-urlmatch dev.null bogus &&
	test_must_fail git config --file=config --fixed-value --get-urlmatch dev.null bogus &&
	test_must_fail git config --file=config --fixed-value --rename-section dev null &&
	test_must_fail git config --file=config --fixed-value --remove-section dev &&
	test_must_fail git config --file=config --fixed-value --list &&
	test_must_fail git config --file=config --fixed-value --get-color dev.null &&
	test_must_fail git config --file=config --fixed-value --get-colorbool dev.null &&

	# These modes complain when --fixed-value has no value-pattern
	test_must_fail git config --file=config --fixed-value dev.null bogus &&
	test_must_fail git config --file=config --fixed-value --replace-all dev.null bogus &&
	test_must_fail git config --file=config --fixed-value --get dev.null &&
	test_must_fail git config --file=config --fixed-value --get-all dev.null &&
	test_must_fail git config --file=config --fixed-value --get-regexp "dev.*" &&
	test_must_fail git config --file=config --fixed-value --unset dev.null &&
	test_must_fail git config --file=config --fixed-value --unset-all dev.null
'

test_expect_success '--fixed-value uses exact string matching' '
	test_when_finished rm -f config initial &&
	META="a+b*c?d[e]f.g" &&
	git config --file=initial fixed.test "$META" &&

	cp initial config &&
	git config --file=config fixed.test bogus "$META" &&
	git config --file=config --list >actual &&
	cat >expect <<-EOF &&
	fixed.test=$META
	fixed.test=bogus
	EOF
	test_cmp expect actual &&

	cp initial config &&
	git config --file=config --fixed-value fixed.test bogus "$META" &&
	git config --file=config --list >actual &&
	cat >expect <<-\EOF &&
	fixed.test=bogus
	EOF
	test_cmp expect actual &&

	cp initial config &&
	test_must_fail git config --file=config --unset fixed.test "$META" &&
	git config --file=config --fixed-value --unset fixed.test "$META" &&
	test_must_fail git config --file=config fixed.test &&

	cp initial config &&
	test_must_fail git config --file=config --unset-all fixed.test "$META" &&
	git config --file=config --fixed-value --unset-all fixed.test "$META" &&
	test_must_fail git config --file=config fixed.test &&

	cp initial config &&
	git config --file=config --replace-all fixed.test bogus "$META" &&
	git config --file=config --list >actual &&
	cat >expect <<-EOF &&
	fixed.test=$META
	fixed.test=bogus
	EOF
	test_cmp expect actual &&

	git config --file=config --fixed-value --replace-all fixed.test bogus "$META" &&
	git config --file=config --list >actual &&
	cat >expect <<-EOF &&
	fixed.test=bogus
	fixed.test=bogus
	EOF
	test_cmp expect actual
'

test_expect_success '--get and --get-all with --fixed-value' '
	test_when_finished rm -f config &&
	META="a+b*c?d[e]f.g" &&
	git config --file=config fixed.test bogus &&
	git config --file=config --add fixed.test "$META" &&

	git config --file=config --get fixed.test bogus &&
	test_must_fail git config --file=config --get fixed.test "$META" &&
	git config --file=config --get --fixed-value fixed.test "$META" &&
	test_must_fail git config --file=config --get --fixed-value fixed.test non-existent &&

	git config --file=config --get-all fixed.test bogus &&
	test_must_fail git config --file=config --get-all fixed.test "$META" &&
	git config --file=config --get-all --fixed-value fixed.test "$META" &&
	test_must_fail git config --file=config --get-all --fixed-value fixed.test non-existent &&

	git config --file=config --get-regexp fixed+ bogus &&
	test_must_fail git config --file=config --get-regexp fixed+ "$META" &&
	git config --file=config --get-regexp --fixed-value fixed+ "$META" &&
	test_must_fail git config --file=config --get-regexp --fixed-value fixed+ non-existent
'

test_expect_success 'includeIf.hasconfig:remote.*.url' '
	git init hasremoteurlTest &&
	test_when_finished "rm -rf hasremoteurlTest" &&

	cat >include-this <<-\EOF &&
	[user]
		this = this-is-included
	EOF
	cat >dont-include-that <<-\EOF &&
	[user]
		that = that-is-not-included
	EOF
	cat >>hasremoteurlTest/.git/config <<-EOF &&
	[includeIf "hasconfig:remote.*.url:foourl"]
		path = "$(pwd)/include-this"
	[includeIf "hasconfig:remote.*.url:barurl"]
		path = "$(pwd)/dont-include-that"
	[remote "foo"]
		url = foourl
	EOF

	echo this-is-included >expect-this &&
	git -C hasremoteurlTest config --get user.this >actual-this &&
	test_cmp expect-this actual-this &&

	test_must_fail git -C hasremoteurlTest config --get user.that
'

test_expect_success 'includeIf.hasconfig:remote.*.url respects last-config-wins' '
	git init hasremoteurlTest &&
	test_when_finished "rm -rf hasremoteurlTest" &&

	cat >include-two-three <<-\EOF &&
	[user]
		two = included-config
		three = included-config
	EOF
	cat >>hasremoteurlTest/.git/config <<-EOF &&
	[remote "foo"]
		url = foourl
	[user]
		one = main-config
		two = main-config
	[includeIf "hasconfig:remote.*.url:foourl"]
		path = "$(pwd)/include-two-three"
	[user]
		three = main-config
	EOF

	echo main-config >expect-main-config &&
	echo included-config >expect-included-config &&

	git -C hasremoteurlTest config --get user.one >actual &&
	test_cmp expect-main-config actual &&

	git -C hasremoteurlTest config --get user.two >actual &&
	test_cmp expect-included-config actual &&

	git -C hasremoteurlTest config --get user.three >actual &&
	test_cmp expect-main-config actual
'

test_expect_success 'includeIf.hasconfig:remote.*.url globs' '
	git init hasremoteurlTest &&
	test_when_finished "rm -rf hasremoteurlTest" &&

	printf "[user]\ndss = yes\n" >double-star-start &&
	printf "[user]\ndse = yes\n" >double-star-end &&
	printf "[user]\ndsm = yes\n" >double-star-middle &&
	printf "[user]\nssm = yes\n" >single-star-middle &&
	printf "[user]\nno = no\n" >no &&

	cat >>hasremoteurlTest/.git/config <<-EOF &&
	[remote "foo"]
		url = https://foo/bar/baz
	[includeIf "hasconfig:remote.*.url:**/baz"]
		path = "$(pwd)/double-star-start"
	[includeIf "hasconfig:remote.*.url:**/nomatch"]
		path = "$(pwd)/no"
	[includeIf "hasconfig:remote.*.url:https:/**"]
		path = "$(pwd)/double-star-end"
	[includeIf "hasconfig:remote.*.url:nomatch:/**"]
		path = "$(pwd)/no"
	[includeIf "hasconfig:remote.*.url:https:/**/baz"]
		path = "$(pwd)/double-star-middle"
	[includeIf "hasconfig:remote.*.url:https:/**/nomatch"]
		path = "$(pwd)/no"
	[includeIf "hasconfig:remote.*.url:https://*/bar/baz"]
		path = "$(pwd)/single-star-middle"
	[includeIf "hasconfig:remote.*.url:https://*/baz"]
		path = "$(pwd)/no"
	EOF

	git -C hasremoteurlTest config --get user.dss &&
	git -C hasremoteurlTest config --get user.dse &&
	git -C hasremoteurlTest config --get user.dsm &&
	git -C hasremoteurlTest config --get user.ssm &&
	test_must_fail git -C hasremoteurlTest config --get user.no
'

test_expect_success 'includeIf.hasconfig:remote.*.url forbids remote url in such included files' '
	git init hasremoteurlTest &&
	test_when_finished "rm -rf hasremoteurlTest" &&

	cat >include-with-url <<-\EOF &&
	[remote "bar"]
		url = barurl
	EOF
	cat >>hasremoteurlTest/.git/config <<-EOF &&
	[includeIf "hasconfig:remote.*.url:foourl"]
		path = "$(pwd)/include-with-url"
	EOF

	# test with any Git command
	test_must_fail git -C hasremoteurlTest status 2>err &&
	grep "fatal: remote URLs cannot be configured in file directly or indirectly included by includeIf.hasconfig:remote.*.url" err
'

test_done
