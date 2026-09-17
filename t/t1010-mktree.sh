#!/bin/sh

test_description='git mktree'

. ./test-lib.sh

test_expect_success setup '
	for d in a a- a0
	do
		mkdir "$d" && echo "$d/one" >"$d/one" &&
		git add "$d" || return 1
	done &&
	echo zero >one &&
	if test_have_prereq BROKEN_OBJECTS
	then
		git update-index --add --info-only one &&
		git write-tree --missing-ok >tree.missing &&
		git ls-tree $(cat tree.missing) >top.missing &&
		git ls-tree -r $(cat tree.missing) >all.missing
	fi &&
	echo one >one &&
	git add one &&
	git write-tree >tree &&
	git ls-tree $(cat tree) >top &&
	git ls-tree -r $(cat tree) >all &&
	test_tick &&
	git commit -q -m one &&
	H=$(git rev-parse HEAD) &&
	git update-index --add --cacheinfo 160000 $H sub &&
	test_tick &&
	git commit -q -m two &&
	git rev-parse HEAD^{tree} >tree.withsub &&
	git ls-tree HEAD >top.withsub &&
	git ls-tree -r HEAD >all.withsub
'

test_expect_success 'ls-tree piped to mktree (1)' '
	git mktree <top >actual &&
	test_cmp tree actual
'

test_expect_success 'ls-tree piped to mktree (2)' '
	git mktree <top.withsub >actual &&
	test_cmp tree.withsub actual
'

test_expect_success 'ls-tree output in wrong order given to mktree (1)' '
	sort -r <top |
	git mktree >actual &&
	test_cmp tree actual
'

test_expect_success 'ls-tree output in wrong order given to mktree (2)' '
	sort -r <top.withsub |
	git mktree >actual &&
	test_cmp tree.withsub actual
'

test_expect_success BROKEN_OBJECTS 'allow missing object with --missing' '
	git mktree --missing <top.missing >actual &&
	test_cmp tree.missing actual
'

test_expect_success 'mktree refuses to read ls-tree -r output (1)' '
	test_must_fail git mktree <all
'

test_expect_success 'mktree refuses to read ls-tree -r output (2)' '
	test_must_fail git mktree <all.withsub
'

test_expect_success PIPE 'mktree --batch survives a concurrent repack retiring a pack' '
	test_when_finished "rm -fr race" &&
	git init race &&
	(
		cd race &&
		test_commit seed &&
		a=$(echo A | git hash-object -w --stdin) &&
		b=$(echo B | git hash-object -w --stdin) &&
		echo "$a" | git pack-objects .git/objects/pack/pack >pack-a &&
		echo "$b" | git pack-objects .git/objects/pack/pack >pack-b &&

		# Drop the loose copies so the blobs resolve only through the
		# packs the multi-pack-index names.
		git prune-packed &&
		git multi-pack-index write &&
		printf "100644 blob %s\ta\n" "$a" >tree-a &&
		printf "100644 blob %s\tb\n" "$b" >tree-b &&

		victim=".git/objects/pack/pack-$(cat pack-b)" &&
		mkfifo in out &&

		# mktree --batch stays resident, so its pack view predates the
		# repack below; feed it one tree at a time over a fifo.  The
		# subshell exit closes the fifos, letting mktree see EOF and quit.
		(git mktree --batch <in >out 2>err &) &&
		exec 9>in &&
		exec 8<out &&

		# The first tree makes the reader cache its (soon stale) view.
		cat tree-a >&9 && echo >&9 && read tree_a <&8 &&

		# Mimic a concurrent repack: a replacement pack holds every
		# object, and the pack for b loses its .idx (its .pack lingers),
		# matching the order in which unlink_pack_path() removes files.
		git cat-file --batch-all-objects --batch-check="%(objectname)" >oids &&
		git pack-objects .git/objects/pack/pack <oids >/dev/null &&
		rm -f "$victim.idx" &&

		# Resolving b used to fail, as its QUICK lookup accepted the
		# miss; without QUICK the reader repreps and finds b in the
		# replacement pack.
		cat tree-b >&9 && echo >&9 && read tree_b <&8 &&
		exec 9>&- &&

		test -n "$tree_b"
	)
'

test_done
