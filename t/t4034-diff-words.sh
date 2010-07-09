#!/bin/sh

test_description='word diff colors'

. ./test-lib.sh

test_expect_success setup '

	git config diff.color.old red
	git config diff.color.new green
	git config diff.color.func magenta

'

word_diff () {
	test_must_fail git diff --no-index "$@" pre post > output &&
	test_decode_color <output >output.decrypted &&
	test_cmp expect output.decrypted
}

cat > pre <<\EOF
h(4)

a = b + c
EOF

cat > post <<\EOF
h(4),hh[44]

a = b + c

aa = a

aeff = aeff * ( aaa )
EOF

cat > expect <<\EOF
<WHITE>diff --git a/pre b/post<RESET>
<WHITE>index 330b04f..5ed8eff 100644<RESET>
<WHITE>--- a/pre<RESET>
<WHITE>+++ b/post<RESET>
<CYAN>@@ -1,3 +1,7 @@<RESET>
<RED>h(4)<RESET><GREEN>h(4),hh[44]<RESET>

a = b + c<RESET>

<GREEN>aa = a<RESET>

<GREEN>aeff = aeff * ( aaa )<RESET>
EOF

test_expect_success 'word diff with runs of whitespace' '

	word_diff --color-words

'

test_expect_success '--word-diff=color' '

	word_diff --word-diff=color

'

test_expect_success '--color --word-diff=color' '

	word_diff --color --word-diff=color

'

sed 's/#.*$//' > expect <<EOF
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

test_expect_success '--word-diff=porcelain' '

	word_diff --word-diff=porcelain

'

cat > expect <<EOF
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

test_expect_success '--word-diff=plain' '

	word_diff --word-diff=plain

'

test_expect_success '--word-diff=plain --no-color' '

	word_diff --word-diff=plain --no-color

'

cat > expect <<EOF
<WHITE>diff --git a/pre b/post<RESET>
<WHITE>index 330b04f..5ed8eff 100644<RESET>
<WHITE>--- a/pre<RESET>
<WHITE>+++ b/post<RESET>
<CYAN>@@ -1,3 +1,7 @@<RESET>
<RED>[-h(4)-]<RESET><GREEN>{+h(4),hh[44]+}<RESET>

a = b + c<RESET>

<GREEN>{+aa = a+}<RESET>

<GREEN>{+aeff = aeff * ( aaa )+}<RESET>
EOF

test_expect_success '--word-diff=plain --color' '

	word_diff --word-diff=plain --color

'

cat > expect <<\EOF
<WHITE>diff --git a/pre b/post<RESET>
<WHITE>index 330b04f..5ed8eff 100644<RESET>
<WHITE>--- a/pre<RESET>
<WHITE>+++ b/post<RESET>
<CYAN>@@ -1 +1 @@<RESET>
<RED>h(4)<RESET><GREEN>h(4),hh[44]<RESET>
<CYAN>@@ -3,0 +4,4 @@<RESET> <RESET><MAGENTA>a = b + c<RESET>

<GREEN>aa = a<RESET>

<GREEN>aeff = aeff * ( aaa )<RESET>
EOF

test_expect_success 'word diff without context' '

	word_diff --color-words --unified=0

'

cat > expect <<\EOF
<WHITE>diff --git a/pre b/post<RESET>
<WHITE>index 330b04f..5ed8eff 100644<RESET>
<WHITE>--- a/pre<RESET>
<WHITE>+++ b/post<RESET>
<CYAN>@@ -1,3 +1,7 @@<RESET>
h(4),<GREEN>hh<RESET>[44]

a = b + c<RESET>

<GREEN>aa = a<RESET>

<GREEN>aeff = aeff * ( aaa<RESET> )
EOF
cp expect expect.letter-runs-are-words

test_expect_success 'word diff with a regular expression' '

	word_diff --color-words="[a-z]+"

'

test_expect_success 'set a diff driver' '
	git config diff.testdriver.wordRegex "[^[:space:]]" &&
	cat <<EOF > .gitattributes
pre diff=testdriver
post diff=testdriver
EOF
'

test_expect_success 'option overrides .gitattributes' '

	word_diff --color-words="[a-z]+"

'

cat > expect <<\EOF
<WHITE>diff --git a/pre b/post<RESET>
<WHITE>index 330b04f..5ed8eff 100644<RESET>
<WHITE>--- a/pre<RESET>
<WHITE>+++ b/post<RESET>
<CYAN>@@ -1,3 +1,7 @@<RESET>
h(4)<GREEN>,hh[44]<RESET>

a = b + c<RESET>

<GREEN>aa = a<RESET>

<GREEN>aeff = aeff * ( aaa )<RESET>
EOF
cp expect expect.non-whitespace-is-word

test_expect_success 'use regex supplied by driver' '

	word_diff --color-words

'

test_expect_success 'set diff.wordRegex option' '
	git config diff.wordRegex "[[:alnum:]]+"
'

cp expect.letter-runs-are-words expect

test_expect_success 'command-line overrides config' '
	word_diff --color-words="[a-z]+"
'

cat > expect <<\EOF
<WHITE>diff --git a/pre b/post<RESET>
<WHITE>index 330b04f..5ed8eff 100644<RESET>
<WHITE>--- a/pre<RESET>
<WHITE>+++ b/post<RESET>
<CYAN>@@ -1,3 +1,7 @@<RESET>
h(4),<GREEN>{+hh+}<RESET>[44]

a = b + c<RESET>

<GREEN>{+aa = a+}<RESET>

<GREEN>{+aeff = aeff * ( aaa+}<RESET> )
EOF

test_expect_success 'command-line overrides config: --word-diff-regex' '
	word_diff --color --word-diff-regex="[a-z]+"
'

cp expect.non-whitespace-is-word expect

test_expect_success '.gitattributes override config' '
	word_diff --color-words
'

test_expect_success 'remove diff driver regex' '
	git config --unset diff.testdriver.wordRegex
'

cat > expect <<\EOF
<WHITE>diff --git a/pre b/post<RESET>
<WHITE>index 330b04f..5ed8eff 100644<RESET>
<WHITE>--- a/pre<RESET>
<WHITE>+++ b/post<RESET>
<CYAN>@@ -1,3 +1,7 @@<RESET>
h(4),<GREEN>hh[44<RESET>]

a = b + c<RESET>

<GREEN>aa = a<RESET>

<GREEN>aeff = aeff * ( aaa<RESET> )
EOF

test_expect_success 'use configured regex' '
	word_diff --color-words
'

echo 'aaa (aaa)' > pre
echo 'aaa (aaa) aaa' > post

cat > expect <<\EOF
<WHITE>diff --git a/pre b/post<RESET>
<WHITE>index c29453b..be22f37 100644<RESET>
<WHITE>--- a/pre<RESET>
<WHITE>+++ b/post<RESET>
<CYAN>@@ -1 +1 @@<RESET>
aaa (aaa) <GREEN>aaa<RESET>
EOF

test_expect_success 'test parsing words for newline' '

	word_diff --color-words="a+"


'

echo '(:' > pre
echo '(' > post

cat > expect <<\EOF
<WHITE>diff --git a/pre b/post<RESET>
<WHITE>index 289cb9d..2d06f37 100644<RESET>
<WHITE>--- a/pre<RESET>
<WHITE>+++ b/post<RESET>
<CYAN>@@ -1 +1 @@<RESET>
(<RED>:<RESET>
EOF

test_expect_success 'test when words are only removed at the end' '

	word_diff --color-words=.

'

cat > expect <<\EOF
diff --git a/pre b/post
index 289cb9d..2d06f37 100644
--- a/pre
+++ b/post
@@ -1 +1 @@
-(:
+(
EOF

test_expect_success '--word-diff=none' '

	word_diff --word-diff=plain --word-diff=none

'

test_done
