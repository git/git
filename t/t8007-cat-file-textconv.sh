#!/bin/sh

test_description='but cat-file textconv support'
. ./test-lib.sh

cat >helper <<'EOF'
#!/bin/sh
grep -q '^bin: ' "$1" || { echo "E: $1 is not \"binary\" file" 1>&2; exit 1; }
sed 's/^bin: /converted: /' "$1"
EOF
chmod +x helper

test_expect_success 'setup ' '
	echo "bin: test" >one.bin &&
	test_ln_s_add one.bin symlink.bin &&
	but add . &&
	GIT_AUTHOR_NAME=Number1 but cummit -a -m First --date="2010-01-01 18:00:00" &&
	echo "bin: test version 2" >one.bin &&
	GIT_AUTHOR_NAME=Number2 but cummit -a -m Second --date="2010-01-01 20:00:00"
'

test_expect_success 'usage: <bad rev>' '
	cat >expect <<-\EOF &&
	fatal: Not a valid object name HEAD2
	EOF
	test_must_fail but cat-file --textconv HEAD2 2>actual &&
	test_cmp expect actual
'

test_expect_success 'usage: <bad rev>:<bad path>' '
	cat >expect <<-\EOF &&
	fatal: invalid object name '\''HEAD2'\''.
	EOF
	test_must_fail but cat-file --textconv HEAD2:two.bin 2>actual &&
	test_cmp expect actual
'

test_expect_success 'usage: <rev>:<bad path>' '
	cat >expect <<-\EOF &&
	fatal: path '\''two.bin'\'' does not exist in '\''HEAD'\''
	EOF
	test_must_fail but cat-file --textconv HEAD:two.bin 2>actual &&
	test_cmp expect actual
'


test_expect_success 'usage: <rev> with no <path>' '
	cat >expect <<-\EOF &&
	fatal: <object>:<path> required, only <object> '\''HEAD'\'' given
	EOF
	test_must_fail but cat-file --textconv HEAD 2>actual &&
	test_cmp expect actual
'


test_expect_success 'usage: <bad rev>:<good (in HEAD) path>' '
	cat >expect <<-\EOF &&
	fatal: invalid object name '\''HEAD2'\''.
	EOF
	test_must_fail but cat-file --textconv HEAD2:one.bin 2>actual &&
	test_cmp expect actual
'

cat >expected <<EOF
bin: test version 2
EOF

test_expect_success 'no filter specified' '
	but cat-file --textconv :one.bin >result &&
	test_cmp expected result
'

test_expect_success 'setup textconv filters' '
	echo "*.bin diff=test" >.butattributes &&
	but config diff.test.textconv ./helper &&
	but config diff.test.cachetextconv false
'

test_expect_success 'cat-file without --textconv' '
	but cat-file blob :one.bin >result &&
	test_cmp expected result
'

cat >expected <<EOF
bin: test
EOF

test_expect_success 'cat-file without --textconv on previous cummit' '
	but cat-file -p HEAD^:one.bin >result &&
	test_cmp expected result
'

cat >expected <<EOF
converted: test version 2
EOF

test_expect_success 'cat-file --textconv on last cummit' '
	but cat-file --textconv :one.bin >result &&
	test_cmp expected result
'

cat >expected <<EOF
converted: test
EOF

test_expect_success 'cat-file --textconv on previous cummit' '
	but cat-file --textconv HEAD^:one.bin >result &&
	test_cmp expected result
'

test_expect_success 'cat-file without --textconv (symlink)' '
	printf "%s" "one.bin" >expected &&
	but cat-file blob :symlink.bin >result &&
	test_cmp expected result
'


test_expect_success 'cat-file --textconv on index (symlink)' '
	but cat-file --textconv :symlink.bin >result &&
	test_cmp expected result
'

test_expect_success 'cat-file --textconv on HEAD (symlink)' '
	but cat-file --textconv HEAD:symlink.bin >result &&
	test_cmp expected result
'

test_done
