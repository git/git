#!/bin/sh

test_description='working-tree-encoding conversion via gitattributes'

. ./test-lib.sh

test_expect_success 'setup test repo' '
	git config core.eol lf &&

	text="hallo there!\ncan you read me?" &&
	echo "*.utf16 text working-tree-encoding=utf-16" >.gitattributes &&
	printf "$text" >test.utf8.raw &&
	printf "$text" | iconv -f UTF-8 -t UTF-16 >test.utf16.raw &&
	cp test.utf16.raw test.utf16 &&

	git add .gitattributes test.utf16 &&
	git commit -m initial
'

test_expect_success 'ensure UTF-8 is stored in Git' '
	git cat-file -p :test.utf16 >test.utf16.git &&
	test_cmp_bin test.utf8.raw test.utf16.git &&

	# cleanup
	rm test.utf8.raw test.utf16.git
'

test_expect_success 're-encode to UTF-16 on checkout' '
	rm test.utf16 &&
	git checkout test.utf16 &&
	test_cmp_bin test.utf16.raw test.utf16 &&

	# cleanup
	rm test.utf16.raw
'

test_expect_success 'check prohibited UTF BOM' '
	printf "\0a\0b\0c"                         >nobom.utf16be.raw &&
	printf "a\0b\0c\0"                         >nobom.utf16le.raw &&
	printf "\376\777\0a\0b\0c"                 >bebom.utf16be.raw &&
	printf "\777\376a\0b\0c\0"                 >lebom.utf16le.raw &&

	printf "\0\0\0a\0\0\0b\0\0\0c"             >nobom.utf32be.raw &&
	printf "a\0\0\0b\0\0\0c\0\0\0"             >nobom.utf32le.raw &&
	printf "\0\0\376\777\0\0\0a\0\0\0b\0\0\0c" >bebom.utf32be.raw &&
	printf "\777\376\0\0a\0\0\0b\0\0\0c\0\0\0" >lebom.utf32le.raw &&

	echo "*.utf16be text working-tree-encoding=utf-16be" >>.gitattributes &&
	echo "*.utf16le text working-tree-encoding=utf-16le" >>.gitattributes &&
	echo "*.utf32be text working-tree-encoding=utf-32be" >>.gitattributes &&
	echo "*.utf32le text working-tree-encoding=utf-32le" >>.gitattributes &&

	# Here we add a UTF-16 files with BOM (big-endian and little-endian)
	# but we tell Git to treat it as UTF-16BE/UTF-16LE. In these cases
	# the BOM is prohibited.
	cp bebom.utf16be.raw bebom.utf16be &&
	test_must_fail git add bebom.utf16be 2>err.out &&
	test_i18ngrep "fatal: BOM is prohibited .* UTF-16BE" err.out &&

	cp lebom.utf16le.raw lebom.utf16be &&
	test_must_fail git add lebom.utf16be 2>err.out &&
	test_i18ngrep "fatal: BOM is prohibited .* UTF-16BE" err.out &&

	cp bebom.utf16be.raw bebom.utf16le &&
	test_must_fail git add bebom.utf16le 2>err.out &&
	test_i18ngrep "fatal: BOM is prohibited .* UTF-16LE" err.out &&

	cp lebom.utf16le.raw lebom.utf16le &&
	test_must_fail git add lebom.utf16le 2>err.out &&
	test_i18ngrep "fatal: BOM is prohibited .* UTF-16LE" err.out &&

	# ... and the same for UTF-32
	cp bebom.utf32be.raw bebom.utf32be &&
	test_must_fail git add bebom.utf32be 2>err.out &&
	test_i18ngrep "fatal: BOM is prohibited .* UTF-32BE" err.out &&

	cp lebom.utf32le.raw lebom.utf32be &&
	test_must_fail git add lebom.utf32be 2>err.out &&
	test_i18ngrep "fatal: BOM is prohibited .* UTF-32BE" err.out &&

	cp bebom.utf32be.raw bebom.utf32le &&
	test_must_fail git add bebom.utf32le 2>err.out &&
	test_i18ngrep "fatal: BOM is prohibited .* UTF-32LE" err.out &&

	cp lebom.utf32le.raw lebom.utf32le &&
	test_must_fail git add lebom.utf32le 2>err.out &&
	test_i18ngrep "fatal: BOM is prohibited .* UTF-32LE" err.out &&

	# cleanup
	git reset --hard HEAD
'

test_expect_success 'check required UTF BOM' '
	echo "*.utf32 text working-tree-encoding=utf-32" >>.gitattributes &&

	cp nobom.utf16be.raw nobom.utf16 &&
	test_must_fail git add nobom.utf16 2>err.out &&
	test_i18ngrep "fatal: BOM is required .* UTF-16" err.out &&

	cp nobom.utf16le.raw nobom.utf16 &&
	test_must_fail git add nobom.utf16 2>err.out &&
	test_i18ngrep "fatal: BOM is required .* UTF-16" err.out &&

	cp nobom.utf32be.raw nobom.utf32 &&
	test_must_fail git add nobom.utf32 2>err.out &&
	test_i18ngrep "fatal: BOM is required .* UTF-32" err.out &&

	cp nobom.utf32le.raw nobom.utf32 &&
	test_must_fail git add nobom.utf32 2>err.out &&
	test_i18ngrep "fatal: BOM is required .* UTF-32" err.out &&

	# cleanup
	rm nobom.utf16 nobom.utf32 &&
	git reset --hard HEAD
'

test_expect_success 'eol conversion for UTF-16 encoded files on checkout' '
	printf "one\ntwo\nthree\n" >lf.utf8.raw &&
	printf "one\r\ntwo\r\nthree\r\n" >crlf.utf8.raw &&

	cat lf.utf8.raw | iconv -f UTF-8 -t UTF-16 >lf.utf16.raw &&
	cat crlf.utf8.raw | iconv -f UTF-8 -t UTF-16 >crlf.utf16.raw &&
	cp crlf.utf16.raw eol.utf16 &&

	cat >expectIndexLF <<-\EOF &&
		i/lf    w/-text attr/text             	eol.utf16
	EOF

	git add eol.utf16 &&
	git commit -m eol &&

	# UTF-16 with CRLF (Windows line endings)
	rm eol.utf16 &&
	git -c core.eol=crlf checkout eol.utf16 &&
	test_cmp_bin crlf.utf16.raw eol.utf16 &&

	# Although the file has CRLF in the working tree, ensure LF in the index
	git ls-files --eol eol.utf16 >actual &&
	test_cmp expectIndexLF actual &&

	# UTF-16 with LF (Unix line endings)
	rm eol.utf16 &&
	git -c core.eol=lf checkout eol.utf16 &&
	test_cmp_bin lf.utf16.raw eol.utf16 &&

	# The file LF in the working tree, ensure LF in the index
	git ls-files --eol eol.utf16 >actual &&
	test_cmp expectIndexLF actual&&

	rm crlf.utf16.raw crlf.utf8.raw lf.utf16.raw lf.utf8.raw &&

	# cleanup
	git reset --hard HEAD^
'

test_expect_success 'check unsupported encodings' '

	echo "*.nothing text working-tree-encoding=" >>.gitattributes &&
	printf "nothing" >t.nothing &&
	git add t.nothing &&

	echo "*.garbage text working-tree-encoding=garbage" >>.gitattributes &&
	printf "garbage" >t.garbage &&
	test_must_fail git add t.garbage 2>err.out &&
	test_i18ngrep "fatal: failed to encode" err.out &&

	# cleanup
	rm err.out &&
	git reset --hard HEAD
'

test_expect_success 'error if encoding round trip is not the same during refresh' '
	BEFORE_STATE=$(git rev-parse HEAD) &&

	# Skip the UTF-16 filter for the added file
	# This simulates a Git version that has no working tree encoding support
	echo "hallo" >nonsense.utf16 &&
	TEST_HASH=$(git hash-object --no-filters -w nonsense.utf16) &&
	git update-index --add --cacheinfo 100644 $TEST_HASH nonsense.utf16 &&
	COMMIT=$(git commit-tree -p $(git rev-parse HEAD) -m "plain commit" $(git write-tree)) &&
	git update-ref refs/heads/master $COMMIT &&

	test_must_fail git checkout HEAD^ 2>err.out &&
	test_i18ngrep "error: .* overwritten by checkout:" err.out &&

	# cleanup
	rm err.out &&
	git reset --hard $BEFORE_STATE
'

test_expect_success 'error if encoding garbage is already in Git' '
	BEFORE_STATE=$(git rev-parse HEAD) &&

	# Skip the UTF-16 filter for the added file
	# This simulates a Git version that has no checkoutEncoding support
	cp nobom.utf16be.raw nonsense.utf16 &&
	TEST_HASH=$(git hash-object --no-filters -w nonsense.utf16) &&
	git update-index --add --cacheinfo 100644 $TEST_HASH nonsense.utf16 &&
	COMMIT=$(git commit-tree -p $(git rev-parse HEAD) -m "plain commit" $(git write-tree)) &&
	git update-ref refs/heads/master $COMMIT &&

	git diff 2>err.out &&
	test_i18ngrep "error: BOM is required" err.out &&

	# cleanup
	rm err.out &&
	git reset --hard $BEFORE_STATE
'

test_done
