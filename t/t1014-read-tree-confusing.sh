#!/bin/sh

test_description='check that read-tree rejects confusing paths'
. ./test-lib.sh

test_expect_success 'create base tree' '
	echo content >file &&
	git add file &&
	git commit -m base &&
	blob=$(git rev-parse HEAD:file) &&
	tree=$(git rev-parse HEAD^{tree})
'

while read path; do
	test_expect_success "reject $path at end of path" '
		printf "100644 blob %s\t%s" "$blob" "$path" >tree &&
		bogus=$(git mktree <tree) &&
		test_must_fail git read-tree $bogus
	'

	test_expect_success "reject $path as subtree" '
		printf "040000 tree %s\t%s" "$tree" "$path" >tree &&
		bogus=$(git mktree <tree) &&
		test_must_fail git read-tree $bogus
	'
done <<-\EOF
.
..
.git
.GIT
EOF

test_done
