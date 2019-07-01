#!/bin/sh

test_description='git grep with a binary pattern files'

. ./test-lib.sh

nul_match () {
	matches=$1
	flags=$2
	pattern=$3
	pattern_human=$(echo "$pattern" | sed 's/Q/<NUL>/g')

	if test "$matches" = 1
	then
		test_expect_success "git grep -f f $flags '$pattern_human' a" "
			printf '$pattern' | q_to_nul >f &&
			git grep -f f $flags a
		"
	elif test "$matches" = 0
	then
		test_expect_success "git grep -f f $flags '$pattern_human' a" "
			printf '$pattern' | q_to_nul >f &&
			test_must_fail git grep -f f $flags a
		"
	elif test "$matches" = T1
	then
		test_expect_failure "git grep -f f $flags '$pattern_human' a" "
			printf '$pattern' | q_to_nul >f &&
			git grep -f f $flags a
		"
	elif test "$matches" = T0
	then
		test_expect_failure "git grep -f f $flags '$pattern_human' a" "
			printf '$pattern' | q_to_nul >f &&
			test_must_fail git grep -f f $flags a
		"
	else
		test_expect_success "PANIC: Test framework error. Unknown matches value $matches" 'false'
	fi
}

test_expect_success 'setup' "
	echo 'binaryQfileQm[*]cQ*æQð' | q_to_nul >a &&
	git add a &&
	git commit -m.
"

nul_match 1 '-F' 'yQf'
nul_match 0 '-F' 'yQx'
nul_match 1 '-Fi' 'YQf'
nul_match 0 '-Fi' 'YQx'
nul_match 1 '' 'yQf'
nul_match 0 '' 'yQx'
nul_match 1 '' 'æQð'
nul_match 1 '-F' 'eQm[*]c'
nul_match 1 '-Fi' 'EQM[*]C'

# Regex patterns that would match but shouldn't with -F
nul_match 0 '-F' 'yQ[f]'
nul_match 0 '-F' '[y]Qf'
nul_match 0 '-Fi' 'YQ[F]'
nul_match 0 '-Fi' '[Y]QF'
nul_match 0 '-F' 'æQ[ð]'
nul_match 0 '-F' '[æ]Qð'
nul_match 0 '-Fi' 'ÆQ[Ð]'
nul_match 0 '-Fi' '[Æ]QÐ'

# kwset is disabled on -i & non-ASCII. No way to match non-ASCII \0
# patterns case-insensitively.
nul_match T1 '-i' 'ÆQÐ'

# \0 implicitly disables regexes. This is an undocumented internal
# limitation.
nul_match T1 '' 'yQ[f]'
nul_match T1 '' '[y]Qf'
nul_match T1 '-i' 'YQ[F]'
nul_match T1 '-i' '[Y]Qf'
nul_match T1 '' 'æQ[ð]'
nul_match T1 '' '[æ]Qð'
nul_match T1 '-i' 'ÆQ[Ð]'

# ... because of \0 implicitly disabling regexes regexes that
# should/shouldn't match don't do the right thing.
nul_match T1 '' 'eQm.*cQ'
nul_match T1 '-i' 'EQM.*cQ'
nul_match T0 '' 'eQm[*]c'
nul_match T0 '-i' 'EQM[*]C'

# Due to the REG_STARTEND extension when kwset() is disabled on -i &
# non-ASCII the string will be matched in its entirety, but the
# pattern will be cut off at the first \0.
nul_match 0 '-i' 'NOMATCHQð'
nul_match T0 '-i' '[Æ]QNOMATCH'
nul_match T0 '-i' '[æ]QNOMATCH'
# Matches, but for the wrong reasons, just stops at [æ]
nul_match 1 '-i' '[Æ]Qð'
nul_match 1 '-i' '[æ]Qð'

# Ensure that the matcher doesn't regress to something that stops at
# \0
nul_match 0 '-F' 'yQ[f]'
nul_match 0 '-Fi' 'YQ[F]'
nul_match 0 '' 'yQNOMATCH'
nul_match 0 '' 'QNOMATCH'
nul_match 0 '-i' 'YQNOMATCH'
nul_match 0 '-i' 'QNOMATCH'
nul_match 0 '-F' 'æQ[ð]'
nul_match 0 '-Fi' 'ÆQ[Ð]'
nul_match 0 '' 'yQNÓMATCH'
nul_match 0 '' 'QNÓMATCH'
nul_match 0 '-i' 'YQNÓMATCH'
nul_match 0 '-i' 'QNÓMATCH'

test_done
