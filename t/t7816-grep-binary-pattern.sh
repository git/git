#!/bin/sh

test_description='git grep with a binary pattern files'

. ./lib-gettext.sh

nul_match_internal () {
	matches=$1
	prereqs=$2
	lc_all=$3
	extra_flags=$4
	flags=$5
	pattern=$6
	pattern_human=$(echo "$pattern" | sed 's/Q/<NUL>/g')

	if test "$matches" = 1
	then
		test_expect_success $prereqs "LC_ALL='$lc_all' git grep $extra_flags -f f $flags '$pattern_human' a" "
			printf '$pattern' | q_to_nul >f &&
			LC_ALL='$lc_all' git grep $extra_flags -f f $flags a
		"
	elif test "$matches" = 0
	then
		test_expect_success $prereqs "LC_ALL='$lc_all' git grep $extra_flags -f f $flags '$pattern_human' a" "
			>stderr &&
			printf '$pattern' | q_to_nul >f &&
			test_must_fail env LC_ALL=\"$lc_all\" git grep $extra_flags -f f $flags a 2>stderr &&
			test_i18ngrep ! 'This is only supported with -P under PCRE v2' stderr
		"
	elif test "$matches" = P
	then
		test_expect_success $prereqs "error, PCRE v2 only: LC_ALL='$lc_all' git grep -f f $flags '$pattern_human' a" "
			>stderr &&
			printf '$pattern' | q_to_nul >f &&
			test_must_fail env LC_ALL=\"$lc_all\" git grep -f f $flags a 2>stderr &&
			test_i18ngrep 'This is only supported with -P under PCRE v2' stderr
		"
	else
		test_expect_success "PANIC: Test framework error. Unknown matches value $matches" 'false'
	fi
}

nul_match () {
	matches=$1
	matches_pcre2=$2
	matches_pcre2_locale=$3
	flags=$4
	pattern=$5
	pattern_human=$(echo "$pattern" | sed 's/Q/<NUL>/g')

	nul_match_internal "$matches" "" "C" "" "$flags" "$pattern"
	nul_match_internal "$matches_pcre2" "LIBPCRE2" "C" "-P" "$flags" "$pattern"
	nul_match_internal "$matches_pcre2_locale" "LIBPCRE2,GETTEXT_LOCALE" "$is_IS_locale" "-P" "$flags" "$pattern"
}

test_expect_success 'setup' "
	echo 'binaryQfileQm[*]cQ*æQð' | q_to_nul >a &&
	git add a &&
	git commit -m.
"

# Simple fixed-string matching
nul_match P P P '-F' 'yQf'
nul_match P P P '-F' 'yQx'
nul_match P P P '-Fi' 'YQf'
nul_match P P P '-Fi' 'YQx'
nul_match P P 1 '' 'yQf'
nul_match P P 0 '' 'yQx'
nul_match P P 1 '' 'æQð'
nul_match P P P '-F' 'eQm[*]c'
nul_match P P P '-Fi' 'EQM[*]C'

# Regex patterns that would match but shouldn't with -F
nul_match P P P '-F' 'yQ[f]'
nul_match P P P '-F' '[y]Qf'
nul_match P P P '-Fi' 'YQ[F]'
nul_match P P P '-Fi' '[Y]QF'
nul_match P P P '-F' 'æQ[ð]'
nul_match P P P '-F' '[æ]Qð'

# Matching pattern and subject case with -i
nul_match P 1 1 '-i' '[æ]Qð'

# ...PCRE v2 only matches non-ASCII with -i casefolding under UTF-8
# semantics
nul_match P P P '-Fi' 'ÆQ[Ð]'
nul_match P 0 1 '-i'  'ÆQ[Ð]'
nul_match P 0 1 '-i'  '[Æ]QÐ'
nul_match P 0 1 '-i' '[Æ]Qð'
nul_match P 0 1 '-i' 'ÆQÐ'

# \0 in regexes can only work with -P & PCRE v2
nul_match P P 1 '' 'yQ[f]'
nul_match P P 1 '' '[y]Qf'
nul_match P P 1 '-i' 'YQ[F]'
nul_match P P 1 '-i' '[Y]Qf'
nul_match P P 1 '' 'æQ[ð]'
nul_match P P 1 '' '[æ]Qð'
nul_match P P 1 '-i' 'ÆQ[Ð]'
nul_match P P 1 '' 'eQm.*cQ'
nul_match P P 1 '-i' 'EQM.*cQ'
nul_match P P 0 '' 'eQm[*]c'
nul_match P P 0 '-i' 'EQM[*]C'

# Assert that we're using REG_STARTEND and the pattern doesn't match
# just because it's cut off at the first \0.
nul_match P P 0 '-i' 'NOMATCHQð'
nul_match P P 0 '-i' '[Æ]QNOMATCH'
nul_match P P 0 '-i' '[æ]QNOMATCH'

# Ensure that the matcher doesn't regress to something that stops at
# \0
nul_match P P P '-F' 'yQ[f]'
nul_match P P P '-Fi' 'YQ[F]'
nul_match P P 0 '' 'yQNOMATCH'
nul_match P P 0 '' 'QNOMATCH'
nul_match P P 0 '-i' 'YQNOMATCH'
nul_match P P 0 '-i' 'QNOMATCH'
nul_match P P P '-F' 'æQ[ð]'
nul_match P P P '-Fi' 'ÆQ[Ð]'
nul_match P P 1 '-i' 'ÆQ[Ð]'
nul_match P P 0 '' 'yQNÓMATCH'
nul_match P P 0 '' 'QNÓMATCH'
nul_match P P 0 '-i' 'YQNÓMATCH'
nul_match P P 0 '-i' 'QNÓMATCH'

test_done
