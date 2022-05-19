#!/bin/sh

test_description='check that read-tree rejects confusing paths'

TEST_PASSES_SANITIZE_LEAK=true
. ./test-lib.sh

test_expect_success 'create base tree' '
	echo content >file &&
	but add file &&
	but cummit -m base &&
	blob=$(but rev-parse HEAD:file) &&
	tree=$(but rev-parse HEAD^{tree})
'

test_expect_success 'enable core.protectHFS for rejection tests' '
	but config core.protectHFS true
'

test_expect_success 'enable core.protectNTFS for rejection tests' '
	but config core.protectNTFS true
'

while read path pretty; do
	: ${pretty:=$path}
	case "$path" in
	*SPACE)
		path="${path%SPACE} "
		;;
	esac
	test_expect_success "reject $pretty at end of path" '
		printf "100644 blob %s\t%s" "$blob" "$path" >tree &&
		bogus=$(but mktree <tree) &&
		test_must_fail but read-tree $bogus
	'

	test_expect_success "reject $pretty as subtree" '
		printf "040000 tree %s\t%s" "$tree" "$path" >tree &&
		bogus=$(but mktree <tree) &&
		test_must_fail but read-tree $bogus
	'
done <<-EOF
.
..
.but
.GIT
${u200c}.Git {u200c}.Git
.gI${u200c}T .gI{u200c}T
.GiT${u200c} .GiT{u200c}
but~1
.but.SPACE .but.{space}
.\\\\.GIT\\\\foobar backslashes
.but\\\\foobar backslashes2
.but...:alternate-stream
EOF

test_expect_success 'utf-8 paths allowed with core.protectHFS off' '
	test_when_finished "but read-tree HEAD" &&
	test_config core.protectHFS false &&
	printf "100644 blob %s\t%s" "$blob" ".gi${u200c}t" >tree &&
	ok=$(but mktree <tree) &&
	but read-tree $ok
'

test_done
