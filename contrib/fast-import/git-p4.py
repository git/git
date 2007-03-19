#!/usr/bin/env python
#
# git-p4.py -- A tool for bidirectional operation between a Perforce depot and git.
#
# Author: Simon Hausmann <hausmann@kde.org>
# License: MIT <http://www.opensource.org/licenses/mit-license.php>
#

import optparse, sys, os, marshal, popen2

def p4CmdList(cmd):
    cmd = "p4 -G %s" % cmd
    pipe = os.popen(cmd, "rb")

    result = []
    try:
        while True:
            entry = marshal.load(pipe)
            result.append(entry)
    except EOFError:
        pass
    pipe.close()

    return result

def p4Cmd(cmd):
    list = p4CmdList(cmd)
    result = {}
    for entry in list:
        result.update(entry)
    return result;

def die(msg):
    sys.stderr.write(msg + "\n")
    sys.exit(1)

def currentGitBranch():
    return os.popen("git-name-rev HEAD").read().split(" ")[1][:-1]

class P4Debug:
    def __init__(self):
        self.options = [
        ]

    def run(self, args):
        for output in p4CmdList(" ".join(args)):
            print output

class P4CleanTags:
    def __init__(self):
        self.options = [
#                optparse.make_option("--branch", dest="branch", default="refs/heads/master")
        ]
    def run(self, args):
        branch = currentGitBranch()
        print "Cleaning out stale p4 import tags..."
        sout, sin, serr = popen2.popen3("git-name-rev --tags `git-rev-parse %s`" % branch)
        output = sout.read()
        try:
            tagIdx = output.index(" tags/p4/")
        except:
            print "Cannot find any p4/* tag. Nothing to do."
            sys.exit(0)

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

        print "%s tags removed." % len(allTags)

def printUsage(commands):
    print "usage: %s <command> [options]" % sys.argv[0]
    print ""
    print "valid commands: %s" % ", ".join(commands)
    print ""
    print "Try %s <command> --help for command specific help." % sys.argv[0]
    print ""

commands = {
    "debug" : P4Debug(),
    "clean-tags" : P4CleanTags()
}

if len(sys.argv[1:]) == 0:
    printUsage(commands.keys())
    sys.exit(2)

cmd = ""
cmdName = sys.argv[1]
try:
    cmd = commands[cmdName]
except KeyError:
    print "unknown command %s" % cmdName
    print ""
    printUsage(commands.keys())
    sys.exit(2)

parser = optparse.OptionParser("usage: %prog " + cmdName + " [options]", cmd.options)

(cmd, args) = parser.parse_args(sys.argv[2:], cmd);

cmd.run(args)
