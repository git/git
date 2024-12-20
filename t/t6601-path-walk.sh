#!/bin/sh

TEST_PASSES_SANITIZE_LEAK=true

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
	0:commit::$(git rev-parse topic)
	0:commit::$(git rev-parse base)
	0:commit::$(git rev-parse base~1)
	0:commit::$(git rev-parse base~2)
	1:tree::$(git rev-parse topic^{tree})
	1:tree::$(git rev-parse base^{tree})
	1:tree::$(git rev-parse base~1^{tree})
	1:tree::$(git rev-parse base~2^{tree})
	2:tree:right/:$(git rev-parse topic:right)
	2:tree:right/:$(git rev-parse base~1:right)
	2:tree:right/:$(git rev-parse base~2:right)
	3:blob:right/d:$(git rev-parse base~1:right/d)
	4:blob:right/c:$(git rev-parse base~2:right/c)
	4:blob:right/c:$(git rev-parse topic:right/c)
	5:tree:left/:$(git rev-parse base:left)
	5:tree:left/:$(git rev-parse base~2:left)
	6:blob:left/b:$(git rev-parse base~2:left/b)
	6:blob:left/b:$(git rev-parse base:left/b)
	7:blob:a:$(git rev-parse base~2:a)
	blobs:6
	commits:4
	trees:9
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
	2:tree:right/:$(git rev-parse topic:right)
	2:tree:right/:$(git rev-parse base~1:right)
	2:tree:right/:$(git rev-parse base~2:right)
	3:blob:right/d:$(git rev-parse base~1:right/d)
	4:blob:right/c:$(git rev-parse base~2:right/c)
	4:blob:right/c:$(git rev-parse topic:right/c)
	5:tree:left/:$(git rev-parse base~2:left)
	6:blob:left/b:$(git rev-parse base~2:left/b)
	7:blob:a:$(git rev-parse base~2:a)
	blobs:5
	commits:3
	trees:7
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'topic, not base' '
	test-tool path-walk -- topic --not base >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	1:tree::$(git rev-parse topic^{tree})
	2:tree:right/:$(git rev-parse topic:right)
	3:blob:right/d:$(git rev-parse topic:right/d)
	4:blob:right/c:$(git rev-parse topic:right/c)
	5:tree:left/:$(git rev-parse topic:left)
	6:blob:left/b:$(git rev-parse topic:left/b)
	7:blob:a:$(git rev-parse topic:a)
	blobs:4
	commits:1
	trees:3
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'topic, not base, only blobs' '
	test-tool path-walk --no-trees --no-commits \
		-- topic --not base >out &&

	cat >expect <<-EOF &&
	commits:0
	trees:0
	0:blob:right/d:$(git rev-parse topic:right/d)
	1:blob:right/c:$(git rev-parse topic:right/c)
	2:blob:left/b:$(git rev-parse topic:left/b)
	3:blob:a:$(git rev-parse topic:a)
	blobs:4
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
	trees:0
	blobs:0
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'topic, not base, only trees' '
	test-tool path-walk --no-blobs --no-commits \
		-- topic --not base >out &&

	cat >expect <<-EOF &&
	commits:0
	0:tree::$(git rev-parse topic^{tree})
	1:tree:right/:$(git rev-parse topic:right)
	2:tree:left/:$(git rev-parse topic:left)
	trees:3
	blobs:0
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'topic, not base, boundary' '
	test-tool path-walk -- --boundary topic --not base >out &&

	cat >expect <<-EOF &&
	0:commit::$(git rev-parse topic)
	0:commit::$(git rev-parse base~1)
	1:tree::$(git rev-parse topic^{tree})
	1:tree::$(git rev-parse base~1^{tree})
	2:tree:right/:$(git rev-parse topic:right)
	2:tree:right/:$(git rev-parse base~1:right)
	3:blob:right/d:$(git rev-parse base~1:right/d)
	4:blob:right/c:$(git rev-parse base~1:right/c)
	4:blob:right/c:$(git rev-parse topic:right/c)
	5:tree:left/:$(git rev-parse base~1:left)
	6:blob:left/b:$(git rev-parse base~1:left/b)
	7:blob:a:$(git rev-parse base~1:a)
	blobs:5
	commits:2
	trees:5
	EOF

	test_cmp_sorted expect out
'

test_done
