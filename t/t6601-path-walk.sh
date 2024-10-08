#!/bin/sh

test_description='direct path-walk API tests'

. ./test-lib.sh

test_expect_success 'setup test repository' '
	git checkout -b base &&

	# Make some objects that will only be reachable
	# via non-commit tags.
	mkdir child &&
	echo file >child/file &&
	git add child &&
	git commit -m "will abandon" &&
	git tag -a -m "tree" tree-tag HEAD^{tree} &&
	echo file2 >file2 &&
	git add file2 &&
	git commit --amend -m "will abandon" &&
	git tag tree-tag2 HEAD^{tree} &&

	echo blob >file &&
	blob_oid=$(git hash-object -t blob -w --stdin <file) &&
	git tag -a -m "blob" blob-tag "$blob_oid" &&
	echo blob2 >file2 &&
	blob2_oid=$(git hash-object -t blob -w --stdin <file2) &&
	git tag blob-tag2 "$blob2_oid" &&

	rm -fr child file file2 &&

	mkdir left &&
	mkdir right &&
	echo a >a &&
	echo b >left/b &&
	echo c >right/c &&
	git add . &&
	git commit --amend -m "first" &&
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
	COMMIT::$(git rev-parse topic)
	COMMIT::$(git rev-parse base)
	COMMIT::$(git rev-parse base~1)
	COMMIT::$(git rev-parse base~2)
	commits:4
	TREE::$(git rev-parse topic^{tree})
	TREE::$(git rev-parse base^{tree})
	TREE::$(git rev-parse base~1^{tree})
	TREE::$(git rev-parse base~2^{tree})
	TREE::$(git rev-parse refs/tags/tree-tag^{})
	TREE::$(git rev-parse refs/tags/tree-tag2^{})
	TREE:a/:$(git rev-parse base:a)
	TREE:left/:$(git rev-parse base:left)
	TREE:left/:$(git rev-parse base~2:left)
	TREE:right/:$(git rev-parse topic:right)
	TREE:right/:$(git rev-parse base~1:right)
	TREE:right/:$(git rev-parse base~2:right)
	TREE:child/:$(git rev-parse refs/tags/tree-tag^{}:child)
	trees:13
	BLOB:a:$(git rev-parse base~2:a)
	BLOB:file2:$(git rev-parse refs/tags/tree-tag2^{}:file2)
	BLOB:left/b:$(git rev-parse base~2:left/b)
	BLOB:left/b:$(git rev-parse base:left/b)
	BLOB:right/c:$(git rev-parse base~2:right/c)
	BLOB:right/c:$(git rev-parse topic:right/c)
	BLOB:right/d:$(git rev-parse base~1:right/d)
	BLOB:/tagged-blobs:$(git rev-parse refs/tags/blob-tag^{})
	BLOB:/tagged-blobs:$(git rev-parse refs/tags/blob-tag2^{})
	BLOB:child/file:$(git rev-parse refs/tags/tree-tag^{}:child/file)
	blobs:10
	TAG::$(git rev-parse refs/tags/first)
	TAG::$(git rev-parse refs/tags/second.1)
	TAG::$(git rev-parse refs/tags/second.2)
	TAG::$(git rev-parse refs/tags/third)
	TAG::$(git rev-parse refs/tags/fourth)
	TAG::$(git rev-parse refs/tags/tree-tag)
	TAG::$(git rev-parse refs/tags/blob-tag)
	tags:7
	EOF

	sort expect >expect.sorted &&
	sort out >out.sorted &&

	test_cmp expect.sorted out.sorted
'

test_expect_success 'base & topic, sparse' '
	cat >patterns <<-EOF &&
	/*
	!/*/
	/left/
	EOF

	test-tool path-walk --stdin-pl -- base topic <patterns >out &&

	cat >expect <<-EOF &&
	COMMIT::$(git rev-parse topic)
	COMMIT::$(git rev-parse base)
	COMMIT::$(git rev-parse base~1)
	COMMIT::$(git rev-parse base~2)
	commits:4
	TREE::$(git rev-parse topic^{tree})
	TREE::$(git rev-parse base^{tree})
	TREE::$(git rev-parse base~1^{tree})
	TREE::$(git rev-parse base~2^{tree})
	TREE:left/:$(git rev-parse base:left)
	TREE:left/:$(git rev-parse base~2:left)
	trees:6
	BLOB:a:$(git rev-parse base~2:a)
	BLOB:left/b:$(git rev-parse base~2:left/b)
	BLOB:left/b:$(git rev-parse base:left/b)
	blobs:3
	tags:0
	EOF

	sort expect >expect.sorted &&
	sort out >out.sorted &&

	test_cmp expect.sorted out.sorted
'

test_expect_success 'topic only' '
	test-tool path-walk -- topic >out &&

	cat >expect <<-EOF &&
	COMMIT::$(git rev-parse topic)
	COMMIT::$(git rev-parse base~1)
	COMMIT::$(git rev-parse base~2)
	commits:3
	TREE::$(git rev-parse topic^{tree})
	TREE::$(git rev-parse base~1^{tree})
	TREE::$(git rev-parse base~2^{tree})
	TREE:left/:$(git rev-parse base~2:left)
	TREE:right/:$(git rev-parse topic:right)
	TREE:right/:$(git rev-parse base~1:right)
	TREE:right/:$(git rev-parse base~2:right)
	trees:7
	BLOB:a:$(git rev-parse base~2:a)
	BLOB:left/b:$(git rev-parse base~2:left/b)
	BLOB:right/c:$(git rev-parse base~2:right/c)
	BLOB:right/c:$(git rev-parse topic:right/c)
	BLOB:right/d:$(git rev-parse base~1:right/d)
	blobs:5
	tags:0
	EOF

	sort expect >expect.sorted &&
	sort out >out.sorted &&

	test_cmp expect.sorted out.sorted
'

test_expect_success 'topic, not base' '
	test-tool path-walk -- topic --not base >out &&

	cat >expect <<-EOF &&
	COMMIT::$(git rev-parse topic)
	commits:1
	TREE::$(git rev-parse topic^{tree})
	TREE:left/:$(git rev-parse topic:left)
	TREE:right/:$(git rev-parse topic:right)
	trees:3
	BLOB:a:$(git rev-parse topic:a)
	BLOB:left/b:$(git rev-parse topic:left/b)
	BLOB:right/c:$(git rev-parse topic:right/c)
	BLOB:right/d:$(git rev-parse topic:right/d)
	blobs:4
	tags:0
	EOF

	sort expect >expect.sorted &&
	sort out >out.sorted &&

	test_cmp expect.sorted out.sorted
'

test_expect_success 'topic, not base, only blobs' '
	test-tool path-walk --no-trees --no-commits \
		-- topic --not base >out &&

	cat >expect <<-EOF &&
	commits:0
	trees:0
	BLOB:a:$(git rev-parse topic:a)
	BLOB:left/b:$(git rev-parse topic:left/b)
	BLOB:right/c:$(git rev-parse topic:right/c)
	BLOB:right/d:$(git rev-parse topic:right/d)
	blobs:4
	tags:0
	EOF

	sort expect >expect.sorted &&
	sort out >out.sorted &&

	test_cmp expect.sorted out.sorted
'

# No, this doesn't make a lot of sense for the path-walk API,
# but it is possible to do.
test_expect_success 'topic, not base, only commits' '
	test-tool path-walk --no-blobs --no-trees \
		-- topic --not base >out &&

	cat >expect <<-EOF &&
	COMMIT::$(git rev-parse topic)
	commits:1
	trees:0
	blobs:0
	tags:0
	EOF

	sort expect >expect.sorted &&
	sort out >out.sorted &&

	test_cmp expect.sorted out.sorted
'

test_expect_success 'topic, not base, only trees' '
	test-tool path-walk --no-blobs --no-commits \
		-- topic --not base >out &&

	cat >expect <<-EOF &&
	commits:0
	TREE::$(git rev-parse topic^{tree})
	TREE:left/:$(git rev-parse topic:left)
	TREE:right/:$(git rev-parse topic:right)
	trees:3
	blobs:0
	tags:0
	EOF

	sort expect >expect.sorted &&
	sort out >out.sorted &&

	test_cmp expect.sorted out.sorted
'

test_expect_success 'topic, not base, boundary' '
	test-tool path-walk -- --boundary topic --not base >out &&

	cat >expect <<-EOF &&
	COMMIT::$(git rev-parse topic)
	COMMIT::$(git rev-parse base~1):UNINTERESTING
	commits:2
	TREE::$(git rev-parse topic^{tree})
	TREE::$(git rev-parse base~1^{tree}):UNINTERESTING
	TREE:left/:$(git rev-parse base~1:left):UNINTERESTING
	TREE:right/:$(git rev-parse topic:right)
	TREE:right/:$(git rev-parse base~1:right):UNINTERESTING
	trees:5
	BLOB:a:$(git rev-parse base~1:a):UNINTERESTING
	BLOB:left/b:$(git rev-parse base~1:left/b):UNINTERESTING
	BLOB:right/c:$(git rev-parse base~1:right/c):UNINTERESTING
	BLOB:right/c:$(git rev-parse topic:right/c)
	BLOB:right/d:$(git rev-parse base~1:right/d):UNINTERESTING
	blobs:5
	tags:0
	EOF

	sort expect >expect.sorted &&
	sort out >out.sorted &&

	test_cmp expect.sorted out.sorted
'

test_expect_success 'topic, not base, boundary with pruning' '
	test-tool path-walk --prune -- --boundary topic --not base >out &&

	cat >expect <<-EOF &&
	COMMIT::$(git rev-parse topic)
	COMMIT::$(git rev-parse base~1):UNINTERESTING
	commits:2
	TREE::$(git rev-parse topic^{tree})
	TREE::$(git rev-parse base~1^{tree}):UNINTERESTING
	TREE:right/:$(git rev-parse topic:right)
	TREE:right/:$(git rev-parse base~1:right):UNINTERESTING
	trees:4
	BLOB:right/c:$(git rev-parse base~1:right/c):UNINTERESTING
	BLOB:right/c:$(git rev-parse topic:right/c)
	blobs:2
	tags:0
	EOF

	sort expect >expect.sorted &&
	sort out >out.sorted &&

	test_cmp expect.sorted out.sorted
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

test_done
