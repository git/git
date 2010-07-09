#!/bin/sh

test_description='git blame textconv support'
. ./test-lib.sh

find_blame() {
	sed -e 's/^[^(]*//'
}

cat >helper <<'EOF'
#!/bin/sh
sed 's/^/converted: /' "$@"
EOF
chmod +x helper

test_expect_success 'setup ' '
	echo test 1 >one.bin &&
	echo test number 2 >two.bin &&
	git add . &&
	GIT_AUTHOR_NAME=Number1 git commit -a -m First --date="2010-01-01 18:00:00" &&
	echo test 1 version 2 >one.bin &&
	echo test number 2 version 2 >>two.bin &&
	GIT_AUTHOR_NAME=Number2 git commit -a -m Second --date="2010-01-01 20:00:00"
'

cat >expected <<EOF
(Number2 2010-01-01 20:00:00 +0000 1) test 1 version 2
EOF

test_expect_success 'no filter specified' '
	git blame one.bin >blame &&
	find_blame Number2 <blame >result &&
	test_cmp expected result
'

test_expect_success 'setup textconv filters' '
	echo "*.bin diff=test" >.gitattributes &&
	git config diff.test.textconv ./helper &&
	git config diff.test.cachetextconv false
'

test_expect_success 'blame with --no-textconv' '
	git blame --no-textconv one.bin >blame &&
	find_blame <blame> result &&
	test_cmp expected result
'

cat >expected <<EOF
(Number2 2010-01-01 20:00:00 +0000 1) converted: test 1 version 2
EOF

test_expect_success 'basic blame on last commit' '
	git blame one.bin >blame &&
	find_blame  <blame >result &&
	test_cmp expected result
'

cat >expected <<EOF
(Number1 2010-01-01 18:00:00 +0000 1) converted: test number 2
(Number2 2010-01-01 20:00:00 +0000 2) converted: test number 2 version 2
EOF

test_expect_success 'blame --textconv going through revisions' '
	git blame --textconv two.bin >blame &&
	find_blame <blame >result &&
	test_cmp expected result
'

test_expect_success 'make a new commit' '
	echo "test number 2 version 3" >>two.bin &&
	GIT_AUTHOR_NAME=Number3 git commit -a -m Third --date="2010-01-01 22:00:00"
'

test_expect_success 'blame from previous revision' '
	git blame HEAD^ two.bin >blame &&
	find_blame <blame >result &&
	test_cmp expected result
'

test_done
