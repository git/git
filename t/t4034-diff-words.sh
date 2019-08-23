#!/bin/sh

test_description='word diff colors'

. ./test-lib.sh
. "$TEST_DIRECTORY"/diff-lib.sh

cat >pre.simple <<-\EOF
	h(4)

	a = b + c
EOF
cat >post.simple <<-\EOF
	h(4),hh[44]

	a = b + c

	aa = a

	aeff = aeff * ( aaa )
EOF
cat >expect.letter-runs-are-words <<-\EOF
	<BOLD>diff --git a/pre b/post<RESET>
	<BOLD>index 330b04f..5ed8eff 100644<RESET>
	<BOLD>--- a/pre<RESET>
	<BOLD>+++ b/post<RESET>
	<CYAN>@@ -1,3 +1,7 @@<RESET>
	h(4),<GREEN>hh<RESET>[44]

	a = b + c<RESET>

	<GREEN>aa = a<RESET>

	<GREEN>aeff = aeff * ( aaa<RESET> )
EOF
cat >expect.non-whitespace-is-word <<-\EOF
	<BOLD>diff --git a/pre b/post<RESET>
	<BOLD>index 330b04f..5ed8eff 100644<RESET>
	<BOLD>--- a/pre<RESET>
	<BOLD>+++ b/post<RESET>
	<CYAN>@@ -1,3 +1,7 @@<RESET>
	h(4)<GREEN>,hh[44]<RESET>

	a = b + c<RESET>

	<GREEN>aa = a<RESET>

	<GREEN>aeff = aeff * ( aaa )<RESET>
EOF

word_diff () {
	test_must_fail git diff --no-index "$@" pre post >output &&
	test_decode_color <output >output.decrypted &&
	test_cmp expect output.decrypted
}

test_language_driver () {
	lang=$1
	test_expect_success "diff driver '$lang'" '
		cp "$TEST_DIRECTORY/t4034/'"$lang"'/pre" \
			"$TEST_DIRECTORY/t4034/'"$lang"'/post" \
			"$TEST_DIRECTORY/t4034/'"$lang"'/expect" . &&
		echo "* diff='"$lang"'" >.gitattributes &&
		word_diff --color-words
	'
}

test_expect_success setup '
	git config diff.color.old red &&
	git config diff.color.new green &&
	git config diff.color.func magenta
'

test_expect_success 'set up pre and post with runs of whitespace' '
	cp pre.simple pre &&
	cp post.simple post
'

test_expect_success 'word diff with runs of whitespace' '
	cat >expect <<-\EOF &&
		<BOLD>diff --git a/pre b/post<RESET>
		<BOLD>index 330b04f..5ed8eff 100644<RESET>
		<BOLD>--- a/pre<RESET>
		<BOLD>+++ b/post<RESET>
		<CYAN>@@ -1,3 +1,7 @@<RESET>
		<RED>h(4)<RESET><GREEN>h(4),hh[44]<RESET>

		a = b + c<RESET>

		<GREEN>aa = a<RESET>

		<GREEN>aeff = aeff * ( aaa )<RESET>
	EOF
	word_diff --color-words &&
	word_diff --word-diff=color &&
	word_diff --color --word-diff=color
'

test_expect_success '--word-diff=porcelain' '
	sed 's/#.*$//' >expect <<-\EOF &&
		diff --git a/pre b/post
		index 330b04f..5ed8eff 100644
		--- a/pre
		+++ b/post
		@@ -1,3 +1,7 @@
		-h(4)
		+h(4),hh[44]
		~
		 # significant space
		~
		 a = b + c
		~
		~
		+aa = a
		~
		~
		+aeff = aeff * ( aaa )
		~
	EOF
	word_diff --word-diff=porcelain
'

test_expect_success '--word-diff=plain' '
	cat >expect <<-\EOF &&
		diff --git a/pre b/post
		index 330b04f..5ed8eff 100644
		--- a/pre
		+++ b/post
		@@ -1,3 +1,7 @@
		[-h(4)-]{+h(4),hh[44]+}

		a = b + c

		{+aa = a+}

		{+aeff = aeff * ( aaa )+}
	EOF
	word_diff --word-diff=plain &&
	word_diff --word-diff=plain --no-color
'

test_expect_success '--word-diff=plain --color' '
	cat >expect <<-\EOF &&
		<BOLD>diff --git a/pre b/post<RESET>
		<BOLD>index 330b04f..5ed8eff 100644<RESET>
		<BOLD>--- a/pre<RESET>
		<BOLD>+++ b/post<RESET>
		<CYAN>@@ -1,3 +1,7 @@<RESET>
		<RED>[-h(4)-]<RESET><GREEN>{+h(4),hh[44]+}<RESET>

		a = b + c<RESET>

		<GREEN>{+aa = a+}<RESET>

		<GREEN>{+aeff = aeff * ( aaa )+}<RESET>
	EOF
	word_diff --word-diff=plain --color
'

test_expect_success 'word diff without context' '
	cat >expect <<-\EOF &&
		<BOLD>diff --git a/pre b/post<RESET>
		<BOLD>index 330b04f..5ed8eff 100644<RESET>
		<BOLD>--- a/pre<RESET>
		<BOLD>+++ b/post<RESET>
		<CYAN>@@ -1 +1 @@<RESET>
		<RED>h(4)<RESET><GREEN>h(4),hh[44]<RESET>
		<CYAN>@@ -3,0 +4,4 @@<RESET> <RESET><MAGENTA>a = b + c<RESET>

		<GREEN>aa = a<RESET>

		<GREEN>aeff = aeff * ( aaa )<RESET>
	EOF
	word_diff --color-words --unified=0
'

test_expect_success 'word diff with a regular expression' '
	cp expect.letter-runs-are-words expect &&
	word_diff --color-words="[a-z]+"
'

test_expect_success 'set up a diff driver' '
	git config diff.testdriver.wordRegex "[^[:space:]]" &&
	cat <<-\EOF >.gitattributes
		pre diff=testdriver
		post diff=testdriver
	EOF
'

test_expect_success 'option overrides .gitattributes' '
	cp expect.letter-runs-are-words expect &&
	word_diff --color-words="[a-z]+"
'

test_expect_success 'use regex supplied by driver' '
	cp expect.non-whitespace-is-word expect &&
	word_diff --color-words
'

test_expect_success 'set up diff.wordRegex option' '
	git config diff.wordRegex "[[:alnum:]]+"
'

test_expect_success 'command-line overrides config' '
	cp expect.letter-runs-are-words expect &&
	word_diff --color-words="[a-z]+"
'

test_expect_success 'command-line overrides config: --word-diff-regex' '
	cat >expect <<-\EOF &&
		<BOLD>diff --git a/pre b/post<RESET>
		<BOLD>index 330b04f..5ed8eff 100644<RESET>
		<BOLD>--- a/pre<RESET>
		<BOLD>+++ b/post<RESET>
		<CYAN>@@ -1,3 +1,7 @@<RESET>
		h(4),<GREEN>{+hh+}<RESET>[44]

		a = b + c<RESET>

		<GREEN>{+aa = a+}<RESET>

		<GREEN>{+aeff = aeff * ( aaa+}<RESET> )
	EOF
	word_diff --color --word-diff-regex="[a-z]+"
'

test_expect_success '.gitattributes override config' '
	cp expect.non-whitespace-is-word expect &&
	word_diff --color-words
'

test_expect_success 'setup: remove diff driver regex' '
	test_unconfig diff.testdriver.wordRegex
'

test_expect_success 'use configured regex' '
	cat >expect <<-\EOF &&
		<BOLD>diff --git a/pre b/post<RESET>
		<BOLD>index 330b04f..5ed8eff 100644<RESET>
		<BOLD>--- a/pre<RESET>
		<BOLD>+++ b/post<RESET>
		<CYAN>@@ -1,3 +1,7 @@<RESET>
		h(4),<GREEN>hh[44<RESET>]

		a = b + c<RESET>

		<GREEN>aa = a<RESET>

		<GREEN>aeff = aeff * ( aaa<RESET> )
	EOF
	word_diff --color-words
'

test_expect_success 'test parsing words for newline' '
	echo "aaa (aaa)" >pre &&
	echo "aaa (aaa) aaa" >post &&
	cat >expect <<-\EOF &&
		<BOLD>diff --git a/pre b/post<RESET>
		<BOLD>index c29453b..be22f37 100644<RESET>
		<BOLD>--- a/pre<RESET>
		<BOLD>+++ b/post<RESET>
		<CYAN>@@ -1 +1 @@<RESET>
		aaa (aaa) <GREEN>aaa<RESET>
	EOF
	word_diff --color-words="a+"
'

test_expect_success 'test when words are only removed at the end' '
	echo "(:" >pre &&
	echo "(" >post &&
	cat >expect <<-\EOF &&
		<BOLD>diff --git a/pre b/post<RESET>
		<BOLD>index 289cb9d..2d06f37 100644<RESET>
		<BOLD>--- a/pre<RESET>
		<BOLD>+++ b/post<RESET>
		<CYAN>@@ -1 +1 @@<RESET>
		(<RED>:<RESET>
	EOF
	word_diff --color-words=.
'

test_expect_success '--word-diff=none' '
	echo "(:" >pre &&
	echo "(" >post &&
	cat >expect <<-\EOF &&
		diff --git a/pre b/post
		index 289cb9d..2d06f37 100644
		--- a/pre
		+++ b/post
		@@ -1 +1 @@
		-(:
		+(
	EOF
	word_diff --word-diff=plain --word-diff=none
'

test_expect_success 'unset default driver' '
	test_unconfig diff.wordregex
'

test_language_driver ada
test_language_driver bibtex
test_language_driver cpp
test_language_driver csharp
test_language_driver css
test_language_driver dts
test_language_driver fortran
test_language_driver html
test_language_driver java
test_language_driver matlab
test_language_driver objc
test_language_driver pascal
test_language_driver perl
test_language_driver php
test_language_driver python
test_language_driver ruby
test_language_driver tex

test_expect_success 'word-diff with diff.sbe' '
	cat >expect <<-\EOF &&
	diff --git a/pre b/post
	index a1a53b5..bc8fe6d 100644
	--- a/pre
	+++ b/post
	@@ -1,3 +1,3 @@
	a

	[-b-]{+c+}
	EOF
	cat >pre <<-\EOF &&
	a

	b
	EOF
	cat >post <<-\EOF &&
	a

	c
	EOF
	test_config diff.suppress-blank-empty true &&
	word_diff --word-diff=plain
'

test_expect_success 'word-diff with no newline at EOF' '
	cat >expect <<-\EOF &&
	diff --git a/pre b/post
	index 7bf316e..3dd0303 100644
	--- a/pre
	+++ b/post
	@@ -1 +1 @@
	a a [-a-]{+ab+} a a
	EOF
	printf "%s" "a a a a a" >pre &&
	printf "%s" "a a ab a a" >post &&
	word_diff --word-diff=plain
'

test_expect_success 'setup history with two files' '
	echo "a b; c" >a.tex &&
	echo "a b; c" >z.txt &&
	git add a.tex z.txt &&
	git commit -minitial &&

	# modify both
	echo "a bx; c" >a.tex &&
	echo "a bx; c" >z.txt &&
	git commit -mmodified -a
'

test_expect_success 'wordRegex for the first file does not apply to the second' '
	echo "*.tex diff=tex" >.gitattributes &&
	test_config diff.tex.wordRegex "[a-z]+|." &&
	cat >expect <<-\EOF &&
		diff --git a/a.tex b/a.tex
		--- a/a.tex
		+++ b/a.tex
		@@ -1 +1 @@
		a [-b-]{+bx+}; c
		diff --git a/z.txt b/z.txt
		--- a/z.txt
		+++ b/z.txt
		@@ -1 +1 @@
		a [-b;-]{+bx;+} c
	EOF
	git diff --word-diff HEAD~ >actual &&
	compare_diff_patch expect actual
'

test_done
