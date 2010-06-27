#!/bin/sh

test_description='git cat-file textconv support'
. ./test-lib.sh

cat >helper <<'EOF'
#!/bin/sh
sed 's/^/converted: /' "$@"
EOF
chmod +x helper

test_expect_success 'setup ' '
	echo test >one.bin &&
	git add . &&
	GIT_AUTHOR_NAME=Number1 git commit -a -m First --date="2010-01-01 18:00:00" &&
	echo test version 2 >one.bin &&
	GIT_AUTHOR_NAME=Number2 git commit -a -m Second --date="2010-01-01 20:00:00"
'

cat >expected <<EOF
fatal: git cat-file --textconv: unable to run textconv on :one.bin
EOF

test_expect_success 'no filter specified' '
	git cat-file --textconv :one.bin 2>result
	test_cmp expected result
'

test_expect_success 'setup textconv filters' '
	echo "*.bin diff=test" >.gitattributes &&
	git config diff.test.textconv ./helper &&
	git config diff.test.cachetextconv false
'

cat >expected <<EOF
test version 2
EOF

test_expect_success 'cat-file without --textconv' '
	git cat-file blob :one.bin >result &&
	test_cmp expected result
'

cat >expected <<EOF
test
EOF

test_expect_success 'cat-file without --textconv on previous commit' '
	git cat-file -p HEAD^:one.bin >result &&
	test_cmp expected result
'

cat >expected <<EOF
converted: test version 2
EOF

test_expect_success 'cat-file --textconv on last commit' '
	git cat-file --textconv :one.bin >result &&
	test_cmp expected result
'

cat >expected <<EOF
converted: test
EOF

test_expect_success 'cat-file --textconv on previous commit' '
	git cat-file --textconv HEAD^:one.bin >result &&
	test_cmp expected result
'
test_done
