#!/bin/sh

test_description='direct path-walk API tests'

. ./test-lib.sh

test_expect_success 'setup test repository' '
	git checkout -b base &&

	mkdir left &&
	mkdir right &&
	echo a >a &&
	echo b >left/b &&
	echo c >right/c &&
	git add . &&
	git commit -m "first" &&

	echo d >right/d &&
	git add right &&
	git commit -m "second" &&

	echo bb >left/b &&
	git commit -a -m "third" &&

	git checkout -b topic HEAD~1 &&
	echo cc >right/c &&
	git commit -a -m "topic"
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
	TREE:left/:$(git rev-parse base:left)
	TREE:left/:$(git rev-parse base~2:left)
	TREE:right/:$(git rev-parse topic:right)
	TREE:right/:$(git rev-parse base~1:right)
	TREE:right/:$(git rev-parse base~2:right)
	trees:9
	BLOB:a:$(git rev-parse base~2:a)
	BLOB:left/b:$(git rev-parse base~2:left/b)
	BLOB:left/b:$(git rev-parse base:left/b)
	BLOB:right/c:$(git rev-parse base~2:right/c)
	BLOB:right/c:$(git rev-parse topic:right/c)
	BLOB:right/d:$(git rev-parse base~1:right/d)
	blobs:6
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
	EOF

	sort expect >expect.sorted &&
	sort out >out.sorted &&

	test_cmp expect.sorted out.sorted
'

test_expect_success 'topic, not base, boundary' '
	test-tool path-walk -- --boundary topic --not base >out &&

	cat >expect <<-EOF &&
	COMMIT::$(git rev-parse topic)
	COMMIT::$(git rev-parse base~1)
	commits:2
	TREE::$(git rev-parse topic^{tree})
	TREE::$(git rev-parse base~1^{tree})
	TREE:left/:$(git rev-parse base~1:left)
	TREE:right/:$(git rev-parse topic:right)
	TREE:right/:$(git rev-parse base~1:right)
	trees:5
	BLOB:a:$(git rev-parse base~1:a)
	BLOB:left/b:$(git rev-parse base~1:left/b)
	BLOB:right/c:$(git rev-parse base~1:right/c)
	BLOB:right/c:$(git rev-parse topic:right/c)
	BLOB:right/d:$(git rev-parse base~1:right/d)
	blobs:5
	EOF

	sort expect >expect.sorted &&
	sort out >out.sorted &&

	test_cmp expect.sorted out.sorted
'

test_done
