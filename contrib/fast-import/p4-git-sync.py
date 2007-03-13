#!/usr/bin/python
#
# p4-git-sync.py
#
# Author: Simon Hausmann <hausmann@kde.org>
# License: MIT <http://www.opensource.org/licenses/mit-license.php>
#

import os, string, shelve, stat
import getopt, sys, marshal

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
                                                   "submit-log-subst=", "log-substitutions=" ])
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
    return
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

#    print "Cleaning index..."
#    system("git checkout -f")

def prepareLogMessage(template, message):
    result = ""

    substs = logSubstitutions
    for k in substs.keys():
        substs[k] = substs[k].replace("%log%", message)

    for line in template.split("\n"):
        if line.startswith("#"):
            result += line + "\n"
            continue

        substituted = False
        for key in substs.keys():
            if line.find(key) != -1:
                value = substs[key]
                if value != "@remove@":
                    result += line.replace(key, value) + "\n"
                substituted = True
                break

        if not substituted:
            result += line + "\n"

    return result

def apply(id):
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
        log = log[:-1]
        if not foundTitle:
            if len(log) == 0:
                foundTitle = 1
            continue

        if len(logMessage) > 0:
            logMessage += "\t"
        logMessage += log + "\n"

    template = os.popen("p4 change -o").read()
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

if len(commits) > 0:
    firstTime = False
    commit = commits[0]
    commits = commits[1:]
    config["commits"] = commits
    apply(commit)

config.close()

if len(commits) == 0:
    if firstTime:
        print "No changes found to apply between %s and current HEAD" % origin
    else:
        print "All changes applied!"
    os.remove(configFile)

