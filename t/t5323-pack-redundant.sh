#!/bin/sh
#
# Copyright (c) 2018 Jiang Xin
#

test_description='Test but pack-redundant

In order to test but-pack-redundant, we will create a number of objects and
packs in the repository `main.but`. The relationship between packs (P1-P8)
and objects (T, A-R) is showed in the following chart. Objects of a pack will
be marked with letter x, while objects of redundant packs will be marked with
exclamation point, and redundant pack itself will be marked with asterisk.

	| T A B C D E F G H I J K L M N O P Q R
    ----+--------------------------------------
    P1  | x x x x x x x                       x
    P2* |     ! ! ! !   ! ! !
    P3  |             x     x x x x x
    P4* |                     ! ! ! !     !
    P5  |               x x           x x
    P6* |                             ! !   !
    P7  |                                 x x
    P8* |   !
    ----+--------------------------------------
    ALL | x x x x x x x x x x x x x x x x x x x

Another repository `shared.but` has unique objects (X-Z), while other objects
(marked with letter s) are shared through alt-odb (of `main.but`). The
relationship between packs and objects is as follows:

	| T A B C D E F G H I J K L M N O P Q R   X Y Z
    ----+----------------------------------------------
    Px1 |   s s s                                 x x x
    Px2 |         s s s                           x x x
'

. ./test-lib.sh

main_repo=main.but
shared_repo=shared.but

but_pack_redundant='but pack-redundant --i-still-use-this'

# Create cummits in <repo> and assign each cummit's oid to shell variables
# given in the arguments (A, B, and C). E.g.:
#
#     create_cummits_in <repo> A B C
#
# NOTE: Avoid calling this function from a subshell since variable
# assignments will disappear when subshell exits.
create_cummits_in () {
	repo="$1" &&
	if ! parent=$(but -C "$repo" rev-parse HEAD^{} 2>/dev/null)
	then
		parent=
	fi &&
	T=$(but -C "$repo" write-tree) &&
	shift &&
	while test $# -gt 0
	do
		name=$1 &&
		test_tick &&
		if test -z "$parent"
		then
			oid=$(echo $name | but -C "$repo" cummit-tree $T)
		else
			oid=$(echo $name | but -C "$repo" cummit-tree -p $parent $T)
		fi &&
		eval $name=$oid &&
		parent=$oid &&
		shift ||
		return 1
	done &&
	but -C "$repo" update-ref refs/heads/main $oid
}

# Create pack in <repo> and assign pack id to variable given in the 2nd argument
# (<name>). cummits in the pack will be read from stdin. E.g.:
#
#     create_pack_in <repo> <name> <<-EOF
#         ...
#         EOF
#
# NOTE: cummits from stdin should be given using heredoc, not using pipe, and
# avoid calling this function from a subshell since variable assignments will
# disappear when subshell exits.
create_pack_in () {
	repo="$1" &&
	name="$2" &&
	pack=$(but -C "$repo/objects/pack" pack-objects -q pack) &&
	eval $name=$pack &&
	eval P$pack=$name:$pack
}

format_packfiles () {
	sed \
		-e "s#.*/pack-\(.*\)\.idx#\1#" \
		-e "s#.*/pack-\(.*\)\.pack#\1#" |
	sort -u |
	while read p
	do
		if test -z "$(eval echo \${P$p})"
		then
			echo $p
		else
			eval echo "\${P$p}"
		fi
	done |
	sort
}

test_expect_success 'setup main repo' '
	but init --bare "$main_repo" &&
	create_cummits_in "$main_repo" A B C D E F G H I J K L M N O P Q R
'

test_expect_success 'main: pack-redundant works with no packfile' '
	(
		cd "$main_repo" &&
		cat >expect <<-EOF &&
			fatal: Zero packs found!
			EOF
		test_must_fail $but_pack_redundant --all >actual 2>&1 &&
		test_cmp expect actual
	)
'

#############################################################################
# Chart of packs and objects for this test case
#
#         | T A B C D E F G H I J K L M N O P Q R
#     ----+--------------------------------------
#     P1  | x x x x x x x                       x
#     ----+--------------------------------------
#     ALL | x x x x x x x                       x
#
#############################################################################
test_expect_success 'main: pack-redundant works with one packfile' '
	create_pack_in "$main_repo" P1 <<-EOF &&
		$T
		$A
		$B
		$C
		$D
		$E
		$F
		$R
		EOF
	(
		cd "$main_repo" &&
		$but_pack_redundant --all >out &&
		test_must_be_empty out
	)
'

#############################################################################
# Chart of packs and objects for this test case
#
#         | T A B C D E F G H I J K L M N O P Q R
#     ----+--------------------------------------
#     P1  | x x x x x x x                       x
#     P2  |     x x x x   x x x
#     P3  |             x     x x x x x
#     ----+--------------------------------------
#     ALL | x x x x x x x x x x x x x x         x
#
#############################################################################
test_expect_success 'main: no redundant for pack 1, 2, 3' '
	create_pack_in "$main_repo" P2 <<-EOF &&
		$B
		$C
		$D
		$E
		$G
		$H
		$I
		EOF
	create_pack_in "$main_repo" P3 <<-EOF &&
		$F
		$I
		$J
		$K
		$L
		$M
		EOF
	(
		cd "$main_repo" &&
		$but_pack_redundant --all >out &&
		test_must_be_empty out
	)
'

#############################################################################
# Chart of packs and objects for this test case
#
#         | T A B C D E F G H I J K L M N O P Q R
#     ----+--------------------------------------
#     P1  | x x x x x x x                       x
#     P2  |     x x x x   x x x
#     P3* |             !     ! ! ! ! !
#     P4  |                     x x x x     x
#     P5  |               x x           x x
#     ----+--------------------------------------
#     ALL | x x x x x x x x x x x x x x x x x   x
#
#############################################################################
test_expect_success 'main: one of pack-2/pack-3 is redundant' '
	create_pack_in "$main_repo" P4 <<-EOF &&
		$J
		$K
		$L
		$M
		$P
		EOF
	create_pack_in "$main_repo" P5 <<-EOF &&
		$G
		$H
		$N
		$O
		EOF
	(
		cd "$main_repo" &&
		cat >expect <<-EOF &&
			P3:$P3
			EOF
		$but_pack_redundant --all >out &&
		format_packfiles <out >actual &&
		test_cmp expect actual
	)
'

#############################################################################
# Chart of packs and objects for this test case
#
#         | T A B C D E F G H I J K L M N O P Q R
#     ----+--------------------------------------
#     P1  | x x x x x x x                       x
#     P2* |     ! ! ! !   ! ! !
#     P3  |             x     x x x x x
#     P4* |                     ! ! ! !     !
#     P5  |               x x           x x
#     P6* |                             ! !   !
#     P7  |                                 x x
#     ----+--------------------------------------
#     ALL | x x x x x x x x x x x x x x x x x x x
#
#############################################################################
test_expect_success 'main: pack 2, 4, and 6 are redundant' '
	create_pack_in "$main_repo" P6 <<-EOF &&
		$N
		$O
		$Q
		EOF
	create_pack_in "$main_repo" P7 <<-EOF &&
		$P
		$Q
		EOF
	(
		cd "$main_repo" &&
		cat >expect <<-EOF &&
			P2:$P2
			P4:$P4
			P6:$P6
			EOF
		$but_pack_redundant --all >out &&
		format_packfiles <out >actual &&
		test_cmp expect actual
	)
'

#############################################################################
# Chart of packs and objects for this test case
#
#         | T A B C D E F G H I J K L M N O P Q R
#     ----+--------------------------------------
#     P1  | x x x x x x x                       x
#     P2* |     ! ! ! !   ! ! !
#     P3  |             x     x x x x x
#     P4* |                     ! ! ! !     !
#     P5  |               x x           x x
#     P6* |                             ! !   !
#     P7  |                                 x x
#     P8* |   !
#     ----+--------------------------------------
#     ALL | x x x x x x x x x x x x x x x x x x x
#
#############################################################################
test_expect_success 'main: pack-8 (subset of pack-1) is also redundant' '
	create_pack_in "$main_repo" P8 <<-EOF &&
		$A
		EOF
	(
		cd "$main_repo" &&
		cat >expect <<-EOF &&
			P2:$P2
			P4:$P4
			P6:$P6
			P8:$P8
			EOF
		$but_pack_redundant --all >out &&
		format_packfiles <out >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'main: clean loose objects' '
	(
		cd "$main_repo" &&
		but prune-packed &&
		find objects -type f | sed -e "/objects\/pack\//d" >out &&
		test_must_be_empty out
	)
'

test_expect_success 'main: remove redundant packs and pass fsck' '
	(
		cd "$main_repo" &&
		$but_pack_redundant --all | xargs rm &&
		but fsck &&
		$but_pack_redundant --all >out &&
		test_must_be_empty out
	)
'

# The following test cases will execute inside `shared.but`, instead of
# inside `main.but`.
test_expect_success 'setup shared.but' '
	but clone --mirror "$main_repo" "$shared_repo" &&
	(
		cd "$shared_repo" &&
		printf "../../$main_repo/objects\n" >objects/info/alternates
	)
'

test_expect_success 'shared: all packs are redundant, but no output without --alt-odb' '
	(
		cd "$shared_repo" &&
		$but_pack_redundant --all >out &&
		test_must_be_empty out
	)
'

#############################################################################
# Chart of packs and objects for this test case
#
#     ================= main.but ================
#         | T A B C D E F G H I J K L M N O P Q R  <----------+
#     ----+--------------------------------------             |
#     P1  | x x x x x x x                       x             |
#     P3  |             x     x x x x x                       |
#     P5  |               x x           x x                   |
#     P7  |                                 x x               |
#     ----+--------------------------------------             |
#     ALL | x x x x x x x x x x x x x x x x x x x             |
#                                                             |
#                                                             |
#     ================ shared.but ===============             |
#         | T A B C D E F G H I J K L M N O P Q R  <objects/info/alternates>
#     ----+--------------------------------------
#     P1* | s s s s s s s                       s
#     P3* |             s     s s s s s
#     P5* |               s s           s s
#     P7* |                                 s s
#     ----+--------------------------------------
#     ALL | x x x x x x x x x x x x x x x x x x x
#
#############################################################################
test_expect_success 'shared: show redundant packs in stderr for verbose mode' '
	(
		cd "$shared_repo" &&
		cat >expect <<-EOF &&
			P1:$P1
			P3:$P3
			P5:$P5
			P7:$P7
			EOF
		$but_pack_redundant --all --verbose >out 2>out.err &&
		test_must_be_empty out &&
		grep "pack$" out.err | format_packfiles >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'shared: remove redundant packs, no packs left' '
	(
		cd "$shared_repo" &&
		cat >expect <<-EOF &&
			fatal: Zero packs found!
			EOF
		$but_pack_redundant --all --alt-odb | xargs rm &&
		but fsck &&
		test_must_fail $but_pack_redundant --all --alt-odb >actual 2>&1 &&
		test_cmp expect actual
	)
'

test_expect_success 'shared: create new objects and packs' '
	create_cummits_in "$shared_repo" X Y Z &&
	create_pack_in "$shared_repo" Px1 <<-EOF &&
		$X
		$Y
		$Z
		$A
		$B
		$C
		EOF
	create_pack_in "$shared_repo" Px2 <<-EOF
		$X
		$Y
		$Z
		$D
		$E
		$F
		EOF
'

test_expect_success 'shared: no redundant without --alt-odb' '
	(
		cd "$shared_repo" &&
		$but_pack_redundant --all >out &&
		test_must_be_empty out
	)
'

#############################################################################
# Chart of packs and objects for this test case
#
#     ================= main.but ================
#         | T A B C D E F G H I J K L M N O P Q R  <----------------+
#     ----+--------------------------------------                   |
#     P1  | x x x x x x x                       x                   |
#     P3  |             x     x x x x x                             |
#     P5  |               x x           x x                         |
#     P7  |                                 x x                     |
#     ----+--------------------------------------                   |
#     ALL | x x x x x x x x x x x x x x x x x x x                   |
#                                                                   |
#                                                                   |
#     ================ shared.but =======================           |
#         | T A B C D E F G H I J K L M N O P Q R   X Y Z <objects/info/alternates>
#     ----+----------------------------------------------
#     Px1 |   s s s                                 x x x
#     Px2*|         s s s                           ! ! !
#     ----+----------------------------------------------
#     ALL | s s s s s s s s s s s s s s s s s s s   x x x
#
#############################################################################
test_expect_success 'shared: one pack is redundant with --alt-odb' '
	(
		cd "$shared_repo" &&
		$but_pack_redundant --all --alt-odb >out &&
		format_packfiles <out >actual &&
		test_line_count = 1 actual
	)
'

#############################################################################
# Chart of packs and objects for this test case
#
#     ================= main.but ================
#         | T A B C D E F G H I J K L M N O P Q R  <----------------+
#     ----+--------------------------------------                   |
#     P1  | x x x x x x x                       x                   |
#     P3  |             x     x x x x x                             |
#     P5  |               x x           x x                         |
#     P7  |                                 x x                     |
#     ----+--------------------------------------                   |
#     ALL | x x x x x x x x x x x x x x x x x x x                   |
#                                                                   |
#                                                                   |
#     ================ shared.but =======================           |
#         | T A B C D E F G H I J K L M N O P Q R   X Y Z <objects/info/alternates>
#     ----+----------------------------------------------
#     Px1*|   s s s                                 i i i
#     Px2*|         s s s                           i i i
#     ----+----------------------------------------------
#     ALL | s s s s s s s s s s s s s s s s s s s   i i i
#                                                  (ignored objects, marked with i)
#
#############################################################################
test_expect_success 'shared: ignore unique objects and all two packs are redundant' '
	(
		cd "$shared_repo" &&
		cat >expect <<-EOF &&
			Px1:$Px1
			Px2:$Px2
			EOF
		$but_pack_redundant --all --alt-odb >out <<-EOF &&
			$X
			$Y
			$Z
			EOF
		format_packfiles <out >actual &&
		test_cmp expect actual
	)
'

test_done
