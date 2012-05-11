#!/bin/sh
#
# Copyright (c) 2007 Junio C Hamano
#

test_description='Test prune and reflog expiration'
. ./test-lib.sh

check_have () {
	gaah= &&
	for N in "$@"
	do
		eval "o=\$$N" && git cat-file -t $o || {
			echo Gaah $N
			gaah=$N
			break
		}
	done &&
	test -z "$gaah"
}

check_fsck () {
	output=$(git fsck --full)
	case "$1" in
	'')
		test -z "$output" ;;
	*)
		echo "$output" | grep "$1" ;;
	esac
}

corrupt () {
	aa=${1%??????????????????????????????????????} zz=${1#??}
	mv .git/objects/$aa/$zz .git/$aa$zz
}

recover () {
	aa=${1%??????????????????????????????????????} zz=${1#??}
	mkdir -p .git/objects/$aa
	mv .git/$aa$zz .git/objects/$aa/$zz
}

check_dont_have () {
	gaah= &&
	for N in "$@"
	do
		eval "o=\$$N"
		git cat-file -t $o && {
			echo Gaah $N
			gaah=$N
			break
		}
	done
	test -z "$gaah"
}

test_expect_success setup '
	mkdir -p A/B &&
	echo rat >C &&
	echo ox >A/D &&
	echo tiger >A/B/E &&
	git add . &&

	test_tick && git commit -m rabbit &&
	H=`git rev-parse --verify HEAD` &&
	A=`git rev-parse --verify HEAD:A` &&
	B=`git rev-parse --verify HEAD:A/B` &&
	C=`git rev-parse --verify HEAD:C` &&
	D=`git rev-parse --verify HEAD:A/D` &&
	E=`git rev-parse --verify HEAD:A/B/E` &&
	check_fsck &&

	test_chmod +x C &&
	git add C &&
	test_tick && git commit -m dragon &&
	L=`git rev-parse --verify HEAD` &&
	check_fsck &&

	rm -f C A/B/E &&
	echo snake >F &&
	echo horse >A/G &&
	git add F A/G &&
	test_tick && git commit -a -m sheep &&
	F=`git rev-parse --verify HEAD:F` &&
	G=`git rev-parse --verify HEAD:A/G` &&
	I=`git rev-parse --verify HEAD:A` &&
	J=`git rev-parse --verify HEAD` &&
	check_fsck &&

	rm -f A/G &&
	test_tick && git commit -a -m monkey &&
	K=`git rev-parse --verify HEAD` &&
	check_fsck &&

	check_have A B C D E F G H I J K L &&

	git prune &&

	check_have A B C D E F G H I J K L &&

	check_fsck &&

	test_line_count = 4 .git/logs/refs/heads/master
'

test_expect_success rewind '
	test_tick && git reset --hard HEAD~2 &&
	test -f C &&
	test -f A/B/E &&
	! test -f F &&
	! test -f A/G &&

	check_have A B C D E F G H I J K L &&

	git prune &&

	check_have A B C D E F G H I J K L &&

	test_line_count = 5 .git/logs/refs/heads/master
'

test_expect_success 'corrupt and check' '

	corrupt $F &&
	check_fsck "missing blob $F"

'

test_expect_success 'reflog expire --dry-run should not touch reflog' '

	git reflog expire --dry-run \
		--expire=$(($test_tick - 10000)) \
		--expire-unreachable=$(($test_tick - 10000)) \
		--stale-fix \
		--all &&

	test_line_count = 5 .git/logs/refs/heads/master &&

	check_fsck "missing blob $F"
'

test_expect_success 'reflog expire' '

	git reflog expire --verbose \
		--expire=$(($test_tick - 10000)) \
		--expire-unreachable=$(($test_tick - 10000)) \
		--stale-fix \
		--all &&

	test_line_count = 2 .git/logs/refs/heads/master &&

	check_fsck "dangling commit $K"
'

test_expect_success 'prune and fsck' '

	git prune &&
	check_fsck &&

	check_have A B C D E H L &&
	check_dont_have F G I J K

'

test_expect_success 'recover and check' '

	recover $F &&
	check_fsck "dangling blob $F"

'

test_expect_success 'delete' '
	echo 1 > C &&
	test_tick &&
	git commit -m rat C &&

	echo 2 > C &&
	test_tick &&
	git commit -m ox C &&

	echo 3 > C &&
	test_tick &&
	git commit -m tiger C &&

	HEAD_entry_count=$(git reflog | wc -l) &&
	master_entry_count=$(git reflog show master | wc -l) &&

	test $HEAD_entry_count = 5 &&
	test $master_entry_count = 5 &&


	git reflog delete master@{1} &&
	git reflog show master > output &&
	test $(($master_entry_count - 1)) = $(wc -l < output) &&
	test $HEAD_entry_count = $(git reflog | wc -l) &&
	! grep ox < output &&

	master_entry_count=$(wc -l < output) &&

	git reflog delete HEAD@{1} &&
	test $(($HEAD_entry_count -1)) = $(git reflog | wc -l) &&
	test $master_entry_count = $(git reflog show master | wc -l) &&

	HEAD_entry_count=$(git reflog | wc -l) &&

	git reflog delete master@{07.04.2005.15:15:00.-0700} &&
	git reflog show master > output &&
	test $(($master_entry_count - 1)) = $(wc -l < output) &&
	! grep dragon < output

'

test_expect_success 'rewind2' '

	test_tick && git reset --hard HEAD~2 &&
	test_line_count = 4 .git/logs/refs/heads/master
'

test_expect_success '--expire=never' '

	git reflog expire --verbose \
		--expire=never \
		--expire-unreachable=never \
		--all &&
	test_line_count = 4 .git/logs/refs/heads/master
'

test_expect_success 'gc.reflogexpire=never' '

	git config gc.reflogexpire never &&
	git config gc.reflogexpireunreachable never &&
	git reflog expire --verbose --all &&
	test_line_count = 4 .git/logs/refs/heads/master
'

test_expect_success 'gc.reflogexpire=false' '

	git config gc.reflogexpire false &&
	git config gc.reflogexpireunreachable false &&
	git reflog expire --verbose --all &&
	test_line_count = 4 .git/logs/refs/heads/master &&

	git config --unset gc.reflogexpire &&
	git config --unset gc.reflogexpireunreachable

'

test_done
