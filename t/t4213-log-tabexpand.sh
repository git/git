#!/bin/sh

test_description='log/show --expand-tabs'

. ./test-lib.sh

HT="	"
title='tab indent at the beginning of the title line'
body='tab indent on a line in the body'

# usage: count_expand $indent $numSP $numHT @format_args
count_expand ()
{
	expect=
	count=$(( $1 + $2 )) ;# expected spaces
	while test $count -gt 0
	do
		expect="$expect "
		count=$(( $count - 1 ))
	done
	shift 2
	count=$1 ;# expected tabs
	while test $count -gt 0
	do
		expect="$expect$HT"
		count=$(( $count - 1 ))
	done
	shift

	# The remainder of the command line is "git show -s" options
	case " $* " in
	*' --pretty=short '*)
		line=$title ;;
	*)
		line=$body ;;
	esac

	# Prefix the output with the command line arguments, and
	# replace SP with a dot both in the expected and actual output
	# so that test_cmp would show the difference together with the
	# breakage in a way easier to consume by the debugging user.
	{
		echo "git show -s $*"
		echo "$expect$line"
	} | sed -e 's/ /./g' >expect

	{
		echo "git show -s $*"
		git show -s "$@" |
		sed -n -e "/$line\$/p"
	} | sed -e 's/ /./g' >actual

	test_cmp expect actual
}

test_expand ()
{
	fmt=$1
	case "$fmt" in
	*=raw | *=short | *=email)
		default="0 1" ;;
	*)
		default="8 0" ;;
	esac
	case "$fmt" in
	*=email)
		in=0 ;;
	*)
		in=4 ;;
	esac
	test_expect_success "expand/no-expand${fmt:+ for $fmt}" '
		count_expand $in $default $fmt &&
		count_expand $in 8 0 $fmt --expand-tabs &&
		count_expand $in 8 0 --expand-tabs $fmt &&
		count_expand $in 8 0 $fmt --expand-tabs=8 &&
		count_expand $in 8 0 --expand-tabs=8 $fmt &&
		count_expand $in 0 1 $fmt --no-expand-tabs &&
		count_expand $in 0 1 --no-expand-tabs $fmt &&
		count_expand $in 0 1 $fmt --expand-tabs=0 &&
		count_expand $in 0 1 --expand-tabs=0 $fmt &&
		count_expand $in 4 0 $fmt --expand-tabs=4 &&
		count_expand $in 4 0 --expand-tabs=4 $fmt
	'
}

test_expect_success 'setup' '
	test_tick &&
	sed -e "s/Q/$HT/g" <<-EOF >msg &&
	Q$title

	Q$body
	EOF
	git commit --allow-empty -F msg
'

test_expand ""
test_expand --pretty
test_expand --pretty=short
test_expand --pretty=medium
test_expand --pretty=full
test_expand --pretty=fuller
test_expand --pretty=raw
test_expand --pretty=email

test_done
