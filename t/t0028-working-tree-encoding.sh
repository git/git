#!/bin/sh

test_description='working-tree-encoding conversion via gitattributes'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

TEST_PASSES_SANITIZE_LEAK=true
TEST_CREATE_REPO_NO_TEMPLATE=1
. ./test-lib.sh
. "$TEST_DIRECTORY/lib-encoding.sh"

GIT_TRACE_WORKING_TREE_ENCODING=1 && export GIT_TRACE_WORKING_TREE_ENCODING

test_expect_success 'setup test files' '
	git config core.eol lf &&

	text="hallo there!\ncan you read me?" &&
	echo "*.utf16 text working-tree-encoding=utf-16" >.gitattributes &&
	echo "*.utf16lebom text working-tree-encoding=UTF-16LE-BOM" >>.gitattributes &&
	printf "$text" >test.utf8.raw &&
	printf "$text" | write_utf16 >test.utf16.raw &&
	printf "$text" | write_utf32 >test.utf32.raw &&
	printf "\377\376"                         >test.utf16lebom.raw &&
	printf "$text" | iconv -f UTF-8 -t UTF-16LE >>test.utf16lebom.raw &&

	# Line ending tests
	printf "one\ntwo\nthree\n" >lf.utf8.raw &&
	printf "one\r\ntwo\r\nthree\r\n" >crlf.utf8.raw &&

	# BOM tests
	printf "\0a\0b\0c"                         >nobom.utf16be.raw &&
	printf "a\0b\0c\0"                         >nobom.utf16le.raw &&
	printf "\376\377\0a\0b\0c"                 >bebom.utf16be.raw &&
	printf "\377\376a\0b\0c\0"                 >lebom.utf16le.raw &&
	printf "\0\0\0a\0\0\0b\0\0\0c"             >nobom.utf32be.raw &&
	printf "a\0\0\0b\0\0\0c\0\0\0"             >nobom.utf32le.raw &&
	printf "\0\0\376\377\0\0\0a\0\0\0b\0\0\0c" >bebom.utf32be.raw &&
	printf "\377\376\0\0a\0\0\0b\0\0\0c\0\0\0" >lebom.utf32le.raw &&

	# Add only UTF-16 file, we will add the UTF-32 file later
	cp test.utf16.raw test.utf16 &&
	cp test.utf32.raw test.utf32 &&
	cp test.utf16lebom.raw test.utf16lebom &&
	git add .gitattributes test.utf16 test.utf16lebom &&
	git commit -m initial
'

test_expect_success 'ensure UTF-8 is stored in Git' '
	test_when_finished "rm -f test.utf16.git" &&

	git cat-file -p :test.utf16 >test.utf16.git &&
	test_cmp_bin test.utf8.raw test.utf16.git
'

test_expect_success 're-encode to UTF-16 on checkout' '
	test_when_finished "rm -f test.utf16.raw" &&

	rm test.utf16 &&
	git checkout test.utf16 &&
	test_cmp_bin test.utf16.raw test.utf16
'

test_expect_success 're-encode to UTF-16-LE-BOM on checkout' '
	rm test.utf16lebom &&
	git checkout test.utf16lebom &&
	test_cmp_bin test.utf16lebom.raw test.utf16lebom
'

test_expect_success 'check $GIT_DIR/info/attributes support' '
	test_when_finished "rm -f test.utf32.git" &&
	test_when_finished "git reset --hard HEAD" &&

	mkdir .git/info &&
	echo "*.utf32 text working-tree-encoding=utf-32" >.git/info/attributes &&
	git add test.utf32 &&

	git cat-file -p :test.utf32 >test.utf32.git &&
	test_cmp_bin test.utf8.raw test.utf32.git
'

for i in 16 32
do
	test_expect_success "check prohibited UTF-${i} BOM" '
		test_when_finished "git reset --hard HEAD" &&

		echo "*.utf${i}be text working-tree-encoding=utf-${i}be" >>.gitattributes &&
		echo "*.utf${i}le text working-tree-encoding=utf-${i}LE" >>.gitattributes &&

		# Here we add a UTF-16 (resp. UTF-32) files with BOM (big/little-endian)
		# but we tell Git to treat it as UTF-16BE/UTF-16LE (resp. UTF-32).
		# In these cases the BOM is prohibited.
		cp bebom.utf${i}be.raw bebom.utf${i}be &&
		test_must_fail git add bebom.utf${i}be 2>err.out &&
		test_grep "fatal: BOM is prohibited .* utf-${i}be" err.out &&
		test_grep "use UTF-${i} as working-tree-encoding" err.out &&

		cp lebom.utf${i}le.raw lebom.utf${i}be &&
		test_must_fail git add lebom.utf${i}be 2>err.out &&
		test_grep "fatal: BOM is prohibited .* utf-${i}be" err.out &&
		test_grep "use UTF-${i} as working-tree-encoding" err.out &&

		cp bebom.utf${i}be.raw bebom.utf${i}le &&
		test_must_fail git add bebom.utf${i}le 2>err.out &&
		test_grep "fatal: BOM is prohibited .* utf-${i}LE" err.out &&
		test_grep "use UTF-${i} as working-tree-encoding" err.out &&

		cp lebom.utf${i}le.raw lebom.utf${i}le &&
		test_must_fail git add lebom.utf${i}le 2>err.out &&
		test_grep "fatal: BOM is prohibited .* utf-${i}LE" err.out &&
		test_grep "use UTF-${i} as working-tree-encoding" err.out
	'

	test_expect_success "check required UTF-${i} BOM" '
		test_when_finished "git reset --hard HEAD" &&

		echo "*.utf${i} text working-tree-encoding=utf-${i}" >>.gitattributes &&

		cp nobom.utf${i}be.raw nobom.utf${i} &&
		test_must_fail git add nobom.utf${i} 2>err.out &&
		test_grep "fatal: BOM is required .* utf-${i}" err.out &&
		test_grep "use UTF-${i}BE or UTF-${i}LE" err.out &&

		cp nobom.utf${i}le.raw nobom.utf${i} &&
		test_must_fail git add nobom.utf${i} 2>err.out &&
		test_grep "fatal: BOM is required .* utf-${i}" err.out &&
		test_grep "use UTF-${i}BE or UTF-${i}LE" err.out
	'

	test_expect_success "eol conversion for UTF-${i} encoded files on checkout" '
		test_when_finished "rm -f crlf.utf${i}.raw lf.utf${i}.raw" &&
		test_when_finished "git reset --hard HEAD^" &&

		cat lf.utf8.raw | write_utf${i} >lf.utf${i}.raw &&
		cat crlf.utf8.raw | write_utf${i} >crlf.utf${i}.raw &&
		cp crlf.utf${i}.raw eol.utf${i} &&

		cat >expectIndexLF <<-EOF &&
			i/lf    w/-text attr/text             	eol.utf${i}
		EOF

		git add eol.utf${i} &&
		git commit -m eol &&

		# UTF-${i} with CRLF (Windows line endings)
		rm eol.utf${i} &&
		git -c core.eol=crlf checkout eol.utf${i} &&
		test_cmp_bin crlf.utf${i}.raw eol.utf${i} &&

		# Although the file has CRLF in the working tree,
		# ensure LF in the index
		git ls-files --eol eol.utf${i} >actual &&
		test_cmp expectIndexLF actual &&

		# UTF-${i} with LF (Unix line endings)
		rm eol.utf${i} &&
		git -c core.eol=lf checkout eol.utf${i} &&
		test_cmp_bin lf.utf${i}.raw eol.utf${i} &&

		# The file LF in the working tree, ensure LF in the index
		git ls-files --eol eol.utf${i} >actual &&
		test_cmp expectIndexLF actual
	'
done

test_expect_success 'check unsupported encodings' '
	test_when_finished "git reset --hard HEAD" &&

	echo "*.set text working-tree-encoding" >.gitattributes &&
	printf "set" >t.set &&
	test_must_fail git add t.set 2>err.out &&
	test_grep "true/false are no valid working-tree-encodings" err.out &&

	echo "*.unset text -working-tree-encoding" >.gitattributes &&
	printf "unset" >t.unset &&
	git add t.unset &&

	echo "*.empty text working-tree-encoding=" >.gitattributes &&
	printf "empty" >t.empty &&
	git add t.empty &&

	echo "*.garbage text working-tree-encoding=garbage" >.gitattributes &&
	printf "garbage" >t.garbage &&
	test_must_fail git add t.garbage 2>err.out &&
	test_grep "failed to encode" err.out
'

test_expect_success 'error if encoding round trip is not the same during refresh' '
	BEFORE_STATE=$(git rev-parse HEAD) &&
	test_when_finished "git reset --hard $BEFORE_STATE" &&

	# Add and commit a UTF-16 file but skip the "working-tree-encoding"
	# filter. Consequently, the in-repo representation is UTF-16 and not
	# UTF-8. This simulates a Git version that has no working tree encoding
	# support.
	echo "*.utf16le text working-tree-encoding=utf-16le" >.gitattributes &&
	echo "hallo" >nonsense.utf16le &&
	TEST_HASH=$(git hash-object --no-filters -w nonsense.utf16le) &&
	git update-index --add --cacheinfo 100644 $TEST_HASH nonsense.utf16le &&
	COMMIT=$(git commit-tree -p $(git rev-parse HEAD) -m "plain commit" $(git write-tree)) &&
	git update-ref refs/heads/main $COMMIT &&

	test_must_fail git checkout HEAD^ 2>err.out &&
	test_grep "error: .* overwritten by checkout:" err.out
'

test_expect_success 'error if encoding garbage is already in Git' '
	BEFORE_STATE=$(git rev-parse HEAD) &&
	test_when_finished "git reset --hard $BEFORE_STATE" &&

	# Skip the UTF-16 filter for the added file
	# This simulates a Git version that has no checkoutEncoding support
	cp nobom.utf16be.raw nonsense.utf16 &&
	TEST_HASH=$(git hash-object --no-filters -w nonsense.utf16) &&
	git update-index --add --cacheinfo 100644 $TEST_HASH nonsense.utf16 &&
	COMMIT=$(git commit-tree -p $(git rev-parse HEAD) -m "plain commit" $(git write-tree)) &&
	git update-ref refs/heads/main $COMMIT &&

	git diff 2>err.out &&
	test_grep "error: BOM is required" err.out
'

test_lazy_prereq ICONV_SHIFT_JIS '
	iconv -f UTF-8 -t SHIFT-JIS </dev/null
'

test_expect_success ICONV_SHIFT_JIS 'check roundtrip encoding' '
	test_when_finished "rm -f roundtrip.shift roundtrip.utf16" &&
	test_when_finished "git reset --hard HEAD" &&

	text="hallo there!\nroundtrip test here!" &&
	printf "$text" | iconv -f UTF-8 -t SHIFT-JIS >roundtrip.shift &&
	printf "$text" | write_utf16 >roundtrip.utf16 &&
	echo "*.shift text working-tree-encoding=SHIFT-JIS" >>.gitattributes &&

	# SHIFT-JIS encoded files are round-trip checked by default...
	GIT_TRACE=1 git add .gitattributes roundtrip.shift 2>&1 |
		grep "Checking roundtrip encoding for SHIFT-JIS" &&
	git reset &&

	# ... unless we overwrite the Git config!
	! GIT_TRACE=1 git -c core.checkRoundtripEncoding=garbage \
		add .gitattributes roundtrip.shift 2>&1 |
		grep "Checking roundtrip encoding for SHIFT-JIS" &&
	git reset &&

	# UTF-16 encoded files should not be round-trip checked by default...
	! GIT_TRACE=1 git add roundtrip.utf16 2>&1 |
		grep "Checking roundtrip encoding for UTF-16" &&
	git reset &&

	# ... unless we tell Git to check it!
	GIT_TRACE=1 git -c core.checkRoundtripEncoding="UTF-16, UTF-32" \
		add roundtrip.utf16 2>&1 |
		grep "Checking roundtrip encoding for utf-16" &&
	git reset &&

	# ... unless we tell Git to check it!
	# (here we also check that the casing of the encoding is irrelevant)
	GIT_TRACE=1 git -c core.checkRoundtripEncoding="UTF-32, utf-16" \
		add roundtrip.utf16 2>&1 |
		grep "Checking roundtrip encoding for utf-16" &&
	git reset
'

# $1: checkout encoding
# $2: test string
# $3: binary test string in checkout encoding
test_commit_utf8_checkout_other () {
	encoding="$1"
	orig_string="$2"
	expect_bytes="$3"

	test_expect_success "Commit UTF-8, checkout $encoding" '
		test_when_finished "git checkout HEAD -- .gitattributes" &&

		test_ext="commit_utf8_checkout_$encoding" &&
		test_file="test.$test_ext" &&

		# Commit as UTF-8
		echo "*.$test_ext text working-tree-encoding=UTF-8" >.gitattributes &&
		printf "$orig_string" >$test_file &&
		git add $test_file &&
		git commit -m "Test data" &&

		# Checkout in tested encoding
		rm $test_file &&
		echo "*.$test_ext text working-tree-encoding=$encoding" >.gitattributes &&
		git checkout HEAD -- $test_file &&

		# Test
		printf $expect_bytes >$test_file.raw &&
		test_cmp_bin $test_file.raw $test_file
	'
}

test_commit_utf8_checkout_other "UTF-8"        "Test Тест" "\124\145\163\164\040\320\242\320\265\321\201\321\202"
test_commit_utf8_checkout_other "UTF-16LE"     "Test Тест" "\124\000\145\000\163\000\164\000\040\000\042\004\065\004\101\004\102\004"
test_commit_utf8_checkout_other "UTF-16BE"     "Test Тест" "\000\124\000\145\000\163\000\164\000\040\004\042\004\065\004\101\004\102"
test_commit_utf8_checkout_other "UTF-16LE-BOM" "Test Тест" "\377\376\124\000\145\000\163\000\164\000\040\000\042\004\065\004\101\004\102\004"
test_commit_utf8_checkout_other "UTF-16BE-BOM" "Test Тест" "\376\377\000\124\000\145\000\163\000\164\000\040\004\042\004\065\004\101\004\102"
test_commit_utf8_checkout_other "UTF-32LE"     "Test Тест" "\124\000\000\000\145\000\000\000\163\000\000\000\164\000\000\000\040\000\000\000\042\004\000\000\065\004\000\000\101\004\000\000\102\004\000\000"
test_commit_utf8_checkout_other "UTF-32BE"     "Test Тест" "\000\000\000\124\000\000\000\145\000\000\000\163\000\000\000\164\000\000\000\040\000\000\004\042\000\000\004\065\000\000\004\101\000\000\004\102"

test_done
