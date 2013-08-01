#!/usr/bin/env python

""" hg-to-git.py - A Mercurial to GIT converter

    Copyright (C)2007 Stelian Pop <stelian@popies.net>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2, or (at your option)
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
"""

import os, os.path, sys
import tempfile, pickle, getopt
import re

if sys.hexversion < 0x02030000:
   # The behavior of the pickle module changed significantly in 2.3
   sys.stderr.write("hg-to-git.py: requires Python 2.3 or later.\n")
   sys.exit(1)

# Maps hg version -> git version
hgvers = {}
# List of children for each hg revision
hgchildren = {}
# List of parents for each hg revision
hgparents = {}
# Current branch for each hg revision
hgbranch = {}
# Number of new changesets converted from hg
hgnewcsets = 0

#------------------------------------------------------------------------------

def usage():

        print """\
%s: [OPTIONS] <hgprj>

options:
    -s, --gitstate=FILE: name of the state to be saved/read
                         for incrementals
    -n, --nrepack=INT:   number of changesets that will trigger
                         a repack (default=0, -1 to deactivate)
    -v, --verbose:       be verbose

required:
    hgprj:  name of the HG project to import (directory)
""" % sys.argv[0]

#------------------------------------------------------------------------------

def getgitenv(user, date):
    env = ''
    elems = re.compile('(.*?)\s+<(.*)>').match(user)
    if elems:
        env += 'export GIT_AUTHOR_NAME="%s" ;' % elems.group(1)
        env += 'export GIT_COMMITTER_NAME="%s" ;' % elems.group(1)
        env += 'export GIT_AUTHOR_EMAIL="%s" ;' % elems.group(2)
        env += 'export GIT_COMMITTER_EMAIL="%s" ;' % elems.group(2)
    else:
        env += 'export GIT_AUTHOR_NAME="%s" ;' % user
        env += 'export GIT_COMMITTER_NAME="%s" ;' % user
        env += 'export GIT_AUTHOR_EMAIL= ;'
        env += 'export GIT_COMMITTER_EMAIL= ;'

    env += 'export GIT_AUTHOR_DATE="%s" ;' % date
    env += 'export GIT_COMMITTER_DATE="%s" ;' % date
    return env

#------------------------------------------------------------------------------

state = ''
opt_nrepack = 0
verbose = False

try:
    opts, args = getopt.getopt(sys.argv[1:], 's:t:n:v', ['gitstate=', 'tempdir=', 'nrepack=', 'verbose'])
    for o, a in opts:
        if o in ('-s', '--gitstate'):
            state = a
            state = os.path.abspath(state)
        if o in ('-n', '--nrepack'):
            opt_nrepack = int(a)
        if o in ('-v', '--verbose'):
            verbose = True
    if len(args) != 1:
        raise Exception('params')
except:
    usage()
    sys.exit(1)

hgprj = args[0]
os.chdir(hgprj)

if state:
    if os.path.exists(state):
        if verbose:
            print 'State does exist, reading'
        f = open(state, 'r')
        hgvers = pickle.load(f)
    else:
        print 'State does not exist, first run'

sock = os.popen('hg tip --template "{rev}"')
tip = sock.read()
if sock.close():
    sys.exit(1)
if verbose:
    print 'tip is', tip

# Calculate the branches
if verbose:
    print 'analysing the branches...'
hgchildren["0"] = ()
hgparents["0"] = (None, None)
hgbranch["0"] = "master"
for cset in range(1, int(tip) + 1):
    hgchildren[str(cset)] = ()
    prnts = os.popen('hg log -r %d --template "{parents}"' % cset).read().strip().split(' ')
    prnts = map(lambda x: x[:x.find(':')], prnts)
    if prnts[0] != '':
        parent = prnts[0].strip()
    else:
        parent = str(cset - 1)
    hgchildren[parent] += ( str(cset), )
    if len(prnts) > 1:
        mparent = prnts[1].strip()
        hgchildren[mparent] += ( str(cset), )
    else:
        mparent = None

    hgparents[str(cset)] = (parent, mparent)

    if mparent:
        # For merge changesets, take either one, preferably the 'master' branch
        if hgbranch[mparent] == 'master':
            hgbranch[str(cset)] = 'master'
        else:
            hgbranch[str(cset)] = hgbranch[parent]
    else:
        # Normal changesets
        # For first children, take the parent branch, for the others create a new branch
        if hgchildren[parent][0] == str(cset):
            hgbranch[str(cset)] = hgbranch[parent]
        else:
            hgbranch[str(cset)] = "branch-" + str(cset)

if not hgvers.has_key("0"):
    print 'creating repository'
    os.system('git init')

# loop through every hg changeset
for cset in range(int(tip) + 1):

    # incremental, already seen
    if hgvers.has_key(str(cset)):
        continue
    hgnewcsets += 1

    # get info
    log_data = os.popen('hg log -r %d --template "{tags}\n{date|date}\n{author}\n"' % cset).readlines()
    tag = log_data[0].strip()
    date = log_data[1].strip()
    user = log_data[2].strip()
    parent = hgparents[str(cset)][0]
    mparent = hgparents[str(cset)][1]

    #get comment
    (fdcomment, filecomment) = tempfile.mkstemp()
    csetcomment = os.popen('hg log -r %d --template "{desc}"' % cset).read().strip()
    os.write(fdcomment, csetcomment)
    os.close(fdcomment)

    print '-----------------------------------------'
    print 'cset:', cset
    print 'branch:', hgbranch[str(cset)]
    print 'user:', user
    print 'date:', date
    print 'comment:', csetcomment
    if parent:
	print 'parent:', parent
    if mparent:
        print 'mparent:', mparent
    if tag:
        print 'tag:', tag
    print '-----------------------------------------'

    # checkout the parent if necessary
    if cset != 0:
        if hgbranch[str(cset)] == "branch-" + str(cset):
            print 'creating new branch', hgbranch[str(cset)]
            os.system('git checkout -b %s %s' % (hgbranch[str(cset)], hgvers[parent]))
        else:
            print 'checking out branch', hgbranch[str(cset)]
            os.system('git checkout %s' % hgbranch[str(cset)])

    # merge
    if mparent:
        if hgbranch[parent] == hgbranch[str(cset)]:
            otherbranch = hgbranch[mparent]
        else:
            otherbranch = hgbranch[parent]
        print 'merging', otherbranch, 'into', hgbranch[str(cset)]
        os.system(getgitenv(user, date) + 'git merge --no-commit -s ours "" %s %s' % (hgbranch[str(cset)], otherbranch))

    # remove everything except .git and .hg directories
    os.system('find . \( -path "./.hg" -o -path "./.git" \) -prune -o ! -name "." -print | xargs rm -rf')

    # repopulate with checkouted files
    os.system('hg update -C %d' % cset)

    # add new files
    os.system('git ls-files -x .hg --others | git update-index --add --stdin')
    # delete removed files
    os.system('git ls-files -x .hg --deleted | git update-index --remove --stdin')

    # commit
    os.system(getgitenv(user, date) + 'git commit --allow-empty --allow-empty-message -a -F %s' % filecomment)
    os.unlink(filecomment)

    # tag
    if tag and tag != 'tip':
        os.system(getgitenv(user, date) + 'git tag %s' % tag)

    # delete branch if not used anymore...
    if mparent and len(hgchildren[str(cset)]):
        print "Deleting unused branch:", otherbranch
        os.system('git branch -d %s' % otherbranch)

    # retrieve and record the version
    vvv = os.popen('git show --quiet --pretty=format:%H').read()
    print 'record', cset, '->', vvv
    hgvers[str(cset)] = vvv

if hgnewcsets >= opt_nrepack and opt_nrepack != -1:
    os.system('git repack -a -d')

# write the state for incrementals
if state:
    if verbose:
        print 'Writing state'
    f = open(state, 'w')
    pickle.dump(hgvers, f)

# vim: et ts=8 sw=4 sts=4
