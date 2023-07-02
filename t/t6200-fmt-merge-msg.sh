#!/bin/sh
#
# Copyright (c) 2006, Junio C Hamano
#

test_description='fmt-merge-msg test'

GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export GIT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"

test_expect_success setup '
	echo one >one &&
	git add one &&
	test_tick &&
	git commit -m "Initial" &&

	git clone . remote &&

	echo uno >one &&
	echo dos >two &&
	git add two &&
	test_tick &&
	git commit -a -m "Second" &&

	git checkout -b left &&

	echo "c1" >one &&
	test_tick &&
	git commit -a -m "Common #1" &&

	echo "c2" >one &&
	test_tick &&
	git commit -a -m "Common #2" &&

	git branch right &&

	echo "l3" >two &&
	test_tick &&
	GIT_COMMITTER_NAME="Another Committer" \
	GIT_AUTHOR_NAME="Another Author" git commit -a -m "Left #3" &&

	echo "l4" >two &&
	test_tick &&
	GIT_COMMITTER_NAME="Another Committer" \
	GIT_AUTHOR_NAME="Another Author" git commit -a -m "Left #4" &&

	echo "l5" >two &&
	test_tick &&
	GIT_COMMITTER_NAME="Another Committer" \
	GIT_AUTHOR_NAME="Another Author" git commit -a -m "Left #5" &&
	git tag tag-l5 &&

	git checkout right &&

	echo "r3" >three &&
	git add three &&
	test_tick &&
	git commit -a -m "Right #3" &&
	git tag tag-r3 &&

	echo "r4" >three &&
	test_tick &&
	git commit -a -m "Right #4" &&

	echo "r5" >three &&
	test_tick &&
	git commit -a -m "Right #5" &&

	git checkout -b long &&
	test_commit_bulk --start=0 --message=%s --filename=one 30 &&

	git show-branch &&

	apos="'\''"
'

test_expect_success GPG 'set up a signed tag' '
	git tag -s -m signed-tag-msg signed-good-tag left
'

test_expect_success GPGSSH 'created ssh signed commit and tag' '
	test_config gpg.format ssh &&
	git checkout -b signed-ssh &&
	touch file &&
	git add file &&
	git commit -m "ssh signed" -S"${GPGSSH_KEY_PRIMARY}" &&
	git tag -s -u"${GPGSSH_KEY_PRIMARY}" -m signed-ssh-tag-msg signed-good-ssh-tag left &&
	git tag -s -u"${GPGSSH_KEY_UNTRUSTED}" -m signed-ssh-tag-msg-untrusted signed-untrusted-ssh-tag left
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'create signed tags with keys having defined lifetimes' '
	test_when_finished "test_unconfig commit.gpgsign" &&
	test_config gpg.format ssh &&
	git checkout -b signed-expiry-ssh &&
	touch file &&
	git add file &&

	echo expired >file && test_tick && git commit -a -m expired -S"${GPGSSH_KEY_EXPIRED}" &&
	git tag -s -u "${GPGSSH_KEY_EXPIRED}" -m expired-signed expired-signed &&

	echo notyetvalid >file && test_tick && git commit -a -m notyetvalid -S"${GPGSSH_KEY_NOTYETVALID}" &&
	git tag -s -u "${GPGSSH_KEY_NOTYETVALID}" -m notyetvalid-signed notyetvalid-signed &&

	echo timeboxedvalid >file && test_tick && git commit -a -m timeboxedvalid -S"${GPGSSH_KEY_TIMEBOXEDVALID}" &&
	git tag -s -u "${GPGSSH_KEY_TIMEBOXEDVALID}" -m timeboxedvalid-signed timeboxedvalid-signed &&

	echo timeboxedinvalid >file && test_tick && git commit -a -m timeboxedinvalid -S"${GPGSSH_KEY_TIMEBOXEDINVALID}" &&
	git tag -s -u "${GPGSSH_KEY_TIMEBOXEDINVALID}" -m timeboxedinvalid-signed timeboxedinvalid-signed
'

test_expect_success 'message for merging local branch' '
	echo "Merge branch ${apos}left${apos}" >expected &&

	git checkout main &&
	git fetch . left &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success GPG 'message for merging local tag signed by good key' '
	git checkout main &&
	git fetch . signed-good-tag &&
	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	grep "^Merge tag ${apos}signed-good-tag${apos}" actual &&
	grep "^signed-tag-msg" actual &&
	grep "^# gpg: Signature made" actual &&
	grep "^# gpg: Good signature from" actual
'

test_expect_success GPG 'message for merging local tag signed by unknown key' '
	git checkout main &&
	git fetch . signed-good-tag &&
	GNUPGHOME=. git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	grep "^Merge tag ${apos}signed-good-tag${apos}" actual &&
	grep "^signed-tag-msg" actual &&
	grep "^# gpg: Signature made" actual &&
	grep -E "^# gpg: Can${apos}t check signature: (public key not found|No public key)" actual
'

test_expect_success GPGSSH 'message for merging local tag signed by good ssh key' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	git checkout main &&
	git fetch . signed-good-ssh-tag &&
	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	grep "^Merge tag ${apos}signed-good-ssh-tag${apos}" actual &&
	grep "^signed-ssh-tag-msg" actual &&
	grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
	! grep "${GPGSSH_BAD_SIGNATURE}" actual
'

test_expect_success GPGSSH 'message for merging local tag signed by unknown ssh key' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	git checkout main &&
	git fetch . signed-untrusted-ssh-tag &&
	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	grep "^Merge tag ${apos}signed-untrusted-ssh-tag${apos}" actual &&
	grep "^signed-ssh-tag-msg-untrusted" actual &&
	grep "${GPGSSH_GOOD_SIGNATURE_UNTRUSTED}" actual &&
	! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
	grep "${GPGSSH_KEY_NOT_TRUSTED}" actual
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'message for merging local tag signed by expired ssh key' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	git checkout main &&
	git fetch . expired-signed &&
	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	grep "^Merge tag ${apos}expired-signed${apos}" actual &&
	grep "^expired-signed" actual &&
	! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'message for merging local tag signed by not yet valid ssh key' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	git checkout main &&
	git fetch . notyetvalid-signed &&
	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	grep "^Merge tag ${apos}notyetvalid-signed${apos}" actual &&
	grep "^notyetvalid-signed" actual &&
	! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'message for merging local tag signed by valid timeboxed ssh key' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	git checkout main &&
	git fetch . timeboxedvalid-signed &&
	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	grep "^Merge tag ${apos}timeboxedvalid-signed${apos}" actual &&
	grep "^timeboxedvalid-signed" actual &&
	grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
	! grep "${GPGSSH_BAD_SIGNATURE}" actual
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'message for merging local tag signed by invalid timeboxed ssh key' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	git checkout main &&
	git fetch . timeboxedinvalid-signed &&
	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	grep "^Merge tag ${apos}timeboxedinvalid-signed${apos}" actual &&
	grep "^timeboxedinvalid-signed" actual &&
	! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual
'

test_expect_success 'message for merging external branch' '
	echo "Merge branch ${apos}left${apos} of $(pwd)" >expected &&

	git checkout main &&
	git fetch "$(pwd)" left &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '[merge] summary/log configuration' '
	cat >expected <<-EOF &&
	Merge branch ${apos}left${apos}

	# By Another Author (3) and A U Thor (2)
	# Via Another Committer
	* left:
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	test_config merge.log true &&
	test_unconfig merge.summary &&

	git checkout main &&
	test_tick &&
	git fetch . left &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual1 &&

	test_unconfig merge.log &&
	test_config merge.summary true &&

	git checkout main &&
	test_tick &&
	git fetch . left &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual2 &&

	test_cmp expected actual1 &&
	test_cmp expected actual2
'

test_expect_success 'setup FETCH_HEAD' '
	git checkout main &&
	test_tick &&
	git fetch . left
'

test_expect_success 'merge.log=3 limits shortlog length' '
	cat >expected <<-EOF &&
	Merge branch ${apos}left${apos}

	# By Another Author (3) and A U Thor (2)
	# Via Another Committer
	* left: (5 commits)
	  Left #5
	  Left #4
	  Left #3
	  ...
	EOF

	git -c merge.log=3 fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge.log=5 shows all 5 commits' '
	cat >expected <<-EOF &&
	Merge branch ${apos}left${apos}

	# By Another Author (3) and A U Thor (2)
	# Via Another Committer
	* left:
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	git -c merge.log=5 fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '--log=5 with custom comment character' '
	cat >expected <<-EOF &&
	Merge branch ${apos}left${apos}

	x By Another Author (3) and A U Thor (2)
	x Via Another Committer
	* left:
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	git -c core.commentchar="x" fmt-merge-msg --log=5 <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge.log=0 disables shortlog' '
	echo "Merge branch ${apos}left${apos}" >expected &&
	git -c merge.log=0 fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '--log=3 limits shortlog length' '
	cat >expected <<-EOF &&
	Merge branch ${apos}left${apos}

	# By Another Author (3) and A U Thor (2)
	# Via Another Committer
	* left: (5 commits)
	  Left #5
	  Left #4
	  Left #3
	  ...
	EOF

	git fmt-merge-msg --log=3 <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '--log=5 shows all 5 commits' '
	cat >expected <<-EOF &&
	Merge branch ${apos}left${apos}

	# By Another Author (3) and A U Thor (2)
	# Via Another Committer
	* left:
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	git fmt-merge-msg --log=5 <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '--no-log disables shortlog' '
	echo "Merge branch ${apos}left${apos}" >expected &&
	git fmt-merge-msg --no-log <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '--log=0 disables shortlog' '
	echo "Merge branch ${apos}left${apos}" >expected &&
	git fmt-merge-msg --no-log <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'fmt-merge-msg -m' '
	echo "Sync with left" >expected &&
	cat >expected.log <<-EOF &&
	Sync with left

	# By Another Author (3) and A U Thor (2)
	# Via Another Committer
	* ${apos}left${apos} of $(pwd):
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	test_unconfig merge.log &&
	test_unconfig merge.summary &&
	git checkout main &&
	git fetch "$(pwd)" left &&
	git fmt-merge-msg -m "Sync with left" <.git/FETCH_HEAD >actual &&
	git fmt-merge-msg --log -m "Sync with left" \
					<.git/FETCH_HEAD >actual.log &&
	test_config merge.log true &&
	git fmt-merge-msg -m "Sync with left" \
					<.git/FETCH_HEAD >actual.log-config &&
	git fmt-merge-msg --no-log -m "Sync with left" \
					<.git/FETCH_HEAD >actual.nolog &&

	test_cmp expected actual &&
	test_cmp expected.log actual.log &&
	test_cmp expected.log actual.log-config &&
	test_cmp expected actual.nolog
'

test_expect_success 'setup: expected shortlog for two branches' '
	cat >expected <<-EOF
	Merge branches ${apos}left${apos} and ${apos}right${apos}

	# By Another Author (3) and A U Thor (2)
	# Via Another Committer
	* left:
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1

	* right:
	  Right #5
	  Right #4
	  Right #3
	  Common #2
	  Common #1
	EOF
'

test_expect_success 'shortlog for two branches' '
	test_config merge.log true &&
	test_unconfig merge.summary &&
	git checkout main &&
	test_tick &&
	git fetch . left right &&
	git fmt-merge-msg <.git/FETCH_HEAD >actual1 &&

	test_unconfig merge.log &&
	test_config merge.summary true &&
	git checkout main &&
	test_tick &&
	git fetch . left right &&
	git fmt-merge-msg <.git/FETCH_HEAD >actual2 &&

	test_config merge.log yes &&
	test_unconfig merge.summary &&
	git checkout main &&
	test_tick &&
	git fetch . left right &&
	git fmt-merge-msg <.git/FETCH_HEAD >actual3 &&

	test_unconfig merge.log &&
	test_config merge.summary yes &&
	git checkout main &&
	test_tick &&
	git fetch . left right &&
	git fmt-merge-msg <.git/FETCH_HEAD >actual4 &&

	test_cmp expected actual1 &&
	test_cmp expected actual2 &&
	test_cmp expected actual3 &&
	test_cmp expected actual4
'

test_expect_success 'merge-msg -F' '
	test_unconfig merge.log &&
	test_config merge.summary yes &&
	git checkout main &&
	test_tick &&
	git fetch . left right &&
	git fmt-merge-msg -F .git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-msg -F in subdirectory' '
	test_unconfig merge.log &&
	test_config merge.summary yes &&
	git checkout main &&
	test_tick &&
	git fetch . left right &&
	mkdir sub &&
	cp .git/FETCH_HEAD sub/FETCH_HEAD &&
	(
		cd sub &&
		git fmt-merge-msg -F FETCH_HEAD >../actual
	) &&
	test_cmp expected actual
'

test_expect_success 'merge-msg with nothing to merge' '
	test_unconfig merge.log &&
	test_config merge.summary yes &&

	(
		cd remote &&
		git checkout -b unrelated &&
		test_tick &&
		git fetch origin &&
		git fmt-merge-msg <.git/FETCH_HEAD >../actual
	) &&

	test_must_be_empty actual
'

test_expect_success 'merge-msg tag' '
	cat >expected <<-EOF &&
	Merge tag ${apos}tag-r3${apos}

	* tag ${apos}tag-r3${apos}:
	  Right #3
	  Common #2
	  Common #1
	EOF

	test_unconfig merge.log &&
	test_config merge.summary yes &&

	git checkout main &&
	test_tick &&
	git fetch . tag tag-r3 &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-msg two tags' '
	cat >expected <<-EOF &&
	Merge tags ${apos}tag-r3${apos} and ${apos}tag-l5${apos}

	* tag ${apos}tag-r3${apos}:
	  Right #3
	  Common #2
	  Common #1

	# By Another Author (3) and A U Thor (2)
	# Via Another Committer
	* tag ${apos}tag-l5${apos}:
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	test_unconfig merge.log &&
	test_config merge.summary yes &&

	git checkout main &&
	test_tick &&
	git fetch . tag tag-r3 tag tag-l5 &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-msg tag and branch' '
	cat >expected <<-EOF &&
	Merge branch ${apos}left${apos}, tag ${apos}tag-r3${apos}

	* tag ${apos}tag-r3${apos}:
	  Right #3
	  Common #2
	  Common #1

	# By Another Author (3) and A U Thor (2)
	# Via Another Committer
	* left:
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	test_unconfig merge.log &&
	test_config merge.summary yes &&

	git checkout main &&
	test_tick &&
	git fetch . tag tag-r3 left &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-msg lots of commits' '
	{
		cat <<-EOF &&
		Merge branch ${apos}long${apos}

		* long: (35 commits)
		EOF

		i=29 &&
		while test $i -gt 9
		do
			echo "  $i" &&
			i=$(($i-1)) || return 1
		done &&
		echo "  ..."
	} >expected &&

	test_config merge.summary yes &&

	git checkout main &&
	test_tick &&
	git fetch . long &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-msg with "merging" an annotated tag' '
	test_config merge.log true &&

	git checkout main^0 &&
	git commit --allow-empty -m "One step ahead" &&
	git tag -a -m "An annotated one" annote HEAD &&

	git checkout main &&
	git fetch . annote &&

	git fmt-merge-msg <.git/FETCH_HEAD >actual &&
	{
		cat <<-\EOF
		Merge tag '\''annote'\''

		An annotated one

		* tag '\''annote'\'':
		  One step ahead
		EOF
	} >expected &&
	test_cmp expected actual &&

	test_when_finished "git reset --hard" &&
	annote=$(git rev-parse annote) &&
	git merge --no-commit --no-ff $annote &&
	{
		cat <<-EOF
		Merge tag '\''$annote'\''

		An annotated one

		* tag '\''$annote'\'':
		  One step ahead
		EOF
	} >expected &&
	test_cmp expected .git/MERGE_MSG
'

test_expect_success 'merge --into-name=<name>' '
	test_when_finished "git checkout main" &&
	git checkout -B side main &&
	git commit --allow-empty -m "One step ahead" &&

	git checkout --detach main &&
	git merge --no-ff side &&
	git show -s --format="%s" >full.0 &&
	head -n1 full.0 >actual &&
	# expect that HEAD is shown as-is
	grep -e "Merge branch .side. into HEAD$" actual &&

	git reset --hard main &&
	git merge --no-ff --into-name=main side &&
	git show -s --format="%s" >full.1 &&
	head -n1 full.1 >actual &&
	# expect that we pretend to be merging to main, that is suppressed
	grep -e "Merge branch .side.$" actual &&

	git checkout -b throwaway main &&
	git merge --no-ff --into-name=main side &&
	git show -s --format="%s" >full.2 &&
	head -n1 full.2 >actual &&
	# expect that we pretend to be merging to main, that is suppressed
	grep -e "Merge branch .side.$" actual
'

test_expect_success 'merge.suppressDest configuration' '
	test_when_finished "git checkout main" &&
	git checkout -B side main &&
	git commit --allow-empty -m "One step ahead" &&
	git checkout main &&
	git fetch . side &&

	git -c merge.suppressDest="" fmt-merge-msg <.git/FETCH_HEAD >full.1 &&
	head -n1 full.1 >actual &&
	grep -e "Merge branch .side. into main" actual &&

	git -c merge.suppressDest="mast" fmt-merge-msg <.git/FETCH_HEAD >full.2 &&
	head -n1 full.2 >actual &&
	grep -e "Merge branch .side. into main$" actual &&

	git -c merge.suppressDest="ma?*[rn]" fmt-merge-msg <.git/FETCH_HEAD >full.3 &&
	head -n1 full.3 >actual &&
	grep -e "Merge branch .side." actual &&
	! grep -e " into main$" actual &&

	git checkout --detach HEAD &&
	git -c merge.suppressDest="main" fmt-merge-msg <.git/FETCH_HEAD >full.4 &&
	head -n1 full.4 >actual &&
	grep -e "Merge branch .side. into HEAD$" actual &&

	git -c merge.suppressDest="main" fmt-merge-msg \
		--into-name=main <.git/FETCH_HEAD >full.5 &&
	head -n1 full.5 >actual &&
	grep -e "Merge branch .side." actual &&
	! grep -e " into main$" actual &&
	! grep -e " into HEAD$" actual
'

test_done
