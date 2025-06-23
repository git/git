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
	# The test setup is somewhat involved because triggering a SIGPIPE is
	# racy with buffered pipes. To avoid the raciness we thus need to make
	# sure that the subprocess in question fills the buffers completely,
	# which requires a couple thousand submodules in total.
	test_when_finished "rm -rf submodule repo" &&
	git init submodule &&
	(
		cd submodule &&
		test_commit initial &&

		COMMIT=$(git rev-parse HEAD) &&
		for i in $(test_seq 2000)
		do
			echo "[submodule \"sm-$i\"]" &&
			echo "path = recursive-submodule-path-$i" ||
			return 1
		done >gitmodules &&
		BLOB=$(git hash-object -w --stdin <gitmodules) &&

		printf "100644 blob $BLOB\t.gitmodules\n" >tree &&
		test_seq -f "160000 commit $COMMIT\trecursive-submodule-path-%d" 2000 >>tree &&
		TREE=$(git mktree <tree) &&

		COMMIT=$(git commit-tree "$TREE") &&
		git reset --hard "$COMMIT"
	) &&

	git init repo &&
	(
		cd repo &&
		GIT_ALLOW_PROTOCOL=file git submodule add "$(pwd)"/../submodule &&
		{ git submodule status --recursive 2>err; echo $?>status; } |
			grep -q recursive-submodule-path-1 &&
		test_must_be_empty err &&
		test_match_signal 13 "$(cat status)"
	)
'

test_done
