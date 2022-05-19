#!/bin/sh
#
# Copyright (c) 2006 Junio C Hamano
#

test_description='but checkout tests.

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

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

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
	but add same one two &&
	but cummit -m "Initial A one, A two" &&

	but checkout -b renamer &&
	rm -f one &&
	fill 1 3 4 5 6 7 8 >uno &&
	but add uno &&
	fill a b c d e f >two &&
	but cummit -a -m "Renamer R one->uno, M two" &&

	but checkout -b side main &&
	fill 1 2 3 4 5 6 7 >one &&
	fill A B C D E >three &&
	rm -f two &&
	but update-index --add --remove one two three &&
	but cummit -m "Side M one, D two, A three" &&

	but checkout -b simple main &&
	rm -f one &&
	fill a c e >two &&
	but cummit -a -m "Simple D one, M two" &&

	but checkout main
'

test_expect_success 'checkout from non-existing branch' '
	but checkout -b delete-me main &&
	but update-ref -d --no-deref refs/heads/delete-me &&
	test refs/heads/delete-me = "$(but symbolic-ref HEAD)" &&
	but checkout main &&
	test refs/heads/main = "$(but symbolic-ref HEAD)"
'

test_expect_success 'checkout with dirty tree without -m' '
	fill 0 1 2 3 4 5 6 7 8 >one &&
	if but checkout side
	then
		echo Not happy
		false
	else
		echo "happy - failed correctly"
	fi
'

test_expect_success 'checkout with unrelated dirty tree without -m' '
	but checkout -f main &&
	fill 0 1 2 3 4 5 6 7 8 >same &&
	cp same kept &&
	but checkout side >messages &&
	test_cmp same kept &&
	printf "M\t%s\n" same >messages.expect &&
	test_cmp messages.expect messages
'

test_expect_success 'checkout -m with dirty tree' '
	but checkout -f main &&
	but clean -f &&

	fill 0 1 2 3 4 5 6 7 8 >one &&
	but checkout -m side >messages &&

	test "$(but symbolic-ref HEAD)" = "refs/heads/side" &&

	printf "M\t%s\n" one >expect.messages &&
	test_cmp expect.messages messages &&

	fill "M	one" "A	three" "D	two" >expect.main &&
	but diff --name-status main >current.main &&
	test_cmp expect.main current.main &&

	fill "M	one" >expect.side &&
	but diff --name-status side >current.side &&
	test_cmp expect.side current.side &&

	but diff --cached >current.index &&
	test_must_be_empty current.index
'

test_expect_success 'checkout -m with dirty tree, renamed' '
	but checkout -f main && but clean -f &&

	fill 1 2 3 4 5 7 8 >one &&
	if but checkout renamer
	then
		echo Not happy
		false
	else
		echo "happy - failed correctly"
	fi &&

	but checkout -m renamer &&
	fill 1 3 4 5 7 8 >expect &&
	test_cmp expect uno &&
	! test -f one &&
	but diff --cached >current &&
	test_must_be_empty current
'

test_expect_success 'checkout -m with merge conflict' '
	but checkout -f main && but clean -f &&

	fill 1 T 3 4 5 6 S 8 >one &&
	if but checkout renamer
	then
		echo Not happy
		false
	else
		echo "happy - failed correctly"
	fi &&

	but checkout -m renamer &&

	but diff main:one :3:uno |
	sed -e "1,/^@@/d" -e "/^ /d" -e "s/^-/d/" -e "s/^+/a/" >current &&
	fill d2 aT d7 aS >expect &&
	test_cmp expect current &&
	but diff --cached two >current &&
	test_must_be_empty current
'

test_expect_success 'format of merge conflict from checkout -m' '
	but checkout -f main &&
	but clean -f &&

	fill b d >two &&
	but checkout -m simple &&

	but ls-files >current &&
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
	but checkout -f main &&
	but reset --hard &&
	but clean -f &&

	fill b d >two &&
	but checkout --merge --conflict=diff3 simple &&

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
	but checkout -f main &&
	but reset --hard &&
	but clean -f &&
	but rm two &&

	test_must_fail but checkout simple 2>errs &&
	test_i18ngrep overwritten errs &&

	test_must_fail but read-tree --quiet -m -u HEAD simple 2>errs &&
	test_must_be_empty errs
'

test_expect_success 'checkout to detach HEAD (with advice declined)' '
	but config advice.detachedHead false &&
	rev=$(but rev-parse --short renamer^) &&
	but checkout -f renamer &&
	but clean -f &&
	but checkout renamer^ 2>messages &&
	test_i18ngrep "HEAD is now at $rev" messages &&
	test_line_count = 1 messages &&
	H=$(but rev-parse --verify HEAD) &&
	M=$(but show-ref -s --verify refs/heads/main) &&
	test "z$H" = "z$M" &&
	if but symbolic-ref HEAD >/dev/null 2>&1
	then
		echo "OOPS, HEAD is still symbolic???"
		false
	else
		: happy
	fi
'

test_expect_success 'checkout to detach HEAD' '
	but config advice.detachedHead true &&
	rev=$(but rev-parse --short renamer^) &&
	but checkout -f renamer &&
	but clean -f &&
	but checkout renamer^ 2>messages &&
	grep "HEAD is now at $rev" messages &&
	test_line_count -gt 1 messages &&
	H=$(but rev-parse --verify HEAD) &&
	M=$(but show-ref -s --verify refs/heads/main) &&
	test "z$H" = "z$M" &&
	if but symbolic-ref HEAD >/dev/null 2>&1
	then
		echo "OOPS, HEAD is still symbolic???"
		false
	else
		: happy
	fi
'

test_expect_success 'checkout to detach HEAD with branchname^' '
	but checkout -f main &&
	but clean -f &&
	but checkout renamer^ &&
	H=$(but rev-parse --verify HEAD) &&
	M=$(but show-ref -s --verify refs/heads/main) &&
	test "z$H" = "z$M" &&
	if but symbolic-ref HEAD >/dev/null 2>&1
	then
		echo "OOPS, HEAD is still symbolic???"
		false
	else
		: happy
	fi
'

test_expect_success 'checkout to detach HEAD with :/message' '
	but checkout -f main &&
	but clean -f &&
	but checkout ":/Initial" &&
	H=$(but rev-parse --verify HEAD) &&
	M=$(but show-ref -s --verify refs/heads/main) &&
	test "z$H" = "z$M" &&
	if but symbolic-ref HEAD >/dev/null 2>&1
	then
		echo "OOPS, HEAD is still symbolic???"
		false
	else
		: happy
	fi
'

test_expect_success 'checkout to detach HEAD with HEAD^0' '
	but checkout -f main &&
	but clean -f &&
	but checkout HEAD^0 &&
	H=$(but rev-parse --verify HEAD) &&
	M=$(but show-ref -s --verify refs/heads/main) &&
	test "z$H" = "z$M" &&
	if but symbolic-ref HEAD >/dev/null 2>&1
	then
		echo "OOPS, HEAD is still symbolic???"
		false
	else
		: happy
	fi
'

test_expect_success 'checkout with ambiguous tag/branch names' '
	but tag both side &&
	but branch both main &&
	but reset --hard &&
	but checkout main &&

	but checkout both &&
	H=$(but rev-parse --verify HEAD) &&
	M=$(but show-ref -s --verify refs/heads/main) &&
	test "z$H" = "z$M" &&
	name=$(but symbolic-ref HEAD 2>/dev/null) &&
	test "z$name" = zrefs/heads/both
'

test_expect_success 'checkout with ambiguous tag/branch names' '
	but reset --hard &&
	but checkout main &&

	but tag frotz side &&
	but branch frotz main &&
	but reset --hard &&
	but checkout main &&

	but checkout tags/frotz &&
	H=$(but rev-parse --verify HEAD) &&
	S=$(but show-ref -s --verify refs/heads/side) &&
	test "z$H" = "z$S" &&
	if name=$(but symbolic-ref HEAD 2>/dev/null)
	then
		echo "Bad -- should have detached"
		false
	else
		: happy
	fi
'

test_expect_success 'switch branches while in subdirectory' '
	but reset --hard &&
	but checkout main &&

	mkdir subs &&
	but -C subs checkout side &&
	! test -f subs/one &&
	rm -fr subs
'

test_expect_success 'checkout specific path while in subdirectory' '
	but reset --hard &&
	but checkout side &&
	mkdir subs &&
	>subs/bero &&
	but add subs/bero &&
	but cummit -m "add subs/bero" &&

	but checkout main &&
	mkdir -p subs &&
	but -C subs checkout side -- bero &&
	test -f subs/bero
'

test_expect_success 'checkout w/--track sets up tracking' '
    but config branch.autosetupmerge false &&
    but checkout main &&
    but checkout --track -b track1 &&
    test "$(but config branch.track1.remote)" &&
    test "$(but config branch.track1.merge)"
'

test_expect_success 'checkout w/autosetupmerge=always sets up tracking' '
    test_when_finished but config branch.autosetupmerge false &&
    but config branch.autosetupmerge always &&
    but checkout main &&
    but checkout -b track2 &&
    test "$(but config branch.track2.remote)" &&
    test "$(but config branch.track2.merge)"
'

test_expect_success 'checkout w/--track from non-branch HEAD fails' '
    but checkout main^0 &&
    test_must_fail but symbolic-ref HEAD &&
    test_must_fail but checkout --track -b track &&
    test_must_fail but rev-parse --verify track &&
    test_must_fail but symbolic-ref HEAD &&
    test "z$(but rev-parse main^0)" = "z$(but rev-parse HEAD)"
'

test_expect_success 'checkout w/--track from tag fails' '
    but checkout main^0 &&
    test_must_fail but symbolic-ref HEAD &&
    test_must_fail but checkout --track -b track frotz &&
    test_must_fail but rev-parse --verify track &&
    test_must_fail but symbolic-ref HEAD &&
    test "z$(but rev-parse main^0)" = "z$(but rev-parse HEAD)"
'

test_expect_success 'detach a symbolic link HEAD' '
    but checkout main &&
    but config --bool core.prefersymlinkrefs yes &&
    but checkout side &&
    but checkout main &&
    it=$(but symbolic-ref HEAD) &&
    test "z$it" = zrefs/heads/main &&
    here=$(but rev-parse --verify refs/heads/main) &&
    but checkout side^ &&
    test "z$(but rev-parse --verify refs/heads/main)" = "z$here"
'

test_expect_success 'checkout with --track fakes a sensible -b <name>' '
    but config remote.origin.fetch "+refs/heads/*:refs/remotes/origin/*" &&
    but update-ref refs/remotes/origin/koala/bear renamer &&

    but checkout --track origin/koala/bear &&
    test "refs/heads/koala/bear" = "$(but symbolic-ref HEAD)" &&
    test "$(but rev-parse HEAD)" = "$(but rev-parse renamer)" &&

    but checkout main && but branch -D koala/bear &&

    but checkout --track refs/remotes/origin/koala/bear &&
    test "refs/heads/koala/bear" = "$(but symbolic-ref HEAD)" &&
    test "$(but rev-parse HEAD)" = "$(but rev-parse renamer)" &&

    but checkout main && but branch -D koala/bear &&

    but checkout --track remotes/origin/koala/bear &&
    test "refs/heads/koala/bear" = "$(but symbolic-ref HEAD)" &&
    test "$(but rev-parse HEAD)" = "$(but rev-parse renamer)"
'

test_expect_success 'checkout with --track, but without -b, fails with too short tracked name' '
    test_must_fail but checkout --track renamer
'

setup_conflicting_index () {
	rm -f .but/index &&
	O=$(echo original | but hash-object -w --stdin) &&
	A=$(echo ourside | but hash-object -w --stdin) &&
	B=$(echo theirside | but hash-object -w --stdin) &&
	(
		echo "100644 $A 0	fild" &&
		echo "100644 $O 1	file" &&
		echo "100644 $A 2	file" &&
		echo "100644 $B 3	file" &&
		echo "100644 $A 0	filf"
	) | but update-index --index-info
}

test_expect_success 'checkout an unmerged path should fail' '
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	test_must_fail but checkout fild file filf &&
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
	but checkout -f fild file filf &&
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
	but checkout --ours . &&
	test_cmp expect fild &&
	test_cmp expect filf &&
	test_cmp expect file &&
	but checkout --theirs file &&
	test ztheirside = "z$(cat file)"
'

test_expect_success 'checkout with --merge' '
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	echo ourside >expect &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	but checkout -m -- fild file filf &&
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

test_expect_success 'checkout with --merge, in diff3 -m style' '
	but config merge.conflictstyle diff3 &&
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	echo ourside >expect &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	but checkout -m -- fild file filf &&
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
	but config merge.conflictstyle diff3 &&
	setup_conflicting_index &&
	echo "none of the above" >sample &&
	echo ourside >expect &&
	cat sample >fild &&
	cat sample >file &&
	cat sample >filf &&
	but checkout --conflict=merge -- fild file filf &&
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
	but checkout --conflict=diff3 -- fild file filf &&
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
	but clean -fd &&  # Remove untracked files in the way
	but reset --hard main &&
	but symbolic-ref HEAD refs/heads/main &&
	test_must_fail but checkout -b renamer side^ &&
	test $(but symbolic-ref HEAD) = refs/heads/main &&
	but diff --exit-code &&
	but diff --cached --exit-code
'

test_expect_success 'switch out of non-branch' '
	but reset --hard main &&
	but checkout main^0 &&
	echo modified >one &&
	test_must_fail but checkout renamer 2>error.log &&
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
	but reset --hard &&

	but config merge.filfre.driver "./filfre.sh %O %A %B" &&
	but config merge.filfre.name "Feel-free merge driver" &&
	but config merge.filfre.recursive binary &&
	echo "arm merge=filfre" >.butattributes &&

	but checkout -b left &&
	echo neutral >arm &&
	but add arm .butattributes &&
	test_tick &&
	but cummit -m neutral &&
	but branch right &&

	echo left >arm &&
	test_tick &&
	but cummit -a -m left &&
	but checkout right &&

	echo right >arm &&
	test_tick &&
	but cummit -a -m right &&

	test_must_fail but merge left &&
	(
		for t in filfre-common left right
		do
			grep $t arm || exit 1
		done
	) &&

	mv arm expect &&
	but checkout -m arm &&
	test_cmp expect arm
'

test_expect_success 'tracking info copied with autoSetupMerge=inherit' '
	but reset --hard main &&
	# default config does not copy tracking info
	but checkout -b foo-no-inherit koala/bear &&
	test_cmp_config "" --default "" branch.foo-no-inherit.remote &&
	test_cmp_config "" --default "" branch.foo-no-inherit.merge &&
	# with autoSetupMerge=inherit, we copy tracking info from koala/bear
	test_config branch.autoSetupMerge inherit &&
	but checkout -b foo koala/bear &&
	test_cmp_config origin branch.foo.remote &&
	test_cmp_config refs/heads/koala/bear branch.foo.merge &&
	# no tracking info to inherit from main
	but checkout -b main2 main &&
	test_cmp_config "" --default "" branch.main2.remote &&
	test_cmp_config "" --default "" branch.main2.merge
'

test_done
