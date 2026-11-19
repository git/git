#!/bin/sh

test_description='git receive-pack'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_expect_success 'setup' '
	test_commit base &&
	git clone -s --bare . fork &&
	git checkout -b public/branch main &&
	test_commit public &&
	git checkout -b private/branch main &&
	test_commit private
'

extract_haves () {
	depacketize | sed -n 's/^\([^ ][^ ]*\) \.have/\1/p'
}

test_expect_success 'with core.alternateRefsCommand' '
	write_script fork/alternate-refs <<-\EOF &&
		git --git-dir="$1" for-each-ref \
			--format="%(objectname)" \
			refs/heads/public/
	EOF
	test_config -C fork core.alternateRefsCommand ./alternate-refs &&
	git rev-parse public/branch >expect &&
	printf "0000" | git receive-pack fork >actual &&
	extract_haves <actual >actual.haves &&
	test_cmp expect actual.haves
'

test_expect_success 'with core.alternateRefsPrefixes' '
	test_config -C fork core.alternateRefsPrefixes "refs/heads/private" &&
	git rev-parse private/branch >expect &&
	printf "0000" | git receive-pack fork >actual &&
	extract_haves <actual >actual.haves &&
	test_cmp expect actual.haves
'

# The `tee.exe` shipped in Git for Windows v2.49.0 is known to hang frequently
# when spawned from `git.exe` and piping its output to `git.exe`. This seems
# related to MSYS2 runtime bug fixes regarding the signal handling; Let's just
# skip the tests that need to exercise this when the faulty MSYS2 runtime is
# detected; The test cases are exercised enough in other matrix jobs of the CI
# runs.
test_lazy_prereq TEE_DOES_NOT_HANG '
	test_have_prereq !MINGW &&
	case "$(uname -a)" in *3.5.7-463ebcdc.x86_64*) false;; esac
'

test_expect_success TEE_DOES_NOT_HANG \
	'receive-pack missing objects fails connectivity check' '
	test_when_finished rm -rf repo remote.git setup.git &&

	git init repo &&
	git -C repo commit --allow-empty -m 1 &&
	git clone --bare repo setup.git &&
	git -C repo commit --allow-empty -m 2 &&

	# Capture git-send-pack(1) output sent to git-receive-pack(1).
	git -C repo send-pack ../setup.git --all \
		--receive-pack="tee ${SQ}$(pwd)/out${SQ} | git-receive-pack" &&

	# Replay captured git-send-pack(1) output on new empty repository.
	git init --bare remote.git &&
	git receive-pack remote.git <out >actual 2>err &&

	test_grep "missing necessary objects" actual &&
	test_grep "fatal: Failed to traverse parents" err &&
	test_must_fail git -C remote.git cat-file -e $(git -C repo rev-parse HEAD)
'

test_expect_success TEE_DOES_NOT_HANG \
	'receive-pack missing objects bypasses connectivity check' '
	test_when_finished rm -rf repo remote.git setup.git &&

	git init repo &&
	git -C repo commit --allow-empty -m 1 &&
	git clone --bare repo setup.git &&
	git -C repo commit --allow-empty -m 2 &&

	# Capture git-send-pack(1) output sent to git-receive-pack(1).
	git -C repo send-pack ../setup.git --all \
		--receive-pack="tee ${SQ}$(pwd)/out${SQ} | git-receive-pack" &&

	# Replay captured git-send-pack(1) output on new empty repository.
	git init --bare remote.git &&
	git receive-pack --skip-connectivity-check remote.git <out >actual 2>err &&

	test_grep ! "missing necessary objects" actual &&
	test_must_be_empty err &&
	git -C remote.git cat-file -e $(git -C repo rev-parse HEAD) &&
	test_must_fail git -C remote.git rev-list $(git -C repo rev-parse HEAD)
'

test_expect_success 'receive-pack rejects multiple updates for the same ref' '
	test_when_finished "rm -rf repo remote.git" &&

	git init repo &&
	git -C repo commit --allow-empty -m A &&
	git -C repo branch A &&
	git -C repo commit --allow-empty -m B &&
	git -C repo branch B &&
	git init --bare remote.git &&
	git -C repo send-pack ../remote.git A B &&
	A=$(git -C repo rev-parse A) &&
	B=$(git -C repo rev-parse B) &&
	{
		printf "%s %s refs/heads/foo\0report-status object-format=%s" \
			$ZERO_OID $A "$(test_oid algo)" |
		test-tool pkt-line pack-raw-stdin &&
		printf "%s %s refs/heads/foo" $ZERO_OID $B |
		test-tool pkt-line pack-raw-stdin &&
		printf 0000 &&
		git pack-objects --stdout </dev/null
	} >request &&
	git receive-pack remote.git <request >response 2>err &&
	test_grep "multiple updates for ref ${SQ}refs/heads/foo${SQ} not allowed" err &&
	test-tool pkt-line unpack <response >report &&
	sed -n "/^unpack /,\$p" report >actual &&
	cat >expect <<-\EOF &&
	unpack ok
	ng refs/heads/foo failed to update refs
	ng refs/heads/foo failed to update refs
	0000
	EOF
	test_cmp expect actual &&
	test_must_fail git --git-dir=remote.git rev-parse --verify refs/heads/foo
'

test_done
