#!/bin/sh

test_description='git-status ignored files'

. ./test-lib.sh

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
?? untracked/
EOF

test_expect_success 'status untracked directory with --ignored' '
	echo "ignored" >.gitignore &&
	mkdir untracked &&
	: >untracked/ignored &&
	: >untracked/uncommitted &&
	git status --porcelain --ignored >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
?? untracked/uncommitted
!! untracked/ignored
EOF

test_expect_success 'status untracked directory with --ignored -u' '
	git status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
!! ignored/
EOF

test_expect_success 'status ignored directory with --ignore' '
	rm -rf untracked &&
	mkdir ignored &&
	: >ignored/uncommitted &&
	git status --porcelain --ignored >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
!! ignored/uncommitted
EOF

test_expect_success 'status ignored directory with --ignore -u' '
	git status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
!! untracked-ignored/
EOF

test_expect_success 'status untracked directory with ignored files with --ignore' '
	rm -rf ignored &&
	mkdir untracked-ignored &&
	mkdir untracked-ignored/test &&
	: >untracked-ignored/ignored &&
	: >untracked-ignored/test/ignored &&
	git status --porcelain --ignored >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
!! untracked-ignored/ignored
!! untracked-ignored/test/ignored
EOF

test_expect_success 'status untracked directory with ignored files with --ignore -u' '
	git status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
EOF

test_expect_success 'status ignored tracked directory with --ignore' '
	rm -rf untracked-ignored &&
	mkdir tracked &&
	: >tracked/committed &&
	git add tracked/committed &&
	git commit -m. &&
	echo "tracked" >.gitignore &&
	git status --porcelain --ignored >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
EOF

test_expect_success 'status ignored tracked directory with --ignore -u' '
	git status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
!! tracked/
EOF

test_expect_success 'status ignored tracked directory and uncommitted file with --ignore' '
	: >tracked/uncommitted &&
	git status --porcelain --ignored >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
!! tracked/uncommitted
EOF

test_expect_success 'status ignored tracked directory and uncommitted file with --ignore -u' '
	git status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'

test_done
