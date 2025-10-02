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
	3:tree::$(git rev-parse refs/tags/tree-tag^{})
	3:tree::$(git rev-parse refs/tags/tree-tag2^{})
	4:blob:a:$(git rev-parse base~2:a)
	5:blob:file2:$(git rev-parse refs/tags/tree-tag2^{}:file2)
	6:tree:a/:$(git rev-parse base:a)
	7:tree:child/:$(git rev-parse refs/tags/tree-tag:child)
	8:blob:child/file:$(git rev-parse refs/tags/tree-tag:child/file)
	9:tree:left/:$(git rev-parse base:left)
	9:tree:left/:$(git rev-parse base~2:left)
	10:blob:left/b:$(git rev-parse base~2:left/b)
	10:blob:left/b:$(git rev-parse base:left/b)
	11:tree:right/:$(git rev-parse topic:right)
	11:tree:right/:$(git rev-parse base~1:right)
	11:tree:right/:$(git rev-parse base~2:right)
	12:blob:right/c:$(git rev-parse base~2:right/c)
	12:blob:right/c:$(git rev-parse topic:right/c)
	13:blob:right/d:$(git rev-parse base~1:right/d)
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

test_done
