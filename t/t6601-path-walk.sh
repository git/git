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
	0:tree::$(git rev-parse topic^{tree})
	0:tree::$(git rev-parse base^{tree})
	0:tree::$(git rev-parse base~1^{tree})
	0:tree::$(git rev-parse base~2^{tree})
	1:tree:right/:$(git rev-parse topic:right)
	1:tree:right/:$(git rev-parse base~1:right)
	1:tree:right/:$(git rev-parse base~2:right)
	2:blob:right/d:$(git rev-parse base~1:right/d)
	3:blob:right/c:$(git rev-parse base~2:right/c)
	3:blob:right/c:$(git rev-parse topic:right/c)
	4:tree:left/:$(git rev-parse base:left)
	4:tree:left/:$(git rev-parse base~2:left)
	5:blob:left/b:$(git rev-parse base~2:left/b)
	5:blob:left/b:$(git rev-parse base:left/b)
	6:blob:a:$(git rev-parse base~2:a)
	blobs:6
	trees:9
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'topic only' '
	test-tool path-walk -- topic >out &&

	cat >expect <<-EOF &&
	0:tree::$(git rev-parse topic^{tree})
	0:tree::$(git rev-parse base~1^{tree})
	0:tree::$(git rev-parse base~2^{tree})
	1:tree:right/:$(git rev-parse topic:right)
	1:tree:right/:$(git rev-parse base~1:right)
	1:tree:right/:$(git rev-parse base~2:right)
	2:blob:right/d:$(git rev-parse base~1:right/d)
	3:blob:right/c:$(git rev-parse base~2:right/c)
	3:blob:right/c:$(git rev-parse topic:right/c)
	4:tree:left/:$(git rev-parse base~2:left)
	5:blob:left/b:$(git rev-parse base~2:left/b)
	6:blob:a:$(git rev-parse base~2:a)
	blobs:5
	trees:7
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'topic, not base' '
	test-tool path-walk -- topic --not base >out &&

	cat >expect <<-EOF &&
	0:tree::$(git rev-parse topic^{tree})
	1:tree:right/:$(git rev-parse topic:right)
	2:blob:right/d:$(git rev-parse topic:right/d)
	3:blob:right/c:$(git rev-parse topic:right/c)
	4:tree:left/:$(git rev-parse topic:left)
	5:blob:left/b:$(git rev-parse topic:left/b)
	6:blob:a:$(git rev-parse topic:a)
	blobs:4
	trees:3
	EOF

	test_cmp_sorted expect out
'

test_expect_success 'topic, not base, boundary' '
	test-tool path-walk -- --boundary topic --not base >out &&

	cat >expect <<-EOF &&
	0:tree::$(git rev-parse topic^{tree})
	0:tree::$(git rev-parse base~1^{tree})
	1:tree:right/:$(git rev-parse topic:right)
	1:tree:right/:$(git rev-parse base~1:right)
	2:blob:right/d:$(git rev-parse base~1:right/d)
	3:blob:right/c:$(git rev-parse base~1:right/c)
	3:blob:right/c:$(git rev-parse topic:right/c)
	4:tree:left/:$(git rev-parse base~1:left)
	5:blob:left/b:$(git rev-parse base~1:left/b)
	6:blob:a:$(git rev-parse base~1:a)
	blobs:5
	trees:5
	EOF

	test_cmp_sorted expect out
'

test_done
