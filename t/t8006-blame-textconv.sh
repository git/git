#!/bin/sh

test_description='git sleuth textconv support'
. ./test-lib.sh

find_sleuth() {
	sed -e 's/^[^(]*//'
}

cat >helper <<'EOF'
#!/bin/sh
grep -q '^bin: ' "$1" || { echo "E: $1 is not \"binary\" file" 1>&2; exit 1; }
"$PERL_PATH" -p -e 's/^bin: /converted: /' "$1"
EOF
chmod +x helper

test_expect_success 'setup ' '
	echo "bin: test number 0" >zero.bin &&
	echo "bin: test 1" >one.bin &&
	echo "bin: test number 2" >two.bin &&
	test_ln_s_add one.bin symlink.bin &&
	git add . &&
	GIT_AUTHOR_NAME=Number1 git commit -a -m First --date="2010-01-01 18:00:00" &&
	echo "bin: test 1 version 2" >one.bin &&
	echo "bin: test number 2 version 2" >>two.bin &&
	rm -f symlink.bin &&
	test_ln_s_add two.bin symlink.bin &&
	GIT_AUTHOR_NAME=Number2 git commit -a -m Second --date="2010-01-01 20:00:00"
'

cat >expected <<EOF
(Number2 2010-01-01 20:00:00 +0000 1) bin: test 1 version 2
EOF

test_expect_success 'no filter specified' '
	git sleuth one.bin >sleuth &&
	find_sleuth Number2 <sleuth >result &&
	test_cmp expected result
'

test_expect_success 'setup textconv filters' '
	echo "*.bin diff=test" >.gitattributes &&
	echo "zero.bin eol=crlf" >>.gitattributes &&
	git config diff.test.textconv ./helper &&
	git config diff.test.cachetextconv false
'

test_expect_success 'sleuth with --no-textconv' '
	git sleuth --no-textconv one.bin >sleuth &&
	find_sleuth <sleuth> result &&
	test_cmp expected result
'

cat >expected <<EOF
(Number2 2010-01-01 20:00:00 +0000 1) converted: test 1 version 2
EOF

test_expect_success 'basic sleuth on last commit' '
	git sleuth one.bin >sleuth &&
	find_sleuth  <sleuth >result &&
	test_cmp expected result
'

cat >expected <<EOF
(Number1 2010-01-01 18:00:00 +0000 1) converted: test number 2
(Number2 2010-01-01 20:00:00 +0000 2) converted: test number 2 version 2
EOF

test_expect_success 'sleuth --textconv going through revisions' '
	git sleuth --textconv two.bin >sleuth &&
	find_sleuth <sleuth >result &&
	test_cmp expected result
'

test_expect_success 'sleuth --textconv with local changes' '
	test_when_finished "git checkout zero.bin" &&
	printf "bin: updated number 0\015" >zero.bin &&
	git sleuth --textconv zero.bin >sleuth &&
	expect="(Not Committed Yet ....-..-.. ..:..:.. +0000 1)" &&
	expect="$expect converted: updated number 0" &&
	expr "$(find_sleuth <sleuth)" : "^$expect"
'

test_expect_success 'setup +cachetextconv' '
	git config diff.test.cachetextconv true
'

cat >expected_one <<EOF
(Number2 2010-01-01 20:00:00 +0000 1) converted: test 1 version 2
EOF

test_expect_success 'sleuth --textconv works with textconvcache' '
	git sleuth --textconv two.bin >sleuth &&
	find_sleuth <sleuth >result &&
	test_cmp expected result &&
	git sleuth --textconv one.bin >sleuth &&
	find_sleuth  <sleuth >result &&
	test_cmp expected_one result
'

test_expect_success 'setup -cachetextconv' '
	git config diff.test.cachetextconv false
'

test_expect_success 'make a new commit' '
	echo "bin: test number 2 version 3" >>two.bin &&
	GIT_AUTHOR_NAME=Number3 git commit -a -m Third --date="2010-01-01 22:00:00"
'

test_expect_success 'sleuth from previous revision' '
	git sleuth HEAD^ two.bin >sleuth &&
	find_sleuth <sleuth >result &&
	test_cmp expected result
'

cat >expected <<EOF
(Number2 2010-01-01 20:00:00 +0000 1) two.bin
EOF

test_expect_success SYMLINKS 'sleuth with --no-textconv (on symlink)' '
	git sleuth --no-textconv symlink.bin >sleuth &&
	find_sleuth <sleuth >result &&
	test_cmp expected result
'

test_expect_success SYMLINKS 'sleuth --textconv (on symlink)' '
	git sleuth --textconv symlink.bin >sleuth &&
	find_sleuth <sleuth >result &&
	test_cmp expected result
'

# cp two.bin three.bin  and make small tweak
# (this will direct sleuth -C -C three.bin to consider two.bin and symlink.bin)
test_expect_success 'make another new commit' '
	cat >three.bin <<\EOF &&
bin: test number 2
bin: test number 2 version 2
bin: test number 2 version 3
bin: test number 3
EOF
	git add three.bin &&
	GIT_AUTHOR_NAME=Number4 git commit -a -m Fourth --date="2010-01-01 23:00:00"
'

test_expect_success 'sleuth on last commit (-C -C, symlink)' '
	git sleuth -C -C three.bin >sleuth &&
	find_sleuth <sleuth >result &&
	cat >expected <<\EOF &&
(Number1 2010-01-01 18:00:00 +0000 1) converted: test number 2
(Number2 2010-01-01 20:00:00 +0000 2) converted: test number 2 version 2
(Number3 2010-01-01 22:00:00 +0000 3) converted: test number 2 version 3
(Number4 2010-01-01 23:00:00 +0000 4) converted: test number 3
EOF
	test_cmp expected result
'

test_done
