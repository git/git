#!/bin/sh

test_description='test parsing of svndiff0 files

Using the "test-svn-fe -d" helper, check that svn-fe correctly
interprets deltas using various facilities (some from the spec,
some only learned from practice).
'
. ./test-lib.sh

>empty
printf foo >preimage

test_expect_success 'reject empty delta' '
	test_must_fail test-svn-fe -d preimage empty 0
'

test_expect_success 'delta can empty file' '
	printf "SVNQ" | q_to_nul >clear.delta &&
	test-svn-fe -d preimage clear.delta 4 >actual &&
	test_cmp empty actual
'

test_expect_success 'reject svndiff2' '
	printf "SVN\002" >bad.filetype &&
	test_must_fail test-svn-fe -d preimage bad.filetype 4
'

test_expect_success 'one-window empty delta' '
	printf "SVNQ%s" "QQQQQ" | q_to_nul >clear.onewindow &&
	test-svn-fe -d preimage clear.onewindow 9 >actual &&
	test_cmp empty actual
'

test_expect_success 'reject incomplete window header' '
	printf "SVNQ%s" "QQQQQ" | q_to_nul >clear.onewindow &&
	printf "SVNQ%s" "QQ" | q_to_nul >clear.partialwindow &&
	test_must_fail test-svn-fe -d preimage clear.onewindow 6 &&
	test_must_fail test-svn-fe -d preimage clear.partialwindow 6
'

test_expect_success 'reject declared delta longer than actual delta' '
	printf "SVNQ%s" "QQQQQ" | q_to_nul >clear.onewindow &&
	printf "SVNQ%s" "QQ" | q_to_nul >clear.partialwindow &&
	test_must_fail test-svn-fe -d preimage clear.onewindow 14 &&
	test_must_fail test-svn-fe -d preimage clear.partialwindow 9
'

test_expect_success 'two-window empty delta' '
	printf "SVNQ%s%s" "QQQQQ" "QQQQQ" | q_to_nul >clear.twowindow &&
	test-svn-fe -d preimage clear.twowindow 14 >actual &&
	test_must_fail test-svn-fe -d preimage clear.twowindow 13 &&
	test_cmp empty actual
'

test_expect_success 'noisy zeroes' '
	printf "SVNQ%s" \
		"RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRQQQQQ" |
		tr R "\200" |
		q_to_nul >clear.noisy &&
	len=$(wc -c <clear.noisy) &&
	test-svn-fe -d preimage clear.noisy $len &&
	test_cmp empty actual
'

test_expect_success 'reject variable-length int in magic' '
	printf "SVNRQ" | tr R "\200" | q_to_nul >clear.badmagic &&
	test_must_fail test-svn-fe -d preimage clear.badmagic 5
'

test_expect_success 'reject truncated integer' '
	printf "SVNQ%s%s" "QQQQQ" "QQQQRRQ" |
		tr R "\200" |
		q_to_nul >clear.fullint &&
	printf "SVNQ%s%s" "QQQQQ" "QQQQRR" |
		tr RT "\201" |
		q_to_nul >clear.partialint &&
	test_must_fail test-svn-fe -d preimage clear.fullint 15 &&
	test-svn-fe -d preimage clear.fullint 16 &&
	test_must_fail test-svn-fe -d preimage clear.partialint 15
'

test_expect_success 'nonempty (but unused) preimage view' '
	printf "SVNQ%b" "Q\003QQQ" | q_to_nul >clear.readpreimage &&
	test-svn-fe -d preimage clear.readpreimage 9 >actual &&
	test_cmp empty actual
'

test_expect_success 'preimage view: right endpoint cannot backtrack' '
	printf "SVNQ%b%b" "Q\003QQQ" "Q\002QQQ" |
		q_to_nul >clear.backtrack &&
	test_must_fail test-svn-fe -d preimage clear.backtrack 14
'

test_expect_success 'preimage view: left endpoint can advance' '
	printf "SVNQ%b%b" "Q\003QQQ" "\001\002QQQ" |
		q_to_nul >clear.preshrink &&
	printf "SVNQ%b%b" "Q\003QQQ" "\001\001QQQ" |
		q_to_nul >clear.shrinkbacktrack &&
	test-svn-fe -d preimage clear.preshrink 14 >actual &&
	test_must_fail test-svn-fe -d preimage clear.shrinkbacktrack 14 &&
	test_cmp empty actual
'

test_expect_success 'preimage view: offsets compared by value' '
	printf "SVNQ%b%b" "\001\001QQQ" "\0200Q\003QQQ" |
		q_to_nul >clear.noisybacktrack &&
	printf "SVNQ%b%b" "\001\001QQQ" "\0200\001\002QQQ" |
		q_to_nul >clear.noisyadvance &&
	test_must_fail test-svn-fe -d preimage clear.noisybacktrack 15 &&
	test-svn-fe -d preimage clear.noisyadvance 15 &&
	test_cmp empty actual
'

test_expect_success 'preimage view: reject truncated preimage' '
	printf "SVNQ%b" "\010QQQQ" | q_to_nul >clear.lateemptyread &&
	printf "SVNQ%b" "\010\001QQQ" | q_to_nul >clear.latenonemptyread &&
	printf "SVNQ%b" "\001\010QQQ" | q_to_nul >clear.longread &&
	test_must_fail test-svn-fe -d preimage clear.lateemptyread 9 &&
	test_must_fail test-svn-fe -d preimage clear.latenonemptyread 9 &&
	test_must_fail test-svn-fe -d preimage clear.longread 9
'

test_expect_success 'forbid unconsumed inline data' '
	printf "SVNQ%b%s%b%s" "QQQQ\003" "bar" "QQQQ\001" "x" |
		q_to_nul >inline.clear &&
	test_must_fail test-svn-fe -d preimage inline.clear 18 >actual
'

test_expect_success 'reject truncated inline data' '
	printf "SVNQ%b%s" "QQQQ\003" "b" | q_to_nul >inline.trunc &&
	test_must_fail test-svn-fe -d preimage inline.trunc 10
'

test_expect_success 'reject truncated inline data (after instruction section)' '
	printf "SVNQ%b%b%s" "QQ\001\001\003" "\0201" "b" | q_to_nul >insn.trunc &&
	test_must_fail test-svn-fe -d preimage insn.trunc 11
'

test_expect_success 'copyfrom_data' '
	echo hi >expect &&
	printf "SVNQ%b%b%b" "QQ\003\001\003" "\0203" "hi\n" | q_to_nul >copydat &&
	test-svn-fe -d preimage copydat 13 >actual &&
	test_cmp expect actual
'

test_expect_success 'multiple copyfrom_data' '
	echo hi >expect &&
	printf "SVNQ%b%b%b%b%b" "QQ\003\002\003" "\0201\0202" "hi\n" \
		"QQQ\002Q" "\0200Q" | q_to_nul >copy.multi &&
	len=$(wc -c <copy.multi) &&
	test-svn-fe -d preimage copy.multi $len >actual &&
	test_cmp expect actual
'

test_expect_success 'incomplete multiple insn' '
	printf "SVNQ%b%b%b" "QQ\003\002\003" "\0203\0200" "hi\n" |
		q_to_nul >copy.partial &&
	len=$(wc -c <copy.partial) &&
	test_must_fail test-svn-fe -d preimage copy.partial $len
'

test_expect_success 'catch attempt to copy missing data' '
	printf "SVNQ%b%b%s%b%s" "QQ\002\002\001" "\0201\0201" "X" \
			"QQQQ\002" "YZ" |
		q_to_nul >copy.incomplete &&
	len=$(wc -c <copy.incomplete) &&
	test_must_fail test-svn-fe -d preimage copy.incomplete $len
'

test_expect_success 'copyfrom target to repeat data' '
	printf foofoo >expect &&
	printf "SVNQ%b%b%s" "QQ\006\004\003" "\0203\0100\003Q" "foo" |
		q_to_nul >copytarget.repeat &&
	len=$(wc -c <copytarget.repeat) &&
	test-svn-fe -d preimage copytarget.repeat $len >actual &&
	test_cmp expect actual
'

test_expect_success 'copyfrom target out of order' '
	printf foooof >expect &&
	printf "SVNQ%b%b%s" \
		"QQ\006\007\003" "\0203\0101\002\0101\001\0101Q" "foo" |
		q_to_nul >copytarget.reverse &&
	len=$(wc -c <copytarget.reverse) &&
	test-svn-fe -d preimage copytarget.reverse $len >actual &&
	test_cmp expect actual
'

test_expect_success 'catch copyfrom future' '
	printf "SVNQ%b%b%s" "QQ\004\004\003" "\0202\0101\002\0201" "XYZ" |
		q_to_nul >copytarget.infuture &&
	len=$(wc -c <copytarget.infuture) &&
	test_must_fail test-svn-fe -d preimage copytarget.infuture $len
'

test_expect_success 'copy to sustain' '
	printf XYXYXYXYXYXZ >expect &&
	printf "SVNQ%b%b%s" "QQ\014\004\003" "\0202\0111Q\0201" "XYZ" |
		q_to_nul >copytarget.sustain &&
	len=$(wc -c <copytarget.sustain) &&
	test-svn-fe -d preimage copytarget.sustain $len >actual &&
	test_cmp expect actual
'

test_expect_success 'catch copy that overflows' '
	printf "SVNQ%b%b%s" "QQ\003\003\001" "\0201\0177Q" X |
		q_to_nul >copytarget.overflow &&
	len=$(wc -c <copytarget.overflow) &&
	test_must_fail test-svn-fe -d preimage copytarget.overflow $len
'

test_expect_success 'copyfrom source' '
	printf foo >expect &&
	printf "SVNQ%b%b" "Q\003\003\002Q" "\003Q" | q_to_nul >copysource.all &&
	test-svn-fe -d preimage copysource.all 11 >actual &&
	test_cmp expect actual
'

test_expect_success 'copy backwards' '
	printf oof >expect &&
	printf "SVNQ%b%b" "Q\003\003\006Q" "\001\002\001\001\001Q" |
		q_to_nul >copysource.rev &&
	test-svn-fe -d preimage copysource.rev 15 >actual &&
	test_cmp expect actual
'

test_expect_success 'offsets are relative to window' '
	printf fo >expect &&
	printf "SVNQ%b%b%b%b" "Q\003\001\002Q" "\001Q" \
		"\002\001\001\002Q" "\001Q" |
		q_to_nul >copysource.two &&
	test-svn-fe -d preimage copysource.two 18 >actual &&
	test_cmp expect actual
'

test_expect_success 'example from notes/svndiff' '
	printf aaaaccccdddddddd >expect &&
	printf aaaabbbbcccc >source &&
	printf "SVNQ%b%b%s" "Q\014\020\007\001" \
		"\004Q\004\010\0201\0107\010" d |
		q_to_nul >delta.example &&
	len=$(wc -c <delta.example) &&
	test-svn-fe -d source delta.example $len >actual &&
	test_cmp expect actual
'

test_done
