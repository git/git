#!/bin/sh

test_description='but blame encoding conversion'
. ./test-lib.sh

. "$TEST_DIRECTORY"/t8005/utf8.txt
. "$TEST_DIRECTORY"/t8005/euc-japan.txt
. "$TEST_DIRECTORY"/t8005/sjis.txt

test_expect_success 'setup the repository' '
	# Create the file
	echo "UTF-8 LINE" > file &&
	but add file &&
	but cummit --author "$UTF8_NAME <utf8@localhost>" -m "$UTF8_MSG" &&

	echo "EUC-JAPAN LINE" >> file &&
	but add file &&
	but config i18n.cummitencoding eucJP &&
	but cummit --author "$EUC_JAPAN_NAME <euc-japan@localhost>" -m "$EUC_JAPAN_MSG" &&

	echo "SJIS LINE" >> file &&
	but add file &&
	but config i18n.cummitencoding SJIS &&
	but cummit --author "$SJIS_NAME <sjis@localhost>" -m "$SJIS_MSG"
'

cat >expected <<EOF
author $SJIS_NAME
summary $SJIS_MSG
author $SJIS_NAME
summary $SJIS_MSG
author $SJIS_NAME
summary $SJIS_MSG
EOF

filter_author_summary () {
	sed -n -e '/^author /p' -e '/^summary /p' "$@"
}

test_expect_success !MINGW \
	'blame respects i18n.cummitencoding' '
	but blame --incremental file >output &&
	filter_author_summary output >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
author $EUC_JAPAN_NAME
summary $EUC_JAPAN_MSG
author $EUC_JAPAN_NAME
summary $EUC_JAPAN_MSG
author $EUC_JAPAN_NAME
summary $EUC_JAPAN_MSG
EOF

test_expect_success !MINGW \
	'blame respects i18n.logoutputencoding' '
	but config i18n.logoutputencoding eucJP &&
	but blame --incremental file >output &&
	filter_author_summary output >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
author $UTF8_NAME
summary $UTF8_MSG
author $UTF8_NAME
summary $UTF8_MSG
author $UTF8_NAME
summary $UTF8_MSG
EOF

test_expect_success !MINGW \
	'blame respects --encoding=UTF-8' '
	but blame --incremental --encoding=UTF-8 file >output &&
	filter_author_summary output >actual &&
	test_cmp expected actual
'

cat >expected <<EOF
author $SJIS_NAME
summary $SJIS_MSG
author $EUC_JAPAN_NAME
summary $EUC_JAPAN_MSG
author $UTF8_NAME
summary $UTF8_MSG
EOF

test_expect_success !MINGW \
	'blame respects --encoding=none' '
	but blame --incremental --encoding=none file >output &&
	filter_author_summary output >actual &&
	test_cmp expected actual
'

test_done
