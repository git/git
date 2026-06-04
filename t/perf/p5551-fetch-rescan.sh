#!/bin/sh

test_description='fetch performance with many packs

It is common for fetch to consider objects that we might not have, and it is an
easy mistake for the code to use a function like `parse_object` that might
give the correct _answer_ on such an object, but do so slowly (due to
re-scanning the pack directory for lookup failures).

The resulting performance drop can be hard to notice in a real repository, but
becomes quite large in a repository with a large number of packs. So this
test creates a more pathological case, since any mistakes would produce a more
noticeable slowdown.
'
. ./perf-lib.sh
. "$TEST_DIRECTORY"/perf/lib-pack.sh

test_expect_success 'create parent and child' '
	git init parent &&
	git clone parent child
'


test_expect_success 'create refs in the parent' '
	(
		cd parent &&
		git commit --allow-empty -m foo &&
		head=$(git rev-parse HEAD) &&
		test_seq 1000 |
		sed "s,.*,update refs/heads/& $head," |
		$MODERN_GIT update-ref --stdin
	)
'

test_expect_success 'create many packs in the child' '
	(
		cd child &&
		setup_many_packs
	)
'

test_perf 'fetch' '
	# start at the same state for each iteration
	obj=$($MODERN_GIT -C parent rev-parse HEAD) &&
	(
		cd child &&
		$MODERN_GIT for-each-ref --format="delete %(refname)" refs/remotes |
		$MODERN_GIT update-ref --stdin &&
		rm -vf .git/objects/$(echo $obj | sed "s|^..|&/|") &&

		git fetch
	)
'

test_done
