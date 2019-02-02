#!/bin/sh
#
# Copyright (c) 2018 Jiang Xin
#

test_description='Test git pack-redundant

In order to test git-pack-redundant, we will create a number of objects and
packs in the repository `master.git`. The relationship between packs (P1-P8)
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

Another repository `shared.git` has unique objects (X-Z), while other objects
(marked with letter s) are shared through alt-odb (of `master.git`). The
relationship between packs and objects is as follows:

	| T A B C D E F G H I J K L M N O P Q R   X Y Z
    ----+----------------------------------------------
    Px1 |   s s s                                 x x x
    Px2 |         s s s                           x x x
'

. ./test-lib.sh

master_repo=master.git
shared_repo=shared.git

# Create commits in <repo> and assign each commit's oid to shell variables
# given in the arguments (A, B, and C). E.g.:
#
#     create_commits_in <repo> A B C
#
# NOTE: Avoid calling this function from a subshell since variable
# assignments will disappear when subshell exits.
create_commits_in () {
	repo="$1" &&
	if ! parent=$(git -C "$repo" rev-parse HEAD^{} 2>/dev/null)
	then
		parent=
	fi &&
	T=$(git -C "$repo" write-tree) &&
	shift &&
	while test $# -gt 0
	do
		name=$1 &&
		test_tick &&
		if test -z "$parent"
		then
			oid=$(echo $name | git -C "$repo" commit-tree $T)
		else
			oid=$(echo $name | git -C "$repo" commit-tree -p $parent $T)
		fi &&
		eval $name=$oid &&
		parent=$oid &&
		shift ||
		return 1
	done &&
	git -C "$repo" update-ref refs/heads/master $oid
}

# Create pack in <repo> and assign pack id to variable given in the 2nd argument
# (<name>). Commits in the pack will be read from stdin. E.g.:
#
#     create_pack_in <repo> <name> <<-EOF
#         ...
#         EOF
#
# NOTE: commits from stdin should be given using heredoc, not using pipe, and
# avoid calling this function from a subshell since variable assignments will
# disappear when subshell exits.
create_pack_in () {
	repo="$1" &&
	name="$2" &&
	pack=$(git -C "$repo/objects/pack" pack-objects -q pack) &&
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

test_expect_success 'setup master repo' '
	git init --bare "$master_repo" &&
	create_commits_in "$master_repo" A B C D E F G H I J K L M N O P Q R
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
test_expect_success 'master: no redundant for pack 1, 2, 3' '
	create_pack_in "$master_repo" P1 <<-EOF &&
		$T
		$A
		$B
		$C
		$D
		$E
		$F
		$R
		EOF
	create_pack_in "$master_repo" P2 <<-EOF &&
		$B
		$C
		$D
		$E
		$G
		$H
		$I
		EOF
	create_pack_in "$master_repo" P3 <<-EOF &&
		$F
		$I
		$J
		$K
		$L
		$M
		EOF
	(
		cd "$master_repo" &&
		git pack-redundant --all >out &&
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
test_expect_success 'master: one of pack-2/pack-3 is redundant' '
	create_pack_in "$master_repo" P4 <<-EOF &&
		$J
		$K
		$L
		$M
		$P
		EOF
	create_pack_in "$master_repo" P5 <<-EOF &&
		$G
		$H
		$N
		$O
		EOF
	(
		cd "$master_repo" &&
		cat >expect <<-EOF &&
			P3:$P3
			EOF
		git pack-redundant --all >out &&
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
test_expect_success 'master: pack 2, 4, and 6 are redundant' '
	create_pack_in "$master_repo" P6 <<-EOF &&
		$N
		$O
		$Q
		EOF
	create_pack_in "$master_repo" P7 <<-EOF &&
		$P
		$Q
		EOF
	(
		cd "$master_repo" &&
		cat >expect <<-EOF &&
			P2:$P2
			P4:$P4
			P6:$P6
			EOF
		git pack-redundant --all >out &&
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
test_expect_success 'master: pack-8 (subset of pack-1) is also redundant' '
	create_pack_in "$master_repo" P8 <<-EOF &&
		$A
		EOF
	(
		cd "$master_repo" &&
		cat >expect <<-EOF &&
			P2:$P2
			P4:$P4
			P6:$P6
			P8:$P8
			EOF
		git pack-redundant --all >out &&
		format_packfiles <out >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'master: clean loose objects' '
	(
		cd "$master_repo" &&
		git prune-packed &&
		find objects -type f | sed -e "/objects\/pack\//d" >out &&
		test_must_be_empty out
	)
'

test_expect_success 'master: remove redundant packs and pass fsck' '
	(
		cd "$master_repo" &&
		git pack-redundant --all | xargs rm &&
		git fsck &&
		git pack-redundant --all >out &&
		test_must_be_empty out
	)
'

# The following test cases will execute inside `shared.git`, instead of
# inside `master.git`.
test_expect_success 'setup shared.git' '
	git clone --mirror "$master_repo" "$shared_repo" &&
	(
		cd "$shared_repo" &&
		printf "../../$master_repo/objects\n" >objects/info/alternates
	)
'

test_expect_success 'shared: all packs are redundant, but no output without --alt-odb' '
	(
		cd "$shared_repo" &&
		git pack-redundant --all >out &&
		test_must_be_empty out
	)
'

#############################################################################
# Chart of packs and objects for this test case
#
#     ================ master.git ===============
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
#     ================ shared.git ===============             |
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
		git pack-redundant --all --verbose >out 2>out.err &&
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
		git pack-redundant --all --alt-odb | xargs rm &&
		git fsck &&
		test_must_fail git pack-redundant --all --alt-odb >actual 2>&1 &&
		test_cmp expect actual
	)
'

test_expect_success 'shared: create new objects and packs' '
	create_commits_in "$shared_repo" X Y Z &&
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
		git pack-redundant --all >out &&
		test_must_be_empty out
	)
'

#############################################################################
# Chart of packs and objects for this test case
#
#     ================ master.git ===============
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
#     ================ shared.git =======================           |
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
		git pack-redundant --all --alt-odb >out &&
		format_packfiles <out >actual &&
		test_line_count = 1 actual
	)
'

#############################################################################
# Chart of packs and objects for this test case
#
#     ================ master.git ===============
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
#     ================ shared.git =======================           |
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
		git pack-redundant --all --alt-odb >out <<-EOF &&
			$X
			$Y
			$Z
			EOF
		format_packfiles <out >actual &&
		test_cmp expect actual
	)
'

test_done
