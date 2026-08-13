#!/bin/sh

test_description='rev-parse --output-object-format names objects in another algorithm

The repositories here do not set extensions.compatObjectFormat, so nothing
is recorded in the loose object map and every name has to be computed.
'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

# The object formats are pinned rather than left to the default so that
# these tests behave the same under GIT_TEST_DEFAULT_HASH=sha256.
test_expect_success 'setup' '
	for fmt in sha1 sha256
	do
		git init --object-format=$fmt ${fmt}repo &&
		mkdir -p ${fmt}repo/sub &&
		test_write_lines hello >${fmt}repo/a.txt &&
		test_write_lines world >${fmt}repo/sub/b.txt &&
		test_write_lines exec >${fmt}repo/sub/run.sh &&
		chmod +x ${fmt}repo/sub/run.sh &&
		test_write_lines dup >${fmt}repo/sub/dup1 &&
		test_write_lines dup >${fmt}repo/dup2 &&
		git -C ${fmt}repo add -A &&
		git -C ${fmt}repo commit -m initial || return 1
	done
'

test_expect_success 'name a tree in the other algorithm' '
	git -C sha256repo rev-parse HEAD^{tree} >expect &&
	git -C sha1repo rev-parse --output-object-format=sha256 HEAD^{tree} >actual &&
	test_cmp expect actual
'

test_expect_success 'name a subtree given as <rev>:<path>' '
	git -C sha256repo rev-parse HEAD:sub >expect &&
	git -C sha1repo rev-parse --output-object-format=sha256 HEAD:sub >actual &&
	test_cmp expect actual
'

test_expect_success 'name a blob in the other algorithm' '
	git -C sha256repo rev-parse HEAD:a.txt >expect &&
	git -C sha1repo rev-parse --output-object-format=sha256 HEAD:a.txt >actual &&
	test_cmp expect actual
'

test_expect_success 'name the empty tree' '
	empty1=$(git -C sha1repo hash-object -t tree /dev/null) &&
	git -C sha256repo hash-object -t tree /dev/null >expect &&
	git -C sha1repo rev-parse --output-object-format=sha256 "$empty1" >actual &&
	test_cmp expect actual
'

test_expect_success 'the conversion runs in both directions' '
	git -C sha1repo rev-parse HEAD^{tree} >expect &&
	git -C sha256repo rev-parse --output-object-format=sha1 HEAD^{tree} >actual &&
	test_cmp expect actual
'

test_expect_success 'asking for the storage algorithm is a no-op' '
	git -C sha1repo rev-parse HEAD^{tree} >expect &&
	git -C sha1repo rev-parse --output-object-format=storage HEAD^{tree} >actual &&
	test_cmp expect actual &&
	git -C sha1repo rev-parse --output-object-format=sha1 HEAD^{tree} >actual &&
	test_cmp expect actual
'

# Before the names could be computed, a lookup miss left the object ID
# untouched and rev-parse printed the storage name with a zero exit code.
test_expect_success 'a name that cannot be computed is an error, not a wrong answer' '
	test_must_fail git -C sha1repo rev-parse --output-object-format=sha256 \
		HEAD >actual 2>err &&
	test_grep "cannot compute the sha256 name of commit" err &&
	test_grep "cannot express HEAD as a sha256 object name" err &&
	test_must_be_empty actual
'

test_expect_success 'a tree containing a submodule reports the submodule' '
	git init --object-format=sha1 withsub &&
	gitlink=$(git -C sha1repo rev-parse HEAD) &&
	(
		cd withsub &&
		test_write_lines x >f &&
		git add f &&
		git update-index --add --cacheinfo 160000,$gitlink,modpath &&
		git commit -m withsub
	) &&
	test_must_fail git -C withsub rev-parse --output-object-format=sha256 \
		HEAD^{tree} 2>err &&
	test_grep "cannot map submodule entry .modpath." err
'

test_expect_success 'an unknown algorithm is rejected' '
	test_must_fail git -C sha1repo rev-parse --output-object-format=md5 \
		HEAD^{tree} 2>err &&
	test_grep "unsupported object format: md5" err
'

test_expect_success 'a missing algorithm is rejected' '
	test_must_fail git -C sha1repo rev-parse --output-object-format \
		HEAD^{tree} 2>err &&
	test_grep "no object format specified" err &&
	test_must_fail git -C sha1repo rev-parse --output-object-format= \
		HEAD^{tree} 2>err &&
	test_grep "unsupported object format:" err
'

test_expect_success 'names computed here match a real conversion' '
	git -C sha1repo fast-export --full-tree HEAD >stream &&
	git init --bare --object-format=sha256 converted.git &&
	git -C converted.git fast-import --quiet <stream &&
	git -C converted.git rev-parse HEAD^{tree} >expect &&
	git -C sha1repo rev-parse --output-object-format=sha256 HEAD^{tree} >actual &&
	test_cmp expect actual
'

test_done
