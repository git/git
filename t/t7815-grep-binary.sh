#!/bin/sh

test_description='but grep in binary files'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'setup' "
	echo 'binaryQfileQm[*]cQ*æQð' | q_to_nul >a &&
	but add a &&
	but cummit -m.
"

test_expect_success 'but grep ina a' '
	echo Binary file a matches >expect &&
	but grep ina a >actual &&
	test_cmp expect actual
'

test_expect_success 'but grep -ah ina a' '
	but grep -ah ina a >actual &&
	test_cmp a actual
'

test_expect_success 'but grep -I ina a' '
	test_must_fail but grep -I ina a >actual &&
	test_must_be_empty actual
'

test_expect_success 'but grep -c ina a' '
	echo a:1 >expect &&
	but grep -c ina a >actual &&
	test_cmp expect actual
'

test_expect_success 'but grep -l ina a' '
	echo a >expect &&
	but grep -l ina a >actual &&
	test_cmp expect actual
'

test_expect_success 'but grep -L bar a' '
	echo a >expect &&
	but grep -L bar a >actual &&
	test_cmp expect actual
'

test_expect_success 'but grep -q ina a' '
	but grep -q ina a >actual &&
	test_must_be_empty actual
'

test_expect_success 'but grep -F ile a' '
	but grep -F ile a
'

test_expect_success 'but grep -Fi iLE a' '
	but grep -Fi iLE a
'

# This test actually passes on platforms where regexec() supports the
# flag REG_STARTEND.
test_expect_success 'but grep ile a' '
	but grep ile a
'

test_expect_failure 'but grep .fi a' '
	but grep .fi a
'

test_expect_success 'grep respects binary diff attribute' '
	echo text >t &&
	but add t &&
	echo t:text >expect &&
	but grep text t >actual &&
	test_cmp expect actual &&
	echo "t -diff" >.butattributes &&
	echo "Binary file t matches" >expect &&
	but grep text t >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --cached respects binary diff attribute' '
	but grep --cached text t >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --cached respects binary diff attribute (2)' '
	but add .butattributes &&
	rm .butattributes &&
	but grep --cached text t >actual &&
	test_when_finished "but rm --cached .butattributes" &&
	test_when_finished "but checkout .butattributes" &&
	test_cmp expect actual
'

test_expect_success 'grep revision respects binary diff attribute' '
	but cummit -m new &&
	echo "Binary file HEAD:t matches" >expect &&
	but grep text HEAD -- t >actual &&
	test_when_finished "but reset HEAD^" &&
	test_cmp expect actual
'

test_expect_success 'grep respects not-binary diff attribute' '
	echo binQary | q_to_nul >b &&
	but add b &&
	echo "Binary file b matches" >expect &&
	but grep bin b >actual &&
	test_cmp expect actual &&
	echo "b diff" >.butattributes &&
	echo "b:binQary" >expect &&
	but grep bin b >actual.raw &&
	nul_to_q <actual.raw >actual &&
	test_cmp expect actual
'

cat >nul_to_q_textconv <<'EOF'
#!/bin/sh
"$PERL_PATH" -pe 'y/\000/Q/' < "$1"
EOF
chmod +x nul_to_q_textconv

test_expect_success 'setup textconv filters' '
	echo a diff=foo >.butattributes &&
	but config diff.foo.textconv "\"$(pwd)\""/nul_to_q_textconv
'

test_expect_success 'grep does not honor textconv' '
	test_must_fail but grep Qfile
'

test_expect_success 'grep --textconv honors textconv' '
	echo "a:binaryQfileQm[*]cQ*æQð" >expect &&
	but grep --textconv Qfile >actual &&
	test_cmp expect actual
'

test_expect_success 'grep --no-textconv does not honor textconv' '
	test_must_fail but grep --no-textconv Qfile
'

test_expect_success 'grep --textconv blob honors textconv' '
	echo "HEAD:a:binaryQfileQm[*]cQ*æQð" >expect &&
	but grep --textconv Qfile HEAD:a >actual &&
	test_cmp expect actual
'

test_done
