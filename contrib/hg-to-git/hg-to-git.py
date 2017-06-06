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

import os, os.path, sys, shutil
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
# Tags for each hg revision
hgtags = {}
# Authors for each hg revision
hgauthor = {}
# Dates for each hg revision
hgdate = {}
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

authorpattern = re.compile('(.*?)\s+<(.*)>')
def setgitenv(author, date):
    elems = authorpattern.match(author)
    if elems:
        os.environ['GIT_AUTHOR_NAME'] = elems.group(1)
        os.environ['GIT_COMMITTER_NAME'] = elems.group(1)
        os.environ['GIT_AUTHOR_EMAIL'] = elems.group(2)
        os.environ['GIT_COMMITTER_EMAIL'] = elems.group(2)
    else:
        os.environ['GIT_AUTHOR_NAME'] = author
        os.environ['GIT_COMMITTER_NAME'] = author
        os.environ['GIT_AUTHOR_EMAIL'] = author + '@example.com'
        os.environ['GIT_COMMITTER_EMAIL'] = author + '@example.com'

    os.environ['GIT_AUTHOR_DATE'] = date
    os.environ['GIT_COMMITTER_DATE'] = date

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
    print 'analyzing the branches...'

# Read all revs' details in at once.
for line in os.popen('hg log --template "{rev}\\0{date|isodatesec}\\0{author}\\0{branch}\\0{tags}\\0{parents}\\n"').read().split('\n'):
    if line == '':
        continue

    linesplit = line.split('\0')
    cset = linesplit[0]
    date = linesplit[1]
    author = linesplit[2]
    branch = linesplit[3]
    tags = linesplit[4].strip()
    parents = linesplit[5].strip()

    if parents == '':
        rev = int(cset)
        parents = [ str(rev - 1) ] if rev > 0 else []
    else:
        parents = parents.split(' ')
        parents = map(lambda x: x[:x.find(':')], parents)

    hgbranch[cset] = branch
    hgdate[cset] = date
    hgauthor[cset] = author
    hgtags[cset] = [t for t in tags.split(' ') if t != 'tip' and t != '']

    if not cset in hgchildren:
        hgchildren[cset] = []
    hgparents[cset] = []

    for p in parents:
        if not p in hgchildren:
            hgchildren[p] = []
        hgchildren[p] += [ cset ]
        hgparents[cset] +=  [ p ]

if not hgvers.has_key("0"):
    print 'creating repository'
    os.system('git init')

# loop through every hg changeset
for rev in range(int(tip) + 1):
    cset = str(rev)

    # incremental, already seen
    if hgvers.has_key(cset):
        continue
    hgnewcsets += 1

    # get info
    tags = hgtags[cset]
    date = hgdate[cset]
    author = hgauthor[cset]
    parents = hgparents[cset]

    #get comment
    (fdcomment, filecomment) = tempfile.mkstemp()
    csetcomment = os.popen('hg log -r %d --template "{desc}"' % rev).read()
    os.write(fdcomment, csetcomment)
    os.close(fdcomment)

    print '-----------------------------------------'
    print 'cset:', cset
    print 'branch:', hgbranch[cset]
    print 'author:', author
    print 'date:', date
    print 'comment:', csetcomment
    for p in parents:
        print 'parent:', p
    for t in tags:
        print 'tag:', t
    print '-----------------------------------------'

    # set head to the first parent
    if rev != 0:
        if verbose:
            print 'checking out branch', hgbranch[cset]
        os.system('git checkout -f %s' % hgvers[parents[0]])

    # merge
    if len(parents) > 1:
        if verbose:
            print 'merging', [hgbranch[p] for p in parents]
        vers = [hgvers[p] for p in parents]
        del vers[0]
        os.system('git merge --no-commit -s ours "" %s' % (" ".join(vers)))

    # remove everything except .git and .hg directories
    if verbose:
        print 'cleaning out working directory'
    for f in os.listdir("."):
        if os.path.isfile(f):
            os.remove(f)
        elif f != ".hg" and f != ".git":
            shutil.rmtree(f)

    # repopulate with checkouted files
    if verbose:
        print 'updating working directory to r%d' % rev
    os.system('hg update -C %d' % rev)

    # add new files and delete removed files
    if verbose:
        print 'updating git index to match working directory'
    os.system('git ls-files -x .hg --others | git -c core.autocrlf=false update-index --add --stdin')
    os.system('git ls-files -x .hg --deleted | git -c core.autocrlf=false update-index --remove --stdin')

    # commit
    if verbose:
        print 'committing'
    setgitenv(author, date)
    os.system('git -c core.autocrlf=false commit%s --allow-empty --allow-empty-message -a -F "%s"' % (' --quiet' if not verbose else '', filecomment))
    os.unlink(filecomment)

    # tag
    for tag in tags:
        os.system('git tag %s' % tag)

    # retrieve and record the version
    vvv = os.popen('git show --quiet --pretty=format:%H').read()
    print 'record', cset, '->', vvv
    hgvers[cset] = vvv
    os.system('git branch -f %s %s' % (hgbranch[cset], vvv))

if hgnewcsets >= opt_nrepack and opt_nrepack != -1:
    if verbose:
        print 'repacking git repo'
    os.system('git repack -a -d')

# write the state for incrementals
if state:
    if verbose:
        print 'Writing state'
    f = open(state, 'w')
    pickle.dump(hgvers, f)

# vim: et ts=8 sw=4 sts=4
