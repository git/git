#!/bin/sh

test_description='word diff colors'

. ./test-lib.sh

test_expect_success setup '

	git config diff.color.old red
	git config diff.color.new green

'

decrypt_color () {
	sed \
		-e 's/.\[1m/<WHITE>/g' \
		-e 's/.\[31m/<RED>/g' \
		-e 's/.\[32m/<GREEN>/g' \
		-e 's/.\[36m/<BROWN>/g' \
		-e 's/.\[m/<RESET>/g'
}

word_diff () {
	test_must_fail git diff --no-index "$@" pre post > output &&
	decrypt_color < output > output.decrypted &&
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
<BROWN>@@ -1,3 +1,7 @@<RESET>
<RED>h(4)<RESET><GREEN>h(4),hh[44]<RESET>
<RESET>
a = b + c<RESET>

<GREEN>aa = a<RESET>

<GREEN>aeff = aeff * ( aaa )<RESET>
EOF

test_expect_success 'word diff with runs of whitespace' '

	word_diff --color-words

'

cat > expect <<\EOF
<WHITE>diff --git a/pre b/post<RESET>
<WHITE>index 330b04f..5ed8eff 100644<RESET>
<WHITE>--- a/pre<RESET>
<WHITE>+++ b/post<RESET>
<BROWN>@@ -1,3 +1,7 @@<RESET>
h(4),<GREEN>hh<RESET>[44]
<RESET>
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
<BROWN>@@ -1,3 +1,7 @@<RESET>
h(4)<GREEN>,hh[44]<RESET>
<RESET>
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
<BROWN>@@ -1,3 +1,7 @@<RESET>
h(4),<GREEN>hh[44<RESET>]
<RESET>
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
<BROWN>@@ -1 +1 @@<RESET>
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
<BROWN>@@ -1 +1 @@<RESET>
(<RED>:<RESET>
EOF

test_expect_success 'test when words are only removed at the end' '

	word_diff --color-words=.

'

test_done
