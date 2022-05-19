#!/bin/sh

test_description='errors in upload-pack'

. ./test-lib.sh

D=$(pwd)

corrupt_repo () {
	object_sha1=$(but rev-parse "$1") &&
	ob=$(expr "$object_sha1" : "\(..\)") &&
	ject=$(expr "$object_sha1" : "..\(..*\)") &&
	rm -f ".but/objects/$ob/$ject"
}

test_expect_success 'setup and corrupt repository' '
	echo file >file &&
	but add file &&
	but rev-parse :file &&
	but cummit -a -m original &&
	test_tick &&
	echo changed >file &&
	but cummit -a -m changed &&
	corrupt_repo HEAD:file

'

test_expect_success 'fsck fails' '
	test_must_fail but fsck
'

test_expect_success 'upload-pack fails due to error in pack-objects packing' '
	head=$(but rev-parse HEAD) &&
	hexsz=$(test_oid hexsz) &&
	printf "%04xwant %s\n00000009done\n0000" \
		$(($hexsz + 10)) $head >input &&
	test_must_fail but upload-pack . <input >/dev/null 2>output.err &&
	test_i18ngrep "unable to read" output.err &&
	test_i18ngrep "pack-objects died" output.err
'

test_expect_success 'corrupt repo differently' '

	but hash-object -w file &&
	corrupt_repo HEAD^^{tree}

'

test_expect_success 'fsck fails' '
	test_must_fail but fsck
'
test_expect_success 'upload-pack fails due to error in rev-list' '

	printf "%04xwant %s\n%04xshallow %s00000009done\n0000" \
		$(($hexsz + 10)) $(but rev-parse HEAD) \
		$(($hexsz + 12)) $(but rev-parse HEAD^) >input &&
	test_must_fail but upload-pack . <input >/dev/null 2>output.err &&
	grep "bad tree object" output.err
'

test_expect_success 'upload-pack fails due to bad want (no object)' '

	printf "%04xwant %s multi_ack_detailed\n00000009done\n0000" \
		$(($hexsz + 29)) $(test_oid deadbeef) >input &&
	test_must_fail but upload-pack . <input >output 2>output.err &&
	grep "not our ref" output.err &&
	grep "ERR" output &&
	! grep multi_ack_detailed output.err
'

test_expect_success 'upload-pack fails due to bad want (not tip)' '

	oid=$(echo an object we have | but hash-object -w --stdin) &&
	printf "%04xwant %s multi_ack_detailed\n00000009done\n0000" \
		$(($hexsz + 29)) "$oid" >input &&
	test_must_fail but upload-pack . <input >output 2>output.err &&
	grep "not our ref" output.err &&
	grep "ERR" output &&
	! grep multi_ack_detailed output.err
'

test_expect_success 'upload-pack fails due to error in pack-objects enumeration' '

	printf "%04xwant %s\n00000009done\n0000" \
		$((hexsz + 10)) $(but rev-parse HEAD) >input &&
	test_must_fail but upload-pack . <input >/dev/null 2>output.err &&
	grep "bad tree object" output.err &&
	grep "pack-objects died" output.err
'

test_expect_success 'upload-pack tolerates EOF just after stateless client wants' '
	test_cummit initial &&
	head=$(but rev-parse HEAD) &&

	{
		packetize "want $head" &&
		packetize "shallow $head" &&
		packetize "deepen 1" &&
		printf "0000"
	} >request &&

	printf "0000" >expect &&

	but upload-pack --stateless-rpc . <request >actual &&
	test_cmp expect actual
'

test_expect_success 'create empty repository' '

	mkdir foo &&
	cd foo &&
	but init

'

test_expect_success 'fetch fails' '

	test_must_fail but fetch .. main

'

test_done
