#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='git checkout tests.

Creates main, forks renamer and side branches from it.
Test switching across them.

  ! [main] Initial A one, A two
   * [renamer] Renamer R one->uno, M two
    ! [side] Side M one, D two, A three
     ! [simple] Simple D one, M two
  ----
     + [simple] Simple D one, M two
    +  [side] Side M one, D two, A three
   *   [renamer] Renamer R one->uno, M two
  +*++ [main] Initial A one, A two

'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh

test_tick

fill () {
	for i
	do
		echo "$i"
	done
}


test_expect_success setup '
	fill x y z >same &&
	fill 1 2 3 4 5 6 7 8 >one &&
	fill a b c d e >two &&
	git add same one two &&
	git commit -m "Initial A one, A two" &&

	git checkout -b renamer &&
	rm -f one &&
	fill 1 3 4 5 6 7 8 >uno &&
	git add uno &&
	fill a b c d e f >two &&
	git commit -a -m "Renamer R one->uno, M two" &&

	git checkout -b side main &&
	fill 1 2 3 4 5 6 7 >one &&
	fill A B C D E >three &&
	rm -f two &&
	git update-index --add --remove one two three &&
	git commit -m "Side M one, D two, A three" &&

	git checkout -b simple main &&
	rm -f one &&
	fill a c e >two &&
	git commit -a -m "Simple D one, M two" &&

	git checkout main
'

test_expect_success 'checkout from non-existing branch' '
	git checkout -b delete-me main &&
	git update-ref -d --no-deref refs/heads/delete-me &&
	test refs/heads/delete-me = "$(git symbolic-ref HEAD)" &&
	git checkout main &&
	test refs/heads/main = "$(git symbolic-ref HEAD)"
'

test_expect_success 'checkout with dirty tree without -m' '
	fill 0 1 2 3 4 5 6 7 8 >one &&
	if git checkout side
	then
		echo Not happy
		false
	else
		echo "happy - failed correctly"
	fi
'

test_expect_success 'checkout with unrelated dirty tree without -m' '
	git checkout -f main &&
	fill 0 1 2 3 4 5 6 7 8 >same &&
	cp same kept &&
	git checkout side >messages &&
	test_cmp same kept &&
	printf "M\t%s\n" same >messages.expect &&
	test_cmp messages.expect messages
'

test_expect_success 'checkout -m with dirty tree' '
	git checkout -f main &&
	git clean -f &&

	fill 0 1 2 3 4 5 6 7 8 >one &&
	git checkout -m side >messages &&

	test "$(git symbolic-ref HEAD)" = "refs/heads/side" &&

	printf "M\t%s\n" one >expect.messages &&
	test_cmp expect.messages messages &&

	fill "M	one" "A	three" "D	two" >expect.main &&
	git diff --name-status main >current.main &&
	test_cmp expect.main current.main &&

	fill "M	one" >expect.side &&
	git diff --name-status side >current.side &&
	test_cmp expect.side current.side &&

	git diff --cached >current.index &&
	test_must_be_empty current.index
'

test_expect_success 'checkout -m with dirty tree, renamed' '
	git checkout -f main && git clean -f &&

	fill 1 2 3 4 5 7 8 >one &&
	if git checkout renamer
	then
		echo Not happy
		false
	else
		echo "happy - failed correctly"
	fi &&

	git checkout -m renamer &&
	fill 1 3 4 5 7 8 >expect &&
	test_cmp expect uno &&
	! test -f one &&
	git diff --cached >current &&
	test_must_be_empty current
'

test_expect_success 'checkout -m with merge conflict' '
	git checkout -f main && git clean -f &&

	fill 1 T 3 4 5 6 S 8 >one &&
	if git checkout renamer
	then
		echo Not happy
		false
	else
		echo "happy - failed correctly"
	fi &&

	git checkout -m renamer &&

	git diff main:one :3:uno |
	sed -e "1,/^@@/d" -e "/^ /d" -e "s/^-/d/" -e "s/^+/a/" >current &&
	fill d2 aT d7 aS >expect &&
	test_cmp expect current &&
	git diff --cached two >current &&
	test_must_be_empty current
'

test_expect_success 'format of merge conflict from checkout -m' '
	git checkout -f main &&
	git clean -f &&

	fill b d >two &&
	git checkout -m simple &&

	git ls-files >current &&
	fill same two two two >expect &&
	test_cmp expect current &&

	cat <<-EOF >expect &&
	<<<<<<< simple
	a
	c
	e
	=======
	b
	d
	>>>>>>> local
	EOF
	test_cmp expect two
'

test_expect_success 'checkout --merge --conflict=diff3 <branch>' '
	git checkout -f main &&
	git reset --hard &&
	git clean -f &&

	fill b d >two &&
	git checkout --merge --conflict=diff3 simple &&

	cat <<-EOF >expect &&
	<<<<<<< simple
	a
	c
	e
	||||||| main
	a
	b
	c
	d
	e
	=======
	b
	d
	>>>>>>> local
	EOF
	test_cmp expect two
'

test_expect_success 'switch to another branch while carrying a deletion' '
	git checkout -f main &&
	git reset --hard &&
	git clean -f &&
	git rm two &&

	test_must_fail git checkout simple 2>errs &&
	test_grep overwritten errs &&

	test_must_fail git read-tree --quiet -m -u HEAD simple 2>errs &&
	test_must_be_empty errs
'

test_expect_success 'checkout to detach HEAD (with advice declined)' '
	git config advice.detachedHead false &&
	rev=$(git rev-parse --short renamer^) &&
	git checkout -f renamer &&
	git clean -f &&
	git checkout renamer^ 2>messages &&
	test_grep "HEAD is now at $rev" messages &&
	test_line_count = 1 messages &&
	H=$(git rev-parse --verify HEAD) &&
	M=$(git show-ref -s --verify refs/heads/main) &&
	test "z$H" = "z$M" &&
	if git symbolic-ref HEAD >/dev/null 2>&1
	then
		echo "OOPS, HEAD is still symbolic???"
		false
	else
		: happy
	fi
'

test_expect_success 'checkout to detach HEAD' '
	git config advice.detachedHead true &&
	rev=$(git rev-parse --short renamer^) &&
	git checkout -f renamer &&
	git clean -f &&
	git checkout renamer^ 2>messages &&
	grep "HEAD is now at $rev" messages &&
	test_line_count -gt 1 messages &&
	H=$(git rev-parse --verify HEAD) &&
	M=$(git show-ref -s --verify refs/heads/main) &&
	test "z$H" = "z$M" &&
	if git symbolic-ref HEAD >/dev/null 2>&1
	then
		echo "OOPS, HEAD is still symbolic???"
		false
	else
		: happy
	fi
'

test_expect_success 'checkout to detach HEAD with branchname^' '
	git checkout -f main &&
	git clean -f &&
	git checkout renamer^ &&
	H=$(git rev-parse --verify HEAD) &&
	M=$(git show-ref -s --verify refs/heads/main) &&
	test "z$H" = "z$M" &&
	if git symbolic-ref HEAD >/dev/null 2>&1
	then
		echo "OOPS, HEAD is still symbolic???"
		false
	else
		: happy
	fi
'

test_expect_success 'checkout to detach HEAD with :/message' '
	git checkout -f main &&
	git clean -f &&
	git checkout ":/Initial" &&
	H=$(git rev-parse --verify HEAD) &&
	M=$(git show-ref -s --verify refs/heads/main) &&
	test "z$H" = "z$M" &&
	if git symbolic-ref HEAD >/dev/null 2>&1
	then
		echo "OOPS, HEAD is still symbolic???"
		false
	else
		: happy
	fi
'

test_expect_success 'checkout to detach HEAD with HEAD^0' '
	git checkout -f main &&
	git clean -f &&
	git checkout HEAD^0 &&
	H=$(git rev-parse --verify HEAD) &&
	M=$(git show-ref -s --verify refs/heads/main) &&
	test "z$H" = "z$M" &&
	if git symbolic-ref HEAD >/dev/null 2>&1
	then
		echo "OOPS, HEAD is still symbolic???"
		false
	else
		: happy
	fi
'

test_expect_success 'checkout with ambiguous tag/branch names' '
	git tag both side &&
	git branch both main &&
	git reset --hard &&
	git checkout main &&

	git checkout both &&
	H=$(git rev-parse --verify HEAD) &&
	M=$(git show-ref -s --verify refs/heads/main) &&
	test "z$H" = "z$M" &&
	name=$(git symbolic-ref HEAD 2>/dev/null) &&
	test "z$name" = zrefs/heads/both
'

test_expect_success 'checkout with ambiguous tag/branch names' '
	git reset --hard &&
	git checkout main &&

	git tag frotz side &&
	git branch frotz main &&
	git reset --hard &&
	git checkout main &&

	git checkout tags/frotz &&
	H=$(git rev-parse --verify HEAD) &&
	S=$(git show-ref -s --verify refs/heads/side) &&
	test "z$H" = "z$S" &&
	if name=$(git symbolic-ref HEAD 2>/dev/null)
	then
		echo "Bad -- should have detached"
		false
	else
		: happy
	fi
'

test_expect_success 'switch branches while in subdirectory' '
	git reset --hard &&
	git checkout main &&

	mkdir subs &&
	git -C subs checkout side &&
	! test -f subs/one &&
	rm -fr subs
'

test_expect_success 'checkout specific path while in subdirectory' '
	git reset --hard &&
	git checkout side &&
	mkdir subs &&
	>subs/bero &&
	git add subs/bero &&
	git commit -m "add subs/bero" &&

	git checkout main &&
	mkdir -p subs &&
	git -C subs checkout side -- bero &&
	test -f subs/bero
'

test_expect_success 'checkout w/--track sets up tracking' '
	git config branch.autosetupmerge false &&
	git checkout main &&
	git checkout --track -b track1 &&
	test "$(git config branch.track1.remote)" &&
	test "$(git config branch.track1.merge)"
'

test_expect_success 'checkout w/autosetupmerge=always sets up tracking' '
	test_when_finished git config branch.autosetupmerge false &&
	git config branch.autosetupmerge always &&
	git checkout main &&
	git checkout -b track2 &&
	test "$(git config branch.track2.remote)" &&
	test "$(git config branch.track2.merge)"
'

test_expect_success 'checkout w/--track from non-branch HEAD fails' '
	git checkout main^0 &&
	test_must_fail git symbolic-ref HEAD &&
	test_must_fail git checkout --track -b track &&
	test_must_fail git rev-parse --verify track &&
	test_must_fail git symbolic-ref HEAD &&
	test "z$(git rev-parse main^0)" = "z$(git rev-parse HEAD)"
'

test_expect_success 'checkout w/--track from tag fails' '
	git checkout main^0 &&
	test_must_fail git symbolic-ref HEAD &&
	test_must_fail git checkout --track -b track frotz &&
	test_must_fail git rev-parse --verify track &&
	test_must_fail git symbolic-ref HEAD &&
	test "z$(git rev-parse main^0)" = "z$(git rev-parse HEAD)"
'

test_expect_success 'detach a symbolic link HEAD' '
	git checkout main &&
	git config --bool core.prefersymlinkrefs yes &&
	git checkout side &&
	git checkout main &&
	it=$(git symbolic-ref HEAD) &&
	test "z$it" = zrefs/heads/main &&
	here=$(git rev-parse --verify refs/heads/main) &&
	git checkout side^ &&
	test "z$(git rev-parse --verify refs/heads/main)" = "z$here"
'

test_expect_success 'checkout with --track fakes a sensible -b <name>' '
	git config remote.origin.fetch "+refs/heads/*:refs/remotes/origin/*" &&
	git update-ref refs/remotes/origin/koala/bear renamer &&

	git checkout --track origin/koala/bear &&
	test "refs/heads/koala/bear" = "$(git symbolic-ref HEAD)" &&
	test "$(git rev-parse HEAD)" = "$(git rev-parse renamer)" &&

	git checkout main && git branch -D koala/bear &&

	git checkout --track refs/remotes/origin/koala/bear &&
	test "refs/heads/koala/bear" = "$(git symbolic-ref HEAD)" &&
	test "$(git rev-parse HEAD)" = "$(git rev-parse renamer)" &&

	git checkout main && git branch -D koala/bear &&

	git checkout --track remotes/origin/koala/bear &&
	test "refs/heads/koala/bear" = "$(git symbolic-ref HEAD)" &&
	test "$(git rev-parse HEAD)" = "$(git rev-parse renamer)"
'

test_expect_success 'checkout with --track, but without -b, fails with too short tracked name' '
	test_must_fail git checkout --track renamer
'

setup_conflicting_index () {
	rm -f .git/index &&
	O=$(echo original | git hash-object -w --stdin) &&
	A=$(echo ourside | git hash-object -w --stdin) &&
	B=$(echo theirside | git hash-object -w --stdin) &&
	(
		echo "100644 $A 0	fild" &&
		echo "100644 $O 1	file" &&
		echo "100644 $A 2	file" &&
		echo "100644 $B 3	file" &&
		echo "100644 $A 0	filf"
	) | git update-index --index-info
}

test_expect_success 'checkout an unmerged path should fail' '
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	test_must_fail git checkout fild file filf &&
	test_cmp sample fild &&
	test_cmp sample filf &&
	test_cmp sample file
'

test_expect_success 'checkout with an unmerged path can be ignored' '
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	echo ourside >expect &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	git checkout -f fild file filf &&
	test_cmp expect fild &&
	test_cmp expect filf &&
	test_cmp sample file
'

test_expect_success 'checkout unmerged stage' '
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	echo ourside >expect &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	git checkout --ours . &&
	test_cmp expect fild &&
	test_cmp expect filf &&
	test_cmp expect file &&
	git checkout --theirs file &&
	test ztheirside = "z$(cat file)"
'

test_expect_success 'checkout path with --merge from tree-ish is a no-no' '
	setup_conflicting_index &&
	test_must_fail git checkout -m HEAD -- file
'

test_expect_success 'checkout with --merge' '
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	echo ourside >expect &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	git checkout -m -- fild file filf &&
	(
		echo "<<<<<<< ours" &&
		echo ourside &&
		echo "=======" &&
		echo theirside &&
		echo ">>>>>>> theirs"
	) >merged &&
	test_cmp expect fild &&
	test_cmp expect filf &&
	test_cmp merged file
'

test_expect_success 'checkout -m works after (mistaken) resolution' '
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	# resolve to something
	git add file &&
	git checkout --merge -- fild file filf &&
	{
		echo "<<<<<<< ours" &&
		echo ourside &&
		echo "=======" &&
		echo theirside &&
		echo ">>>>>>> theirs"
	} >merged &&
	test_cmp expect fild &&
	test_cmp expect filf &&
	test_cmp merged file
'

test_expect_success 'checkout -m works after (mistaken) resolution to remove' '
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	# resolve to remove
	git rm file &&
	git checkout --merge -- fild file filf &&
	{
		echo "<<<<<<< ours" &&
		echo ourside &&
		echo "=======" &&
		echo theirside &&
		echo ">>>>>>> theirs"
	} >merged &&
	test_cmp expect fild &&
	test_cmp expect filf &&
	test_cmp merged file
'

test_expect_success 'checkout with --merge, in diff3 -m style' '
	git config merge.conflictstyle diff3 &&
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	echo ourside >expect &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	git checkout -m -- fild file filf &&
	(
		echo "<<<<<<< ours" &&
		echo ourside &&
		echo "||||||| base" &&
		echo original &&
		echo "=======" &&
		echo theirside &&
		echo ">>>>>>> theirs"
	) >merged &&
	test_cmp expect fild &&
	test_cmp expect filf &&
	test_cmp merged file
'

test_expect_success 'checkout --conflict=merge, overriding config' '
	git config merge.conflictstyle diff3 &&
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	echo ourside >expect &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	git checkout --conflict=merge -- fild file filf &&
	(
		echo "<<<<<<< ours" &&
		echo ourside &&
		echo "=======" &&
		echo theirside &&
		echo ">>>>>>> theirs"
	) >merged &&
	test_cmp expect fild &&
	test_cmp expect filf &&
	test_cmp merged file
'

test_expect_success 'checkout --conflict=diff3' '
	test_unconfig merge.conflictstyle &&
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	echo ourside >expect &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	git checkout --conflict=diff3 -- fild file filf &&
	(
		echo "<<<<<<< ours" &&
		echo ourside &&
		echo "||||||| base" &&
		echo original &&
		echo "=======" &&
		echo theirside &&
		echo ">>>>>>> theirs"
	) >merged &&
	test_cmp expect fild &&
	test_cmp expect filf &&
	test_cmp merged file
'

test_expect_success 'failing checkout -b should not break working tree' '
	git clean -fd &&  # Remove untracked files in the way
	git reset --hard main &&
	git symbolic-ref HEAD refs/heads/main &&
	test_must_fail git checkout -b renamer side^ &&
	test $(git symbolic-ref HEAD) = refs/heads/main &&
	git diff --exit-code &&
	git diff --cached --exit-code
'

test_expect_success 'switch out of non-branch' '
	git reset --hard main &&
	git checkout main^0 &&
	echo modified >one &&
	test_must_fail git checkout renamer 2>error.log &&
	! grep "^Previous HEAD" error.log
'

(
 echo "#!$SHELL_PATH"
 cat <<\EOF
O=$1 A=$2 B=$3
cat "$A" >.tmp
exec >"$A"
echo '<<<<<<< filfre-theirs'
cat "$B"
echo '||||||| filfre-common'
cat "$O"
echo '======='
cat ".tmp"
echo '>>>>>>> filfre-ours'
rm -f .tmp
exit 1
EOF
) >filfre.sh
chmod +x filfre.sh

test_expect_success 'custom merge driver with checkout -m' '
	git reset --hard &&

	git config merge.filfre.driver "./filfre.sh %O %A %B" &&
	git config merge.filfre.name "Feel-free merge driver" &&
	git config merge.filfre.recursive binary &&
	echo "arm merge=filfre" >.gitattributes &&

	git checkout -b left &&
	echo neutral >arm &&
	git add arm .gitattributes &&
	test_tick &&
	git commit -m neutral &&
	git branch right &&

	echo left >arm &&
	test_tick &&
	git commit -a -m left &&
	git checkout right &&

	echo right >arm &&
	test_tick &&
	git commit -a -m right &&

	test_must_fail git merge left &&
	(
		for t in filfre-common left right
		do
			grep $t arm || exit 1
		done
	) &&

	mv arm expect &&
	git checkout -m arm &&
	test_cmp expect arm
'

test_expect_success 'tracking info copied with autoSetupMerge=inherit' '
	git reset --hard main &&
	# default config does not copy tracking info
	git checkout -b foo-no-inherit koala/bear &&
	test_cmp_config "" --default "" branch.foo-no-inherit.remote &&
	test_cmp_config "" --default "" branch.foo-no-inherit.merge &&
	# with autoSetupMerge=inherit, we copy tracking info from koala/bear
	test_config branch.autoSetupMerge inherit &&
	git checkout -b foo koala/bear &&
	test_cmp_config origin branch.foo.remote &&
	test_cmp_config refs/heads/koala/bear branch.foo.merge &&
	# no tracking info to inherit from main
	git checkout -b main2 main &&
	test_cmp_config "" --default "" branch.main2.remote &&
	test_cmp_config "" --default "" branch.main2.merge
'

test_done
