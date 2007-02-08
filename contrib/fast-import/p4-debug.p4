#!/usr/bin/python
#
# p4-debug.py
#
# Author: Simon Hausmann <hausmann@kde.org>
# License: MIT <http://www.opensource.org/licenses/mit-license.php>
#
# executes a p4 command with -G and prints the resulting python dicts
#
import os, string, sys
import marshal, popen2

cmd = ""
for arg in sys.argv[1:]:
    cmd += arg + " "

pipe = os.popen("p4 -G %s" % cmd, "rb")
try:
    while True:
        entry = marshal.load(pipe)
        print entry
except EOFError:
    pass
pipe.close()

