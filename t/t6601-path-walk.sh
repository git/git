#!/bin/sh

test_description='direct path-walk API tests'

. ./test-lib.sh

test_expect_success 'setup test repository' '
	git checkout -b base &&

	# Create tree objects that are only reachable via tags,
	# not from any commit in the history.
	child_blob_oid=$(echo "child blob content" | git hash-object -t blob -w --stdin) &&
	child_tree_oid=$(printf "100644 blob %s\tfile\n" "$child_blob_oid" | git mktree) &&
	tree_tag_oid=$(printf "040000 tree %s\tchild\n" "$child_tree_oid" | git mktree) &&
	git tag -a -m "tree" tree-tag "$tree_tag_oid" &&
	file2_blob_oid=$(echo "tagged tree file2" | git hash-object -t blob -w --stdin) &&
	tree_tag2_oid=$(printf "040000 tree %s\tchild\n100644 blob %s\tfile2\n" "$child_tree_oid" "$file2_blob_oid" | git mktree) &&
	git tag tree-tag2 "$tree_tag2_oid" &&

	echo blob >file &&
	blob_oid=$(git hash-object -t blob -w --stdin <file) &&
	git tag -a -m "blob" blob-tag "$blob_oid" &&
	echo blob2 >file2 &&
	blob2_oid=$(git hash-object -t blob -w --stdin <file2) &&
	git tag blob-tag2 "$blob2_oid" &&

	rm -fr file file2 &&

	mkdir left &&
	mkdir right &&
	echo a >a &&
	echo b >left/b &&
	echo c >right/c &&
	git add . &&
	git commit -m "first" &&
	git tag -m "first" first HEAD &&

	echo d >right/d &&
	git add right &&
	git commit -m "second" &&
	git tag -a -m "second (under)" second.1 HEAD &&
	git tag -a -m "second (top)" second.2 second.1 &&

	# Set up file/dir collision in history.
	rm a &&
	mkdir a &&
	echo a >a/a &&
	echo bb >left/b &&
	git add a left &&
	git commit -m "third" &&
	git tag -a -m "third" third &&

	git checkout -b topic HEAD~1 &&
	echo cc >right/c &&
	git commit -a -m "topic" &&
	git tag -a -m "fourth" fourth
'

test_expect_success 'all' '
	test-tool path-walk -- --all >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	0:commit::$(git rev-parse base)
	0:commit::$(git rev-parse base~1)
	0:commit::$(git rev-parse base~2)
	1:tag:/tags:$(git rev-parse refs/tags/first)
	1:tag:/tags:$(git rev-parse refs/tags/second.1)
	1:tag:/tags:$(git rev-parse refs/tags/second.2)
	1:tag:/tags:$(git rev-parse refs/tags/third)
	1:tag:/tags:$(git rev-parse refs/tags/fourth)
	1:tag:/tags:$(git rev-parse refs/tags/tree-tag)
	1:tag:/tags:$(git rev-parse refs/tags/blob-tag)
	2:blob:/tagged-blobs:$(git rev-parse refs/tags/blob-tag^{})
	2:blob:/tagged-blobs:$(git rev-parse refs/tags/blob-tag2^{})
	3:tree::$(git rev-parse topic^{tree})
	3:tree::$(git rev-parse base^{tree})
	3:tree::$(git rev-parse base~1^{tree})
	3:tree::$(git rev-parse base~2^{tree})
	4:blob:a:$(git rev-parse base~2:a)
	5:tree:/tagged-trees:$(git rev-parse refs/tags/tree-tag^{})
	5:tree:/tagged-trees:$(git rev-parse refs/tags/tree-tag2^{})
	6:blob:file2:$(git rev-parse refs/tags/tree-tag2^{}:file2)
	7:tree:a/:$(git rev-parse base:a)
	8:tree:child/:$(git rev-parse refs/tags/tree-tag:child)
	9:blob:child/file:$(git rev-parse refs/tags/tree-tag:child/file)
	10:tree:left/:$(git rev-parse base:left)
	10:tree:left/:$(git rev-parse base~2:left)
	11:blob:left/b:$(git rev-parse base~2:left/b)
	11:blob:left/b:$(git rev-parse base:left/b)
	12:tree:right/:$(git rev-parse topic:right)
	12:tree:right/:$(git rev-parse base~1:right)
	12:tree:right/:$(git rev-parse base~2:right)
	13:blob:right/c:$(git rev-parse base~2:right/c)
	13:blob:right/c:$(git rev-parse topic:right/c)
	14:blob:right/d:$(git rev-parse base~1:right/d)
	blobs:10
	commits:4
	tags:7
	trees:13
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'indexed objects' '
	test_when_finished git reset --hard &&

	# stage change into index, adding a blob but
	# also invalidating the cache-tree for the root
	# and the "left" directory.
	echo bogus >left/c &&
	git add left &&

	test-tool path-walk -- --indexed-objects >out &&

	cat >expect <<-EOF &&
	0:blob:a:$(git rev-parse HEAD:a)
	1:blob:left/b:$(git rev-parse HEAD:left/b)
	2:blob:left/c:$(git rev-parse :left/c)
	3:blob:right/c:$(git rev-parse HEAD:right/c)
	4:blob:right/d:$(git rev-parse HEAD:right/d)
	5:tree:right/:$(git rev-parse topic:right)
	blobs:5
	commits:0
	tags:0
	trees:1
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'branches and indexed objects mix well' '
	test_when_finished git reset --hard &&

	# stage change into index, adding a blob but
	# also invalidating the cache-tree for the root
	# and the "right" directory.
	echo fake >right/d &&
	git add right &&

	test-tool path-walk -- --indexed-objects --branches >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	0:commit::$(git rev-parse base)
	0:commit::$(git rev-parse base~1)
	0:commit::$(git rev-parse base~2)
	1:tree::$(git rev-parse topic^{tree})
	1:tree::$(git rev-parse base^{tree})
	1:tree::$(git rev-parse base~1^{tree})
	1:tree::$(git rev-parse base~2^{tree})
	2:tree:a/:$(git rev-parse refs/tags/third:a)
	3:tree:left/:$(git rev-parse base:left)
	3:tree:left/:$(git rev-parse base~2:left)
	4:blob:left/b:$(git rev-parse base:left/b)
	4:blob:left/b:$(git rev-parse base~2:left/b)
	5:tree:right/:$(git rev-parse topic:right)
	5:tree:right/:$(git rev-parse base~1:right)
	5:tree:right/:$(git rev-parse base~2:right)
	6:blob:right/c:$(git rev-parse base~2:right/c)
	6:blob:right/c:$(git rev-parse topic:right/c)
	7:blob:right/d:$(git rev-parse base~1:right/d)
	7:blob:right/d:$(git rev-parse :right/d)
	8:blob:a:$(git rev-parse base~2:a)
	blobs:7
	commits:4
	tags:0
	trees:10
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'base & topic, sparse' '
	cat >patterns <<-EOF &&
	/*
	!/*/
	/left/
	EOF

	test-tool path-walk --stdin-pl -- base topic <patterns >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	0:commit::$(git rev-parse base)
	0:commit::$(git rev-parse base~1)
	0:commit::$(git rev-parse base~2)
	1:tree::$(git rev-parse topic^{tree})
	1:tree::$(git rev-parse base^{tree})
	1:tree::$(git rev-parse base~1^{tree})
	1:tree::$(git rev-parse base~2^{tree})
	2:blob:a:$(git rev-parse base~2:a)
	3:tree:left/:$(git rev-parse base:left)
	3:tree:left/:$(git rev-parse base~2:left)
	4:blob:left/b:$(git rev-parse base~2:left/b)
	4:blob:left/b:$(git rev-parse base:left/b)
	blobs:3
	commits:4
	tags:0
	trees:6
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'base & topic, sparse, no tree pruning' '
	cat >patterns <<-EOF &&
	/*
	!/*/
	/left/
	EOF

	test-tool path-walk --stdin-pl --no-pl-sparse-trees \
		-- base topic <patterns >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	0:commit::$(git rev-parse base)
	0:commit::$(git rev-parse base~1)
	0:commit::$(git rev-parse base~2)
	1:tree::$(git rev-parse topic^{tree})
	1:tree::$(git rev-parse base^{tree})
	1:tree::$(git rev-parse base~1^{tree})
	1:tree::$(git rev-parse base~2^{tree})
	2:blob:a:$(git rev-parse base~2:a)
	3:tree:a/:$(git rev-parse base:a)
	4:tree:left/:$(git rev-parse base:left)
	4:tree:left/:$(git rev-parse base~2:left)
	5:blob:left/b:$(git rev-parse base~2:left/b)
	5:blob:left/b:$(git rev-parse base:left/b)
	6:tree:right/:$(git rev-parse topic:right)
	6:tree:right/:$(git rev-parse base~1:right)
	6:tree:right/:$(git rev-parse base~2:right)
	blobs:3
	commits:4
	tags:0
	trees:10
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'topic only' '
	test-tool path-walk -- topic >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	0:commit::$(git rev-parse base~1)
	0:commit::$(git rev-parse base~2)
	1:tree::$(git rev-parse topic^{tree})
	1:tree::$(git rev-parse base~1^{tree})
	1:tree::$(git rev-parse base~2^{tree})
	2:blob:a:$(git rev-parse base~2:a)
	3:tree:left/:$(git rev-parse base~2:left)
	4:blob:left/b:$(git rev-parse base~2:left/b)
	5:tree:right/:$(git rev-parse topic:right)
	5:tree:right/:$(git rev-parse base~1:right)
	5:tree:right/:$(git rev-parse base~2:right)
	6:blob:right/c:$(git rev-parse base~2:right/c)
	6:blob:right/c:$(git rev-parse topic:right/c)
	7:blob:right/d:$(git rev-parse base~1:right/d)
	blobs:5
	commits:3
	tags:0
	trees:7
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'topic, not base' '
	test-tool path-walk -- topic --not base >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	1:tree::$(git rev-parse topic^{tree})
	2:blob:a:$(git rev-parse topic:a):UNINTERESTING
	3:tree:left/:$(git rev-parse topic:left):UNINTERESTING
	4:blob:left/b:$(git rev-parse topic:left/b):UNINTERESTING
	5:tree:right/:$(git rev-parse topic:right)
	6:blob:right/c:$(git rev-parse topic:right/c)
	7:blob:right/d:$(git rev-parse topic:right/d):UNINTERESTING
	blobs:4
	commits:1
	tags:0
	trees:3
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'fourth, blob-tag2, not base' '
	test-tool path-walk -- fourth blob-tag2 --not base >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	1:tag:/tags:$(git rev-parse fourth)
	2:blob:/tagged-blobs:$(git rev-parse refs/tags/blob-tag2^{})
	3:tree::$(git rev-parse topic^{tree})
	4:blob:a:$(git rev-parse base~1:a):UNINTERESTING
	5:tree:left/:$(git rev-parse base~1:left):UNINTERESTING
	6:blob:left/b:$(git rev-parse base~1:left/b):UNINTERESTING
	7:tree:right/:$(git rev-parse topic:right)
	8:blob:right/c:$(git rev-parse topic:right/c)
	9:blob:right/d:$(git rev-parse base~1:right/d):UNINTERESTING
	blobs:5
	commits:1
	tags:1
	trees:3
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'topic, not base, only blobs' '
	test-tool path-walk --no-trees --no-commits \
		-- topic --not base >out &&

	cat >expect <<-EOF &&
	0:blob:a:$(git rev-parse topic:a):UNINTERESTING
	1:blob:left/b:$(git rev-parse topic:left/b):UNINTERESTING
	2:blob:right/c:$(git rev-parse topic:right/c)
	3:blob:right/d:$(git rev-parse topic:right/d):UNINTERESTING
	blobs:4
	commits:0
	tags:0
	trees:0
	EOF

	test_cmp_sorted expect out
'

# No, this doesn't make a lot of sense for the path-walk API,
# but it is possible to do.
test_expect_success 'topic, not base, only commits' '
	test-tool path-walk --no-blobs --no-trees \
		-- topic --not base >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	commits:1
	blobs:0
	tags:0
	trees:0
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'topic, not base, only trees' '
	test-tool path-walk --no-blobs --no-commits \
		-- topic --not base >out &&

	cat >expect <<-EOF &&
	0:tree::$(git rev-parse topic^{tree})
	1:tree:left/:$(git rev-parse topic:left):UNINTERESTING
	2:tree:right/:$(git rev-parse topic:right)
	commits:0
	blobs:0
	tags:0
	trees:3
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'topic, not base, boundary' '
	test-tool path-walk -- --boundary topic --not base >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	0:commit::$(git rev-parse base~1):UNINTERESTING
	1:tree::$(git rev-parse topic^{tree})
	1:tree::$(git rev-parse base~1^{tree}):UNINTERESTING
	2:blob:a:$(git rev-parse base~1:a):UNINTERESTING
	3:tree:left/:$(git rev-parse base~1:left):UNINTERESTING
	4:blob:left/b:$(git rev-parse base~1:left/b):UNINTERESTING
	5:tree:right/:$(git rev-parse topic:right)
	5:tree:right/:$(git rev-parse base~1:right):UNINTERESTING
	6:blob:right/c:$(git rev-parse base~1:right/c):UNINTERESTING
	6:blob:right/c:$(git rev-parse topic:right/c)
	7:blob:right/d:$(git rev-parse base~1:right/d):UNINTERESTING
	blobs:5
	commits:2
	tags:0
	trees:5
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'topic, not base, boundary with pruning' '
	test-tool path-walk --prune -- --boundary topic --not base >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	0:commit::$(git rev-parse base~1):UNINTERESTING
	1:tree::$(git rev-parse topic^{tree})
	1:tree::$(git rev-parse base~1^{tree}):UNINTERESTING
	2:tree:right/:$(git rev-parse topic:right)
	2:tree:right/:$(git rev-parse base~1:right):UNINTERESTING
	3:blob:right/c:$(git rev-parse base~1:right/c):UNINTERESTING
	3:blob:right/c:$(git rev-parse topic:right/c)
	blobs:2
	commits:2
	tags:0
	trees:4
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'topic, not base, --edge-aggressive with pruning' '
	test-tool path-walk --prune --edge-aggressive -- topic --not base >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	1:tree::$(git rev-parse topic^{tree})
	1:tree::$(git rev-parse base^{tree}):UNINTERESTING
	2:tree:right/:$(git rev-parse topic:right)
	2:tree:right/:$(git rev-parse base:right):UNINTERESTING
	3:blob:right/c:$(git rev-parse base:right/c):UNINTERESTING
	3:blob:right/c:$(git rev-parse topic:right/c)
	blobs:2
	commits:1
	tags:0
	trees:4
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'trees are reported exactly once' '
	test_when_finished "rm -rf unique-trees" &&
	test_create_repo unique-trees &&
	(
		cd unique-trees &&
		mkdir initial &&
		test_commit initial/file &&
		git switch -c move-to-top &&
		git mv initial/file.t ./ &&
		test_tick &&
		git commit -m moved &&
		git update-ref refs/heads/other HEAD
	) &&
	test-tool -C unique-trees path-walk -- --all >out &&
	tree=$(git -C unique-trees rev-parse HEAD:) &&
	grep "$tree" out >out-filtered &&
	test_line_count = 1 out-filtered
'

test_expect_success 'all, blob:none filter' '
	test-tool path-walk --filter=blob:none -- --all >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	0:commit::$(git rev-parse base)
	0:commit::$(git rev-parse base~1)
	0:commit::$(git rev-parse base~2)
	1:tag:/tags:$(git rev-parse refs/tags/first)
	1:tag:/tags:$(git rev-parse refs/tags/second.1)
	1:tag:/tags:$(git rev-parse refs/tags/second.2)
	1:tag:/tags:$(git rev-parse refs/tags/third)
	1:tag:/tags:$(git rev-parse refs/tags/fourth)
	1:tag:/tags:$(git rev-parse refs/tags/tree-tag)
	1:tag:/tags:$(git rev-parse refs/tags/blob-tag)
	2:blob:/tagged-blobs:$(git rev-parse refs/tags/blob-tag^{})
	2:blob:/tagged-blobs:$(git rev-parse refs/tags/blob-tag2^{})
	3:tree::$(git rev-parse topic^{tree})
	3:tree::$(git rev-parse base^{tree})
	3:tree::$(git rev-parse base~1^{tree})
	3:tree::$(git rev-parse base~2^{tree})
	4:tree:/tagged-trees:$(git rev-parse refs/tags/tree-tag^{})
	4:tree:/tagged-trees:$(git rev-parse refs/tags/tree-tag2^{})
	5:tree:a/:$(git rev-parse base:a)
	6:tree:child/:$(git rev-parse refs/tags/tree-tag:child)
	7:tree:left/:$(git rev-parse base:left)
	7:tree:left/:$(git rev-parse base~2:left)
	8:tree:right/:$(git rev-parse topic:right)
	8:tree:right/:$(git rev-parse base~1:right)
	8:tree:right/:$(git rev-parse base~2:right)
	blobs:2
	commits:4
	tags:7
	trees:13
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'topic only, blob:none filter' '
	test-tool path-walk --filter=blob:none -- topic >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	0:commit::$(git rev-parse base~1)
	0:commit::$(git rev-parse base~2)
	1:tree::$(git rev-parse topic^{tree})
	1:tree::$(git rev-parse base~1^{tree})
	1:tree::$(git rev-parse base~2^{tree})
	2:tree:left/:$(git rev-parse base~2:left)
	3:tree:right/:$(git rev-parse topic:right)
	3:tree:right/:$(git rev-parse base~1:right)
	3:tree:right/:$(git rev-parse base~2:right)
	blobs:0
	commits:3
	tags:0
	trees:7
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'all, blob:limit=0 filter' '
	test-tool path-walk --filter=blob:limit=0 -- --all >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	0:commit::$(git rev-parse base)
	0:commit::$(git rev-parse base~1)
	0:commit::$(git rev-parse base~2)
	1:tag:/tags:$(git rev-parse refs/tags/first)
	1:tag:/tags:$(git rev-parse refs/tags/second.1)
	1:tag:/tags:$(git rev-parse refs/tags/second.2)
	1:tag:/tags:$(git rev-parse refs/tags/third)
	1:tag:/tags:$(git rev-parse refs/tags/fourth)
	1:tag:/tags:$(git rev-parse refs/tags/tree-tag)
	1:tag:/tags:$(git rev-parse refs/tags/blob-tag)
	2:blob:/tagged-blobs:$(git rev-parse refs/tags/blob-tag^{})
	2:blob:/tagged-blobs:$(git rev-parse refs/tags/blob-tag2^{})
	3:tree::$(git rev-parse topic^{tree})
	3:tree::$(git rev-parse base^{tree})
	3:tree::$(git rev-parse base~1^{tree})
	3:tree::$(git rev-parse base~2^{tree})
	4:tree:/tagged-trees:$(git rev-parse refs/tags/tree-tag^{})
	4:tree:/tagged-trees:$(git rev-parse refs/tags/tree-tag2^{})
	5:tree:a/:$(git rev-parse base:a)
	6:tree:child/:$(git rev-parse refs/tags/tree-tag:child)
	7:tree:left/:$(git rev-parse base:left)
	7:tree:left/:$(git rev-parse base~2:left)
	8:tree:right/:$(git rev-parse topic:right)
	8:tree:right/:$(git rev-parse base~1:right)
	8:tree:right/:$(git rev-parse base~2:right)
	blobs:2
	commits:4
	tags:7
	trees:13
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'all, blob:limit=3 filter' '
	test-tool path-walk --filter=blob:limit=3 -- --all >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	0:commit::$(git rev-parse base)
	0:commit::$(git rev-parse base~1)
	0:commit::$(git rev-parse base~2)
	1:tag:/tags:$(git rev-parse refs/tags/first)
	1:tag:/tags:$(git rev-parse refs/tags/second.1)
	1:tag:/tags:$(git rev-parse refs/tags/second.2)
	1:tag:/tags:$(git rev-parse refs/tags/third)
	1:tag:/tags:$(git rev-parse refs/tags/fourth)
	1:tag:/tags:$(git rev-parse refs/tags/tree-tag)
	1:tag:/tags:$(git rev-parse refs/tags/blob-tag)
	2:blob:/tagged-blobs:$(git rev-parse refs/tags/blob-tag^{})
	2:blob:/tagged-blobs:$(git rev-parse refs/tags/blob-tag2^{})
	3:tree::$(git rev-parse topic^{tree})
	3:tree::$(git rev-parse base^{tree})
	3:tree::$(git rev-parse base~1^{tree})
	3:tree::$(git rev-parse base~2^{tree})
	4:blob:a:$(git rev-parse base~2:a)
	5:tree:/tagged-trees:$(git rev-parse refs/tags/tree-tag^{})
	5:tree:/tagged-trees:$(git rev-parse refs/tags/tree-tag2^{})
	6:tree:a/:$(git rev-parse base:a)
	7:tree:child/:$(git rev-parse refs/tags/tree-tag:child)
	8:tree:left/:$(git rev-parse base:left)
	8:tree:left/:$(git rev-parse base~2:left)
	9:blob:left/b:$(git rev-parse base~2:left/b)
	10:tree:right/:$(git rev-parse topic:right)
	10:tree:right/:$(git rev-parse base~1:right)
	10:tree:right/:$(git rev-parse base~2:right)
	11:blob:right/c:$(git rev-parse base~2:right/c)
	12:blob:right/d:$(git rev-parse base~1:right/d)
	blobs:6
	commits:4
	tags:7
	trees:13
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'all, tree:0 filter' '
	test-tool path-walk --filter=tree:0 -- --all >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	0:commit::$(git rev-parse base)
	0:commit::$(git rev-parse base~1)
	0:commit::$(git rev-parse base~2)
	1:tag:/tags:$(git rev-parse refs/tags/first)
	1:tag:/tags:$(git rev-parse refs/tags/second.1)
	1:tag:/tags:$(git rev-parse refs/tags/second.2)
	1:tag:/tags:$(git rev-parse refs/tags/third)
	1:tag:/tags:$(git rev-parse refs/tags/fourth)
	1:tag:/tags:$(git rev-parse refs/tags/tree-tag)
	1:tag:/tags:$(git rev-parse refs/tags/blob-tag)
	2:blob:/tagged-blobs:$(git rev-parse refs/tags/blob-tag^{})
	2:blob:/tagged-blobs:$(git rev-parse refs/tags/blob-tag2^{})
	3:tree:/tagged-trees:$(git rev-parse refs/tags/tree-tag^{tree})
	3:tree:/tagged-trees:$(git rev-parse refs/tags/tree-tag2)
	blobs:2
	commits:4
	tags:7
	trees:2
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'topic only, tree:0 filter' '
	test-tool path-walk --filter=tree:0 -- topic >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	0:commit::$(git rev-parse base~1)
	0:commit::$(git rev-parse base~2)
	blobs:0
	commits:3
	tags:0
	trees:0
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'tree:1 filter is rejected' '
	test_must_fail test-tool path-walk --filter=tree:1 -- --all 2>err &&
	test_grep "tree:1 filter not supported by the path-walk API" err
'

test_expect_success 'all, object:type=commit filter' '
	test-tool path-walk --filter=object:type=commit -- --all >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	0:commit::$(git rev-parse base)
	0:commit::$(git rev-parse base~1)
	0:commit::$(git rev-parse base~2)
	blobs:0
	commits:4
	tags:0
	trees:0
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'all, object:type=tag filter' '
	test-tool path-walk --filter=object:type=tag -- --all >out &&

	cat >expect <<-EOF &&
	0:tag:/tags:$(git rev-parse refs/tags/first)
	0:tag:/tags:$(git rev-parse refs/tags/second.1)
	0:tag:/tags:$(git rev-parse refs/tags/second.2)
	0:tag:/tags:$(git rev-parse refs/tags/third)
	0:tag:/tags:$(git rev-parse refs/tags/fourth)
	0:tag:/tags:$(git rev-parse refs/tags/tree-tag)
	0:tag:/tags:$(git rev-parse refs/tags/blob-tag)
	blobs:0
	commits:0
	tags:7
	trees:0
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'all, object:type=tree filter' '
	test-tool path-walk --filter=object:type=tree -- --all >out &&

	cat >expect <<-EOF &&
	0:tree::$(git rev-parse topic^{tree})
	0:tree::$(git rev-parse base^{tree})
	0:tree::$(git rev-parse base~1^{tree})
	0:tree::$(git rev-parse base~2^{tree})
	1:tree:/tagged-trees:$(git rev-parse refs/tags/tree-tag^{})
	1:tree:/tagged-trees:$(git rev-parse refs/tags/tree-tag2^{})
	2:tree:a/:$(git rev-parse base:a)
	3:tree:child/:$(git rev-parse refs/tags/tree-tag:child)
	4:tree:left/:$(git rev-parse base:left)
	4:tree:left/:$(git rev-parse base~2:left)
	5:tree:right/:$(git rev-parse topic:right)
	5:tree:right/:$(git rev-parse base~1:right)
	5:tree:right/:$(git rev-parse base~2:right)
	blobs:0
	commits:0
	tags:0
	trees:13
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'all, object:type=blob filter' '
	test-tool path-walk --filter=object:type=blob -- --all >out &&

	cat >expect <<-EOF &&
	0:blob:/tagged-blobs:$(git rev-parse refs/tags/blob-tag^{})
	0:blob:/tagged-blobs:$(git rev-parse refs/tags/blob-tag2^{})
	1:blob:a:$(git rev-parse base~2:a)
	2:blob:left/b:$(git rev-parse base:left/b)
	2:blob:left/b:$(git rev-parse base~2:left/b)
	3:blob:right/c:$(git rev-parse base~2:right/c)
	3:blob:right/c:$(git rev-parse topic:right/c)
	4:blob:right/d:$(git rev-parse base~1:right/d)
	blobs:8
	commits:0
	tags:0
	trees:0
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'all, combine:blob:none+tree:0 filter' '
	test-tool path-walk \
		--filter=combine:blob:none+tree:0 -- --all >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	0:commit::$(git rev-parse base)
	0:commit::$(git rev-parse base~1)
	0:commit::$(git rev-parse base~2)
	1:tag:/tags:$(git rev-parse refs/tags/first)
	1:tag:/tags:$(git rev-parse refs/tags/second.1)
	1:tag:/tags:$(git rev-parse refs/tags/second.2)
	1:tag:/tags:$(git rev-parse refs/tags/third)
	1:tag:/tags:$(git rev-parse refs/tags/fourth)
	1:tag:/tags:$(git rev-parse refs/tags/tree-tag)
	1:tag:/tags:$(git rev-parse refs/tags/blob-tag)
	2:blob:/tagged-blobs:$(git rev-parse refs/tags/blob-tag^{})
	2:blob:/tagged-blobs:$(git rev-parse refs/tags/blob-tag2^{})
	3:tree:/tagged-trees:$(git rev-parse refs/tags/tree-tag^{tree})
	3:tree:/tagged-trees:$(git rev-parse refs/tags/tree-tag2)
	blobs:2
	commits:4
	tags:7
	trees:2
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'all, combine:object:type=blob+blob:limit=3 filter' '
	test-tool path-walk \
		--filter=combine:object:type=blob+blob:limit=3 \
		-- --all >out &&

	cat >expect <<-EOF &&
	0:blob:/tagged-blobs:$(git rev-parse refs/tags/blob-tag^{})
	0:blob:/tagged-blobs:$(git rev-parse refs/tags/blob-tag2^{})
	1:blob:a:$(git rev-parse base~2:a)
	2:blob:left/b:$(git rev-parse base~2:left/b)
	3:blob:right/c:$(git rev-parse base~2:right/c)
	4:blob:right/d:$(git rev-parse base~1:right/d)
	blobs:6
	commits:0
	tags:0
	trees:0
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'all, combine of disjoint object:types is empty' '
	test-tool path-walk \
		--filter=combine:object:type=blob+object:type=tree \
		-- --all >out &&

	cat >expect <<-EOF &&
	blobs:0
	commits:0
	tags:0
	trees:0
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'combine: rejects unsupported subfilters' '
	test_must_fail test-tool path-walk \
		--filter=combine:tree:1+blob:none -- --all 2>err &&
	test_grep "tree:1 filter not supported by the path-walk API" err
'

test_expect_success 'setup sparse filter blob' '
	# Cone-mode patterns: include root, exclude all dirs, include left/
	cat >patterns <<-\EOF &&
	/*
	!/*/
	/left/
	EOF
	sparse_oid=$(git hash-object -w -t blob patterns)
'

test_expect_success 'all, sparse:oid filter' '
	test-tool path-walk --filter=sparse:oid=$sparse_oid -- --all >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	0:commit::$(git rev-parse base)
	0:commit::$(git rev-parse base~1)
	0:commit::$(git rev-parse base~2)
	1:tag:/tags:$(git rev-parse refs/tags/first)
	1:tag:/tags:$(git rev-parse refs/tags/second.1)
	1:tag:/tags:$(git rev-parse refs/tags/second.2)
	1:tag:/tags:$(git rev-parse refs/tags/third)
	1:tag:/tags:$(git rev-parse refs/tags/fourth)
	1:tag:/tags:$(git rev-parse refs/tags/tree-tag)
	1:tag:/tags:$(git rev-parse refs/tags/blob-tag)
	2:blob:/tagged-blobs:$(git rev-parse refs/tags/blob-tag^{})
	2:blob:/tagged-blobs:$(git rev-parse refs/tags/blob-tag2^{})
	3:tree::$(git rev-parse topic^{tree})
	3:tree::$(git rev-parse base^{tree})
	3:tree::$(git rev-parse base~1^{tree})
	3:tree::$(git rev-parse base~2^{tree})
	4:blob:a:$(git rev-parse base~2:a)
	5:tree:/tagged-trees:$(git rev-parse refs/tags/tree-tag^{})
	5:tree:/tagged-trees:$(git rev-parse refs/tags/tree-tag2^{})
	6:blob:file2:$(git rev-parse refs/tags/tree-tag2^{}:file2)
	7:tree:a/:$(git rev-parse base:a)
	8:tree:child/:$(git rev-parse refs/tags/tree-tag:child)
	9:tree:left/:$(git rev-parse base:left)
	9:tree:left/:$(git rev-parse base~2:left)
	10:blob:left/b:$(git rev-parse base~2:left/b)
	10:blob:left/b:$(git rev-parse base:left/b)
	11:tree:right/:$(git rev-parse topic:right)
	11:tree:right/:$(git rev-parse base~1:right)
	11:tree:right/:$(git rev-parse base~2:right)
	blobs:6
	commits:4
	tags:7
	trees:13
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'topic only, sparse:oid filter' '
	test-tool path-walk --filter=sparse:oid=$sparse_oid -- topic >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	0:commit::$(git rev-parse base~1)
	0:commit::$(git rev-parse base~2)
	1:tree::$(git rev-parse topic^{tree})
	1:tree::$(git rev-parse base~1^{tree})
	1:tree::$(git rev-parse base~2^{tree})
	2:blob:a:$(git rev-parse base~2:a)
	3:tree:left/:$(git rev-parse base~2:left)
	4:blob:left/b:$(git rev-parse base~2:left/b)
	5:tree:right/:$(git rev-parse topic:right)
	5:tree:right/:$(git rev-parse base~1:right)
	5:tree:right/:$(git rev-parse base~2:right)
	blobs:2
	commits:3
	tags:0
	trees:7
	EOF

	test_cmp_sorted expect out
'

# Demonstrate the SEEN flag ordering issue: when the same tree/blob OID
# appears at two sibling paths where one is in-cone and the other is
# out-of-cone, the path-walk must still discover blobs at the in-cone
# path even when the shared tree OID was first encountered out-of-cone.
# Since sparse:oid includes all trees, the out-of-cone tree (aaa/) is
# walked first, and its blob is skipped. The path-walk then re-walks
# the same tree OID at the in-cone path (zzz/) to find the blob there.

test_expect_success 'setup shared tree OID across cone boundary' '
	git checkout --orphan shared-tree &&
	git rm -rf . &&
	mkdir aaa zzz &&
	echo "shared content" >aaa/file &&
	echo "shared content" >zzz/file &&
	echo "root file" >rootfile &&
	git add aaa zzz rootfile &&
	git commit -m "aaa and zzz have same tree OID" &&

	# Verify they really share a tree OID
	aaa_tree=$(git rev-parse HEAD:aaa) &&
	zzz_tree=$(git rev-parse HEAD:zzz) &&
	test "$aaa_tree" = "$zzz_tree" &&

	# Cone pattern: include root + zzz/ (not aaa/)
	cat >shared-patterns <<-\EOF &&
	/*
	!/*/
	/zzz/
	EOF
	shared_sparse_oid=$(git hash-object -w -t blob shared-patterns)
'

test_expect_success 'sparse:oid with shared tree OID across cone boundary' '
	test-tool path-walk \
		--filter=sparse:oid=$shared_sparse_oid \
		-- shared-tree >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse shared-tree)
	1:tree::$(git rev-parse shared-tree^{tree})
	2:blob:rootfile:$(git rev-parse shared-tree:rootfile)
	3:tree:aaa/:$(git rev-parse shared-tree:aaa)
	4:tree:zzz/:$(git rev-parse shared-tree:zzz)
	5:blob:zzz/file:$(git rev-parse shared-tree:zzz/file)
	blobs:2
	commits:1
	tags:0
	trees:3
	EOF

	test_cmp_sorted expect out
'

test_done
