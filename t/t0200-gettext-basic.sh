#!/bin/sh
#
# Copyright (c) 2010 Ævar Arnfjörð Bjarmason
#

test_description='Gettext support for Git'

. ./lib-gettext.sh

test_expect_success "sanity: \$GIT_INTERNAL_GETTEXT_SH_SCHEME is set (to $GIT_INTERNAL_GETTEXT_SH_SCHEME)" '
    test -n "$GIT_INTERNAL_GETTEXT_SH_SCHEME"
'

test_expect_success 'sanity: $TEXTDOMAIN is git' '
    test $TEXTDOMAIN = "git"
'

test_expect_success 'xgettext sanity: Perl _() strings are not extracted' '
    ! grep "A Perl string xgettext will not get" "$GIT_PO_PATH"/is.po
'

test_expect_success 'xgettext sanity: Comment extraction with --add-comments' '
    grep "TRANSLATORS: This is a test" "$TEST_DIRECTORY"/t0200/* | wc -l >expect &&
    grep "TRANSLATORS: This is a test" "$GIT_PO_PATH"/is.po  | wc -l >actual &&
    test_cmp expect actual
'

test_expect_success 'xgettext sanity: Comment extraction with --add-comments stops at statements' '
    ! grep "This is a phony" "$GIT_PO_PATH"/is.po &&
    ! grep "the above comment" "$GIT_PO_PATH"/is.po
'

test_expect_success GETTEXT 'sanity: $TEXTDOMAINDIR exists without NO_GETTEXT=YesPlease' '
    test -d "$TEXTDOMAINDIR" &&
    test "$TEXTDOMAINDIR" = "$GIT_TEXTDOMAINDIR"
'

test_expect_success GETTEXT 'sanity: Icelandic locale was compiled' '
    test -f "$TEXTDOMAINDIR/is/LC_MESSAGES/git.mo"
'

# TODO: When we have more locales, generalize this to test them
# all. Maybe we'll need a dir->locale map for that.
test_expect_success GETTEXT_LOCALE 'sanity: gettext("") metadata is OK' '
    # Return value may be non-zero
    LANGUAGE=is LC_ALL="$is_IS_locale" gettext "" >zero-expect &&
    grep "Project-Id-Version: Git" zero-expect &&
    grep "Git Mailing List <git@vger.kernel.org>" zero-expect &&
    grep "Content-Type: text/plain; charset=UTF-8" zero-expect &&
    grep "Content-Transfer-Encoding: 8bit" zero-expect
'

test_expect_success GETTEXT_LOCALE 'sanity: gettext(unknown) is passed through' '
    printf "This is not a translation string"  >expect &&
    gettext "This is not a translation string" >actual &&
    eval_gettext "This is not a translation string" >actual &&
    test_cmp expect actual
'

# xgettext from C
test_expect_success GETTEXT_LOCALE 'xgettext: C extraction of _() and N_() strings' '
    printf "TILRAUN: C tilraunastrengur" >expect &&
    printf "\n" >>expect &&
    printf "Sjá '\''git help SKIPUN'\'' til að sjá hjálp fyrir tiltekna skipun." >>expect &&
    LANGUAGE=is LC_ALL="$is_IS_locale" gettext "TEST: A C test string" >actual &&
    printf "\n" >>actual &&
    LANGUAGE=is LC_ALL="$is_IS_locale" gettext "See '\''git help COMMAND'\'' for more information on a specific command." >>actual &&
    test_cmp expect actual
'

test_expect_success GETTEXT_LOCALE 'xgettext: C extraction with %s' '
    printf "TILRAUN: C tilraunastrengur %%s" >expect &&
    LANGUAGE=is LC_ALL="$is_IS_locale" gettext "TEST: A C test string %s" >actual &&
    test_cmp expect actual
'

# xgettext from Shell
test_expect_success GETTEXT_LOCALE 'xgettext: Shell extraction' '
    printf "TILRAUN: Skeljartilraunastrengur" >expect &&
    LANGUAGE=is LC_ALL="$is_IS_locale" gettext "TEST: A Shell test string" >actual &&
    test_cmp expect actual
'

test_expect_success GETTEXT_LOCALE 'xgettext: Shell extraction with $variable' '
    printf "TILRAUN: Skeljartilraunastrengur með breytunni a var i able" >x-expect &&
    LANGUAGE=is LC_ALL="$is_IS_locale" variable="a var i able" eval_gettext "TEST: A Shell test \$variable" >x-actual &&
    test_cmp x-expect x-actual
'

# xgettext from Perl
test_expect_success GETTEXT_LOCALE 'xgettext: Perl extraction' '
    printf "TILRAUN: Perl tilraunastrengur" >expect &&
    LANGUAGE=is LC_ALL="$is_IS_locale" gettext "TEST: A Perl test string" >actual &&
    test_cmp expect actual
'

test_expect_success GETTEXT_LOCALE 'xgettext: Perl extraction with %s' '
    printf "TILRAUN: Perl tilraunastrengur með breytunni %%s" >expect &&
    LANGUAGE=is LC_ALL="$is_IS_locale" gettext "TEST: A Perl test variable %s" >actual &&
    test_cmp expect actual
'

test_expect_success GETTEXT_LOCALE 'sanity: Some gettext("") data for real locale' '
    LANGUAGE=is LC_ALL="$is_IS_locale" gettext "" >real-locale &&
    test -s real-locale
'

test_done
