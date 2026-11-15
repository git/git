#!/bin/sh

test_description='speed of push into a repository with many hidden refs'
. ./perf-lib.sh

test_perf_fresh_repo

test_expect_success 'set up repository with many hidden refs' '
	git init --bare dst.git &&
	test_commit_bulk --ref=refs/hidden/base --id=hidden 1000 &&
	git rev-list refs/hidden/base >commits &&
	for i in $(test_seq 200)
	do
		sed "s|^\(.*\)$|create refs/hidden/$i/\1 \1|" commits || return 1
	done >input &&
	git push dst.git refs/hidden/base &&
	git -C dst.git update-ref --stdin <input &&
	git -C dst.git pack-refs --all &&
	cp -R dst.git dst-visible.git &&
	git -C dst.git config receive.hideRefs refs/hidden &&
	git symbolic-ref HEAD refs/heads/main &&
	test_commit_bulk --id=visible 1 &&
	git push dst.git main &&
	git push dst-visible.git main
'

test_perf 'push, hidden refs' '
	git commit --allow-empty -m new &&
	git push -f dst.git HEAD:refs/heads/new
'

test_perf 'push, no hidden refs' '
	git commit --allow-empty -m new &&
	git push -f dst-visible.git HEAD:refs/heads/new
'

test_done
