#!/usr/bin/env python
#
# git-p4.py -- A tool for bidirectional operation between a Perforce depot and git.
#
# Author: Simon Hausmann <hausmann@kde.org>
# Copyright: 2007 Simon Hausmann <hausmann@kde.org>
#            2007 Trolltech ASA
# License: MIT <http://www.opensource.org/licenses/mit-license.php>
#

import optparse, sys, os, marshal, popen2, shelve
import tempfile

gitdir = os.environ.get("GIT_DIR", "")

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

def isValidGitDir(path):
    if os.path.exists(path + "/HEAD") and os.path.exists(path + "/refs") and os.path.exists(path + "/objects"):
        return True;
    return False

def system(cmd):
    if os.system(cmd) != 0:
        die("command failed: %s" % cmd)

class P4Debug:
    def __init__(self):
        self.options = [
        ]
        self.description = "A tool to debug the output of p4 -G."

    def run(self, args):
        for output in p4CmdList(" ".join(args)):
            print output

class P4CleanTags:
    def __init__(self):
        self.options = [
#                optparse.make_option("--branch", dest="branch", default="refs/heads/master")
        ]
        self.description = "A tool to remove stale unused tags from incremental perforce imports."
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

class P4Sync:
    def __init__(self):
        self.options = [
                optparse.make_option("--continue", action="store_false", dest="firstTime"),
                optparse.make_option("--origin", dest="origin"),
                optparse.make_option("--reset", action="store_true", dest="reset"),
                optparse.make_option("--master", dest="master"),
                optparse.make_option("--log-substitutions", dest="substFile"),
                optparse.make_option("--noninteractive", action="store_false"),
                optparse.make_option("--dry-run", action="store_true")
        ]
        self.description = "Submit changes from git to the perforce depot."
        self.firstTime = True
        self.reset = False
        self.interactive = True
        self.dryRun = False
        self.substFile = ""
        self.firstTime = True
        self.origin = "origin"
        self.master = ""

        self.logSubstitutions = {}
        self.logSubstitutions["<enter description here>"] = "%log%"
        self.logSubstitutions["\tDetails:"] = "\tDetails:  %log%"

    def check(self):
        if len(p4CmdList("opened ...")) > 0:
            die("You have files opened with perforce! Close them before starting the sync.")

    def start(self):
        if len(self.config) > 0 and not self.reset:
            die("Cannot start sync. Previous sync config found at %s" % self.configFile)

        commits = []
        for line in os.popen("git-rev-list --no-merges %s..%s" % (self.origin, self.master)).readlines():
            commits.append(line[:-1])
        commits.reverse()

        self.config["commits"] = commits

        print "Creating temporary p4-sync branch from %s ..." % self.origin
        system("git checkout -f -b p4-sync %s" % self.origin)

    def prepareLogMessage(self, template, message):
        result = ""

        for line in template.split("\n"):
            if line.startswith("#"):
                result += line + "\n"
                continue

            substituted = False
            for key in self.logSubstitutions.keys():
                if line.find(key) != -1:
                    value = self.logSubstitutions[key]
                    value = value.replace("%log%", message)
                    if value != "@remove@":
                        result += line.replace(key, value) + "\n"
                    substituted = True
                    break

            if not substituted:
                result += line + "\n"

        return result

    def apply(self, id):
        print "Applying %s" % (os.popen("git-log --max-count=1 --pretty=oneline %s" % id).read())
        diff = os.popen("git diff-tree -r --name-status \"%s^\" \"%s\"" % (id, id)).readlines()
        filesToAdd = set()
        filesToDelete = set()
        for line in diff:
            modifier = line[0]
            path = line[1:].strip()
            if modifier == "M":
                system("p4 edit %s" % path)
            elif modifier == "A":
                filesToAdd.add(path)
                if path in filesToDelete:
                    filesToDelete.remove(path)
            elif modifier == "D":
                filesToDelete.add(path)
                if path in filesToAdd:
                    filesToAdd.remove(path)
            else:
                die("unknown modifier %s for %s" % (modifier, path))

        system("git-diff-files --name-only -z | git-update-index --remove -z --stdin")
        system("git cherry-pick --no-commit \"%s\"" % id)

        for f in filesToAdd:
            system("p4 add %s" % f)
        for f in filesToDelete:
            system("p4 revert %s" % f)
            system("p4 delete %s" % f)

        logMessage = ""
        foundTitle = False
        for log in os.popen("git-cat-file commit %s" % id).readlines():
            if not foundTitle:
                if len(log) == 1:
                    foundTitle = 1
                continue

            if len(logMessage) > 0:
                logMessage += "\t"
            logMessage += log

        template = os.popen("p4 change -o").read()

        if self.interactive:
            submitTemplate = self.prepareLogMessage(template, logMessage)
            diff = os.popen("p4 diff -du ...").read()

            for newFile in filesToAdd:
                diff += "==== new file ====\n"
                diff += "--- /dev/null\n"
                diff += "+++ %s\n" % newFile
                f = open(newFile, "r")
                for line in f.readlines():
                    diff += "+" + line
                f.close()

            pipe = os.popen("less", "w")
            pipe.write(submitTemplate + diff)
            pipe.close()

            response = "e"
            while response == "e":
                response = raw_input("Do you want to submit this change (y/e/n)? ")
                if response == "e":
                    [handle, fileName] = tempfile.mkstemp()
                    tmpFile = os.fdopen(handle, "w+")
                    tmpFile.write(submitTemplate)
                    tmpFile.close()
                    editor = os.environ.get("EDITOR", "vi")
                    system(editor + " " + fileName)
                    tmpFile = open(fileName, "r")
                    submitTemplate = tmpFile.read()
                    tmpFile.close()
                    os.remove(fileName)

            if response == "y" or response == "yes":
               if self.dryRun:
                   print submitTemplate
                   raw_input("Press return to continue...")
               else:
                    pipe = os.popen("p4 submit -i", "w")
                    pipe.write(submitTemplate)
                    pipe.close()
            else:
                print "Not submitting!"
                self.interactive = False
        else:
            fileName = "submit.txt"
            file = open(fileName, "w+")
            file.write(self.prepareLogMessage(template, logMessage))
            file.close()
            print "Perforce submit template written as %s. Please review/edit and then use p4 submit -i < %s to submit directly!" % (fileName, fileName)

    def run(self, args):
        if self.reset:
            self.firstTime = True

        if len(self.substFile) > 0:
            for line in open(self.substFile, "r").readlines():
                tokens = line[:-1].split("=")
                self.logSubstitutions[tokens[0]] = tokens[1]

        if len(self.master) == 0:
            self.master = currentGitBranch()
            if len(self.master) == 0 or not os.path.exists("%s/refs/heads/%s" % (gitdir, self.master)):
                die("Detecting current git branch failed!")

        self.check()
        self.configFile = gitdir + "/p4-git-sync.cfg"
        self.config = shelve.open(self.configFile, writeback=True)

        if self.firstTime:
            self.start()

        commits = self.config.get("commits", [])

        while len(commits) > 0:
            self.firstTime = False
            commit = commits[0]
            commits = commits[1:]
            self.config["commits"] = commits
            self.apply(commit)
            if not self.interactive:
                break

        self.config.close()

        if len(commits) == 0:
            if self.firstTime:
                print "No changes found to apply between %s and current HEAD" % self.origin
            else:
                print "All changes applied!"
                print "Deleting temporary p4-sync branch and going back to %s" % self.master
                system("git checkout %s" % self.master)
                system("git branch -D p4-sync")
                print "Cleaning out your perforce checkout by doing p4 edit ... ; p4 revert ..."
                system("p4 edit ... >/dev/null")
                system("p4 revert ... >/dev/null")
            os.remove(self.configFile)


def printUsage(commands):
    print "usage: %s <command> [options]" % sys.argv[0]
    print ""
    print "valid commands: %s" % ", ".join(commands)
    print ""
    print "Try %s <command> --help for command specific help." % sys.argv[0]
    print ""

commands = {
    "debug" : P4Debug(),
    "clean-tags" : P4CleanTags(),
    "sync-to-perforce" : P4Sync()
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

options = cmd.options
cmd.gitdir = gitdir
options.append(optparse.make_option("--git-dir", dest="gitdir"))

parser = optparse.OptionParser("usage: %prog " + cmdName + " [options]", options,
                               description = cmd.description)

(cmd, args) = parser.parse_args(sys.argv[2:], cmd);

gitdir = cmd.gitdir
if len(gitdir) == 0:
    gitdir = ".git"

if not isValidGitDir(gitdir):
    if isValidGitDir(gitdir + "/.git"):
        gitdir += "/.git"
    else:
        dir("fatal: cannot locate git repository at %s" % gitdir)

os.environ["GIT_DIR"] = gitdir

cmd.run(args)
