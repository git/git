#!/bin/sh

test_description='fetch/receive strict mode'
BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup and inject "corrupt or missing" object' '
	echo hello >greetings &&
	but add greetings &&
	but cummit -m greetings &&

	S=$(but rev-parse :greetings | sed -e "s|^..|&/|") &&
	X=$(echo bye | but hash-object -w --stdin | sed -e "s|^..|&/|") &&
	echo $S >S &&
	echo $X >X &&
	cp .but/objects/$S .but/objects/$S.back &&
	mv -f .but/objects/$X .but/objects/$S &&

	test_must_fail but fsck
'

test_expect_success 'fetch without strict' '
	rm -rf dst &&
	but init dst &&
	(
		cd dst &&
		but config fetch.fsckobjects false &&
		but config transfer.fsckobjects false &&
		test_must_fail but fetch ../.but main
	)
'

test_expect_success 'fetch with !fetch.fsckobjects' '
	rm -rf dst &&
	but init dst &&
	(
		cd dst &&
		but config fetch.fsckobjects false &&
		but config transfer.fsckobjects true &&
		test_must_fail but fetch ../.but main
	)
'

test_expect_success 'fetch with fetch.fsckobjects' '
	rm -rf dst &&
	but init dst &&
	(
		cd dst &&
		but config fetch.fsckobjects true &&
		but config transfer.fsckobjects false &&
		test_must_fail but fetch ../.but main
	)
'

test_expect_success 'fetch with transfer.fsckobjects' '
	rm -rf dst &&
	but init dst &&
	(
		cd dst &&
		but config transfer.fsckobjects true &&
		test_must_fail but fetch ../.but main
	)
'

cat >exp <<EOF
To dst
!	refs/heads/main:refs/heads/test	[remote rejected] (missing necessary objects)
Done
EOF

test_expect_success 'push without strict' '
	rm -rf dst &&
	but init dst &&
	(
		cd dst &&
		but config fetch.fsckobjects false &&
		but config transfer.fsckobjects false
	) &&
	test_must_fail but push --porcelain dst main:refs/heads/test >act &&
	test_cmp exp act
'

test_expect_success 'push with !receive.fsckobjects' '
	rm -rf dst &&
	but init dst &&
	(
		cd dst &&
		but config receive.fsckobjects false &&
		but config transfer.fsckobjects true
	) &&
	test_must_fail but push --porcelain dst main:refs/heads/test >act &&
	test_cmp exp act
'

cat >exp <<EOF
To dst
!	refs/heads/main:refs/heads/test	[remote rejected] (unpacker error)
EOF

test_expect_success 'push with receive.fsckobjects' '
	rm -rf dst &&
	but init dst &&
	(
		cd dst &&
		but config receive.fsckobjects true &&
		but config transfer.fsckobjects false
	) &&
	test_must_fail but push --porcelain dst main:refs/heads/test >act &&
	test_cmp exp act
'

test_expect_success 'push with transfer.fsckobjects' '
	rm -rf dst &&
	but init dst &&
	(
		cd dst &&
		but config transfer.fsckobjects true
	) &&
	test_must_fail but push --porcelain dst main:refs/heads/test >act &&
	test_cmp exp act
'

test_expect_success 'repair the "corrupt or missing" object' '
	mv -f .but/objects/$(cat S) .but/objects/$(cat X) &&
	mv .but/objects/$(cat S).back .but/objects/$(cat S) &&
	rm -rf .but/objects/$(cat X) &&
	but fsck
'

cat >bogus-cummit <<EOF
tree $EMPTY_TREE
author Bugs Bunny 1234567890 +0000
cummitter Bugs Bunny <bugs@bun.ni> 1234567890 +0000

This cummit object intentionally broken
EOF

test_expect_success 'setup bogus cummit' '
	cummit="$(but hash-object -t cummit -w --stdin <bogus-cummit)"
'

test_expect_success 'fsck with no skipList input' '
	test_must_fail but fsck 2>err &&
	test_i18ngrep "missingEmail" err
'

test_expect_success 'setup sorted and unsorted skipLists' '
	cat >SKIP.unsorted <<-EOF &&
	$(test_oid 004)
	$(test_oid 002)
	$cummit
	$(test_oid 001)
	$(test_oid 003)
	EOF
	sort SKIP.unsorted >SKIP.sorted
'

test_expect_success 'fsck with sorted skipList' '
	but -c fsck.skipList=SKIP.sorted fsck
'

test_expect_success 'fsck with unsorted skipList' '
	but -c fsck.skipList=SKIP.unsorted fsck
'

test_expect_success 'fsck with invalid or bogus skipList input' '
	but -c fsck.skipList=/dev/null -c fsck.missingEmail=ignore fsck &&
	test_must_fail but -c fsck.skipList=does-not-exist -c fsck.missingEmail=ignore fsck 2>err &&
	test_i18ngrep "could not open.*: does-not-exist" err &&
	test_must_fail but -c fsck.skipList=.but/config -c fsck.missingEmail=ignore fsck 2>err &&
	test_i18ngrep "invalid object name: \[core\]" err
'

test_expect_success 'fsck with other accepted skipList input (comments & empty lines)' '
	cat >SKIP.with-comment <<-EOF &&
	# Some bad cummit
	$(test_oid 001)
	EOF
	test_must_fail but -c fsck.skipList=SKIP.with-comment fsck 2>err-with-comment &&
	test_i18ngrep "missingEmail" err-with-comment &&
	cat >SKIP.with-empty-line <<-EOF &&
	$(test_oid 001)

	$(test_oid 002)
	EOF
	test_must_fail but -c fsck.skipList=SKIP.with-empty-line fsck 2>err-with-empty-line &&
	test_i18ngrep "missingEmail" err-with-empty-line
'

test_expect_success 'fsck no garbage output from comments & empty lines errors' '
	test_line_count = 1 err-with-comment &&
	test_line_count = 1 err-with-empty-line
'

test_expect_success 'fsck with invalid abbreviated skipList input' '
	echo $cummit | test_copy_bytes 20 >SKIP.abbreviated &&
	test_must_fail but -c fsck.skipList=SKIP.abbreviated fsck 2>err-abbreviated &&
	test_i18ngrep "^fatal: invalid object name: " err-abbreviated
'

test_expect_success 'fsck with exhaustive accepted skipList input (various types of comments etc.)' '
	>SKIP.exhaustive &&
	echo "# A commented line" >>SKIP.exhaustive &&
	echo "" >>SKIP.exhaustive &&
	echo " " >>SKIP.exhaustive &&
	echo " # Comment after whitespace" >>SKIP.exhaustive &&
	echo "$cummit # Our bad cummit (with leading whitespace and trailing comment)" >>SKIP.exhaustive &&
	echo "# Some bad cummit (leading whitespace)" >>SKIP.exhaustive &&
	echo "  $(test_oid 001)" >>SKIP.exhaustive &&
	but -c fsck.skipList=SKIP.exhaustive fsck 2>err &&
	test_must_be_empty err
'

test_expect_success 'push with receive.fsck.skipList' '
	but push . $cummit:refs/heads/bogus &&
	rm -rf dst &&
	but init dst &&
	but --but-dir=dst/.but config receive.fsckObjects true &&
	test_must_fail but push --porcelain dst bogus &&
	echo $cummit >dst/.but/SKIP &&

	# receive.fsck.* does not fall back on fsck.*
	but --but-dir=dst/.but config fsck.skipList SKIP &&
	test_must_fail but push --porcelain dst bogus &&

	# Invalid and/or bogus skipList input
	but --but-dir=dst/.but config receive.fsck.skipList /dev/null &&
	test_must_fail but push --porcelain dst bogus &&
	but --but-dir=dst/.but config receive.fsck.skipList does-not-exist &&
	test_must_fail but push --porcelain dst bogus 2>err &&
	test_i18ngrep "could not open.*: does-not-exist" err &&
	but --but-dir=dst/.but config receive.fsck.skipList config &&
	test_must_fail but push --porcelain dst bogus 2>err &&
	test_i18ngrep "invalid object name: \[core\]" err &&

	but --but-dir=dst/.but config receive.fsck.skipList SKIP &&
	but push --porcelain dst bogus
'

test_expect_success 'fetch with fetch.fsck.skipList' '
	refspec=refs/heads/bogus:refs/heads/bogus &&
	but push . $cummit:refs/heads/bogus &&
	rm -rf dst &&
	but init dst &&
	but --but-dir=dst/.but config fetch.fsckObjects true &&
	test_must_fail but --but-dir=dst/.but fetch "file://$(pwd)" $refspec &&
	but --but-dir=dst/.but config fetch.fsck.skipList /dev/null &&
	test_must_fail but --but-dir=dst/.but fetch "file://$(pwd)" $refspec &&
	echo $cummit >dst/.but/SKIP &&

	# fetch.fsck.* does not fall back on fsck.*
	but --but-dir=dst/.but config fsck.skipList dst/.but/SKIP &&
	test_must_fail but --but-dir=dst/.but fetch "file://$(pwd)" $refspec &&

	# Invalid and/or bogus skipList input
	but --but-dir=dst/.but config fetch.fsck.skipList /dev/null &&
	test_must_fail but --but-dir=dst/.but fetch "file://$(pwd)" $refspec &&
	but --but-dir=dst/.but config fetch.fsck.skipList does-not-exist &&
	test_must_fail but --but-dir=dst/.but fetch "file://$(pwd)" $refspec 2>err &&
	test_i18ngrep "could not open.*: does-not-exist" err &&
	but --but-dir=dst/.but config fetch.fsck.skipList dst/.but/config &&
	test_must_fail but --but-dir=dst/.but fetch "file://$(pwd)" $refspec 2>err &&
	test_i18ngrep "invalid object name: \[core\]" err &&

	but --but-dir=dst/.but config fetch.fsck.skipList dst/.but/SKIP &&
	but --but-dir=dst/.but fetch "file://$(pwd)" $refspec
'

test_expect_success 'fsck.<unknownmsg-id> dies' '
	test_must_fail but -c fsck.whatEver=ignore fsck 2>err &&
	test_i18ngrep "Unhandled message id: whatever" err
'

test_expect_success 'push with receive.fsck.missingEmail=warn' '
	but push . $cummit:refs/heads/bogus &&
	rm -rf dst &&
	but init dst &&
	but --but-dir=dst/.but config receive.fsckobjects true &&
	test_must_fail but push --porcelain dst bogus &&

	# receive.fsck.<msg-id> does not fall back on fsck.<msg-id>
	but --but-dir=dst/.but config fsck.missingEmail warn &&
	test_must_fail but push --porcelain dst bogus &&

	# receive.fsck.<unknownmsg-id> warns
	but --but-dir=dst/.but config \
		receive.fsck.whatEver error &&

	but --but-dir=dst/.but config \
		receive.fsck.missingEmail warn &&
	but push --porcelain dst bogus >act 2>&1 &&
	grep "missingEmail" act &&
	test_i18ngrep "skipping unknown msg id.*whatever" act &&
	but --but-dir=dst/.but branch -D bogus &&
	but --but-dir=dst/.but config --add \
		receive.fsck.missingEmail ignore &&
	but push --porcelain dst bogus >act 2>&1 &&
	! grep "missingEmail" act
'

test_expect_success 'fetch with fetch.fsck.missingEmail=warn' '
	refspec=refs/heads/bogus:refs/heads/bogus &&
	but push . $cummit:refs/heads/bogus &&
	rm -rf dst &&
	but init dst &&
	but --but-dir=dst/.but config fetch.fsckobjects true &&
	test_must_fail but --but-dir=dst/.but fetch "file://$(pwd)" $refspec &&

	# fetch.fsck.<msg-id> does not fall back on fsck.<msg-id>
	but --but-dir=dst/.but config fsck.missingEmail warn &&
	test_must_fail but --but-dir=dst/.but fetch "file://$(pwd)" $refspec &&

	# receive.fsck.<unknownmsg-id> warns
	but --but-dir=dst/.but config \
		fetch.fsck.whatEver error &&

	but --but-dir=dst/.but config \
		fetch.fsck.missingEmail warn &&
	but --but-dir=dst/.but fetch "file://$(pwd)" $refspec >act 2>&1 &&
	grep "missingEmail" act &&
	test_i18ngrep "Skipping unknown msg id.*whatever" act &&
	rm -rf dst &&
	but init dst &&
	but --but-dir=dst/.but config fetch.fsckobjects true &&
	but --but-dir=dst/.but config \
		fetch.fsck.missingEmail ignore &&
	but --but-dir=dst/.but fetch "file://$(pwd)" $refspec >act 2>&1 &&
	! grep "missingEmail" act
'

test_expect_success \
	'receive.fsck.unterminatedHeader=warn triggers error' '
	rm -rf dst &&
	but init dst &&
	but --but-dir=dst/.but config receive.fsckobjects true &&
	but --but-dir=dst/.but config \
		receive.fsck.unterminatedheader warn &&
	test_must_fail but push --porcelain dst HEAD >act 2>&1 &&
	grep "Cannot demote unterminatedheader" act
'

test_expect_success \
	'fetch.fsck.unterminatedHeader=warn triggers error' '
	rm -rf dst &&
	but init dst &&
	but --but-dir=dst/.but config fetch.fsckobjects true &&
	but --but-dir=dst/.but config \
		fetch.fsck.unterminatedheader warn &&
	test_must_fail but --but-dir=dst/.but fetch "file://$(pwd)" HEAD &&
	grep "Cannot demote unterminatedheader" act
'

test_done
