#!/bin/sh
#
# Copyright (c) 2005 Junio C Hamano
#

test_description='Test diff raw-output.

'
. ./test-lib.sh

. "$TEST_DIRECTORY"/lib-read-tree-m-3way.sh

test_oid_init

test_oid_cache <<\EOF
aa_1 sha1:ccba72ad3888a3520b39efcf780b9ee64167535d
aa_1 sha256:9febfbf18197819b2735c45291f138525d2476d59470f98239647544586ba403

aa_2 sha1:6aa2b5335b16431a0ef71e5c0a28be69183cf6a2
aa_2 sha256:6eaa3437de83f145a4aaa6ba355303075ade547b128ec6a2cd00a81ff7ce7a56

an_1 sha1:7e426fb079479fd67f6d81f984e4ec649a44bc25
an_1 sha256:8f92a0bec99e399a38e3bd0e1bf19fbf121e0160efb29b857df79d439f1c4536

dd_1 sha1:bcc68ef997017466d5c9094bcf7692295f588c9a
dd_1 sha256:07e17428b00639b85485d2b01083d219e2f3e3ba8579e9ca44e9cc8dd554d952

df_1 sha1:6d50f65d3bdab91c63444294d38f08aeff328e42
df_1 sha256:e367cecc27e9bf5451b1c65828cb21938d36a5f8e39c1b03ad6509cc36bb8e9d

df_2 sha1:71420ab81e254145d26d6fc0cddee64c1acd4787
df_2 sha256:0f0a86d10347ff6921d03a3c954679f3f1d14fa3d5cd82f57b32c09755f3a47d

dfd1 sha1:68a6d8b91da11045cf4aa3a5ab9f2a781c701249
dfd1 sha256:f3bd3265b02b6978ce86490d8ad026c573639c974b3de1d9faf30d8d5a77d3d5

dm_1 sha1:141c1f1642328e4bc46a7d801a71da392e66791e
dm_1 sha256:c89f8656e7b94e21ee5fbaf0e2149bbf783c51edbe2ce110349cac13059ee7ed

dm_2 sha1:3c4d8de5fbad08572bab8e10eef8dbb264cf0231
dm_2 sha256:83a572e37e0c94086294dae2cecc43d9131afd6f6c906e495c78972230b54988

dn_1 sha1:35abde1506ddf806572ff4d407bd06885d0f8ee9
dn_1 sha256:775d5852582070e620be63327bfa515fab8f71c7ac3e4f0c3cd6267b4377ba28

ll_2 sha1:1d41122ebdd7a640f29d3c9cc4f9d70094374762
ll_2 sha256:7917b4948a883cfed0a77d3d5a625dc8577d6ddcc3c6c3bbc56c4d4226a2246d

md_1 sha1:03f24c8c4700babccfd28b654e7e8eac402ad6cd
md_1 sha256:fc9f30369b978595ad685ba11ca9a17de0af16d79cd4b629975f4f1590033902

md_2 sha1:103d9f89b50b9aad03054b579be5e7aa665f2d57
md_2 sha256:fc78ec75275628762fe520479a6b2398dec295ce7aabcb1d15e5963c7b4e9317

mm_1 sha1:b258508afb7ceb449981bd9d63d2d3e971bf8d34
mm_1 sha256:a4b7847d228e900e3000285e240c20fd96f9dd41ce1445305f6eada126d4a04a

mm_2 sha1:b431b272d829ff3aa4d1a5085f4394ab4d3305b6
mm_2 sha256:3f8b83ea36aacf689bcf1a1290a9a8ed341564d32682ea6f76fea9a979186782

mm_3 sha1:19989d4559aae417fedee240ccf2ba315ea4dc2b
mm_3 sha256:71b3bfc5747ac033fff9ea0ab39ee453a3af2969890e75d6ef547b87544e2681

mn_1 sha1:bd084b0c27c7b6cc34f11d6d0509a29be3caf970
mn_1 sha256:47a67450583d7a329eb01a7c4ba644945af72c0ed2c7c95eb5a00d6e46d4d483

mn_2 sha1:a716d58de4a570e0038f5c307bd8db34daea021f
mn_2 sha256:f95104c1ebe27acb84bac25a7be98c71f6b8d3054b21f357a5be0c524ad97e08

nm_1 sha1:c8f25781e8f1792e3e40b74225e20553041b5226
nm_1 sha256:09baddc7afaa62e62e152c23c9c3ab94bf15a3894031e227e9be7fe68e1f4e49

nm_2 sha1:cdb9a8c3da571502ac30225e9c17beccb8387983
nm_2 sha256:58b5227956ac2d2a08d0efa513c0ae37430948b16791ea3869a1308dbf05536d

na_1 sha1:15885881ea69115351c09b38371f0348a3fb8c67
na_1 sha256:18e4fdd1670cd7968ee23d35bfd29e5418d56fb190c840094c1c57ceee0aad8f

nd_1 sha1:a4e179e4291e5536a5e1c82e091052772d2c5a93
nd_1 sha256:07dac9b01d00956ea0c65bd993d7de4864aeef2ed3cbb1255d9f1d949fcd6df6

ss_1 sha1:40c959f984c8b89a2b02520d17f00d717f024397
ss_1 sha256:50fc1b5df74d9910db2f9270993484235f15b69b75b01bcfb53e059289d14af9

ss_2 sha1:2ac547ae9614a00d1b28275de608131f7a0e259f
ss_2 sha256:a90f02e6044f1497d13db587d22ab12f90150a7d1e084afcf96065fab35ae2bc

tt_1 sha1:4ac13458899ab908ef3b1128fa378daefc88d356
tt_1 sha256:c53113c7dd5060e86b5b251428bd058f6726f66273c6a24bff1c61a04f498dd3

tt_2 sha1:4c86f9a85fbc5e6804ee2e17a797538fbe785bca
tt_2 sha256:0775f2a296129a7cf2862b46bc0e88c14d593f2773a3e3fb1c5193db6f5a7e77

tt_3 sha1:c4e4a12231b9fa79a0053cb6077fcb21bb5b135a
tt_3 sha256:47860f93cdd211f96443e0560f21c57ab6c2f4b0ac27ff03651a352e53fe8484

z__1 sha1:7d670fdcdb9929f6c7dac196ff78689cd1c566a1
z__1 sha256:44d0f37aff5e51cfcfdd1134c93a6419bcca7b9964f792ffcd5f9b4fcba1ee63

z__2 sha1:5e5f22072bb39f6e12cf663a57cb634c76eefb49
z__2 sha256:d29de162113190fed104eb5f010820cef4e315f89b9326e8497f7219fb737894

z__3 sha1:1ba523955d5160681af65cb776411f574c1e8155
z__3 sha256:07422d772b07794ab4369a5648e617719f89c2d2212cbeab05d97214b6471636

zaa1 sha1:8acb8e9750e3f644bf323fcf3d338849db106c77
zaa1 sha256:e79b029282c8abec2d9f3f7faceaf2a1405e02d1f368e66450ae66cf5b68d1f4

zaa2 sha1:6c0b99286d0bce551ac4a7b3dff8b706edff3715
zaa2 sha256:c82bd78c3e69ea1796e6b1a7a3ba45bb106c50e819296475b862123d3f5cc5a0

zan1 sha1:087494262084cefee7ed484d20c8dc0580791272
zan1 sha256:4b159eb3804d05599023dd074f771d06d02870f4ab24a7165add8ac3d703b8d3

zdd1 sha1:879007efae624d2b1307214b24a956f0a8d686a8
zdd1 sha256:eecfdd4d8092dd0363fb6d4548b54c6afc8982c3ed9b34e393f1d6a921d8eaa3

zdm1 sha1:9b541b2275c06e3a7b13f28badf5294e2ae63df4
zdm1 sha256:ab136e88e19a843c4bf7713d2090d5a2186ba16a6a80dacc12eeddd256a8e556

zdm2 sha1:d77371d15817fcaa57eeec27f770c505ba974ec1
zdm2 sha256:1c1a5f57363f46a15d95ce8527b3c2c158d88d16853b4acbf81bd20fd2c89a46

zdn1 sha1:beb5d38c55283d280685ea21a0e50cfcc0ca064a
zdn1 sha256:0f0eca66183617b0aa5ad74b256540329f841470922ca6760263c996d825eb18

zmd1 sha1:d41fda41b7ec4de46b43cb7ea42a45001ae393d5
zmd1 sha256:1ed32d481852eddf31a0ce12652a0ad14bf5b7a842667b5dbb0b50f35bf1c80a

zmd2 sha1:a79ac3be9377639e1c7d1edf1ae1b3a5f0ccd8a9
zmd2 sha256:b238da211b404f8917df2d9c6f7030535e904b2186131007a3c292ec6902f933

zmm1 sha1:4ca22bae2527d3d9e1676498a0fba3b355bd1278
zmm1 sha256:072b1d85b5f34fabc99dfa46008c5418df68302d3e317430006f49b32d244226

zmm2 sha1:61422ba9c2c873416061a88cd40a59a35b576474
zmm2 sha256:81dd5d2b3c5cda16fef552256aed4e2ea0802a8450a08f308a92142112ff6dda

zmm3 sha1:697aad7715a1e7306ca76290a3dd4208fbaeddfa
zmm3 sha256:8b10fab49e9be3414aa5e9a93d0e46f9569053440138a7c19a5eb5536d8e95bf

zmn1 sha1:b16d7b25b869f2beb124efa53467d8a1550ad694
zmn1 sha256:609e4f75d1295e844c826feeba213acb0b6cfc609adfe8ff705b19e3829ae3e9

zmn2 sha1:a5c544c21cfcb07eb80a4d89a5b7d1570002edfd
zmn2 sha256:d6d03edf2dc1a3b267a8205de5f41a2ff4b03def8c7ae02052b543fb09d589fc

zna1 sha1:d12979c22fff69c59ca9409e7a8fe3ee25eaee80
zna1 sha256:b37b80e789e8ea32aa323f004628f02013f632124b0282c7fe00a127d3c64c3c

znd1 sha1:a18393c636b98e9bd7296b8b437ea4992b72440c
znd1 sha256:af92a22eee8c38410a0c9d2b5135a10aeb052cbc7cf675541ed9a67bfcaf7cf9

znm1 sha1:3fdbe17fd013303a2e981e1ca1c6cd6e72789087
znm1 sha256:f75aeaa0c11e76918e381c105f0752932c6150e941fec565d24fa31098a13dc1

znm2 sha1:7e09d6a3a14bd630913e8c75693cea32157b606d
znm2 sha256:938d73cfbaa1c902a84fb5b3afd9736aa0590367fb9bd59c6c4d072ce70fcd6d
EOF

cat >.test-plain-OA <<EOF
:000000 100644 $(test_oid zero) $(test_oid aa_1) A	AA
:000000 100644 $(test_oid zero) $(test_oid an_1) A	AN
:100644 000000 $(test_oid dd_1) $(test_oid zero) D	DD
:000000 040000 $(test_oid zero) $(test_oid df_1) A	DF
:100644 000000 $(test_oid dm_1) $(test_oid zero) D	DM
:100644 000000 $(test_oid dn_1) $(test_oid zero) D	DN
:000000 100644 $(test_oid zero) $(test_oid ll_2) A	LL
:100644 100644 $(test_oid md_1) $(test_oid md_2) M	MD
:100644 100644 $(test_oid mm_1) $(test_oid mm_2) M	MM
:100644 100644 $(test_oid mn_1) $(test_oid mn_2) M	MN
:100644 100644 $(test_oid ss_1) $(test_oid ss_2) M	SS
:100644 100644 $(test_oid tt_1) $(test_oid tt_2) M	TT
:040000 040000 $(test_oid z__1) $(test_oid z__2) M	Z
EOF

cat >.test-recursive-OA <<EOF
:000000 100644 $(test_oid zero) $(test_oid aa_1) A	AA
:000000 100644 $(test_oid zero) $(test_oid an_1) A	AN
:100644 000000 $(test_oid dd_1) $(test_oid zero) D	DD
:000000 100644 $(test_oid zero) $(test_oid dfd1) A	DF/DF
:100644 000000 $(test_oid dm_1) $(test_oid zero) D	DM
:100644 000000 $(test_oid dn_1) $(test_oid zero) D	DN
:000000 100644 $(test_oid zero) $(test_oid ll_2) A	LL
:100644 100644 $(test_oid md_1) $(test_oid md_2) M	MD
:100644 100644 $(test_oid mm_1) $(test_oid mm_2) M	MM
:100644 100644 $(test_oid mn_1) $(test_oid mn_2) M	MN
:100644 100644 $(test_oid ss_1) $(test_oid ss_2) M	SS
:100644 100644 $(test_oid tt_1) $(test_oid tt_2) M	TT
:000000 100644 $(test_oid zero) $(test_oid zaa1) A	Z/AA
:000000 100644 $(test_oid zero) $(test_oid zan1) A	Z/AN
:100644 000000 $(test_oid zdd1) $(test_oid zero) D	Z/DD
:100644 000000 $(test_oid zdm1) $(test_oid zero) D	Z/DM
:100644 000000 $(test_oid zdn1) $(test_oid zero) D	Z/DN
:100644 100644 $(test_oid zmd1) $(test_oid zmd2) M	Z/MD
:100644 100644 $(test_oid zmm1) $(test_oid zmm2) M	Z/MM
:100644 100644 $(test_oid zmn1) $(test_oid zmn2) M	Z/MN
EOF
cat >.test-plain-OB <<EOF
:000000 100644 $(test_oid zero) $(test_oid aa_2) A	AA
:100644 000000 $(test_oid dd_1) $(test_oid zero) D	DD
:000000 100644 $(test_oid zero) $(test_oid df_2) A	DF
:100644 100644 $(test_oid dm_1) $(test_oid dm_2) M	DM
:000000 100644 $(test_oid zero) $(test_oid ll_2) A	LL
:100644 000000 $(test_oid md_1) $(test_oid zero) D	MD
:100644 100644 $(test_oid mm_1) $(test_oid mm_3) M	MM
:000000 100644 $(test_oid zero) $(test_oid na_1) A	NA
:100644 000000 $(test_oid nd_1) $(test_oid zero) D	ND
:100644 100644 $(test_oid nm_1) $(test_oid nm_2) M	NM
:100644 100644 $(test_oid ss_1) $(test_oid ss_2) M	SS
:100644 100644 $(test_oid tt_1) $(test_oid tt_3) M	TT
:040000 040000 $(test_oid z__1) $(test_oid z__3) M	Z
EOF
cat >.test-recursive-OB <<EOF
:000000 100644 $(test_oid zero) $(test_oid aa_2) A	AA
:100644 000000 $(test_oid dd_1) $(test_oid zero) D	DD
:000000 100644 $(test_oid zero) $(test_oid df_2) A	DF
:100644 100644 $(test_oid dm_1) $(test_oid dm_2) M	DM
:000000 100644 $(test_oid zero) $(test_oid ll_2) A	LL
:100644 000000 $(test_oid md_1) $(test_oid zero) D	MD
:100644 100644 $(test_oid mm_1) $(test_oid mm_3) M	MM
:000000 100644 $(test_oid zero) $(test_oid na_1) A	NA
:100644 000000 $(test_oid nd_1) $(test_oid zero) D	ND
:100644 100644 $(test_oid nm_1) $(test_oid nm_2) M	NM
:100644 100644 $(test_oid ss_1) $(test_oid ss_2) M	SS
:100644 100644 $(test_oid tt_1) $(test_oid tt_3) M	TT
:000000 100644 $(test_oid zero) $(test_oid zaa2) A	Z/AA
:100644 000000 $(test_oid zdd1) $(test_oid zero) D	Z/DD
:100644 100644 $(test_oid zdm1) $(test_oid zdm2) M	Z/DM
:100644 000000 $(test_oid zmd1) $(test_oid zero) D	Z/MD
:100644 100644 $(test_oid zmm1) $(test_oid zmm3) M	Z/MM
:000000 100644 $(test_oid zero) $(test_oid zna1) A	Z/NA
:100644 000000 $(test_oid znd1) $(test_oid zero) D	Z/ND
:100644 100644 $(test_oid znm1) $(test_oid znm2) M	Z/NM
EOF
cat >.test-plain-AB <<EOF
:100644 100644 $(test_oid aa_1) $(test_oid aa_2) M	AA
:100644 000000 $(test_oid an_1) $(test_oid zero) D	AN
:000000 100644 $(test_oid zero) $(test_oid df_2) A	DF
:040000 000000 $(test_oid df_1) $(test_oid zero) D	DF
:000000 100644 $(test_oid zero) $(test_oid dm_2) A	DM
:000000 100644 $(test_oid zero) $(test_oid dn_1) A	DN
:100644 000000 $(test_oid md_2) $(test_oid zero) D	MD
:100644 100644 $(test_oid mm_2) $(test_oid mm_3) M	MM
:100644 100644 $(test_oid mn_2) $(test_oid mn_1) M	MN
:000000 100644 $(test_oid zero) $(test_oid na_1) A	NA
:100644 000000 $(test_oid nd_1) $(test_oid zero) D	ND
:100644 100644 $(test_oid nm_1) $(test_oid nm_2) M	NM
:100644 100644 $(test_oid tt_2) $(test_oid tt_3) M	TT
:040000 040000 $(test_oid z__2) $(test_oid z__3) M	Z
EOF
cat >.test-recursive-AB <<EOF
:100644 100644 $(test_oid aa_1) $(test_oid aa_2) M	AA
:100644 000000 $(test_oid an_1) $(test_oid zero) D	AN
:000000 100644 $(test_oid zero) $(test_oid df_2) A	DF
:100644 000000 $(test_oid dfd1) $(test_oid zero) D	DF/DF
:000000 100644 $(test_oid zero) $(test_oid dm_2) A	DM
:000000 100644 $(test_oid zero) $(test_oid dn_1) A	DN
:100644 000000 $(test_oid md_2) $(test_oid zero) D	MD
:100644 100644 $(test_oid mm_2) $(test_oid mm_3) M	MM
:100644 100644 $(test_oid mn_2) $(test_oid mn_1) M	MN
:000000 100644 $(test_oid zero) $(test_oid na_1) A	NA
:100644 000000 $(test_oid nd_1) $(test_oid zero) D	ND
:100644 100644 $(test_oid nm_1) $(test_oid nm_2) M	NM
:100644 100644 $(test_oid tt_2) $(test_oid tt_3) M	TT
:100644 100644 $(test_oid zaa1) $(test_oid zaa2) M	Z/AA
:100644 000000 $(test_oid zan1) $(test_oid zero) D	Z/AN
:000000 100644 $(test_oid zero) $(test_oid zdm2) A	Z/DM
:000000 100644 $(test_oid zero) $(test_oid zdn1) A	Z/DN
:100644 000000 $(test_oid zmd2) $(test_oid zero) D	Z/MD
:100644 100644 $(test_oid zmm2) $(test_oid zmm3) M	Z/MM
:100644 100644 $(test_oid zmn2) $(test_oid zmn1) M	Z/MN
:000000 100644 $(test_oid zero) $(test_oid zna1) A	Z/NA
:100644 000000 $(test_oid znd1) $(test_oid zero) D	Z/ND
:100644 100644 $(test_oid znm1) $(test_oid znm2) M	Z/NM
EOF

cmp_diff_files_output () {
    # diff-files never reports additions.  Also it does not fill in the
    # object ID for the changed files because it wants you to look at the
    # filesystem.
    sed <"$2" >.test-tmp \
	-e '/^:000000 /d;s/'$OID_REGEX'\( [MCRNDU][0-9]*\)	/'$ZERO_OID'\1	/' &&
    test_cmp "$1" .test-tmp
}

test_expect_success \
    'diff-tree of known trees.' \
    'git diff-tree $tree_O $tree_A >.test-a &&
     cmp -s .test-a .test-plain-OA'

test_expect_success \
    'diff-tree of known trees.' \
    'git diff-tree -r $tree_O $tree_A >.test-a &&
     cmp -s .test-a .test-recursive-OA'

test_expect_success \
    'diff-tree of known trees.' \
    'git diff-tree $tree_O $tree_B >.test-a &&
     cmp -s .test-a .test-plain-OB'

test_expect_success \
    'diff-tree of known trees.' \
    'git diff-tree -r $tree_O $tree_B >.test-a &&
     cmp -s .test-a .test-recursive-OB'

test_expect_success \
    'diff-tree of known trees.' \
    'git diff-tree $tree_A $tree_B >.test-a &&
     cmp -s .test-a .test-plain-AB'

test_expect_success \
    'diff-tree of known trees.' \
    'git diff-tree -r $tree_A $tree_B >.test-a &&
     cmp -s .test-a .test-recursive-AB'

test_expect_success \
    'diff-tree --stdin of known trees.' \
    'echo $tree_A $tree_B | git diff-tree --stdin > .test-a &&
     echo $tree_A $tree_B > .test-plain-ABx &&
     cat .test-plain-AB >> .test-plain-ABx &&
     cmp -s .test-a .test-plain-ABx'

test_expect_success \
    'diff-tree --stdin of known trees.' \
    'echo $tree_A $tree_B | git diff-tree -r --stdin > .test-a &&
     echo $tree_A $tree_B > .test-recursive-ABx &&
     cat .test-recursive-AB >> .test-recursive-ABx &&
     cmp -s .test-a .test-recursive-ABx'

test_expect_success \
    'diff-cache O with A in cache' \
    'git read-tree $tree_A &&
     git diff-index --cached $tree_O >.test-a &&
     cmp -s .test-a .test-recursive-OA'

test_expect_success \
    'diff-cache O with B in cache' \
    'git read-tree $tree_B &&
     git diff-index --cached $tree_O >.test-a &&
     cmp -s .test-a .test-recursive-OB'

test_expect_success \
    'diff-cache A with B in cache' \
    'git read-tree $tree_B &&
     git diff-index --cached $tree_A >.test-a &&
     cmp -s .test-a .test-recursive-AB'

test_expect_success \
    'diff-files with O in cache and A checked out' \
    'rm -fr Z [A-Z][A-Z] &&
     git read-tree $tree_A &&
     git checkout-index -f -a &&
     git read-tree --reset $tree_O &&
     test_must_fail git update-index --refresh -q &&
     git diff-files >.test-a &&
     cmp_diff_files_output .test-a .test-recursive-OA'

test_expect_success \
    'diff-files with O in cache and B checked out' \
    'rm -fr Z [A-Z][A-Z] &&
     git read-tree $tree_B &&
     git checkout-index -f -a &&
     git read-tree --reset $tree_O &&
     test_must_fail git update-index --refresh -q &&
     git diff-files >.test-a &&
     cmp_diff_files_output .test-a .test-recursive-OB'

test_expect_success \
    'diff-files with A in cache and B checked out' \
    'rm -fr Z [A-Z][A-Z] &&
     git read-tree $tree_B &&
     git checkout-index -f -a &&
     git read-tree --reset $tree_A &&
     test_must_fail git update-index --refresh -q &&
     git diff-files >.test-a &&
     cmp_diff_files_output .test-a .test-recursive-AB'

################################################################
# Now we have established the baseline, we do not have to
# rely on individual object ID values that much.

test_expect_success \
    'diff-tree O A == diff-tree -R A O' \
    'git diff-tree $tree_O $tree_A >.test-a &&
    git diff-tree -R $tree_A $tree_O >.test-b &&
    cmp -s .test-a .test-b'

test_expect_success \
    'diff-tree -r O A == diff-tree -r -R A O' \
    'git diff-tree -r $tree_O $tree_A >.test-a &&
    git diff-tree -r -R $tree_A $tree_O >.test-b &&
    cmp -s .test-a .test-b'

test_expect_success \
    'diff-tree B A == diff-tree -R A B' \
    'git diff-tree $tree_B $tree_A >.test-a &&
    git diff-tree -R $tree_A $tree_B >.test-b &&
    cmp -s .test-a .test-b'

test_expect_success \
    'diff-tree -r B A == diff-tree -r -R A B' \
    'git diff-tree -r $tree_B $tree_A >.test-a &&
    git diff-tree -r -R $tree_A $tree_B >.test-b &&
    cmp -s .test-a .test-b'

test_expect_success \
    'diff can read from stdin' \
    'test_must_fail git diff --no-index -- MN - < NN |
        grep -v "^index" | sed "s#/-#/NN#" >.test-a &&
    test_must_fail git diff --no-index -- MN NN |
        grep -v "^index" >.test-b &&
    test_cmp .test-a .test-b'

test_done
