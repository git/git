#!/bin/sh

test_description='submodule --cached, --quiet etc. output'

. ./test-lib.sh
. "$TEST_DIRECTORY"/lib-t3100.sh

setup_sub () {
	local d="$1" &&
	shift &&
	git $@ clone . "$d" &&
	git $@ submodule add ./"$d"
}

normalize_status () {
	sed -e 's/-g[0-9a-f]*/-gHASH/'
}

test_expect_success 'setup' '
	test_commit A &&
	test_commit B &&
	setup_sub S  &&
	setup_sub S.D &&
	setup_sub S.C &&
	setup_sub S.C.D &&
	setup_sub X &&
	git add S* &&
	test_commit C &&

	# recursive in X/
	git -C X pull &&
	GIT_ALLOW_PROTOCOL=file git -C X submodule update --init &&

	# dirty
	for d in S.D X/S.D
	do
		echo dirty >"$d"/A.t || return 1
	done &&

	# commit (for --cached)
	for d in S.C* X/S.C*
	do
		git -C "$d" reset --hard A || return 1
	done &&

	# dirty
	for d in S*.D X/S*.D
	do
		echo dirty >"$d/C2.t" || return 1
	done &&

	for ref in A B C
	do
		# Not different with SHA-1 and SHA-256, just (ab)using
		# test_oid_cache as a variable bag to avoid using
		# $(git rev-parse ...).
		oid=$(git rev-parse $ref) &&
		test_oid_cache <<-EOF || return 1
		$ref sha1:$oid
		$ref sha256:$oid
		EOF
	done
'

for opts in "" "status"
do
	test_expect_success "git submodule $opts" '
		sed -e "s/^>//" >expect <<-EOF &&
		> $(test_oid B) S (B)
		>+$(test_oid A) S.C (A)
		>+$(test_oid A) S.C.D (A)
		> $(test_oid B) S.D (B)
		>+$(test_oid C) X (C)
		EOF
		git submodule $opts >actual.raw &&
		normalize_status <actual.raw >actual &&
		test_cmp expect actual
	'
done

for opts in \
	"status --recursive"
do
	test_expect_success "git submodule $opts" '
		sed -e "s/^>//" >expect <<-EOF &&
		> $(test_oid B) S (B)
		>+$(test_oid A) S.C (A)
		>+$(test_oid A) S.C.D (A)
		> $(test_oid B) S.D (B)
		>+$(test_oid C) X (C)
		> $(test_oid B) X/S (B)
		>+$(test_oid A) X/S.C (A)
		>+$(test_oid A) X/S.C.D (A)
		> $(test_oid B) X/S.D (B)
		> $(test_oid B) X/X (B)
		EOF
		git submodule $opts >actual.raw &&
		normalize_status <actual.raw >actual &&
		test_cmp expect actual
	'
done

for opts in \
	"--quiet" \
	"--quiet status" \
	"status --quiet"
do
	test_expect_success "git submodule $opts" '
		git submodule $opts >out &&
		test_must_be_empty out
	'
done

for opts in \
	"--cached" \
	"--cached status" \
	"status --cached"
do
	test_expect_success "git submodule $opts" '
		sed -e "s/^>//" >expect <<-EOF &&
		> $(test_oid B) S (B)
		>+$(test_oid B) S.C (B)
		>+$(test_oid B) S.C.D (B)
		> $(test_oid B) S.D (B)
		>+$(test_oid B) X (B)
		EOF
		git submodule $opts >actual.raw &&
		normalize_status <actual.raw >actual &&
		test_cmp expect actual
	'
done

for opts in \
	"--cached --quiet" \
	"--cached --quiet status" \
	"--cached status --quiet" \
	"--quiet status --cached" \
	"status --cached --quiet"
do
	test_expect_success "git submodule $opts" '
		git submodule $opts >out &&
		test_must_be_empty out
	'
done

for opts in \
	"status --cached --recursive" \
	"--cached status --recursive"
do
	test_expect_success "git submodule $opts" '
		sed -e "s/^>//" >expect <<-EOF &&
		> $(test_oid B) S (B)
		>+$(test_oid B) S.C (B)
		>+$(test_oid B) S.C.D (B)
		> $(test_oid B) S.D (B)
		>+$(test_oid B) X (B)
		> $(test_oid B) X/S (B)
		>+$(test_oid B) X/S.C (B)
		>+$(test_oid B) X/S.C.D (B)
		> $(test_oid B) X/S.D (B)
		> $(test_oid B) X/X (B)
		EOF
		git submodule $opts >actual.raw &&
		normalize_status <actual.raw >actual &&
		test_cmp expect actual
	'
done

test_expect_success !MINGW 'git submodule status --recursive propagates SIGPIPE' '
	{ git submodule status --recursive 2>err; echo $?>status; } |
		grep -q X/S &&
	test_must_be_empty err &&
	test_match_signal 13 "$(cat status)"
'

test_done
