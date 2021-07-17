#!/bin/sh
#
# Copyright (c) 2021 Jiang Xin
#

test_description='Test git-bundle'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-bundle.sh

# Create a commit or tag and set the variable with the object ID.
test_commit_setvar () {
	notick=
	signoff=
	indir=
	merge=
	tag=
	var=

	while test $# != 0
	do
		case "$1" in
		--merge)
			merge=t
			;;
		--tag)
			tag=t
			;;
		--notick)
			notick=t
			;;
		--signoff)
			signoff="$1"
			;;
		-C)
			shift
			indir="$1"
			;;
		-*)
			echo >&2 "error: unknown option $1"
			return 1
			;;
		*)
			break
			;;
		esac
		shift
	done
	if test $# -lt 2
	then
		echo >&2 "error: test_commit_setvar must have at least 2 arguments"
		return 1
	fi
	var=$1
	shift
	indir=${indir:+"$indir"/}
	if test -z "$notick"
	then
		test_tick
	fi &&
	if test -n "$merge"
	then
		git ${indir:+ -C "$indir"} merge --no-edit --no-ff \
			${2:+-m "$2"} "$1" &&
		oid=$(git ${indir:+ -C "$indir"} rev-parse HEAD)
	elif test -n "$tag"
	then
		git ${indir:+ -C "$indir"} tag -m "$1" "$1" "${2:-HEAD}" &&
		oid=$(git ${indir:+ -C "$indir"} rev-parse "$1")
	else
		file=${2:-"$1.t"} &&
		echo "${3-$1}" >"$indir$file" &&
		git ${indir:+ -C "$indir"} add "$file" &&
		git ${indir:+ -C "$indir"} commit $signoff -m "$1" &&
		oid=$(git ${indir:+ -C "$indir"} rev-parse HEAD)
	fi &&
	eval $var=$oid
}

get_abbrev_oid () {
	oid=$1 &&
	suffix=${oid#???????} &&
	oid=${oid%$suffix} &&
	if test -n "$oid"
	then
		echo "$oid"
	else
		echo "undefined-oid"
	fi
}

# Format the output of git commands to make a user-friendly and stable
# text.  We can easily prepare the expect text without having to worry
# about future changes of the commit ID.
make_user_friendly_and_stable_output () {
	sed \
		-e "s/$(get_abbrev_oid $A)[0-9a-f]*/<COMMIT-A>/g" \
		-e "s/$(get_abbrev_oid $B)[0-9a-f]*/<COMMIT-B>/g" \
		-e "s/$(get_abbrev_oid $C)[0-9a-f]*/<COMMIT-C>/g" \
		-e "s/$(get_abbrev_oid $D)[0-9a-f]*/<COMMIT-D>/g" \
		-e "s/$(get_abbrev_oid $E)[0-9a-f]*/<COMMIT-E>/g" \
		-e "s/$(get_abbrev_oid $F)[0-9a-f]*/<COMMIT-F>/g" \
		-e "s/$(get_abbrev_oid $G)[0-9a-f]*/<COMMIT-G>/g" \
		-e "s/$(get_abbrev_oid $H)[0-9a-f]*/<COMMIT-H>/g" \
		-e "s/$(get_abbrev_oid $I)[0-9a-f]*/<COMMIT-I>/g" \
		-e "s/$(get_abbrev_oid $J)[0-9a-f]*/<COMMIT-J>/g" \
		-e "s/$(get_abbrev_oid $K)[0-9a-f]*/<COMMIT-K>/g" \
		-e "s/$(get_abbrev_oid $L)[0-9a-f]*/<COMMIT-L>/g" \
		-e "s/$(get_abbrev_oid $M)[0-9a-f]*/<COMMIT-M>/g" \
		-e "s/$(get_abbrev_oid $N)[0-9a-f]*/<COMMIT-N>/g" \
		-e "s/$(get_abbrev_oid $O)[0-9a-f]*/<COMMIT-O>/g" \
		-e "s/$(get_abbrev_oid $P)[0-9a-f]*/<COMMIT-P>/g" \
		-e "s/$(get_abbrev_oid $TAG1)[0-9a-f]*/<TAG-1>/g" \
		-e "s/$(get_abbrev_oid $TAG2)[0-9a-f]*/<TAG-2>/g" \
		-e "s/$(get_abbrev_oid $TAG3)[0-9a-f]*/<TAG-3>/g"
}

format_and_save_expect () {
	sed -e 's/Z$//' >expect
}

#            (C)   (D, pull/1/head, topic/1)
#             o --- o
#            /       \                              (L)
#           /         \        o (H, topic/2)             (M, tag:v2)
#          /    (F)    \      /                                 (N, tag:v3)
#         /      o --------- o (G, pull/2/head)      o --- o --- o (release)
#        /      /        \    \                      /       \
#  o --- o --- o -------- o -- o ------------------ o ------- o --- o (main)
# (A)   (B)  (E, tag:v1) (I)  (J)                  (K)       (O)   (P)
#
test_expect_success 'setup' '
	# Try to make a stable fixed width for abbreviated commit ID,
	# this fixed-width oid will be replaced with "<OID>".
	git config core.abbrev 7 &&

	# branch main: commit A & B
	test_commit_setvar A "Commit A" main.txt &&
	test_commit_setvar B "Commit B" main.txt &&

	# branch topic/1: commit C & D, refs/pull/1/head
	git checkout -b topic/1 &&
	test_commit_setvar C "Commit C" topic-1.txt &&
	test_commit_setvar D "Commit D" topic-1.txt &&
	git update-ref refs/pull/1/head HEAD &&

	# branch topic/1: commit E, tag v1
	git checkout main &&
	test_commit_setvar E "Commit E" main.txt &&
	test_commit_setvar --tag TAG1 v1 &&

	# branch topic/2: commit F & G, refs/pull/2/head
	git checkout -b topic/2 &&
	test_commit_setvar F "Commit F" topic-2.txt &&
	test_commit_setvar G "Commit G" topic-2.txt &&
	git update-ref refs/pull/2/head HEAD &&
	test_commit_setvar H "Commit H" topic-2.txt &&

	# branch main: merge commit I & J
	git checkout main &&
	test_commit_setvar --merge I topic/1 "Merge commit I" &&
	test_commit_setvar --merge J refs/pull/2/head "Merge commit J" &&

	# branch main: commit K
	git checkout main &&
	test_commit_setvar K "Commit K" main.txt &&

	# branch release:
	git checkout -b release &&
	test_commit_setvar L "Commit L" release.txt &&
	test_commit_setvar M "Commit M" release.txt &&
	test_commit_setvar --tag TAG2 v2 &&
	test_commit_setvar N "Commit N" release.txt &&
	test_commit_setvar --tag TAG3 v3 &&

	# branch main: merge commit O, commit P
	git checkout main &&
	test_commit_setvar --merge O tags/v2 "Merge commit O" &&
	test_commit_setvar P "Commit P" main.txt
'

test_expect_success 'create bundle from special rev: main^!' '
	git bundle create special-rev.bdl "main^!" &&

	git bundle list-heads special-rev.bdl |
		make_user_friendly_and_stable_output >actual &&
	cat >expect <<-\EOF &&
	<COMMIT-P> refs/heads/main
	EOF
	test_cmp expect actual &&

	git bundle verify special-rev.bdl |
		make_user_friendly_and_stable_output >actual &&
	format_and_save_expect <<-\EOF &&
	The bundle contains this ref:
	<COMMIT-P> refs/heads/main
	The bundle requires this ref:
	<COMMIT-O> Z
	EOF
	test_cmp expect actual &&

	test_bundle_object_count special-rev.bdl 3
'

test_expect_success 'create bundle with --max-count option' '
	git bundle create max-count.bdl --max-count 1 \
		main \
		"^release" \
		refs/tags/v1 \
		refs/pull/1/head \
		refs/pull/2/head &&

	git bundle verify max-count.bdl |
		make_user_friendly_and_stable_output >actual &&
	format_and_save_expect <<-\EOF &&
	The bundle contains these 2 refs:
	<COMMIT-P> refs/heads/main
	<TAG-1> refs/tags/v1
	The bundle requires this ref:
	<COMMIT-O> Z
	EOF
	test_cmp expect actual &&

	test_bundle_object_count max-count.bdl 4
'

test_expect_success 'create bundle with --since option' '
	git log -1 --pretty="%ad" $M >actual &&
	cat >expect <<-\EOF &&
	Thu Apr 7 15:26:13 2005 -0700
	EOF
	test_cmp expect actual &&

	git bundle create since.bdl \
		--since "Thu Apr 7 15:27:00 2005 -0700" \
		--all &&

	git bundle verify since.bdl |
		make_user_friendly_and_stable_output >actual &&
	format_and_save_expect <<-\EOF &&
	The bundle contains these 5 refs:
	<COMMIT-P> refs/heads/main
	<COMMIT-N> refs/heads/release
	<TAG-2> refs/tags/v2
	<TAG-3> refs/tags/v3
	<COMMIT-P> HEAD
	The bundle requires these 2 refs:
	<COMMIT-M> Z
	<COMMIT-K> Z
	EOF
	test_cmp expect actual &&

	test_bundle_object_count --thin since.bdl 13
'

test_expect_success 'create bundle 1 - no prerequisites' '
	# create bundle from args
	git bundle create 1.bdl topic/1 topic/2 &&

	# create bundle from stdin
	cat >input <<-\EOF &&
	topic/1
	topic/2
	EOF
	git bundle create stdin-1.bdl --stdin <input &&

	cat >expect <<-\EOF &&
	The bundle contains these 2 refs:
	<COMMIT-D> refs/heads/topic/1
	<COMMIT-H> refs/heads/topic/2
	The bundle records a complete history.
	EOF

	# verify bundle, which has no prerequisites
	git bundle verify 1.bdl |
		make_user_friendly_and_stable_output >actual &&
	test_cmp expect actual &&

	git bundle verify stdin-1.bdl |
		make_user_friendly_and_stable_output >actual &&
	test_cmp expect actual &&

	test_bundle_object_count       1.bdl 24 &&
	test_bundle_object_count stdin-1.bdl 24
'

test_expect_success 'create bundle 2 - has prerequisites' '
	# create bundle from args
	git bundle create 2.bdl \
		--ignore-missing \
		^topic/deleted \
		^$D \
		^topic/2 \
		release &&

	# create bundle from stdin
	# input has a non-exist reference: "topic/deleted"
	cat >input <<-EOF &&
	^topic/deleted
	^$D
	^topic/2
	EOF
	git bundle create stdin-2.bdl \
		--ignore-missing \
		--stdin \
		release <input &&

	format_and_save_expect <<-\EOF &&
	The bundle contains this ref:
	<COMMIT-N> refs/heads/release
	The bundle requires these 3 refs:
	<COMMIT-D> Z
	<COMMIT-E> Z
	<COMMIT-G> Z
	EOF

	git bundle verify 2.bdl |
		make_user_friendly_and_stable_output >actual &&
	test_cmp expect actual &&

	git bundle verify stdin-2.bdl |
		make_user_friendly_and_stable_output >actual &&
	test_cmp expect actual &&

	test_bundle_object_count       2.bdl 16 &&
	test_bundle_object_count stdin-2.bdl 16
'

test_expect_success 'fail to verify bundle without prerequisites' '
	git init --bare test1.git &&

	format_and_save_expect <<-\EOF &&
	error: Repository lacks these prerequisite commits:
	error: <COMMIT-D> Z
	error: <COMMIT-E> Z
	error: <COMMIT-G> Z
	EOF

	test_must_fail git -C test1.git bundle verify ../2.bdl 2>&1 |
		make_user_friendly_and_stable_output >actual &&
	test_cmp expect actual &&

	test_must_fail git -C test1.git bundle verify ../stdin-2.bdl 2>&1 |
		make_user_friendly_and_stable_output >actual &&
	test_cmp expect actual
'

test_expect_success 'create bundle 3 - two refs, same object' '
	# create bundle from args
	git bundle create --version=3 3.bdl \
		^release \
		^topic/1 \
		^topic/2 \
		main \
		HEAD &&

	# create bundle from stdin
	cat >input <<-\EOF &&
	^release
	^topic/1
	^topic/2
	EOF
	git bundle create --version=3 stdin-3.bdl \
		--stdin \
		main HEAD <input &&

	format_and_save_expect <<-\EOF &&
	The bundle contains these 2 refs:
	<COMMIT-P> refs/heads/main
	<COMMIT-P> HEAD
	The bundle requires these 2 refs:
	<COMMIT-M> Z
	<COMMIT-K> Z
	EOF

	git bundle verify 3.bdl |
		make_user_friendly_and_stable_output >actual &&
	test_cmp expect actual &&

	git bundle verify stdin-3.bdl |
		make_user_friendly_and_stable_output >actual &&
	test_cmp expect actual &&

	test_bundle_object_count       3.bdl 4 &&
	test_bundle_object_count stdin-3.bdl 4
'

test_expect_success 'create bundle 4 - with tags' '
	# create bundle from args
	git bundle create 4.bdl \
		^main \
		^release \
		^topic/1 \
		^topic/2 \
		--all &&

	# create bundle from stdin
	cat >input <<-\EOF &&
	^main
	^release
	^topic/1
	^topic/2
	EOF
	git bundle create stdin-4.bdl \
		--ignore-missing \
		--stdin \
		--all <input &&

	cat >expect <<-\EOF &&
	The bundle contains these 3 refs:
	<TAG-1> refs/tags/v1
	<TAG-2> refs/tags/v2
	<TAG-3> refs/tags/v3
	The bundle records a complete history.
	EOF

	git bundle verify 4.bdl |
		make_user_friendly_and_stable_output >actual &&
	test_cmp expect actual &&

	git bundle verify stdin-4.bdl |
		make_user_friendly_and_stable_output >actual &&
	test_cmp expect actual &&

	test_bundle_object_count       4.bdl 3 &&
	test_bundle_object_count stdin-4.bdl 3
'

test_expect_success 'clone from bundle' '
	git clone --mirror 1.bdl mirror.git &&
	git -C mirror.git show-ref |
		make_user_friendly_and_stable_output >actual &&
	cat >expect <<-\EOF &&
	<COMMIT-D> refs/heads/topic/1
	<COMMIT-H> refs/heads/topic/2
	EOF
	test_cmp expect actual &&

	git -C mirror.git fetch ../2.bdl "+refs/*:refs/*" &&
	git -C mirror.git show-ref |
		make_user_friendly_and_stable_output >actual &&
	cat >expect <<-\EOF &&
	<COMMIT-N> refs/heads/release
	<COMMIT-D> refs/heads/topic/1
	<COMMIT-H> refs/heads/topic/2
	EOF
	test_cmp expect actual &&

	git -C mirror.git fetch ../3.bdl "+refs/*:refs/*" &&
	git -C mirror.git show-ref |
		make_user_friendly_and_stable_output >actual &&
	cat >expect <<-\EOF &&
	<COMMIT-P> refs/heads/main
	<COMMIT-N> refs/heads/release
	<COMMIT-D> refs/heads/topic/1
	<COMMIT-H> refs/heads/topic/2
	EOF
	test_cmp expect actual &&

	git -C mirror.git fetch ../4.bdl "+refs/*:refs/*" &&
	git -C mirror.git show-ref |
		make_user_friendly_and_stable_output >actual &&
	cat >expect <<-\EOF &&
	<COMMIT-P> refs/heads/main
	<COMMIT-N> refs/heads/release
	<COMMIT-D> refs/heads/topic/1
	<COMMIT-H> refs/heads/topic/2
	<TAG-1> refs/tags/v1
	<TAG-2> refs/tags/v2
	<TAG-3> refs/tags/v3
	EOF
	test_cmp expect actual
'

test_done
