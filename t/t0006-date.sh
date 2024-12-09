#!/bin/sh

test_description='test date parsing and printing'

. ./test-lib.sh

# arbitrary reference time: 2009-08-30 19:20:00
GIT_TEST_DATE_NOW=1251660000; export GIT_TEST_DATE_NOW

if test_have_prereq TIME_IS_64BIT,TIME_T_IS_64BIT
then
	test_set_prereq HAVE_64BIT_TIME
fi

check_relative() {
	t=$(($GIT_TEST_DATE_NOW - $1))
	echo "$t -> $2" >expect
	test_expect_${3:-success} "relative date ($2)" "
	test-tool date relative $t >actual &&
	test_cmp expect actual
	"
}

check_relative 5 '5 seconds ago'
check_relative 300 '5 minutes ago'
check_relative 18000 '5 hours ago'
check_relative 432000 '5 days ago'
check_relative 1728000 '3 weeks ago'
check_relative 13000000 '5 months ago'
check_relative 37500000 '1 year, 2 months ago'
check_relative 55188000 '1 year, 9 months ago'
check_relative 630000000 '20 years ago'
check_relative 31449600 '12 months ago'
check_relative 62985600 '2 years ago'

check_show () {
	format=$1
	time=$2
	expect=$3
	prereqs=$4
	zone=$5
	test_expect_success $prereqs "show date ($format:$time)" '
		echo "$time -> $expect" >expect &&
		TZ=${zone:-$TZ} test-tool date show:"$format" "$time" >actual &&
		test_cmp expect actual
	'
}

# arbitrary but sensible time for examples
TIME='1466000000 +0200'
check_show iso8601 "$TIME" '2016-06-15 16:13:20 +0200'
check_show iso8601-strict "$TIME" '2016-06-15T16:13:20+02:00'
check_show iso8601-strict "$(echo "$TIME" | sed 's/+0200$/+0000/')" '2016-06-15T14:13:20Z'
check_show rfc2822 "$TIME" 'Wed, 15 Jun 2016 16:13:20 +0200'
check_show short "$TIME" '2016-06-15'
check_show default "$TIME" 'Wed Jun 15 16:13:20 2016 +0200'
check_show raw "$TIME" '1466000000 +0200'
check_show unix "$TIME" '1466000000'
check_show iso-local "$TIME" '2016-06-15 14:13:20 +0000'
check_show raw-local "$TIME" '1466000000 +0000'
check_show unix-local "$TIME" '1466000000'

check_show 'format:%z' "$TIME" '+0200'
check_show 'format-local:%z' "$TIME" '+0000'
check_show 'format:%Z' "$TIME" ''
check_show 'format-local:%Z' "$TIME" 'UTC'
check_show 'format:%%z' "$TIME" '%z'
check_show 'format-local:%%z' "$TIME" '%z'

check_show 'format:%Y-%m-%d %H:%M:%S' "$TIME" '2016-06-15 16:13:20'
check_show 'format-local:%Y-%m-%d %H:%M:%S' "$TIME" '2016-06-15 09:13:20' '' EST5

check_show 'format:%s' '123456789 +1234' 123456789
check_show 'format:%s' '123456789 -1234' 123456789
check_show 'format-local:%s' '123456789 -1234' 123456789

# negative TZ offset
TIME='1466000000 -0200'
check_show iso8601 "$TIME" '2016-06-15 12:13:20 -0200'
check_show iso8601-strict "$TIME" '2016-06-15T12:13:20-02:00'
check_show rfc2822 "$TIME" 'Wed, 15 Jun 2016 12:13:20 -0200'
check_show default "$TIME" 'Wed Jun 15 12:13:20 2016 -0200'
check_show raw "$TIME" '1466000000 -0200'

# arbitrary time absurdly far in the future
FUTURE="5758122296 -0400"
check_show iso       "$FUTURE" "2152-06-19 18:24:56 -0400" HAVE_64BIT_TIME
check_show iso-local "$FUTURE" "2152-06-19 22:24:56 +0000" HAVE_64BIT_TIME

REQUIRE_64BIT_TIME=
check_parse () {
	echo "$1 -> $2" >expect
	test_expect_success $REQUIRE_64BIT_TIME "parse date ($1${3:+ TZ=$3}) -> $2" "
		TZ=${3:-$TZ} test-tool date parse '$1' >actual &&
		test_cmp expect actual
	"
}

check_parse 2008 bad
check_parse 2008-02 bad
check_parse 2008-02-14 bad
check_parse '2008-02-14 20:30:45' '2008-02-14 20:30:45 +0000'
check_parse '2008-02-14 20:30:45 -0500' '2008-02-14 20:30:45 -0500'
check_parse '2008.02.14 20:30:45 -0500' '2008-02-14 20:30:45 -0500'
check_parse '20080214T20:30:45' '2008-02-14 20:30:45 +0000'
check_parse '20080214T20:30' '2008-02-14 20:30:00 +0000'
check_parse '20080214T20' '2008-02-14 20:00:00 +0000'
check_parse '20080214T203045' '2008-02-14 20:30:45 +0000'
check_parse '20080214T2030' '2008-02-14 20:30:00 +0000'
check_parse '20080214T000000.20' '2008-02-14 00:00:00 +0000'
check_parse '20080214T00:00:00.20' '2008-02-14 00:00:00 +0000'
check_parse '20080214T203045-04:00' '2008-02-14 20:30:45 -0400'
check_parse '20080214T203045 -04:00' '2008-02-14 20:30:45 -0400'
check_parse '20080214T203045.019-04:00' '2008-02-14 20:30:45 -0400'
check_parse '2008-02-14 20:30:45.019-04:00' '2008-02-14 20:30:45 -0400'
check_parse '2008-02-14 20:30:45 -0015' '2008-02-14 20:30:45 -0015'
check_parse '2008-02-14 20:30:45 -5' '2008-02-14 20:30:45 +0000'
check_parse '2008-02-14 20:30:45 -5:' '2008-02-14 20:30:45 +0000'
check_parse '2008-02-14 20:30:45 -05' '2008-02-14 20:30:45 -0500'
check_parse '2008-02-14 20:30:45 -:30' '2008-02-14 20:30:45 +0000'
check_parse '2008-02-14 20:30:45 -05:00' '2008-02-14 20:30:45 -0500'
check_parse '2008-02-14 20:30:45' '2008-02-14 20:30:45 -0500' EST5
check_parse 'Thu, 7 Apr 2005 15:14:13 -0700' '2005-04-07 15:14:13 -0700'

check_parse '1970-01-01 00:00:00' '1970-01-01 00:00:00 +0000'
check_parse '1970-01-01 00:00:00 +00' '1970-01-01 00:00:00 +0000'
check_parse '1970-01-01 00:00:00 Z' '1970-01-01 00:00:00 +0000'
check_parse '1970-01-01 00:00:00 -01' '1970-01-01 00:00:00 -0100'
check_parse '1970-01-01 00:00:00 +01' bad
check_parse '1970-01-01 00:00:00 +11' bad
check_parse '1970-01-01 00:59:59 +01' bad
check_parse '1970-01-01 01:00:00 +01' '1970-01-01 01:00:00 +0100'
check_parse '1970-01-01 01:00:00 +11' bad
check_parse '1970-01-02 00:00:00 +11' '1970-01-02 00:00:00 +1100'
check_parse '1969-12-31 23:59:59' bad
check_parse '1969-12-31 23:59:59 +00' bad
check_parse '1969-12-31 23:59:59 Z' bad
check_parse '1969-12-31 23:59:59 +11' bad
check_parse '1969-12-31 23:59:59 -11' bad

REQUIRE_64BIT_TIME=HAVE_64BIT_TIME
check_parse '2099-12-31 23:59:59' '2099-12-31 23:59:59 +0000'
check_parse '2099-12-31 23:59:59 +00' '2099-12-31 23:59:59 +0000'
check_parse '2099-12-31 23:59:59 Z' '2099-12-31 23:59:59 +0000'
check_parse '2099-12-31 23:59:59 +01' '2099-12-31 23:59:59 +0100'
check_parse '2099-12-31 23:59:59 -01' bad
check_parse '2099-12-31 23:59:59 -11' bad
check_parse '2099-12-31 23:00:00 -01' bad
check_parse '2099-12-31 22:59:59 -01' '2099-12-31 22:59:59 -0100'
check_parse '2100-00-00 00:00:00' bad
check_parse '2099-12-30 00:00:00 -11' '2099-12-30 00:00:00 -1100'
check_parse '2100-00-00 00:00:00 +00' bad
check_parse '2100-00-00 00:00:00 Z' bad
check_parse '2100-00-00 00:00:00 -11' bad
check_parse '2100-00-00 00:00:00 +11' bad
REQUIRE_64BIT_TIME=

check_approxidate() {
	echo "$1 -> $2 +0000" >expect
	test_expect_${3:-success} "parse approxidate ($1)" "
	test-tool date approxidate '$1' >actual &&
	test_cmp expect actual
	"
}

check_approxidate now '2009-08-30 19:20:00'
check_approxidate '5 seconds ago' '2009-08-30 19:19:55'
check_approxidate 5.seconds.ago '2009-08-30 19:19:55'
check_approxidate 10.minutes.ago '2009-08-30 19:10:00'
check_approxidate yesterday '2009-08-29 19:20:00'
check_approxidate 3.days.ago '2009-08-27 19:20:00'
check_approxidate '12:34:56.3.days.ago' '2009-08-27 12:34:56'
check_approxidate 3.weeks.ago '2009-08-09 19:20:00'
check_approxidate 3.months.ago '2009-05-30 19:20:00'
check_approxidate 2.years.3.months.ago '2007-05-30 19:20:00'

check_approxidate '6am yesterday' '2009-08-29 06:00:00'
check_approxidate '6pm yesterday' '2009-08-29 18:00:00'
check_approxidate '3:00' '2009-08-30 03:00:00'
check_approxidate '15:00' '2009-08-30 15:00:00'
check_approxidate 'noon today' '2009-08-30 12:00:00'
check_approxidate 'noon yesterday' '2009-08-29 12:00:00'
check_approxidate 'January 5th noon pm' '2009-01-05 12:00:00'
check_approxidate '10am noon' '2009-08-29 12:00:00'

check_approxidate 'last tuesday' '2009-08-25 19:20:00'
check_approxidate 'July 5th' '2009-07-05 19:20:00'
check_approxidate '06/05/2009' '2009-06-05 19:20:00'
check_approxidate '06.05.2009' '2009-05-06 19:20:00'

check_approxidate 'Jun 6, 5AM' '2009-06-06 05:00:00'
check_approxidate '5AM Jun 6' '2009-06-06 05:00:00'
check_approxidate '6AM, June 7, 2009' '2009-06-07 06:00:00'

check_approxidate '2008-12-01' '2008-12-01 19:20:00'
check_approxidate '2009-12-01' '2009-12-01 19:20:00'

check_date_format_human() {
	t=$(($GIT_TEST_DATE_NOW - $1))
	echo "$t -> $2" >expect
	test_expect_success "human date $t" '
		test-tool date human $t >actual &&
		test_cmp expect actual
'
}

check_date_format_human 18000 "5 hours ago" # 5 hours ago
check_date_format_human 432000 "Tue Aug 25 19:20" # 5 days ago
check_date_format_human 1728000 "Mon Aug 10 19:20" # 3 weeks ago
check_date_format_human 13000000 "Thu Apr 2 08:13" # 5 months ago
check_date_format_human 31449600 "Aug 31 2008" # 12 months ago
check_date_format_human 37500000 "Jun 22 2008" # 1 year, 2 months ago
check_date_format_human 55188000 "Dec 1 2007" # 1 year, 9 months ago
check_date_format_human 630000000 "Sep 13 1989" # 20 years ago

test_done
