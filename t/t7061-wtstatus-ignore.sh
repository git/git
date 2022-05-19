#!/bin/sh

test_description='but-status ignored files'

. ./test-lib.sh

cat >expected <<\EOF
?? .butignore
?? actual
?? expected
?? untracked/
!! untracked/ignored
EOF

test_expect_success 'status untracked directory with --ignored' '
	echo "ignored" >.butignore &&
	mkdir untracked &&
	: >untracked/ignored &&
	: >untracked/uncummitted &&
	but status --porcelain --ignored >actual &&
	test_cmp expected actual
'

test_expect_success 'same with butignore starting with BOM' '
	printf "\357\273\277ignored\n" >.butignore &&
	mkdir -p untracked &&
	: >untracked/ignored &&
	: >untracked/uncummitted &&
	but status --porcelain --ignored >actual &&
	test_cmp expected actual
'

test_expect_success 'status untracked files --ignored with pathspec (no match)' '
	but status --porcelain --ignored -- untracked/i >actual &&
	test_must_be_empty actual &&
	but status --porcelain --ignored -- untracked/u >actual &&
	test_must_be_empty actual
'

test_expect_success 'status untracked files --ignored with pathspec (literal match)' '
	but status --porcelain --ignored -- untracked/ignored >actual &&
	echo "!! untracked/ignored" >expected &&
	test_cmp expected actual &&
	but status --porcelain --ignored -- untracked/uncummitted >actual &&
	echo "?? untracked/uncummitted" >expected &&
	test_cmp expected actual
'

test_expect_success 'status untracked files --ignored with pathspec (glob match)' '
	but status --porcelain --ignored -- untracked/i\* >actual &&
	echo "!! untracked/ignored" >expected &&
	test_cmp expected actual &&
	but status --porcelain --ignored -- untracked/u\* >actual &&
	echo "?? untracked/uncummitted" >expected &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .butignore
?? actual
?? expected
?? untracked/uncummitted
!! untracked/ignored
EOF

test_expect_success 'status untracked directory with --ignored -u' '
	but status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'
cat >expected <<\EOF
?? untracked/
!! untracked/ignored
EOF

test_expect_success 'status of untracked directory with --ignored works with or without prefix' '
	but status --porcelain --ignored >tmp &&
	grep untracked/ tmp >actual &&
	rm tmp &&
	test_cmp expected actual &&

	but status --porcelain --ignored untracked/ >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? untracked/uncummitted
!! untracked/ignored
EOF

test_expect_success 'status prefixed untracked sub-directory with --ignored -u' '
	but status --porcelain --ignored -u untracked/ >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .butignore
?? actual
?? expected
!! ignored/
EOF

test_expect_success 'status ignored directory with --ignore' '
	rm -rf untracked &&
	mkdir ignored &&
	: >ignored/uncummitted &&
	but status --porcelain --ignored >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .butignore
?? actual
?? expected
!! ignored/uncummitted
EOF

test_expect_success 'status ignored directory with --ignore -u' '
	but status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .butignore
?? actual
?? expected
EOF

test_expect_success 'status empty untracked directory with --ignore' '
	rm -rf ignored &&
	mkdir untracked-ignored &&
	mkdir untracked-ignored/test &&
	but status --porcelain --ignored >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .butignore
?? actual
?? expected
EOF

test_expect_success 'status empty untracked directory with --ignore -u' '
	but status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .butignore
?? actual
?? expected
!! untracked-ignored/
EOF

test_expect_success 'status untracked directory with ignored files with --ignore' '
	: >untracked-ignored/ignored &&
	: >untracked-ignored/test/ignored &&
	but status --porcelain --ignored >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .butignore
?? actual
?? expected
!! untracked-ignored/ignored
!! untracked-ignored/test/ignored
EOF

test_expect_success 'status untracked directory with ignored files with --ignore -u' '
	but status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .butignore
?? actual
?? expected
EOF

test_expect_success 'status ignored tracked directory with --ignore' '
	rm -rf untracked-ignored &&
	mkdir tracked &&
	: >tracked/cummitted &&
	but add tracked/cummitted &&
	but cummit -m. &&
	echo "tracked" >.butignore &&
	but status --porcelain --ignored >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .butignore
?? actual
?? expected
EOF

test_expect_success 'status ignored tracked directory with --ignore -u' '
	but status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .butignore
?? actual
?? expected
EOF

test_expect_success 'status ignored tracked directory and ignored file with --ignore' '
	echo "cummitted" >>.butignore &&
	but status --porcelain --ignored >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .butignore
?? actual
?? expected
EOF

test_expect_success 'status ignored tracked directory and ignored file with --ignore -u' '
	but status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .butignore
?? actual
?? expected
!! tracked/uncummitted
EOF

test_expect_success 'status ignored tracked directory and uncummitted file with --ignore' '
	echo "tracked" >.butignore &&
	: >tracked/uncummitted &&
	but status --porcelain --ignored >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .butignore
?? actual
?? expected
!! tracked/uncummitted
EOF

test_expect_success 'status ignored tracked directory and uncummitted file with --ignore -u' '
	but status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .butignore
?? actual
?? expected
!! tracked/ignored/
EOF

test_expect_success 'status ignored tracked directory with uncummitted file in untracked subdir with --ignore' '
	rm -rf tracked/uncummitted &&
	mkdir tracked/ignored &&
	: >tracked/ignored/uncummitted &&
	but status --porcelain --ignored >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .butignore
?? actual
?? expected
!! tracked/ignored/uncummitted
EOF

test_expect_success 'status ignored tracked directory with uncummitted file in untracked subdir with --ignore -u' '
	but status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .butignore
?? actual
?? expected
!! tracked/ignored/uncummitted
EOF

test_expect_success 'status ignored tracked directory with uncummitted file in tracked subdir with --ignore' '
	: >tracked/ignored/cummitted &&
	but add -f tracked/ignored/cummitted &&
	but cummit -m. &&
	but status --porcelain --ignored >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
?? .butignore
?? actual
?? expected
!! tracked/ignored/uncummitted
EOF

test_expect_success 'status ignored tracked directory with uncummitted file in tracked subdir with --ignore -u' '
	but status --porcelain --ignored -u >actual &&
	test_cmp expected actual
'

cat >expected <<\EOF
!! tracked/submodule/
EOF

test_expect_success 'status ignores submodule in excluded directory' '
	but init tracked/submodule &&
	test_cummit -C tracked/submodule initial &&
	but status --porcelain --ignored -u tracked/submodule >actual &&
	test_cmp expected actual
'

test_done
