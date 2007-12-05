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

	chmod +x C &&
	( test "`git config --bool core.filemode`" != false ||
	  echo executable >>C ) &&
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

	loglen=$(wc -l <.git/logs/refs/heads/master) &&
	test $loglen = 4
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

	loglen=$(wc -l <.git/logs/refs/heads/master) &&
	test $loglen = 5
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

	loglen=$(wc -l <.git/logs/refs/heads/master) &&
	test $loglen = 5 &&

	check_fsck "missing blob $F"
'

test_expect_success 'reflog expire' '

	git reflog expire --verbose \
		--expire=$(($test_tick - 10000)) \
		--expire-unreachable=$(($test_tick - 10000)) \
		--stale-fix \
		--all &&

	loglen=$(wc -l <.git/logs/refs/heads/master) &&
	test $loglen = 2 &&

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

test_expect_success 'prune --expire' '

	before=$(git count-objects | sed "s/ .*//") &&
	BLOB=$(echo aleph | git hash-object -w --stdin) &&
	BLOB_FILE=.git/objects/$(echo $BLOB | sed "s/^../&\//") &&
	test $((1 + $before)) = $(git count-objects | sed "s/ .*//") &&
	test -f $BLOB_FILE &&
	git reset --hard &&
	git prune --expire=1.hour.ago &&
	test $((1 + $before)) = $(git count-objects | sed "s/ .*//") &&
	test -f $BLOB_FILE &&
	test-chmtime -86500 $BLOB_FILE &&
	git prune --expire 1.day &&
	test $before = $(git count-objects | sed "s/ .*//") &&
	! test -f $BLOB_FILE

'

test_done
