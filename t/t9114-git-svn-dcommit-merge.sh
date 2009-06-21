#!/bin/sh
#
# Copyright (c) 2007 Eric Wong
# Based on a script by Joakim Tjernlund <joakim.tjernlund@transmode.se>

test_description='git svn dcommit handles merges'

. ./lib-git-svn.sh

big_text_block () {
cat << EOF
#
# (C) Copyright 2000 - 2005
# Wolfgang Denk, DENX Software Engineering, wd@denx.de.
#
# See file CREDITS for list of people who contributed to this
# project.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License as
# published by the Free Software Foundation; either version 2 of
# the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston,
# MA 02111-1307 USA
#
EOF
}

test_expect_success 'setup svn repository' '
	svn_cmd co "$svnrepo" mysvnwork &&
	mkdir -p mysvnwork/trunk &&
	cd mysvnwork &&
		big_text_block >> trunk/README &&
		svn_cmd add trunk &&
		svn_cmd ci -m "first commit" trunk &&
		cd ..
	'

test_expect_success 'setup git mirror and merge' '
	git svn init "$svnrepo" -t tags -T trunk -b branches &&
	git svn fetch &&
	git checkout --track -b svn remotes/trunk &&
	git checkout -b merge &&
	echo new file > new_file &&
	git add new_file &&
	git commit -a -m "New file" &&
	echo hello >> README &&
	git commit -a -m "hello" &&
	echo add some stuff >> new_file &&
	git commit -a -m "add some stuff" &&
	git checkout svn &&
	mv -f README tmp &&
	echo friend > README &&
	cat tmp >> README &&
	git commit -a -m "friend" &&
	git pull . merge
	'

test_debug 'gitk --all & sleep 1'

test_expect_success 'verify pre-merge ancestry' "
	test x\`git rev-parse --verify refs/heads/svn^2\` = \
	     x\`git rev-parse --verify refs/heads/merge\` &&
	git cat-file commit refs/heads/svn^ | grep '^friend$'
	"

test_expect_success 'git svn dcommit merges' "
	git svn dcommit
	"

test_debug 'gitk --all & sleep 1'

test_expect_success 'verify post-merge ancestry' "
	test x\`git rev-parse --verify refs/heads/svn\` = \
	     x\`git rev-parse --verify refs/remotes/trunk \` &&
	test x\`git rev-parse --verify refs/heads/svn^2\` = \
	     x\`git rev-parse --verify refs/heads/merge\` &&
	git cat-file commit refs/heads/svn^ | grep '^friend$'
	"

test_expect_success 'verify merge commit message' "
	git rev-list --pretty=raw -1 refs/heads/svn | \
	  grep \"    Merge branch 'merge' into svn\"
	"

test_done
