#!/bin/sh
#
# Copyright (c) 2006, Junio C Hamano
#

test_description='fmt-merge-msg test'

BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME=main
export BUT_TEST_DEFAULT_INITIAL_BRANCH_NAME

. ./test-lib.sh
. "$TEST_DIRECTORY/lib-gpg.sh"

test_expect_success setup '
	echo one >one &&
	but add one &&
	test_tick &&
	but cummit -m "Initial" &&

	but clone . remote &&

	echo uno >one &&
	echo dos >two &&
	but add two &&
	test_tick &&
	but cummit -a -m "Second" &&

	but checkout -b left &&

	echo "c1" >one &&
	test_tick &&
	but cummit -a -m "Common #1" &&

	echo "c2" >one &&
	test_tick &&
	but cummit -a -m "Common #2" &&

	but branch right &&

	echo "l3" >two &&
	test_tick &&
	BUT_CUMMITTER_NAME="Another cummitter" \
	BUT_AUTHOR_NAME="Another Author" but cummit -a -m "Left #3" &&

	echo "l4" >two &&
	test_tick &&
	BUT_CUMMITTER_NAME="Another cummitter" \
	BUT_AUTHOR_NAME="Another Author" but cummit -a -m "Left #4" &&

	echo "l5" >two &&
	test_tick &&
	BUT_CUMMITTER_NAME="Another cummitter" \
	BUT_AUTHOR_NAME="Another Author" but cummit -a -m "Left #5" &&
	but tag tag-l5 &&

	but checkout right &&

	echo "r3" >three &&
	but add three &&
	test_tick &&
	but cummit -a -m "Right #3" &&
	but tag tag-r3 &&

	echo "r4" >three &&
	test_tick &&
	but cummit -a -m "Right #4" &&

	echo "r5" >three &&
	test_tick &&
	but cummit -a -m "Right #5" &&

	but checkout -b long &&
	test_cummit_bulk --start=0 --message=%s --filename=one 30 &&

	but show-branch &&

	apos="'\''"
'

test_expect_success GPG 'set up a signed tag' '
	but tag -s -m signed-tag-msg signed-good-tag left
'

test_expect_success GPGSSH 'created ssh signed cummit and tag' '
	test_config gpg.format ssh &&
	but checkout -b signed-ssh &&
	touch file &&
	but add file &&
	but cummit -m "ssh signed" -S"${GPGSSH_KEY_PRIMARY}" &&
	but tag -s -u"${GPGSSH_KEY_PRIMARY}" -m signed-ssh-tag-msg signed-good-ssh-tag left &&
	but tag -s -u"${GPGSSH_KEY_UNTRUSTED}" -m signed-ssh-tag-msg-untrusted signed-untrusted-ssh-tag left
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'create signed tags with keys having defined lifetimes' '
	test_when_finished "test_unconfig cummit.gpgsign" &&
	test_config gpg.format ssh &&
	but checkout -b signed-expiry-ssh &&
	touch file &&
	but add file &&

	echo expired >file && test_tick && but cummit -a -m expired -S"${GPGSSH_KEY_EXPIRED}" &&
	but tag -s -u "${GPGSSH_KEY_EXPIRED}" -m expired-signed expired-signed &&

	echo notyetvalid >file && test_tick && but cummit -a -m notyetvalid -S"${GPGSSH_KEY_NOTYETVALID}" &&
	but tag -s -u "${GPGSSH_KEY_NOTYETVALID}" -m notyetvalid-signed notyetvalid-signed &&

	echo timeboxedvalid >file && test_tick && but cummit -a -m timeboxedvalid -S"${GPGSSH_KEY_TIMEBOXEDVALID}" &&
	but tag -s -u "${GPGSSH_KEY_TIMEBOXEDVALID}" -m timeboxedvalid-signed timeboxedvalid-signed &&

	echo timeboxedinvalid >file && test_tick && but cummit -a -m timeboxedinvalid -S"${GPGSSH_KEY_TIMEBOXEDINVALID}" &&
	but tag -s -u "${GPGSSH_KEY_TIMEBOXEDINVALID}" -m timeboxedinvalid-signed timeboxedinvalid-signed
'

test_expect_success 'message for merging local branch' '
	echo "Merge branch ${apos}left${apos}" >expected &&

	but checkout main &&
	but fetch . left &&

	but fmt-merge-msg <.but/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success GPG 'message for merging local tag signed by good key' '
	but checkout main &&
	but fetch . signed-good-tag &&
	but fmt-merge-msg <.but/FETCH_HEAD >actual &&
	grep "^Merge tag ${apos}signed-good-tag${apos}" actual &&
	grep "^signed-tag-msg" actual &&
	grep "^# gpg: Signature made" actual &&
	grep "^# gpg: Good signature from" actual
'

test_expect_success GPG 'message for merging local tag signed by unknown key' '
	but checkout main &&
	but fetch . signed-good-tag &&
	GNUPGHOME=. but fmt-merge-msg <.but/FETCH_HEAD >actual &&
	grep "^Merge tag ${apos}signed-good-tag${apos}" actual &&
	grep "^signed-tag-msg" actual &&
	grep "^# gpg: Signature made" actual &&
	grep -E "^# gpg: Can${apos}t check signature: (public key not found|No public key)" actual
'

test_expect_success GPGSSH 'message for merging local tag signed by good ssh key' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	but checkout main &&
	but fetch . signed-good-ssh-tag &&
	but fmt-merge-msg <.but/FETCH_HEAD >actual &&
	grep "^Merge tag ${apos}signed-good-ssh-tag${apos}" actual &&
	grep "^signed-ssh-tag-msg" actual &&
	grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
	! grep "${GPGSSH_BAD_SIGNATURE}" actual
'

test_expect_success GPGSSH 'message for merging local tag signed by unknown ssh key' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	but checkout main &&
	but fetch . signed-untrusted-ssh-tag &&
	but fmt-merge-msg <.but/FETCH_HEAD >actual &&
	grep "^Merge tag ${apos}signed-untrusted-ssh-tag${apos}" actual &&
	grep "^signed-ssh-tag-msg-untrusted" actual &&
	grep "${GPGSSH_GOOD_SIGNATURE_UNTRUSTED}" actual &&
	! grep "${GPGSSH_BAD_SIGNATURE}" actual &&
	grep "${GPGSSH_KEY_NOT_TRUSTED}" actual
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'message for merging local tag signed by expired ssh key' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	but checkout main &&
	but fetch . expired-signed &&
	but fmt-merge-msg <.but/FETCH_HEAD >actual &&
	grep "^Merge tag ${apos}expired-signed${apos}" actual &&
	grep "^expired-signed" actual &&
	! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'message for merging local tag signed by not yet valid ssh key' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	but checkout main &&
	but fetch . notyetvalid-signed &&
	but fmt-merge-msg <.but/FETCH_HEAD >actual &&
	grep "^Merge tag ${apos}notyetvalid-signed${apos}" actual &&
	grep "^notyetvalid-signed" actual &&
	! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'message for merging local tag signed by valid timeboxed ssh key' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	but checkout main &&
	but fetch . timeboxedvalid-signed &&
	but fmt-merge-msg <.but/FETCH_HEAD >actual &&
	grep "^Merge tag ${apos}timeboxedvalid-signed${apos}" actual &&
	grep "^timeboxedvalid-signed" actual &&
	grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual &&
	! grep "${GPGSSH_BAD_SIGNATURE}" actual
'

test_expect_success GPGSSH,GPGSSH_VERIFYTIME 'message for merging local tag signed by invalid timeboxed ssh key' '
	test_config gpg.ssh.allowedSignersFile "${GPGSSH_ALLOWED_SIGNERS}" &&
	but checkout main &&
	but fetch . timeboxedinvalid-signed &&
	but fmt-merge-msg <.but/FETCH_HEAD >actual &&
	grep "^Merge tag ${apos}timeboxedinvalid-signed${apos}" actual &&
	grep "^timeboxedinvalid-signed" actual &&
	! grep "${GPGSSH_GOOD_SIGNATURE_TRUSTED}" actual
'

test_expect_success 'message for merging external branch' '
	echo "Merge branch ${apos}left${apos} of $(pwd)" >expected &&

	but checkout main &&
	but fetch "$(pwd)" left &&

	but fmt-merge-msg <.but/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '[merge] summary/log configuration' '
	cat >expected <<-EOF &&
	Merge branch ${apos}left${apos}

	# By Another Author (3) and A U Thor (2)
	# Via Another cummitter
	* left:
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	test_config merge.log true &&
	test_unconfig merge.summary &&

	but checkout main &&
	test_tick &&
	but fetch . left &&

	but fmt-merge-msg <.but/FETCH_HEAD >actual1 &&

	test_unconfig merge.log &&
	test_config merge.summary true &&

	but checkout main &&
	test_tick &&
	but fetch . left &&

	but fmt-merge-msg <.but/FETCH_HEAD >actual2 &&

	test_cmp expected actual1 &&
	test_cmp expected actual2
'

test_expect_success 'setup FETCH_HEAD' '
	but checkout main &&
	test_tick &&
	but fetch . left
'

test_expect_success 'merge.log=3 limits shortlog length' '
	cat >expected <<-EOF &&
	Merge branch ${apos}left${apos}

	# By Another Author (3) and A U Thor (2)
	# Via Another cummitter
	* left: (5 cummits)
	  Left #5
	  Left #4
	  Left #3
	  ...
	EOF

	but -c merge.log=3 fmt-merge-msg <.but/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge.log=5 shows all 5 cummits' '
	cat >expected <<-EOF &&
	Merge branch ${apos}left${apos}

	# By Another Author (3) and A U Thor (2)
	# Via Another cummitter
	* left:
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	but -c merge.log=5 fmt-merge-msg <.but/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '--log=5 with custom comment character' '
	cat >expected <<-EOF &&
	Merge branch ${apos}left${apos}

	x By Another Author (3) and A U Thor (2)
	x Via Another cummitter
	* left:
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	but -c core.commentchar="x" fmt-merge-msg --log=5 <.but/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge.log=0 disables shortlog' '
	echo "Merge branch ${apos}left${apos}" >expected &&
	but -c merge.log=0 fmt-merge-msg <.but/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '--log=3 limits shortlog length' '
	cat >expected <<-EOF &&
	Merge branch ${apos}left${apos}

	# By Another Author (3) and A U Thor (2)
	# Via Another cummitter
	* left: (5 cummits)
	  Left #5
	  Left #4
	  Left #3
	  ...
	EOF

	but fmt-merge-msg --log=3 <.but/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '--log=5 shows all 5 cummits' '
	cat >expected <<-EOF &&
	Merge branch ${apos}left${apos}

	# By Another Author (3) and A U Thor (2)
	# Via Another cummitter
	* left:
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	but fmt-merge-msg --log=5 <.but/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '--no-log disables shortlog' '
	echo "Merge branch ${apos}left${apos}" >expected &&
	but fmt-merge-msg --no-log <.but/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success '--log=0 disables shortlog' '
	echo "Merge branch ${apos}left${apos}" >expected &&
	but fmt-merge-msg --no-log <.but/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'fmt-merge-msg -m' '
	echo "Sync with left" >expected &&
	cat >expected.log <<-EOF &&
	Sync with left

	# By Another Author (3) and A U Thor (2)
	# Via Another cummitter
	* ${apos}left${apos} of $(pwd):
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	test_unconfig merge.log &&
	test_unconfig merge.summary &&
	but checkout main &&
	but fetch "$(pwd)" left &&
	but fmt-merge-msg -m "Sync with left" <.but/FETCH_HEAD >actual &&
	but fmt-merge-msg --log -m "Sync with left" \
					<.but/FETCH_HEAD >actual.log &&
	test_config merge.log true &&
	but fmt-merge-msg -m "Sync with left" \
					<.but/FETCH_HEAD >actual.log-config &&
	but fmt-merge-msg --no-log -m "Sync with left" \
					<.but/FETCH_HEAD >actual.nolog &&

	test_cmp expected actual &&
	test_cmp expected.log actual.log &&
	test_cmp expected.log actual.log-config &&
	test_cmp expected actual.nolog
'

test_expect_success 'setup: expected shortlog for two branches' '
	cat >expected <<-EOF
	Merge branches ${apos}left${apos} and ${apos}right${apos}

	# By Another Author (3) and A U Thor (2)
	# Via Another cummitter
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
	but checkout main &&
	test_tick &&
	but fetch . left right &&
	but fmt-merge-msg <.but/FETCH_HEAD >actual1 &&

	test_unconfig merge.log &&
	test_config merge.summary true &&
	but checkout main &&
	test_tick &&
	but fetch . left right &&
	but fmt-merge-msg <.but/FETCH_HEAD >actual2 &&

	test_config merge.log yes &&
	test_unconfig merge.summary &&
	but checkout main &&
	test_tick &&
	but fetch . left right &&
	but fmt-merge-msg <.but/FETCH_HEAD >actual3 &&

	test_unconfig merge.log &&
	test_config merge.summary yes &&
	but checkout main &&
	test_tick &&
	but fetch . left right &&
	but fmt-merge-msg <.but/FETCH_HEAD >actual4 &&

	test_cmp expected actual1 &&
	test_cmp expected actual2 &&
	test_cmp expected actual3 &&
	test_cmp expected actual4
'

test_expect_success 'merge-msg -F' '
	test_unconfig merge.log &&
	test_config merge.summary yes &&
	but checkout main &&
	test_tick &&
	but fetch . left right &&
	but fmt-merge-msg -F .but/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-msg -F in subdirectory' '
	test_unconfig merge.log &&
	test_config merge.summary yes &&
	but checkout main &&
	test_tick &&
	but fetch . left right &&
	mkdir sub &&
	cp .but/FETCH_HEAD sub/FETCH_HEAD &&
	(
		cd sub &&
		but fmt-merge-msg -F FETCH_HEAD >../actual
	) &&
	test_cmp expected actual
'

test_expect_success 'merge-msg with nothing to merge' '
	test_unconfig merge.log &&
	test_config merge.summary yes &&

	(
		cd remote &&
		but checkout -b unrelated &&
		test_tick &&
		but fetch origin &&
		but fmt-merge-msg <.but/FETCH_HEAD >../actual
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

	but checkout main &&
	test_tick &&
	but fetch . tag tag-r3 &&

	but fmt-merge-msg <.but/FETCH_HEAD >actual &&
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
	# Via Another cummitter
	* tag ${apos}tag-l5${apos}:
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	test_unconfig merge.log &&
	test_config merge.summary yes &&

	but checkout main &&
	test_tick &&
	but fetch . tag tag-r3 tag tag-l5 &&

	but fmt-merge-msg <.but/FETCH_HEAD >actual &&
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
	# Via Another cummitter
	* left:
	  Left #5
	  Left #4
	  Left #3
	  Common #2
	  Common #1
	EOF

	test_unconfig merge.log &&
	test_config merge.summary yes &&

	but checkout main &&
	test_tick &&
	but fetch . tag tag-r3 left &&

	but fmt-merge-msg <.but/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-msg lots of cummits' '
	{
		cat <<-EOF &&
		Merge branch ${apos}long${apos}

		* long: (35 cummits)
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

	but checkout main &&
	test_tick &&
	but fetch . long &&

	but fmt-merge-msg <.but/FETCH_HEAD >actual &&
	test_cmp expected actual
'

test_expect_success 'merge-msg with "merging" an annotated tag' '
	test_config merge.log true &&

	but checkout main^0 &&
	but cummit --allow-empty -m "One step ahead" &&
	but tag -a -m "An annotated one" annote HEAD &&

	but checkout main &&
	but fetch . annote &&

	but fmt-merge-msg <.but/FETCH_HEAD >actual &&
	{
		cat <<-\EOF
		Merge tag '\''annote'\''

		An annotated one

		* tag '\''annote'\'':
		  One step ahead
		EOF
	} >expected &&
	test_cmp expected actual &&

	test_when_finished "but reset --hard" &&
	annote=$(but rev-parse annote) &&
	but merge --no-cummit --no-ff $annote &&
	{
		cat <<-EOF
		Merge tag '\''$annote'\''

		An annotated one

		* tag '\''$annote'\'':
		  One step ahead
		EOF
	} >expected &&
	test_cmp expected .but/MERGE_MSG
'

test_expect_success 'merge --into-name=<name>' '
	test_when_finished "but checkout main" &&
	but checkout -B side main &&
	but cummit --allow-empty -m "One step ahead" &&

	but checkout --detach main &&
	but merge --no-ff side &&
	but show -s --format="%s" >full.0 &&
	head -n1 full.0 >actual &&
	# expect that HEAD is shown as-is
	grep -e "Merge branch .side. into HEAD$" actual &&

	but reset --hard main &&
	but merge --no-ff --into-name=main side &&
	but show -s --format="%s" >full.1 &&
	head -n1 full.1 >actual &&
	# expect that we pretend to be merging to main, that is suppressed
	grep -e "Merge branch .side.$" actual &&

	but checkout -b throwaway main &&
	but merge --no-ff --into-name=main side &&
	but show -s --format="%s" >full.2 &&
	head -n1 full.2 >actual &&
	# expect that we pretend to be merging to main, that is suppressed
	grep -e "Merge branch .side.$" actual
'

test_expect_success 'merge.suppressDest configuration' '
	test_when_finished "but checkout main" &&
	but checkout -B side main &&
	but cummit --allow-empty -m "One step ahead" &&
	but checkout main &&
	but fetch . side &&

	but -c merge.suppressDest="" fmt-merge-msg <.but/FETCH_HEAD >full.1 &&
	head -n1 full.1 >actual &&
	grep -e "Merge branch .side. into main" actual &&

	but -c merge.suppressDest="mast" fmt-merge-msg <.but/FETCH_HEAD >full.2 &&
	head -n1 full.2 >actual &&
	grep -e "Merge branch .side. into main$" actual &&

	but -c merge.suppressDest="ma?*[rn]" fmt-merge-msg <.but/FETCH_HEAD >full.3 &&
	head -n1 full.3 >actual &&
	grep -e "Merge branch .side." actual &&
	! grep -e " into main$" actual &&

	but checkout --detach HEAD &&
	but -c merge.suppressDest="main" fmt-merge-msg <.but/FETCH_HEAD >full.4 &&
	head -n1 full.4 >actual &&
	grep -e "Merge branch .side. into HEAD$" actual &&

	but -c merge.suppressDest="main" fmt-merge-msg \
		--into-name=main <.but/FETCH_HEAD >full.5 &&
	head -n1 full.5 >actual &&
	grep -e "Merge branch .side." actual &&
	! grep -e " into main$" actual &&
	! grep -e " into HEAD$" actual
'

test_done
