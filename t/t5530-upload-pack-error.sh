#!/bin/sh

test_description='errors in upload-pack'

. ./test-lib.sh

corrupt_repo () {
	object_sha1=$(git rev-parse "$1") &&
	ob=$(expr "$object_sha1" : "\(..\)") &&
	ject=$(expr "$object_sha1" : "..\(..*\)") &&
	rm -f ".git/objects/$ob/$ject"
}

test_expect_success 'setup and corrupt repository' '
	echo file >file &&
	git add file &&
	git rev-parse :file &&
	git commit -a -m original &&
	test_tick &&
	echo changed >file &&
	git commit -a -m changed &&
	corrupt_repo HEAD:file &&
	test_must_fail git fsck
'

test_expect_success 'upload-pack fails due to error in pack-objects packing' '
	head=$(git rev-parse HEAD) &&
	hexsz=$(test_oid hexsz) &&
	printf "%04xwant %s\n00000009done\n0000" \
		$(($hexsz + 10)) $head >input &&
	test_must_fail git upload-pack . <input >/dev/null 2>output.err &&
	test_grep "unable to read" output.err &&
	test_grep "pack-objects died" output.err
'

test_expect_success 'corrupt repo differently' '
	git hash-object -w file &&
	corrupt_repo HEAD^^{tree} &&
	test_must_fail git fsck
'

test_expect_success 'upload-pack fails due to error in rev-list' '
	printf "%04xwant %s\n%04xshallow %s00000009done\n0000" \
		$(($hexsz + 10)) $(git rev-parse HEAD) \
		$(($hexsz + 12)) $(git rev-parse HEAD^) >input &&
	test_must_fail git upload-pack . <input >/dev/null 2>output.err &&
	grep "bad tree object" output.err
'

test_expect_success 'upload-pack fails due to bad want (no object)' '
	printf "%04xwant %s multi_ack_detailed\n00000009done\n0000" \
		$(($hexsz + 29)) $(test_oid deadbeef) >input &&
	test_must_fail git upload-pack . <input >output 2>output.err &&
	grep "not our ref" output.err &&
	grep "ERR" output &&
	! grep multi_ack_detailed output.err
'

test_expect_success 'upload-pack fails due to bad want (not tip)' '
	oid=$(echo an object we have | git hash-object -w --stdin) &&
	printf "%04xwant %s multi_ack_detailed\n00000009done\n0000" \
		$(($hexsz + 29)) "$oid" >input &&
	test_must_fail git upload-pack . <input >output 2>output.err &&
	grep "not our ref" output.err &&
	grep "ERR" output &&
	! grep multi_ack_detailed output.err
'

test_expect_success 'upload-pack fails due to error in pack-objects enumeration' '
	printf "%04xwant %s\n00000009done\n0000" \
		$((hexsz + 10)) $(git rev-parse HEAD) >input &&
	test_must_fail git upload-pack . <input >/dev/null 2>output.err &&
	grep "bad tree object" output.err &&
	grep "pack-objects died" output.err
'

test_expect_success 'upload-pack tolerates EOF just after stateless client wants' '
	test_commit initial &&
	head=$(git rev-parse HEAD) &&

	{
		packetize "want $head" &&
		packetize "shallow $head" &&
		packetize "deepen 1" &&
		printf "0000"
	} >request &&

	printf "0000" >expect &&

	git upload-pack --stateless-rpc . <request >actual &&
	test_cmp expect actual
'

test_expect_success 'fetch fails' '
	git init foo &&
	test_must_fail git -C foo fetch .. main
'

test_expect_success 'upload-pack ACKs repeated non-commit objects repeatedly (protocol v0)' '
	commit_id=$(git rev-parse HEAD) &&
	tree_id=$(git rev-parse HEAD^{tree}) &&
	test-tool pkt-line pack >request <<-EOF &&
	want $commit_id
	0000
	have $tree_id
	have $tree_id
	0000
	EOF
	git upload-pack --stateless-rpc . <request >actual &&
	depacketize <actual >actual.raw &&
	grep ^ACK actual.raw >actual.acks &&
	cat >expect <<-EOF &&
	ACK $tree_id
	ACK $tree_id
	EOF
	test_cmp expect actual.acks
'

test_expect_success 'upload-pack ACKs repeated non-commit objects once only (protocol v2)' '
	commit_id=$(git rev-parse HEAD) &&
	tree_id=$(git rev-parse HEAD^{tree}) &&
	test-tool pkt-line pack >request <<-EOF &&
	command=fetch
	object-format=$(test_oid algo)
	0001
	want $commit_id
	have $tree_id
	have $tree_id
	0000
	EOF
	GIT_PROTOCOL=version=2 git upload-pack . <request >actual &&
	depacketize <actual >actual.raw &&
	grep ^ACK actual.raw >actual.acks &&
	echo "ACK $tree_id" >expect &&
	test_cmp expect actual.acks
'

test_done
