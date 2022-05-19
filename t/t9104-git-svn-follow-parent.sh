#!/bin/sh
#
# Copyright (c) 2006 Eric Wong
#

test_description='but svn fetching'
. ./lib-but-svn.sh

test_expect_success 'initialize repo' '
	mkdir import &&
	(
		cd import &&
		mkdir -p trunk &&
		echo hello >trunk/readme &&
		svn_cmd import -m "initial" . "$svnrepo"
	) &&
	svn_cmd co "$svnrepo" wc &&
	(
		cd wc &&
		echo world >>trunk/readme &&
		poke trunk/readme &&
		svn_cmd cummit -m "another cummit" &&
		svn_cmd up &&
		svn_cmd mv trunk thunk &&
		echo goodbye >>thunk/readme &&
		poke thunk/readme &&
		svn_cmd cummit -m "bye now"
	)
	'

test_expect_success 'init and fetch a moved directory' '
	but svn init --minimize-url -i thunk "$svnrepo"/thunk &&
	but svn fetch -i thunk &&
	test "$(but rev-parse --verify refs/remotes/thunk@2)" \
	   = "$(but rev-parse --verify refs/remotes/thunk~1)" &&
	but cat-file blob refs/remotes/thunk:readme >actual &&
	test "$(sed -n -e "3p" actual)" = goodbye &&
	test -z "$(but config --get svn-remote.svn.fetch \
		 "^trunk:refs/remotes/thunk@2$")"
	'

test_expect_success 'init and fetch from one svn-remote' '
        but config svn-remote.svn.url "$svnrepo" &&
        but config --add svn-remote.svn.fetch \
          trunk:refs/remotes/svn/trunk &&
        but config --add svn-remote.svn.fetch \
          thunk:refs/remotes/svn/thunk &&
        but svn fetch -i svn/thunk &&
	test "$(but rev-parse --verify refs/remotes/svn/trunk)" \
	   = "$(but rev-parse --verify refs/remotes/svn/thunk~1)" &&
	but cat-file blob refs/remotes/svn/thunk:readme >actual &&
	test "$(sed -n -e "3p" actual)" = goodbye
        '

test_expect_success 'follow deleted parent' '
        (svn_cmd cp -m "resurrecting trunk as junk" \
               "$svnrepo"/trunk@2 "$svnrepo"/junk ||
         svn cp -m "resurrecting trunk as junk" \
               -r2 "$svnrepo"/trunk "$svnrepo"/junk) &&
        but config --add svn-remote.svn.fetch \
          junk:refs/remotes/svn/junk &&
        but svn fetch -i svn/thunk &&
        but svn fetch -i svn/junk &&
	test -z "$(but diff svn/junk svn/trunk)" &&
	test "$(but merge-base svn/junk svn/trunk)" \
	   = "$(but rev-parse svn/trunk)"
        '

test_expect_success 'follow larger parent' '
        mkdir -p import/trunk/thunk/bump/thud &&
        echo hi > import/trunk/thunk/bump/thud/file &&
        svn import -m "import a larger parent" import "$svnrepo"/larger-parent &&
        svn cp -m "hi" "$svnrepo"/larger-parent "$svnrepo"/another-larger &&
        but svn init --minimize-url -i larger \
	  "$svnrepo"/larger-parent/trunk/thunk/bump/thud &&
        but svn fetch -i larger &&
	but svn init --minimize-url -i larger-parent \
	  "$svnrepo"/another-larger/trunk/thunk/bump/thud &&
	but svn fetch -i larger-parent &&
        but rev-parse --verify refs/remotes/larger &&
        but rev-parse --verify \
	   refs/remotes/larger-parent &&
	test "$(but merge-base \
		 refs/remotes/larger-parent \
		 refs/remotes/larger)" = \
	     "$(but rev-parse refs/remotes/larger)"
        '

test_expect_success 'follow higher-level parent' '
	svn mkdir -m "follow higher-level parent" "$svnrepo"/blob &&
	svn co "$svnrepo"/blob blob &&
	(
		cd blob &&
		echo hi > hi &&
		svn add hi &&
		svn cummit -m "hihi"
	) &&
	svn mkdir -m "new glob at top level" "$svnrepo"/glob &&
	svn mv -m "move blob down a level" "$svnrepo"/blob "$svnrepo"/glob/blob &&
	but svn init --minimize-url -i blob "$svnrepo"/glob/blob &&
        but svn fetch -i blob
        '

test_expect_success 'follow deleted directory' '
	svn_cmd mv -m "bye!" "$svnrepo"/glob/blob/hi "$svnrepo"/glob/blob/bye &&
	svn_cmd rm -m "remove glob" "$svnrepo"/glob &&
	but svn init --minimize-url -i glob "$svnrepo"/glob &&
	but svn fetch -i glob &&
	test "$(but cat-file blob refs/remotes/glob:blob/bye)" = hi &&
	but ls-tree refs/remotes/glob >actual &&
	test_line_count = 1 actual
	'

# ref: r9270 of the Subversion repository: (http://svn.collab.net/repos/svn)
# in trunk/subversion/bindings/swig/perl
test_expect_success 'follow-parent avoids deleting relevant info' '
	mkdir -p import/trunk/subversion/bindings/swig/perl/t &&
	for i in a b c ; do \
	  echo $i > import/trunk/subversion/bindings/swig/perl/$i.pm &&
	  echo _$i > import/trunk/subversion/bindings/swig/perl/t/$i.t || return 1
	done &&
	  echo "bad delete test" > \
	   import/trunk/subversion/bindings/swig/perl/t/larger-parent &&
	  echo "bad delete test 2" > \
	   import/trunk/subversion/bindings/swig/perl/another-larger &&
	(
		cd import &&
		svn import -m "r9270 test" . "$svnrepo"/r9270
	) &&
	svn_cmd co "$svnrepo"/r9270/trunk/subversion/bindings/swig/perl r9270 &&
	(
		cd r9270 &&
		svn mkdir native &&
		svn mv t native/t &&
		for i in a b c
		do
			svn mv $i.pm native/$i.pm || return 1
		done &&
		echo z >>native/t/c.t &&
		poke native/t/c.t &&
		svn cummit -m "reorg test"
	) &&
	but svn init --minimize-url -i r9270-t \
	  "$svnrepo"/r9270/trunk/subversion/bindings/swig/perl/native/t &&
	but svn fetch -i r9270-t &&
	test $(but rev-list r9270-t | wc -l) -eq 2 &&
	test "$(but ls-tree --name-only r9270-t~1)" = \
	     "$(but ls-tree --name-only r9270-t)"
	'

test_expect_success "track initial change if it was only made to parent" '
	svn_cmd cp -m "wheee!" "$svnrepo"/r9270/trunk "$svnrepo"/r9270/drunk &&
	but svn init --minimize-url -i r9270-d \
	  "$svnrepo"/r9270/drunk/subversion/bindings/swig/perl/native/t &&
	but svn fetch -i r9270-d &&
	test $(but rev-list r9270-d | wc -l) -eq 3 &&
	test "$(but ls-tree --name-only r9270-t)" = \
	     "$(but ls-tree --name-only r9270-d)" &&
	test "$(but rev-parse r9270-t)" = \
	     "$(but rev-parse r9270-d~1)"
	'

test_expect_success "follow-parent is atomic" '
	record_size=$(($(test_oid rawsz) + 4)) &&
	(
		cd wc &&
		svn_cmd up &&
		svn_cmd mkdir stunk &&
		echo "trunk stunk" > stunk/readme &&
		svn_cmd add stunk/readme &&
		svn_cmd ci -m "trunk stunk" &&
		echo "stunk like junk" >> stunk/readme &&
		svn_cmd ci -m "really stunk" &&
		echo "stink stank stunk" >> stunk/readme &&
		svn_cmd ci -m "even the grinch agrees"
	) &&
	svn_cmd copy -m "stunk flunked" "$svnrepo"/stunk "$svnrepo"/flunk &&
	{ svn cp -m "early stunk flunked too" \
		"$svnrepo"/stunk@17 "$svnrepo"/flunked ||
	svn_cmd cp -m "early stunk flunked too" \
		-r17 "$svnrepo"/stunk "$svnrepo"/flunked; } &&
	but svn init --minimize-url -i stunk "$svnrepo"/stunk &&
	but svn fetch -i stunk &&
	but update-ref refs/remotes/flunk@18 refs/remotes/stunk~2 &&
	but update-ref -d refs/remotes/stunk &&
	but config --unset svn-remote.svn.fetch stunk &&
	mkdir -p "$GIT_DIR"/svn/refs/remotes/flunk@18 &&
	rev_map=$(cd "$GIT_DIR"/svn/refs/remotes/stunk && ls .rev_map*) &&
	dd if="$GIT_DIR"/svn/refs/remotes/stunk/$rev_map \
	   of="$GIT_DIR"/svn/refs/remotes/flunk@18/$rev_map bs=$record_size count=1 &&
	rm -rf "$GIT_DIR"/svn/refs/remotes/stunk &&
	but svn init --minimize-url -i flunk "$svnrepo"/flunk &&
	but svn fetch -i flunk &&
	but svn init --minimize-url -i stunk "$svnrepo"/stunk &&
	but svn fetch -i stunk &&
	but svn init --minimize-url -i flunked "$svnrepo"/flunked &&
	but svn fetch -i flunked &&
	test "$(but rev-parse --verify refs/remotes/flunk@18)" \
	   = "$(but rev-parse --verify refs/remotes/stunk)" &&
	test "$(but rev-parse --verify refs/remotes/flunk~1)" \
	   = "$(but rev-parse --verify refs/remotes/stunk)" &&
	test "$(but rev-parse --verify refs/remotes/flunked~1)" \
	   = "$(but rev-parse --verify refs/remotes/stunk~1)"
	'

test_expect_success "track multi-parent paths" '
	svn_cmd cp -m "resurrect /glob" "$svnrepo"/r9270 "$svnrepo"/glob &&
	but svn multi-fetch &&
	but cat-file cummit refs/remotes/glob >actual &&
	grep "^parent " actual >actual2 &&
	test_line_count = 2 actual2
	'

test_expect_success "multi-fetch continues to work" "
	but svn multi-fetch
	"

test_expect_success "multi-fetch works off a 'clean' repository" '
	rm -rf "$GIT_DIR/svn" &&
	but for-each-ref --format="option no-deref%0adelete %(refname)" refs/remotes |
	but update-ref --stdin &&
	but reflog expire --all --expire=all &&
	mkdir "$GIT_DIR/svn" &&
	but svn multi-fetch
	'

test_debug 'butk --all &'

test_done
