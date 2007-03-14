#!/usr/bin/python
#
# p4-git-sync.py
#
# Author: Simon Hausmann <hausmann@kde.org>
# Copyright: 2007 Simon Hausmann <hausmann@kde.org>
#            2007 Trolltech ASA
# License: MIT <http://www.opensource.org/licenses/mit-license.php>
#

import os, string, shelve, stat
import getopt, sys, marshal, tempfile

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

try:
    opts, args = getopt.getopt(sys.argv[1:], "", [ "continue", "git-dir=", "origin=", "reset", "master=",
                                                   "submit-log-subst=", "log-substitutions=", "interactive",
                                                   "dry-run" ])
except getopt.GetoptError:
    print "fixme, syntax error"
    sys.exit(1)

logSubstitutions = {}
logSubstitutions["<enter description here>"] = "%log%"
logSubstitutions["\tDetails:"] = "\tDetails:  %log%"
gitdir = os.environ.get("GIT_DIR", "")
origin = "origin"
master = "master"
firstTime = True
reset = False
interactive = False
dryRun = False

for o, a in opts:
    if o == "--git-dir":
        gitdir = a
    elif o == "--origin":
        origin = a
    elif o == "--master":
        master = a
    elif o == "--continue":
        firstTime = False
    elif o == "--reset":
        reset = True
        firstTime = True
    elif o == "--submit-log-subst":
        key = a.split("%")[0]
        value = a.split("%")[1]
        logSubstitutions[key] = value
    elif o == "--log-substitutions":
        for line in open(a, "r").readlines():
            tokens = line[:-1].split("=")
            logSubstitutions[tokens[0]] = tokens[1]
    elif o == "--interactive":
        interactive = True
    elif o == "--dry-run":
        dryRun = True

if len(gitdir) == 0:
    gitdir = ".git"
else:
    os.environ["GIT_DIR"] = gitdir

configFile = gitdir + "/p4-git-sync.cfg"

origin = "origin"
if len(args) == 1:
    origin = args[0]

def die(msg):
    sys.stderr.write(msg + "\n")
    sys.exit(1)

def system(cmd):
    if os.system(cmd) != 0:
        die("command failed: %s" % cmd)

def check():
    if len(p4CmdList("opened ...")) > 0:
        die("You have files opened with perforce! Close them before starting the sync.")

def start(config):
    if len(config) > 0 and not reset:
        die("Cannot start sync. Previous sync config found at %s" % configFile)

    #if len(os.popen("git-update-index --refresh").read()) > 0:
    #    die("Your working tree is not clean. Check with git status!")

    commits = []
    for line in os.popen("git-rev-list --no-merges %s..%s" % (origin, master)).readlines():
        commits.append(line[:-1])
    commits.reverse()

    config["commits"] = commits

    print "Creating temporary p4-sync branch from %s ..." % origin
    system("git checkout -f -b p4-sync %s" % origin)

#    print "Cleaning index..."
#    system("git checkout -f")

def prepareLogMessage(template, message):
    result = ""

    for line in template.split("\n"):
        if line.startswith("#"):
            result += line + "\n"
            continue

        substituted = False
        for key in logSubstitutions.keys():
            if line.find(key) != -1:
                value = logSubstitutions[key]
                value = value.replace("%log%", message)
                if value != "@remove@":
                    result += line.replace(key, value) + "\n"
                substituted = True
                break

        if not substituted:
            result += line + "\n"

    return result

def apply(id):
    global interactive
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
    #system("git format-patch --stdout -k \"%s^\"..\"%s\" | git-am -k" % (id, id))
    #system("git branch -D tmp")
    #system("git checkout -f -b tmp \"%s^\"" % id)

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

    if interactive:
        submitTemplate = prepareLogMessage(template, logMessage)
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
           if dryRun:
               print submitTemplate
               raw_input("Press return to continue...")
           else:
                pipe = os.popen("p4 submit -i", "w")
                pipe.write(submitTemplate)
                pipe.close()
        else:
            print "Not submitting!"
            interactive = False
    else:
        fileName = "submit.txt"
        file = open(fileName, "w+")
        file.write(prepareLogMessage(template, logMessage))
        file.close()
        print "Perforce submit template written as %s. Please review/edit and then use p4 submit -i < %s to submit directly!" % (fileName, fileName)

check()

config = shelve.open(configFile, writeback=True)

if firstTime:
    start(config)

commits = config.get("commits", [])

while len(commits) > 0:
    firstTime = False
    commit = commits[0]
    commits = commits[1:]
    config["commits"] = commits
    apply(commit)
    if not interactive:
        break

config.close()

if len(commits) == 0:
    if firstTime:
        print "No changes found to apply between %s and current HEAD" % origin
    else:
        print "All changes applied!"
        print "Deleting temporary p4-sync branch and going back to %s" % master
        system("git checkout %s" % master)
        system("git branch -D p4-sync")
        print "Cleaning out your perforce checkout by doing p4 edit ... ; p4 revert -a ..."
        system("p4 edit ...")
        system("p4 revert -a ...")
    os.remove(configFile)

