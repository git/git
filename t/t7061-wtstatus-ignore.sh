#!/bin/sh

test_description='git-status ignored files'

. ./test-lib.sh

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
?? untracked/
!! untracked/ignored
EOF

test_expect_success 'status untracked directory with --ignored' '
	echo "ignored" >.gitignore &&
	mkdir untracked &&
	: >untracked/ignored &&
	: >untracked/uncummitted &&
	git status --porcelain --ignored >actual &&
	test_cmp expected actual
'

test_expect_success 'same with gitignore starting with BOM' '
	printf "\357\273\277ignored\n" >.gitignore &&
	mkdir -p untracked &&
	: >untracked/ignored &&
	: >untracked/uncummitted &&
	git status --porcelain --ignored >actual &&
	test_cmp expected actual
'

test_expect_success 'status untracked files --ignored with pathspec (no match)' '
	git status --porcelain --ignored -- untracked/i >actual &&
	test_must_be_empty actual &&
	git status --porcelain --ignored -- untracked/u >actual &&
	test_must_be_empty actual
'

test_expect_success 'status untracked files --ignored with pathspec (literal match)' '
	git status --porcelain --ignored -- untracked/ignored >actual &&
	echo "!! untracked/ignored" >expected &&
	test_cmp expected actual &&
	git status --porcelain --ignored -- untracked/uncummitted >actual &&
	echo "?? untracked/uncummitted" >expected &&
	test_cmp expected actual
'

test_expect_success 'status untracked files --ignored with pathspec (glob match)' '
	git status --porcelain --ignored -- untracked/i\* >actual &&
	echo "!! untracked/ignored" >expected &&
	test_cmp expected actual &&
	git status --porcelain --ignored -- untracked/u\* >actual &&
	echo "?? untracked/uncummitted" >expected &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
?? untracked/uncummitted
!! untracked/ignored
EOF

test_expect_success 'status untracked directory with --ignored -u' '
	git status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'
cat >expected <<\EOF
?? untracked/
!! untracked/ignored
EOF

test_expect_success 'status of untracked directory with --ignored works with or without prefix' '
	git status --porcelain --ignored >tmp &&
	grep untracked/ tmp >actual &&
	rm tmp &&
	test_cmp expected actual &&

	git status --porcelain --ignored untracked/ >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? untracked/uncummitted
!! untracked/ignored
EOF

test_expect_success 'status prefixed untracked sub-directory with --ignored -u' '
	git status --porcelain --ignored -u untracked/ >actual &&
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
	: >ignored/uncummitted &&
	git status --porcelain --ignored >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
!! ignored/uncummitted
EOF

test_expect_success 'status ignored directory with --ignore -u' '
	git status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
EOF

test_expect_success 'status empty untracked directory with --ignore' '
	rm -rf ignored &&
	mkdir untracked-ignored &&
	mkdir untracked-ignored/test &&
	git status --porcelain --ignored >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
EOF

test_expect_success 'status empty untracked directory with --ignore -u' '
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
	: >tracked/cummitted &&
	git add tracked/cummitted &&
	git cummit -m. &&
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
EOF

test_expect_success 'status ignored tracked directory and ignored file with --ignore' '
	echo "cummitted" >>.gitignore &&
	git status --porcelain --ignored >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
EOF

test_expect_success 'status ignored tracked directory and ignored file with --ignore -u' '
	git status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
!! tracked/uncummitted
EOF

test_expect_success 'status ignored tracked directory and uncummitted file with --ignore' '
	echo "tracked" >.gitignore &&
	: >tracked/uncummitted &&
	git status --porcelain --ignored >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
!! tracked/uncummitted
EOF

test_expect_success 'status ignored tracked directory and uncummitted file with --ignore -u' '
	git status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
!! tracked/ignored/
EOF

test_expect_success 'status ignored tracked directory with uncummitted file in untracked subdir with --ignore' '
	rm -rf tracked/uncummitted &&
	mkdir tracked/ignored &&
	: >tracked/ignored/uncummitted &&
	git status --porcelain --ignored >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
!! tracked/ignored/uncummitted
EOF

test_expect_success 'status ignored tracked directory with uncummitted file in untracked subdir with --ignore -u' '
	git status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
!! tracked/ignored/uncummitted
EOF

test_expect_success 'status ignored tracked directory with uncummitted file in tracked subdir with --ignore' '
	: >tracked/ignored/cummitted &&
	git add -f tracked/ignored/cummitted &&
	git cummit -m. &&
	git status --porcelain --ignored >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .gitignore
?? actual
?? expected
!! tracked/ignored/uncummitted
EOF

test_expect_success 'status ignored tracked directory with uncummitted file in tracked subdir with --ignore -u' '
	git status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
!! tracked/submodule/
EOF

test_expect_success 'status ignores submodule in excluded directory' '
	git init tracked/submodule &&
	test_cummit -C tracked/submodule initial &&
	git status --porcelain --ignored -u tracked/submodule >actual &&
	test_cmp expected actual
'

test_done
