#!/bin/sh

test_description='grep icase on non-English locales'

TEST_PASSES_SANITIZE_LEAK=true
. ./lib-gettext.sh

test_expect_success GETTEXT_ISO_LOCALE 'setup' '
	printf "TILRAUN: Hall� Heimur!" >file &&
	but add file &&
	LC_ALL="$is_IS_iso_locale" &&
	export LC_ALL
'

test_expect_success GETTEXT_ISO_LOCALE,PCRE 'grep pcre string' '
	but grep --perl-regexp -i "TILRAUN: H.ll� Heimur!" &&
	but grep --perl-regexp -i "TILRAUN: H.LL� HEIMUR!"
'

test_done
