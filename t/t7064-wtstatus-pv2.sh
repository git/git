#!/bin/sh

test_description='git status --porcelain=v2

This test exercises porcelain V2 output for git status.'

. ./test-lib.sh


test_expect_success setup '
	git checkout -f --orphan initial-branch &&
	test_tick &&
	git config core.autocrlf false &&
	echo x >file_x &&
	echo y >file_y &&
	echo z >file_z &&
	mkdir dir1 &&
	echo a >dir1/file_a &&
	echo b >dir1/file_b
'

test_expect_success 'before initial commit, nothing added, only untracked' '
	cat >expect <<-EOF &&
	# branch.oid (initial)
	# branch.head initial-branch
	? actual
	? dir1/
	? expect
	? file_x
	? file_y
	? file_z
	EOF

	git status --porcelain=v2 --branch --untracked-files=normal >actual &&
	test_cmp expect actual
'

test_expect_success 'before initial commit, things added' '
	git add file_x file_y file_z dir1 &&
	OID_A=$(git hash-object -t blob -- dir1/file_a) &&
	OID_B=$(git hash-object -t blob -- dir1/file_b) &&
	OID_X=$(git hash-object -t blob -- file_x) &&
	OID_Y=$(git hash-object -t blob -- file_y) &&
	OID_Z=$(git hash-object -t blob -- file_z) &&

	cat >expect <<-EOF &&
	# branch.oid (initial)
	# branch.head initial-branch
	1 A. N... 000000 100644 100644 $ZERO_OID $OID_A dir1/file_a
	1 A. N... 000000 100644 100644 $ZERO_OID $OID_B dir1/file_b
	1 A. N... 000000 100644 100644 $ZERO_OID $OID_X file_x
	1 A. N... 000000 100644 100644 $ZERO_OID $OID_Y file_y
	1 A. N... 000000 100644 100644 $ZERO_OID $OID_Z file_z
	? actual
	? expect
	EOF

	git status --porcelain=v2 --branch --untracked-files=all >actual &&
	test_cmp expect actual
'

test_expect_success 'before initial commit, things added (-z)' '
	lf_to_nul >expect <<-EOF &&
	# branch.oid (initial)
	# branch.head initial-branch
	1 A. N... 000000 100644 100644 $ZERO_OID $OID_A dir1/file_a
	1 A. N... 000000 100644 100644 $ZERO_OID $OID_B dir1/file_b
	1 A. N... 000000 100644 100644 $ZERO_OID $OID_X file_x
	1 A. N... 000000 100644 100644 $ZERO_OID $OID_Y file_y
	1 A. N... 000000 100644 100644 $ZERO_OID $OID_Z file_z
	? actual
	? expect
	EOF

	git status -z --porcelain=v2 --branch --untracked-files=all >actual &&
	test_cmp expect actual
'

test_expect_success 'make first commit, comfirm HEAD oid and branch' '
	git commit -m initial &&
	H0=$(git rev-parse HEAD) &&
	cat >expect <<-EOF &&
	# branch.oid $H0
	# branch.head initial-branch
	? actual
	? expect
	EOF

	git status --porcelain=v2 --branch --untracked-files=all >actual &&
	test_cmp expect actual
'

test_expect_success 'after first commit, create unstaged changes' '
	echo x >>file_x &&
	OID_X1=$(git hash-object -t blob -- file_x) &&
	rm file_z &&
	H0=$(git rev-parse HEAD) &&

	cat >expect <<-EOF &&
	# branch.oid $H0
	# branch.head initial-branch
	1 .M N... 100644 100644 100644 $OID_X $OID_X file_x
	1 .D N... 100644 100644 000000 $OID_Z $OID_Z file_z
	? actual
	? expect
	EOF

	git status --porcelain=v2 --branch --untracked-files=all >actual &&
	test_cmp expect actual
'

test_expect_success 'after first commit, stash existing changes' '
	cat >expect <<-EOF &&
	# branch.oid $H0
	# branch.head initial-branch
	# stash 2
	EOF

	test_when_finished "git stash pop && git stash pop" &&

	git stash -- file_x &&
	git stash &&
	git status --porcelain=v2 --branch --show-stash --untracked-files=no >actual &&
	test_cmp expect actual
'

test_expect_success 'after first commit but omit untracked files and branch' '
	cat >expect <<-EOF &&
	1 .M N... 100644 100644 100644 $OID_X $OID_X file_x
	1 .D N... 100644 100644 000000 $OID_Z $OID_Z file_z
	EOF

	git status --porcelain=v2 --untracked-files=no >actual &&
	test_cmp expect actual
'

test_expect_success 'after first commit, stage existing changes' '
	git add file_x &&
	git rm file_z &&
	H0=$(git rev-parse HEAD) &&

	cat >expect <<-EOF &&
	# branch.oid $H0
	# branch.head initial-branch
	1 M. N... 100644 100644 100644 $OID_X $OID_X1 file_x
	1 D. N... 100644 000000 000000 $OID_Z $ZERO_OID file_z
	? actual
	? expect
	EOF

	git status --porcelain=v2 --branch --untracked-files=all >actual &&
	test_cmp expect actual
'

test_expect_success 'rename causes 2 path lines' '
	git mv file_y renamed_y &&
	H0=$(git rev-parse HEAD) &&

	q_to_tab >expect <<-EOF &&
	# branch.oid $H0
	# branch.head initial-branch
	1 M. N... 100644 100644 100644 $OID_X $OID_X1 file_x
	1 D. N... 100644 000000 000000 $OID_Z $ZERO_OID file_z
	2 R. N... 100644 100644 100644 $OID_Y $OID_Y R100 renamed_yQfile_y
	? actual
	? expect
	EOF

	git status --porcelain=v2 --branch --untracked-files=all >actual &&
	test_cmp expect actual
'

test_expect_success 'rename causes 2 path lines (-z)' '
	H0=$(git rev-parse HEAD) &&

	## Lines use NUL path separator and line terminator, so double transform here.
	q_to_nul <<-EOF | lf_to_nul >expect &&
	# branch.oid $H0
	# branch.head initial-branch
	1 M. N... 100644 100644 100644 $OID_X $OID_X1 file_x
	1 D. N... 100644 000000 000000 $OID_Z $ZERO_OID file_z
	2 R. N... 100644 100644 100644 $OID_Y $OID_Y R100 renamed_yQfile_y
	? actual
	? expect
	EOF

	git status --porcelain=v2 --branch --untracked-files=all -z >actual &&
	test_cmp expect actual
'

test_expect_success 'make second commit, confirm clean and new HEAD oid' '
	git commit -m second &&
	H1=$(git rev-parse HEAD) &&

	cat >expect <<-EOF &&
	# branch.oid $H1
	# branch.head initial-branch
	? actual
	? expect
	EOF

	git status --porcelain=v2 --branch --untracked-files=all >actual &&
	test_cmp expect actual
'

test_expect_success 'confirm ignored files are not printed' '
	test_when_finished "rm -f x.ign .gitignore" &&
	echo x.ign >.gitignore &&
	echo "ignore me" >x.ign &&

	cat >expect <<-EOF &&
	? .gitignore
	? actual
	? expect
	EOF

	git status --porcelain=v2 --untracked-files=all >actual &&
	test_cmp expect actual
'

test_expect_success 'ignored files are printed with --ignored' '
	test_when_finished "rm -f x.ign .gitignore" &&
	echo x.ign >.gitignore &&
	echo "ignore me" >x.ign &&

	cat >expect <<-EOF &&
	? .gitignore
	? actual
	? expect
	! x.ign
	EOF

	git status --porcelain=v2 --ignored --untracked-files=all >actual &&
	test_cmp expect actual
'

test_expect_success 'create and commit permanent ignore file' '
	cat >.gitignore <<-EOF &&
	actual*
	expect*
	EOF

	git add .gitignore &&
	git commit -m ignore_trash &&
	H1=$(git rev-parse HEAD) &&

	cat >expect <<-EOF &&
	# branch.oid $H1
	# branch.head initial-branch
	EOF

	git status --porcelain=v2 --branch >actual &&
	test_cmp expect actual
'

test_expect_success 'verify --intent-to-add output' '
	test_when_finished "git rm -f intent1.add intent2.add" &&
	touch intent1.add &&
	echo test >intent2.add &&

	git add --intent-to-add intent1.add intent2.add &&

	cat >expect <<-EOF &&
	1 .A N... 000000 000000 100644 $ZERO_OID $ZERO_OID intent1.add
	1 .A N... 000000 000000 100644 $ZERO_OID $ZERO_OID intent2.add
	EOF

	git status --porcelain=v2 >actual &&
	test_cmp expect actual
'

test_expect_success 'verify AA (add-add) conflict' '
	test_when_finished "git reset --hard" &&

	git branch AA_A initial-branch &&
	git checkout AA_A &&
	echo "Branch AA_A" >conflict.txt &&
	OID_AA_A=$(git hash-object -t blob -- conflict.txt) &&
	git add conflict.txt &&
	git commit -m "branch aa_a" &&

	git branch AA_B initial-branch &&
	git checkout AA_B &&
	echo "Branch AA_B" >conflict.txt &&
	OID_AA_B=$(git hash-object -t blob -- conflict.txt) &&
	git add conflict.txt &&
	git commit -m "branch aa_b" &&

	git branch AA_M AA_B &&
	git checkout AA_M &&
	test_must_fail git merge AA_A &&

	HM=$(git rev-parse HEAD) &&

	cat >expect <<-EOF &&
	# branch.oid $HM
	# branch.head AA_M
	u AA N... 000000 100644 100644 100644 $ZERO_OID $OID_AA_B $OID_AA_A conflict.txt
	EOF

	git status --porcelain=v2 --branch --untracked-files=all >actual &&
	test_cmp expect actual
'

test_expect_success 'verify UU (edit-edit) conflict' '
	test_when_finished "git reset --hard" &&

	git branch UU_ANC initial-branch &&
	git checkout UU_ANC &&
	echo "Ancestor" >conflict.txt &&
	OID_UU_ANC=$(git hash-object -t blob -- conflict.txt) &&
	git add conflict.txt &&
	git commit -m "UU_ANC" &&

	git branch UU_A UU_ANC &&
	git checkout UU_A &&
	echo "Branch UU_A" >conflict.txt &&
	OID_UU_A=$(git hash-object -t blob -- conflict.txt) &&
	git add conflict.txt &&
	git commit -m "branch uu_a" &&

	git branch UU_B UU_ANC &&
	git checkout UU_B &&
	echo "Branch UU_B" >conflict.txt &&
	OID_UU_B=$(git hash-object -t blob -- conflict.txt) &&
	git add conflict.txt &&
	git commit -m "branch uu_b" &&

	git branch UU_M UU_B &&
	git checkout UU_M &&
	test_must_fail git merge UU_A &&

	HM=$(git rev-parse HEAD) &&

	cat >expect <<-EOF &&
	# branch.oid $HM
	# branch.head UU_M
	u UU N... 100644 100644 100644 100644 $OID_UU_ANC $OID_UU_B $OID_UU_A conflict.txt
	EOF

	git status --porcelain=v2 --branch --untracked-files=all >actual &&
	test_cmp expect actual
'

test_expect_success 'verify upstream fields in branch header' '
	git checkout initial-branch &&
	test_when_finished "rm -rf sub_repo" &&
	git clone . sub_repo &&
	(
		## Confirm local initial-branch tracks remote initial-branch.
		cd sub_repo &&
		HUF=$(git rev-parse HEAD) &&

		cat >expect <<-EOF &&
		# branch.oid $HUF
		# branch.head initial-branch
		# branch.upstream origin/initial-branch
		# branch.ab +0 -0
		EOF

		git status --porcelain=v2 --branch --untracked-files=all >actual &&
		test_cmp expect actual &&

		## Test ahead/behind.
		echo xyz >file_xyz &&
		git add file_xyz &&
		git commit -m xyz &&

		HUF=$(git rev-parse HEAD) &&

		cat >expect <<-EOF &&
		# branch.oid $HUF
		# branch.head initial-branch
		# branch.upstream origin/initial-branch
		# branch.ab +1 -0
		EOF

		git status --porcelain=v2 --branch --untracked-files=all >actual &&
		test_cmp expect actual &&

		## Repeat the above but without --branch.
		git status --porcelain=v2 --untracked-files=all >actual &&
		test_must_be_empty actual &&

		## Test upstream-gone case. Fake this by pointing
		## origin/initial-branch at a non-existing commit.
		git update-ref -d refs/remotes/origin/initial-branch &&

		HUF=$(git rev-parse HEAD) &&

		cat >expect <<-EOF &&
		# branch.oid $HUF
		# branch.head initial-branch
		# branch.upstream origin/initial-branch
		EOF

		git status --porcelain=v2 --branch --untracked-files=all >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'verify --[no-]ahead-behind with V2 format' '
	git checkout initial-branch &&
	test_when_finished "rm -rf sub_repo" &&
	git clone . sub_repo &&
	(
		## Confirm local initial-branch tracks remote initial-branch.
		cd sub_repo &&
		HUF=$(git rev-parse HEAD) &&

		# Confirm --no-ahead-behind reports traditional branch.ab with 0/0 for equal branches.
		cat >expect <<-EOF &&
		# branch.oid $HUF
		# branch.head initial-branch
		# branch.upstream origin/initial-branch
		# branch.ab +0 -0
		EOF

		git status --no-ahead-behind --porcelain=v2 --branch --untracked-files=all >actual &&
		test_cmp expect actual &&

		# Confirm --ahead-behind reports traditional branch.ab with 0/0.
		cat >expect <<-EOF &&
		# branch.oid $HUF
		# branch.head initial-branch
		# branch.upstream origin/initial-branch
		# branch.ab +0 -0
		EOF

		git status --ahead-behind --porcelain=v2 --branch --untracked-files=all >actual &&
		test_cmp expect actual &&

		## Test non-equal ahead/behind.
		echo xyz >file_xyz &&
		git add file_xyz &&
		git commit -m xyz &&

		HUF=$(git rev-parse HEAD) &&

		# Confirm --no-ahead-behind reports branch.ab with ?/? for non-equal branches.
		cat >expect <<-EOF &&
		# branch.oid $HUF
		# branch.head initial-branch
		# branch.upstream origin/initial-branch
		# branch.ab +? -?
		EOF

		git status --no-ahead-behind --porcelain=v2 --branch --untracked-files=all >actual &&
		test_cmp expect actual &&

		# Confirm --ahead-behind reports traditional branch.ab with 1/0.
		cat >expect <<-EOF &&
		# branch.oid $HUF
		# branch.head initial-branch
		# branch.upstream origin/initial-branch
		# branch.ab +1 -0
		EOF

		git status --ahead-behind --porcelain=v2 --branch --untracked-files=all >actual &&
		test_cmp expect actual &&

		# Confirm that "status.aheadbehind" DOES NOT work on V2 format.
		git -c status.aheadbehind=false status --porcelain=v2 --branch --untracked-files=all >actual &&
		test_cmp expect actual &&

		# Confirm that "status.aheadbehind" DOES NOT work on V2 format.
		git -c status.aheadbehind=true status --porcelain=v2 --branch --untracked-files=all >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'create and add submodule, submodule appears clean (A. S...)' '
	git checkout initial-branch &&
	git clone . sub_repo &&
	git clone . super_repo &&
	test_config_global protocol.file.allow always &&
	(	cd super_repo &&
		git submodule add ../sub_repo sub1 &&

		## Confirm stage/add of clean submodule.
		HMOD=$(git hash-object -t blob -- .gitmodules) &&
		HSUP=$(git rev-parse HEAD) &&
		HSUB=$HSUP &&

		cat >expect <<-EOF &&
		# branch.oid $HSUP
		# branch.head initial-branch
		# branch.upstream origin/initial-branch
		# branch.ab +0 -0
		1 A. N... 000000 100644 100644 $ZERO_OID $HMOD .gitmodules
		1 A. S... 000000 160000 160000 $ZERO_OID $HSUB sub1
		EOF

		git status --porcelain=v2 --branch --untracked-files=all >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'untracked changes in added submodule (AM S..U)' '
	(	cd super_repo &&
		## create untracked file in the submodule.
		(	cd sub1 &&
			echo "xxxx" >file_in_sub
		) &&

		HMOD=$(git hash-object -t blob -- .gitmodules) &&
		HSUP=$(git rev-parse HEAD) &&
		HSUB=$HSUP &&

		cat >expect <<-EOF &&
		# branch.oid $HSUP
		# branch.head initial-branch
		# branch.upstream origin/initial-branch
		# branch.ab +0 -0
		1 A. N... 000000 100644 100644 $ZERO_OID $HMOD .gitmodules
		1 AM S..U 000000 160000 160000 $ZERO_OID $HSUB sub1
		EOF

		git status --porcelain=v2 --branch --untracked-files=all >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'staged changes in added submodule (AM S.M.)' '
	(	cd super_repo &&
		## stage the changes in the submodule.
		(	cd sub1 &&
			git add file_in_sub
		) &&

		HMOD=$(git hash-object -t blob -- .gitmodules) &&
		HSUP=$(git rev-parse HEAD) &&
		HSUB=$HSUP &&

		cat >expect <<-EOF &&
		# branch.oid $HSUP
		# branch.head initial-branch
		# branch.upstream origin/initial-branch
		# branch.ab +0 -0
		1 A. N... 000000 100644 100644 $ZERO_OID $HMOD .gitmodules
		1 AM S.M. 000000 160000 160000 $ZERO_OID $HSUB sub1
		EOF

		git status --porcelain=v2 --branch --untracked-files=all >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'staged and unstaged changes in added (AM S.M.)' '
	(	cd super_repo &&
		(	cd sub1 &&
			## make additional unstaged changes (on the same file) in the submodule.
			## This does not cause us to get S.MU (because the submodule does not report
			## a "?" line for the unstaged changes).
			echo "more changes" >>file_in_sub
		) &&

		HMOD=$(git hash-object -t blob -- .gitmodules) &&
		HSUP=$(git rev-parse HEAD) &&
		HSUB=$HSUP &&

		cat >expect <<-EOF &&
		# branch.oid $HSUP
		# branch.head initial-branch
		# branch.upstream origin/initial-branch
		# branch.ab +0 -0
		1 A. N... 000000 100644 100644 $ZERO_OID $HMOD .gitmodules
		1 AM S.M. 000000 160000 160000 $ZERO_OID $HSUB sub1
		EOF

		git status --porcelain=v2 --branch --untracked-files=all >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'staged and untracked changes in added submodule (AM S.MU)' '
	(	cd super_repo &&
		(	cd sub1 &&
			## stage new changes in tracked file.
			git add file_in_sub &&
			## create new untracked file.
			echo "yyyy" >>another_file_in_sub
		) &&

		HMOD=$(git hash-object -t blob -- .gitmodules) &&
		HSUP=$(git rev-parse HEAD) &&
		HSUB=$HSUP &&

		cat >expect <<-EOF &&
		# branch.oid $HSUP
		# branch.head initial-branch
		# branch.upstream origin/initial-branch
		# branch.ab +0 -0
		1 A. N... 000000 100644 100644 $ZERO_OID $HMOD .gitmodules
		1 AM S.MU 000000 160000 160000 $ZERO_OID $HSUB sub1
		EOF

		git status --porcelain=v2 --branch --untracked-files=all >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'commit within the submodule appears as new commit in super (AM SC..)' '
	(	cd super_repo &&
		(	cd sub1 &&
			## Make a new commit in the submodule.
			git add file_in_sub &&
			rm -f another_file_in_sub &&
			git commit -m "new commit"
		) &&

		HMOD=$(git hash-object -t blob -- .gitmodules) &&
		HSUP=$(git rev-parse HEAD) &&
		HSUB=$HSUP &&

		cat >expect <<-EOF &&
		# branch.oid $HSUP
		# branch.head initial-branch
		# branch.upstream origin/initial-branch
		# branch.ab +0 -0
		1 A. N... 000000 100644 100644 $ZERO_OID $HMOD .gitmodules
		1 AM SC.. 000000 160000 160000 $ZERO_OID $HSUB sub1
		EOF

		git status --porcelain=v2 --branch --untracked-files=all >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'stage submodule in super and commit' '
	(	cd super_repo &&
		## Stage the new submodule commit in the super.
		git add sub1 &&
		## Commit the super so that the sub no longer appears as added.
		git commit -m "super commit" &&

		HSUP=$(git rev-parse HEAD) &&

		cat >expect <<-EOF &&
		# branch.oid $HSUP
		# branch.head initial-branch
		# branch.upstream origin/initial-branch
		# branch.ab +1 -0
		EOF

		git status --porcelain=v2 --branch --untracked-files=all >actual &&
		test_cmp expect actual
	)
'

test_expect_success 'make unstaged changes in existing submodule (.M S.M.)' '
	(	cd super_repo &&
		(	cd sub1 &&
			echo "zzzz" >>file_in_sub
		) &&

		HSUP=$(git rev-parse HEAD) &&
		HSUB=$(cd sub1 && git rev-parse HEAD) &&

		cat >expect <<-EOF &&
		# branch.oid $HSUP
		# branch.head initial-branch
		# branch.upstream origin/initial-branch
		# branch.ab +1 -0
		1 .M S.M. 160000 160000 160000 $HSUB $HSUB sub1
		EOF

		git status --porcelain=v2 --branch --untracked-files=all >actual &&
		test_cmp expect actual
	)
'

test_done
