#!/bin/sh
#
# Copyright (c) 2005 Johannes Schindelin
#

test_description='Test git config in different settings'

. ./test-lib.sh

test -f .git/config && rm .git/config

git config core.penguin "little blue"

cat > expect << EOF
[core]
	penguin = little blue
EOF

test_expect_success 'initial' 'cmp .git/config expect'

git config Core.Movie BadPhysics

cat > expect << EOF
[core]
	penguin = little blue
	Movie = BadPhysics
EOF

test_expect_success 'mixed case' 'cmp .git/config expect'

git config Cores.WhatEver Second

cat > expect << EOF
[core]
	penguin = little blue
	Movie = BadPhysics
[Cores]
	WhatEver = Second
EOF

test_expect_success 'similar section' 'cmp .git/config expect'

git config CORE.UPPERCASE true

cat > expect << EOF
[core]
	penguin = little blue
	Movie = BadPhysics
	UPPERCASE = true
[Cores]
	WhatEver = Second
EOF

test_expect_success 'similar section' 'cmp .git/config expect'

test_expect_success 'replace with non-match' \
	'git config core.penguin kingpin !blue'

test_expect_success 'replace with non-match (actually matching)' \
	'git config core.penguin "very blue" !kingpin'

cat > expect << EOF
[core]
	penguin = very blue
	Movie = BadPhysics
	UPPERCASE = true
	penguin = kingpin
[Cores]
	WhatEver = Second
EOF

test_expect_success 'non-match result' 'cmp .git/config expect'

cat > .git/config <<\EOF
[alpha]
bar = foo
[beta]
baz = multiple \
lines
EOF

test_expect_success 'unset with cont. lines' \
	'git config --unset beta.baz'

cat > expect <<\EOF
[alpha]
bar = foo
[beta]
EOF

test_expect_success 'unset with cont. lines is correct' 'cmp .git/config expect'

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

test_expect_success 'multiple unset' \
	'git config --unset-all beta.haha'

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
[nextSection] noNewline = ouch
EOF

test_expect_success 'multiple unset is correct' 'cmp .git/config expect'

cp .git/config2 .git/config

test_expect_success '--replace-all missing value' '
	test_must_fail git config --replace-all beta.haha &&
	test_cmp .git/config2 .git/config
'

rm .git/config2

test_expect_success '--replace-all' \
	'git config --replace-all beta.haha gamma'

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
	haha = gamma
[nextSection] noNewline = ouch
EOF

test_expect_success 'all replaced' 'cmp .git/config expect'

git config beta.haha alpha

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
	haha = alpha
[nextSection] noNewline = ouch
EOF

test_expect_success 'really mean test' 'cmp .git/config expect'

git config nextsection.nonewline wow

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
	haha = alpha
[nextSection]
	nonewline = wow
EOF

test_expect_success 'really really mean test' 'cmp .git/config expect'

test_expect_success 'get value' 'test alpha = $(git config beta.haha)'
git config --unset beta.haha

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
[nextSection]
	nonewline = wow
EOF

test_expect_success 'unset' 'cmp .git/config expect'

git config nextsection.NoNewLine "wow2 for me" "for me$"

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
[nextSection]
	nonewline = wow
	NoNewLine = wow2 for me
EOF

test_expect_success 'multivar' 'cmp .git/config expect'

test_expect_success 'non-match' \
	'git config --get nextsection.nonewline !for'

test_expect_success 'non-match value' \
	'test wow = $(git config --get nextsection.nonewline !for)'

test_expect_success 'ambiguous get' '
	test_must_fail git config --get nextsection.nonewline
'

test_expect_success 'get multivar' \
	'git config --get-all nextsection.nonewline'

git config nextsection.nonewline "wow3" "wow$"

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
[nextSection]
	nonewline = wow3
	NoNewLine = wow2 for me
EOF

test_expect_success 'multivar replace' 'cmp .git/config expect'

test_expect_success 'ambiguous value' '
	test_must_fail git config nextsection.nonewline
'

test_expect_success 'ambiguous unset' '
	test_must_fail git config --unset nextsection.nonewline
'

test_expect_success 'invalid unset' '
	test_must_fail git config --unset somesection.nonewline
'

git config --unset nextsection.nonewline "wow3$"

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
[nextSection]
	NoNewLine = wow2 for me
EOF

test_expect_success 'multivar unset' 'cmp .git/config expect'

test_expect_success 'invalid key' 'test_must_fail git config inval.2key blabla'

test_expect_success 'correct key' 'git config 123456.a123 987'

test_expect_success 'hierarchical section' \
	'git config Version.1.2.3eX.Alpha beta'

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

test_expect_success 'hierarchical section value' 'cmp .git/config expect'

cat > expect << EOF
beta.noindent=sillyValue
nextsection.nonewline=wow2 for me
123456.a123=987
version.1.2.3eX.alpha=beta
EOF

test_expect_success 'working --list' \
	'git config --list > output && cmp output expect'

cat > expect << EOF
EOF

test_expect_success '--list without repo produces empty output' '
	git --git-dir=nonexistent config --list >output &&
	test_cmp expect output
'

cat > expect << EOF
beta.noindent sillyValue
nextsection.nonewline wow2 for me
EOF

test_expect_success '--get-regexp' \
	'git config --get-regexp in > output && cmp output expect'

git config --add nextsection.nonewline "wow4 for you"

cat > expect << EOF
wow2 for me
wow4 for you
EOF

test_expect_success '--add' \
	'git config --get-all nextsection.nonewline > output && cmp output expect'

cat > .git/config << EOF
[novalue]
	variable
[emptyvalue]
	variable =
EOF

test_expect_success 'get variable with no value' \
	'git config --get novalue.variable ^$'

test_expect_success 'get variable with empty value' \
	'git config --get emptyvalue.variable ^$'

echo novalue.variable > expect

test_expect_success 'get-regexp variable with no value' \
	'git config --get-regexp novalue > output &&
	 cmp output expect'

echo 'emptyvalue.variable ' > expect

test_expect_success 'get-regexp variable with empty value' \
	'git config --get-regexp emptyvalue > output &&
	 cmp output expect'

echo true > expect

test_expect_success 'get bool variable with no value' \
	'git config --bool novalue.variable > output &&
	 cmp output expect'

echo false > expect

test_expect_success 'get bool variable with empty value' \
	'git config --bool emptyvalue.variable > output &&
	 cmp output expect'

test_expect_success 'no arguments, but no crash' '
	test_must_fail git config >output 2>&1 &&
	grep usage output
'

cat > .git/config << EOF
[a.b]
	c = d
EOF

git config a.x y

cat > expect << EOF
[a.b]
	c = d
[a]
	x = y
EOF

test_expect_success 'new section is partial match of another' 'cmp .git/config expect'

git config b.x y
git config a.b c

cat > expect << EOF
[a.b]
	c = d
[a]
	x = y
	b = c
[b]
	x = y
EOF

test_expect_success 'new variable inserts into proper section' 'cmp .git/config expect'

test_expect_success 'alternative GIT_CONFIG (non-existing file should fail)' \
	'test_must_fail git config --file non-existing-config -l'

cat > other-config << EOF
[ein]
	bahn = strasse
EOF

cat > expect << EOF
ein.bahn=strasse
EOF

GIT_CONFIG=other-config git config -l > output

test_expect_success 'alternative GIT_CONFIG' 'cmp output expect'

test_expect_success 'alternative GIT_CONFIG (--file)' \
	'git config --file other-config -l > output && cmp output expect'

test_expect_success 'refer config from subdirectory' '
	mkdir x &&
	(
		cd x &&
		echo strasse >expect
		git config --get --file ../other-config ein.bahn >actual &&
		test_cmp expect actual
	)

'

GIT_CONFIG=other-config git config anwohner.park ausweis

cat > expect << EOF
[ein]
	bahn = strasse
[anwohner]
	park = ausweis
EOF

test_expect_success '--set in alternative GIT_CONFIG' 'cmp other-config expect'

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

test_expect_success "rename section" \
	"git config --rename-section branch.eins branch.zwei"

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

test_expect_success "rename succeeded" "test_cmp expect .git/config"

test_expect_success "rename non-existing section" '
	test_must_fail git config --rename-section \
		branch."world domination" branch.drei
'

test_expect_success "rename succeeded" "test_cmp expect .git/config"

test_expect_success "rename another section" \
	'git config --rename-section branch."1 234 blabl/a" branch.drei'

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

test_expect_success "rename succeeded" "test_cmp expect .git/config"

cat >> .git/config << EOF
[branch "vier"] z = 1
EOF

test_expect_success "rename a section with a var on the same line" \
	'git config --rename-section branch.vier branch.zwei'

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

test_expect_success "rename succeeded" "test_cmp expect .git/config"

cat >> .git/config << EOF
  [branch "zwei"] a = 1 [branch "vier"]
EOF

test_expect_success "remove section" "git config --remove-section branch.zwei"

cat > expect << EOF
# Hallo
	#Bello
[branch "drei"]
weird
EOF

test_expect_success "section was removed properly" \
	"test_cmp expect .git/config"

rm .git/config

cat > expect << EOF
[gitcvs]
	enabled = true
	dbname = %Ggitcvs2.%a.%m.sqlite
[gitcvs "ext"]
	dbname = %Ggitcvs1.%a.%m.sqlite
EOF

test_expect_success 'section ending' '

	git config gitcvs.enabled true &&
	git config gitcvs.ext.dbname %Ggitcvs1.%a.%m.sqlite &&
	git config gitcvs.dbname %Ggitcvs2.%a.%m.sqlite &&
	cmp .git/config expect

'

test_expect_success numbers '

	git config kilo.gram 1k &&
	git config mega.ton 1m &&
	k=$(git config --int --get kilo.gram) &&
	test z1024 = "z$k" &&
	m=$(git config --int --get mega.ton) &&
	test z1048576 = "z$m"
'

cat > expect <<EOF
fatal: bad config value for 'aninvalid.unit' in .git/config
EOF

test_expect_success 'invalid unit' '

	git config aninvalid.unit "1auto" &&
	s=$(git config aninvalid.unit) &&
	test "z1auto" = "z$s" &&
	if git config --int --get aninvalid.unit 2>actual
	then
		echo config should have failed
		false
	fi &&
	cmp actual expect
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
	cmp expect result'

test_expect_success 'invalid bool (--get)' '

	git config bool.nobool foobar &&
	test_must_fail git config --bool --get bool.nobool'

test_expect_success 'invalid bool (set)' '

	test_must_fail git config --bool bool.nobool foobar'

rm .git/config

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

	git config --bool bool.true1 01 &&
	git config --bool bool.true2 -1 &&
	git config --bool bool.true3 YeS &&
	git config --bool bool.true4 true &&
	git config --bool bool.false1 000 &&
	git config --bool bool.false2 "" &&
	git config --bool bool.false3 nO &&
	git config --bool bool.false4 FALSE &&
	cmp expect .git/config'

rm .git/config

cat > expect <<\EOF
[int]
	val1 = 1
	val2 = -1
	val3 = 5242880
EOF

test_expect_success 'set --int' '

	git config --int int.val1 01 &&
	git config --int int.val2 -1 &&
	git config --int int.val3 5m &&
	cmp expect .git/config'

rm .git/config

cat >expect <<\EOF
[bool]
	true1 = true
	true2 = true
	false1 = false
	false2 = false
[int]
	int1 = 0
	int2 = 1
	int3 = -1
EOF

test_expect_success 'get --bool-or-int' '
	(
		echo "[bool]"
		echo true1
		echo true2 = true
		echo false = false
		echo "[int]"
		echo int1 = 0
		echo int2 = 1
		echo int3 = -1
	) >>.git/config &&
	test $(git config --bool-or-int bool.true1) = true &&
	test $(git config --bool-or-int bool.true2) = true &&
	test $(git config --bool-or-int bool.false) = false &&
	test $(git config --bool-or-int int.int1) = 0 &&
	test $(git config --bool-or-int int.int2) = 1 &&
	test $(git config --bool-or-int int.int3) = -1

'

rm .git/config
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
	git config --bool-or-int bool.true1 true &&
	git config --bool-or-int bool.false1 false &&
	git config --bool-or-int bool.true2 yes &&
	git config --bool-or-int bool.false2 no &&
	git config --bool-or-int int.int1 0 &&
	git config --bool-or-int int.int2 1 &&
	git config --bool-or-int int.int3 -1 &&
	test_cmp expect .git/config
'

rm .git/config

cat >expect <<\EOF
[path]
	home = ~/
	normal = /dev/null
	trailingtilde = foo~
EOF

test_expect_success NOT_MINGW 'set --path' '
	git config --path path.home "~/" &&
	git config --path path.normal "/dev/null" &&
	git config --path path.trailingtilde "foo~" &&
	test_cmp expect .git/config'

if test_have_prereq NOT_MINGW && test "${HOME+set}"
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

test_expect_success NOT_MINGW 'get --path copes with unset $HOME' '
	(
		unset HOME;
		test_must_fail git config --get --path path.home \
			>result 2>msg &&
		git config --get --path path.normal >>result &&
		git config --get --path path.trailingtilde >>result
	) &&
	grep "[Ff]ailed to expand.*~/" msg &&
	test_cmp expect result
'

rm .git/config

git config quote.leading " test"
git config quote.ending "test "
git config quote.semicolon "test;test"
git config quote.hash "test#test"

cat > expect << EOF
[quote]
	leading = " test"
	ending = "test "
	semicolon = "test;test"
	hash = "test#test"
EOF

test_expect_success 'quoting' 'cmp .git/config expect'

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

git config --list > result

test_expect_success 'value continued on next line' 'cmp result expect'

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

git config --null --list | perl -pe 'y/\000/Q/' > result
echo >>result

test_expect_success '--null --list' 'cmp result expect'

git config --null --get-regexp 'val[0-9]' | perl -pe 'y/\000/Q/' > result
echo >>result

test_expect_success '--null --get-regexp' 'cmp result expect'

test_expect_success 'inner whitespace kept verbatim' '
	git config section.val "foo 	  bar" &&
	test "z$(git config section.val)" = "zfoo 	  bar"
'

test_expect_success SYMLINKS 'symlinked configuration' '

	ln -s notyet myconfig &&
	GIT_CONFIG=myconfig git config test.frotz nitfol &&
	test -h myconfig &&
	test -f notyet &&
	test "z$(GIT_CONFIG=notyet git config test.frotz)" = znitfol &&
	GIT_CONFIG=myconfig git config test.xyzzy rezrov &&
	test -h myconfig &&
	test -f notyet &&
	test "z$(GIT_CONFIG=notyet git config test.frotz)" = znitfol &&
	test "z$(GIT_CONFIG=notyet git config test.xyzzy)" = zrezrov

'

test_expect_success 'nonexistent configuration' '
	(
		GIT_CONFIG=doesnotexist &&
		export GIT_CONFIG &&
		test_must_fail git config --list &&
		test_must_fail git config test.xyzzy
	)
'

test_expect_success SYMLINKS 'symlink to nonexistent configuration' '
	ln -s doesnotexist linktonada &&
	ln -s linktonada linktolinktonada &&
	(
		GIT_CONFIG=linktonada &&
		export GIT_CONFIG &&
		test_must_fail git config --list &&
		GIT_CONFIG=linktolinktonada &&
		test_must_fail git config --list
	)
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
	test "z$(git -c core.name=value config core.name)" = zvalue &&
	test "z$(git -c foo.CamelCase=value config foo.camelcase)" = zvalue &&
	test "z$(git -c foo.flag config --bool foo.flag)" = ztrue &&
	test_must_fail git -c name=value config core.name
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

test_done
