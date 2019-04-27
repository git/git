#!/bin/sh
#
# Copyright (c) 2010 Ævar Arnfjörð Bjarmason
#

test_description='Gettext Shell poison'

GIT_TEST_GETTEXT_POISON=YesPlease
export GIT_TEST_GETTEXT_POISON
. ./lib-gettext.sh

test_expect_success 'sanity: $GIT_INTERNAL_GETTEXT_SH_SCHEME" is poison' '
    test "$GIT_INTERNAL_GETTEXT_SH_SCHEME" = "poison"
'

test_expect_success 'gettext: our gettext() fallback has poison semantics' '
    printf "# GETTEXT POISON #" >expect &&
    gettext "test" >actual &&
    test_cmp expect actual &&
    printf "# GETTEXT POISON #" >expect &&
    gettext "test more words" >actual &&
    test_cmp expect actual
'

test_expect_success 'eval_gettext: our eval_gettext() fallback has poison semantics' '
    printf "# GETTEXT POISON #" >expect &&
    eval_gettext "test" >actual &&
    test_cmp expect actual &&
    printf "# GETTEXT POISON #" >expect &&
    eval_gettext "test more words" >actual &&
    test_cmp expect actual
'

test_done
