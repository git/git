#!/bin/sh

test_description='rebase should reread the todo file if an exec modifies it'

. ./test-lib.sh

test_expect_success 'rebase exec modifies rebase-todo' '
	test_commit initial &&
	todo=.git/rebase-merge/git-rebase-todo &&
	git rebase HEAD -x "echo exec touch F >>$todo" &&
	test -e F
'

test_expect_success SHA1 'loose object cache vs re-reading todo list' '
	GIT_REBASE_TODO=.git/rebase-merge/git-rebase-todo &&
	export GIT_REBASE_TODO &&
	write_script append-todo.sh <<-\EOS &&
	# For values 5 and 6, this yields SHA-1s with the same first two digits
	echo "pick $(git rev-parse --short \
		$(printf "%s\\n" \
			"tree $EMPTY_TREE" \
			"author A U Thor <author@example.org> $1 +0000" \
			"committer A U Thor <author@example.org> $1 +0000" \
			"" \
			"$1" |
		  git hash-object -t commit -w --stdin))" >>$GIT_REBASE_TODO

	shift
	test -z "$*" ||
	echo "exec $0 $*" >>$GIT_REBASE_TODO
	EOS

	git rebase HEAD -x "./append-todo.sh 5 6"
'

test_done
