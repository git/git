#!/bin/sh
#
# Copyright (c) 2010 Ævar Arnfjörð Bjarmason
#

test_description="Gettext reencoding of our *.po/*.mo files works"

. ./lib-gettext.sh


test_expect_success GETTEXT_LOCALE 'gettext: Emitting UTF-8 from our UTF-8 *.mo files / Icelandic' '
    printf "TILRAUN: Halló Heimur!" >expect &&
    LANGUAGE=is LC_ALL="$is_IS_locale" gettext "TEST: Hello World!" >actual &&
    test_cmp expect actual
'

test_expect_success GETTEXT_LOCALE 'gettext: Emitting UTF-8 from our UTF-8 *.mo files / Runes' '
    printf "TILRAUN: ᚻᛖ ᚳᚹᚫᚦ ᚦᚫᛏ ᚻᛖ ᛒᚢᛞᛖ ᚩᚾ ᚦᚫᛗ ᛚᚪᚾᛞᛖ ᚾᚩᚱᚦᚹᛖᚪᚱᛞᚢᛗ ᚹᛁᚦ ᚦᚪ ᚹᛖᛥᚫ" >expect &&
    LANGUAGE=is LC_ALL="$is_IS_locale" gettext "TEST: Old English Runes" >actual &&
    test_cmp expect actual
'

test_expect_success GETTEXT_ISO_LOCALE 'gettext: Emitting ISO-8859-1 from our UTF-8 *.mo files / Icelandic' '
    printf "TILRAUN: Halló Heimur!" | iconv -f UTF-8 -t ISO8859-1 >expect &&
    LANGUAGE=is LC_ALL="$is_IS_iso_locale" gettext "TEST: Hello World!" >actual &&
    test_cmp expect actual
'

test_expect_success GETTEXT_ISO_LOCALE 'gettext: Emitting ISO-8859-1 from our UTF-8 *.mo files / Runes' '
    LANGUAGE=is LC_ALL="$is_IS_iso_locale" gettext "TEST: Old English Runes" >runes &&

	if grep "^TEST: Old English Runes$" runes
	then
		say "Your system can not handle this complexity and returns the string as-is"
	else
		# Both Solaris and GNU libintl will return this stream of
		# question marks, so it is s probably portable enough
		printf "TILRAUN: ?? ???? ??? ?? ???? ?? ??? ????? ??????????? ??? ?? ????" >runes-expect &&
		test_cmp runes-expect runes
	fi
'

test_expect_success GETTEXT_LOCALE 'gettext: Fetching a UTF-8 msgid -> UTF-8' '
    printf "TILRAUN: ‚einfaldar‘ og „tvöfaldar“ gæsalappir" >expect &&
    LANGUAGE=is LC_ALL="$is_IS_locale" gettext "TEST: ‘single’ and “double” quotes" >actual &&
    test_cmp expect actual
'

# How these quotes get transliterated depends on the gettext implementation:
#
#   Debian:  ,einfaldar' og ,,tvöfaldar" [GNU libintl]
#   FreeBSD: `einfaldar` og "tvöfaldar"  [GNU libintl]
#   Solaris: ?einfaldar? og ?tvöfaldar?  [Solaris libintl]
#
# Just make sure the contents are transliterated, and don't use grep -q
# so that these differences are emitted under --verbose for curious
# eyes.
test_expect_success GETTEXT_ISO_LOCALE 'gettext: Fetching a UTF-8 msgid -> ISO-8859-1' '
    LANGUAGE=is LC_ALL="$is_IS_iso_locale" gettext "TEST: ‘single’ and “double” quotes" >actual &&
    grep "einfaldar" actual &&
    grep "$(echo tvöfaldar | iconv -f UTF-8 -t ISO8859-1)" actual
'

test_expect_success GETTEXT_LOCALE 'gettext.c: git init UTF-8 -> UTF-8' '
    printf "Bjó til tóma Git lind" >expect &&
    LANGUAGE=is LC_ALL="$is_IS_locale" git init repo >actual &&
    test_when_finished "rm -rf repo" &&
    grep "^$(cat expect) " actual
'

test_expect_success GETTEXT_ISO_LOCALE 'gettext.c: git init UTF-8 -> ISO-8859-1' '
    printf "Bjó til tóma Git lind" >expect &&
    LANGUAGE=is LC_ALL="$is_IS_iso_locale" git init repo >actual &&
    test_when_finished "rm -rf repo" &&
    grep "^$(cat expect | iconv -f UTF-8 -t ISO8859-1) " actual
'

test_done
