#!/bin/sh
#
# Copyright (c) 2005 Johannes Schindelin
#

test_description='Test git-repo-config in different settings'

. ./test-lib.sh

test -f .git/config && rm .git/config

git-repo-config core.penguin "little blue"

cat > expect << EOF
[core]
	penguin = little blue
EOF

test_expect_success 'initial' 'cmp .git/config expect'

git-repo-config Core.Movie BadPhysics

cat > expect << EOF
[core]
	penguin = little blue
	Movie = BadPhysics
EOF

test_expect_success 'mixed case' 'cmp .git/config expect'

git-repo-config Cores.WhatEver Second

cat > expect << EOF
[core]
	penguin = little blue
	Movie = BadPhysics
[Cores]
	WhatEver = Second
EOF

test_expect_success 'similar section' 'cmp .git/config expect'

git-repo-config CORE.UPPERCASE true

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
	'git-repo-config core.penguin kingpin !blue'

test_expect_success 'replace with non-match (actually matching)' \
	'git-repo-config core.penguin "very blue" !kingpin'

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
	'git-repo-config --unset-all beta.haha'

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
	'git-repo-config --replace-all beta.haha gamma'

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
	haha = gamma
[nextSection] noNewline = ouch
EOF

test_expect_success 'all replaced' 'cmp .git/config expect'

git-repo-config beta.haha alpha

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
	haha = alpha
[nextSection] noNewline = ouch
EOF

test_expect_success 'really mean test' 'cmp .git/config expect'

git-repo-config nextsection.nonewline wow

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

test_expect_success 'get value' 'test alpha = $(git-repo-config beta.haha)'
git-repo-config --unset beta.haha

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
[nextSection]
	nonewline = wow
EOF

test_expect_success 'unset' 'cmp .git/config expect'

git-repo-config nextsection.NoNewLine "wow2 for me" "for me$"

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
	'git-repo-config --get nextsection.nonewline !for'

test_expect_success 'non-match value' \
	'test wow = $(git-repo-config --get nextsection.nonewline !for)'

test_expect_failure 'ambiguous get' \
	'git-repo-config --get nextsection.nonewline'

test_expect_success 'get multivar' \
	'git-repo-config --get-all nextsection.nonewline'

git-repo-config nextsection.nonewline "wow3" "wow$"

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

test_expect_failure 'ambiguous value' 'git-repo-config nextsection.nonewline'

test_expect_failure 'ambiguous unset' \
	'git-repo-config --unset nextsection.nonewline'

test_expect_failure 'invalid unset' \
	'git-repo-config --unset somesection.nonewline'

git-repo-config --unset nextsection.nonewline "wow3$"

cat > expect << EOF
[beta] ; silly comment # another comment
noIndent= sillyValue ; 'nother silly comment

# empty line
		; comment
[nextSection]
	NoNewLine = wow2 for me
EOF

test_expect_success 'multivar unset' 'cmp .git/config expect'

test_expect_failure 'invalid key' 'git-repo-config inval.2key blabla'

test_expect_success 'correct key' 'git-repo-config 123456.a123 987'

test_expect_success 'hierarchical section' \
	'git-repo-config Version.1.2.3eX.Alpha beta'

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
	'git-repo-config --list > output && cmp output expect'

cat > expect << EOF
beta.noindent sillyValue
nextsection.nonewline wow2 for me
EOF

test_expect_success '--get-regexp' \
	'git-repo-config --get-regexp in > output && cmp output expect'

git-repo-config --add nextsection.nonewline "wow4 for you"

cat > expect << EOF
wow2 for me
wow4 for you
EOF

test_expect_success '--add' \
	'git-repo-config --get-all nextsection.nonewline > output && cmp output expect'

cat > .git/config << EOF
[novalue]
	variable
EOF

test_expect_success 'get variable with no value' \
	'git-repo-config --get novalue.variable ^$'

git-repo-config > output 2>&1

test_expect_success 'no arguments, but no crash' \
	"test $? = 129 && grep usage output"

cat > .git/config << EOF
[a.b]
	c = d
EOF

git-repo-config a.x y

cat > expect << EOF
[a.b]
	c = d
[a]
	x = y
EOF

test_expect_success 'new section is partial match of another' 'cmp .git/config expect'

git-repo-config b.x y
git-repo-config a.b c

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

cat > other-config << EOF
[ein]
	bahn = strasse
EOF

cat > expect << EOF
ein.bahn=strasse
EOF

GIT_CONFIG=other-config git-repo-config -l > output

test_expect_success 'alternative GIT_CONFIG' 'cmp output expect'

GIT_CONFIG=other-config git-repo-config anwohner.park ausweis

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
	"git-repo-config --rename-section branch.eins branch.zwei"

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

test_expect_success "rename succeeded" "diff -u expect .git/config"

test_expect_failure "rename non-existing section" \
	'git-repo-config --rename-section branch."world domination" branch.drei'

test_expect_success "rename succeeded" "diff -u expect .git/config"

test_expect_success "rename another section" \
	'git-repo-config --rename-section branch."1 234 blabl/a" branch.drei'

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

test_expect_success "rename succeeded" "diff -u expect .git/config"

test_expect_success numbers '

	git-repo-config kilo.gram 1k &&
	git-repo-config mega.ton 1m &&
	k=$(git-repo-config --int --get kilo.gram) &&
	test z1024 = "z$k" &&
	m=$(git-repo-config --int --get mega.ton) &&
	test z1048576 = "z$m"
'

rm .git/config

git-repo-config quote.leading " test"
git-repo-config quote.ending "test "
git-repo-config quote.semicolon "test;test"
git-repo-config quote.hash "test#test"

cat > expect << EOF
[quote]
	leading = " test"
	ending = "test "
	semicolon = "test;test"
	hash = "test#test"
EOF

test_expect_success 'quoting' 'cmp .git/config expect'

test_done

