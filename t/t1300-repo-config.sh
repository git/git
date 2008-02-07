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

mv .git/config2 .git/config

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
	! git config --get nextsection.nonewline
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
	! git config nextsection.nonewline
'

test_expect_success 'ambiguous unset' '
	! git config --unset nextsection.nonewline
'

test_expect_success 'invalid unset' '
	! git config --unset somesection.nonewline
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

test_expect_success 'invalid key' '! git config inval.2key blabla'

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

git config > output 2>&1

test_expect_success 'no arguments, but no crash' \
	"test $? = 129 && grep usage output"

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
	'git config --file non-existing-config -l; test $? != 0'

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

test_expect_success "rename succeeded" "git diff expect .git/config"

test_expect_success "rename non-existing section" '
	! git config --rename-section branch."world domination" branch.drei
'

test_expect_success "rename succeeded" "git diff expect .git/config"

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

test_expect_success "rename succeeded" "git diff expect .git/config"

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
	"git diff -u expect .git/config"

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
	! git config --bool --get bool.nobool'

test_expect_success 'invalid bool (set)' '

	! git config --bool bool.nobool foobar'

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
	! git config "key.with
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

git config --null --list | tr '\000' 'Q' > result
echo >>result

test_expect_success '--null --list' 'cmp result expect'

git config --null --get-regexp 'val[0-9]' | tr '\000' 'Q' > result
echo >>result

test_expect_success '--null --get-regexp' 'cmp result expect'

test_expect_success 'symlinked configuration' '

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

test_done
