#!/usr/bin/python
#
# p4-debug.py
#
# Author: Simon Hausmann <hausmann@kde.org>
# License: MIT <http://www.opensource.org/licenses/mit-license.php>
#
# removes unused p4 import tags
#
import os, string, sys
import popen2, getopt

branch = "refs/heads/master"

try:
    opts, args = getopt.getopt(sys.argv[1:], "", [ "branch=" ])
except getopt.GetoptError:
    print "fixme, syntax error"
    sys.exit(1)

for o, a in opts:
    if o == "--branch":
        branch = "refs/heads/" + a

sout, sin, serr = popen2.popen3("git-name-rev --tags `git-rev-parse %s`" % branch)
output = sout.read()
tagIdx = output.index(" tags/p4/")
try:
    caretIdx = output.index("^")
except:
    caretIdx = len(output) - 1
rev = int(output[tagIdx + 9 : caretIdx])

allTags = os.popen("git tag -l p4/").readlines()
for i in range(len(allTags)):
    allTags[i] = int(allTags[i][3:-1])

allTags.sort()

allTags.remove(rev)

for rev in allTags:
    print os.popen("git tag -d p4/%s" % rev).read()
