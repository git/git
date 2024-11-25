#!/bin/sh

test_description='test log with i18n features'

. ./lib-gettext.sh

if ! test_have_prereq ICONV
then
	skip_all='skipping log i18n tests; iconv not available'
	test_done
fi

# two forms of é
utf8_e=$(printf '\303\251')
latin1_e=$(printf '\351')

# invalid UTF-8
invalid_e=$(printf '\303\50)') # ")" at end to close opening "("

have_reg_illseq=
if test_have_prereq GETTEXT_LOCALE &&
	! LC_ALL=$is_IS_locale test-tool regex --silent $latin1_e
then
	have_reg_illseq=1
fi

test_expect_success 'create commits in different encodings' '
	test_tick &&
	cat >msg <<-EOF &&
	utf8

	t${utf8_e}st
	EOF
	git add msg &&
	git -c i18n.commitencoding=utf8 commit -F msg &&
	cat >msg <<-EOF &&
	latin1

	t${latin1_e}st
	EOF
	git add msg &&
	git -c i18n.commitencoding=ISO-8859-1 commit -F msg
'

test_expect_success 'log --grep searches in log output encoding (utf8)' '
	cat >expect <<-\EOF &&
	latin1
	utf8
	EOF
	git log --encoding=utf8 --format=%s --grep=$utf8_e >actual &&
	test_cmp expect actual
'

test_expect_success !MINGW 'log --grep searches in log output encoding (latin1)' '
	cat >expect <<-\EOF &&
	latin1
	utf8
	EOF
	git log --encoding=ISO-8859-1 --format=%s --grep=$latin1_e >actual &&
	test_cmp expect actual
'

test_expect_success !MINGW 'log --grep does not find non-reencoded values (utf8)' '
	git log --encoding=utf8 --format=%s --grep=$latin1_e >actual &&
	test_must_be_empty actual
'

test_expect_success 'log --grep does not find non-reencoded values (latin1)' '
	git log --encoding=ISO-8859-1 --format=%s --grep=$utf8_e >actual &&
	test_must_be_empty actual
'

triggers_undefined_behaviour () {
	local engine="$1"

	case $engine in
	fixed)
		if test -n "$have_reg_illseq" &&
			! test_have_prereq LIBPCRE2
		then
			return 0
		fi
		;;
	basic|extended)
		if test -n "$have_reg_illseq"
		then
			return 0
		fi
		;;
	esac
	return 1
}

mismatched_git_log () {
	local pattern="$1"

	LC_ALL=$is_IS_locale git log --encoding=ISO-8859-1 --format=%s \
		--grep=$pattern
}

for engine in fixed basic extended perl
do
	prereq=
	if test $engine = "perl"
	then
		prereq=PCRE
	fi
	force_regex=
	if test $engine != "fixed"
	then
		force_regex='.*'
	fi

	test_expect_success $prereq "config grep.patternType=$engine" "
		git config grep.patternType $engine
	"

	test_expect_success GETTEXT_LOCALE,$prereq "log --grep does not find non-reencoded values (latin1 + locale)" "
		mismatched_git_log '$force_regex$utf8_e' >actual &&
		test_must_be_empty actual
	"

	if ! triggers_undefined_behaviour $engine
	then
		test_expect_success !MINGW,GETTEXT_LOCALE,$prereq "log --grep searches in log output encoding (latin1 + locale)" "
			cat >expect <<-\EOF &&
			latin1
			utf8
			EOF
			mismatched_git_log '$force_regex$latin1_e' >actual &&
			test_cmp expect actual
		"

		test_expect_success GETTEXT_LOCALE,$prereq "log --grep does not die on invalid UTF-8 value (latin1 + locale + invalid needle)" "
			mismatched_git_log '$force_regex$invalid_e' >actual &&
			test_must_be_empty actual
		"
	fi
done

test_done
