#!/usr/bin/python
#
# p4-fast-export.py
#
# Author: Simon Hausmann <hausmann@kde.org>
# License: MIT <http://www.opensource.org/licenses/mit-license.php>
#
# TODO:
#       - support integrations (at least p4i)
#       - support p4 submit (hah!)
#       - emulate p4's delete behavior: if a directory becomes empty delete it. continue
#         with parent dir until non-empty dir is found.
#
import os, string, sys, time
import marshal, popen2, getopt
from sets import Set;

silent = False
knownBranches = Set()
committedChanges = Set()
branch = "refs/heads/master"
globalPrefix = previousDepotPath = os.popen("git-repo-config --get p4.depotpath").read()
detectBranches = False
changesFile = ""
if len(globalPrefix) != 0:
    globalPrefix = globalPrefix[:-1]

try:
    opts, args = getopt.getopt(sys.argv[1:], "", [ "branch=", "detect-branches", "changesfile=", "silent" ])
except getopt.GetoptError:
    print "fixme, syntax error"
    sys.exit(1)

for o, a in opts:
    if o == "--branch":
        branch = "refs/heads/" + a
    elif o == "--detect-branches":
        detectBranches = True
    elif o == "--changesfile":
        changesFile = a
    elif o == "--silent":
        silent= True

if len(args) == 0 and len(globalPrefix) != 0:
    print "[using previously specified depot path %s]" % globalPrefix
elif len(args) != 1:
    print "usage: %s //depot/path[@revRange]" % sys.argv[0]
    print "\n    example:"
    print "    %s //depot/my/project/ -- to import the current head"
    print "    %s //depot/my/project/@all -- to import everything"
    print "    %s //depot/my/project/@1,6 -- to import only from revision 1 to 6"
    print ""
    print "    (a ... is not needed in the path p4 specification, it's added implicitly)"
    print ""
    sys.exit(1)
else:
    if len(globalPrefix) != 0 and globalPrefix != args[0]:
        print "previous import used depot path %s and now %s was specified. this doesn't work!" % (globalPrefix, args[0])
        sys.exit(1)
    globalPrefix = args[0]

changeRange = ""
revision = ""
users = {}
initialParent = ""
lastChange = 0
initialTag = ""

if globalPrefix.find("@") != -1:
    atIdx = globalPrefix.index("@")
    changeRange = globalPrefix[atIdx:]
    if changeRange == "@all":
        changeRange = ""
    elif changeRange.find(",") == -1:
        revision = changeRange
        changeRange = ""
    globalPrefix = globalPrefix[0:atIdx]
elif globalPrefix.find("#") != -1:
    hashIdx = globalPrefix.index("#")
    revision = globalPrefix[hashIdx:]
    globalPrefix = globalPrefix[0:hashIdx]
elif len(previousDepotPath) == 0:
    revision = "#head"

if globalPrefix.endswith("..."):
    globalPrefix = globalPrefix[:-3]

if not globalPrefix.endswith("/"):
    globalPrefix += "/"

def p4CmdList(cmd):
    pipe = os.popen("p4 -G %s" % cmd, "rb")
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

def extractFilesFromCommit(commit):
    files = []
    fnum = 0
    while commit.has_key("depotFile%s" % fnum):
        path =  commit["depotFile%s" % fnum]
        if not path.startswith(globalPrefix):
            if not silent:
                print "\nchanged files: ignoring path %s outside of %s in change %s" % (path, globalPrefix, change)
            fnum = fnum + 1
            continue

        file = {}
        file["path"] = path
        file["rev"] = commit["rev%s" % fnum]
        file["action"] = commit["action%s" % fnum]
        file["type"] = commit["type%s" % fnum]
        files.append(file)
        fnum = fnum + 1
    return files

def isSubPathOf(first, second):
    if not first.startswith(second):
        return False
    if first == second:
        return True
    return first[len(second)] == "/"

def branchesForCommit(files):
    global knownBranches
    branches = Set()

    for file in files:
        relativePath = file["path"][len(globalPrefix):]
        # strip off the filename
        relativePath = relativePath[0:relativePath.rfind("/")]

#        if len(branches) == 0:
#            branches.add(relativePath)
#            knownBranches.add(relativePath)
#            continue

        ###### this needs more testing :)
        knownBranch = False
        for branch in branches:
            if relativePath == branch:
                knownBranch = True
                break
#            if relativePath.startswith(branch):
            if isSubPathOf(relativePath, branch):
                knownBranch = True
                break
#            if branch.startswith(relativePath):
            if isSubPathOf(branch, relativePath):
                branches.remove(branch)
                break

        if knownBranch:
            continue

        for branch in knownBranches:
            #if relativePath.startswith(branch):
            if isSubPathOf(relativePath, branch):
                if len(branches) == 0:
                    relativePath = branch
                else:
                    knownBranch = True
                break

        if knownBranch:
            continue

        branches.add(relativePath)
        knownBranches.add(relativePath)

    return branches

def commit(details, files, branch, branchPrefix):
    global initialParent
    global users
    global lastChange
    global committedChanges

    epoch = details["time"]
    author = details["user"]

    gitStream.write("commit %s\n" % branch)
    gitStream.write("mark :%s\n" % details["change"])
    committedChanges.add(int(details["change"]))
    committer = ""
    if author in users:
        committer = "%s %s %s" % (users[author], epoch, tz)
    else:
        committer = "%s <a@b> %s %s" % (author, epoch, tz)

    gitStream.write("committer %s\n" % committer)

    gitStream.write("data <<EOT\n")
    gitStream.write(details["desc"])
    gitStream.write("\n[ imported from %s; change %s ]\n" % (branchPrefix, details["change"]))
    gitStream.write("EOT\n\n")

    if len(initialParent) > 0:
        gitStream.write("from %s\n" % initialParent)
        initialParent = ""

    #mergedBranches = Set()
    merges = Set()

    for file in files:
        if lastChange == 0 or not detectBranches:
            continue
        path = file["path"]
        if not path.startswith(branchPrefix):
            continue
        action = file["action"]
        if action != "integrate" and action != "branch":
            continue
        rev = file["rev"]
        depotPath = path + "#" + rev

        log = p4CmdList("filelog \"%s\"" % depotPath)
        if len(log) != 1:
            print "eek! I got confused by the filelog of %s" % depotPath
            sys.exit(1);

        log = log[0]
        if log["action0"] != action:
            print "eek! wrong action in filelog for %s : found %s, expected %s" % (depotPath, log["action0"], action)
            sys.exit(1);

        branchAction = log["how0,0"]
#        if branchAction == "branch into" or branchAction == "ignored":
#            continue # ignore for branching

        if not branchAction.endswith(" from"):
            continue # ignore for branching
#            print "eek! file %s was not branched from but instead: %s" % (depotPath, branchAction)
#            sys.exit(1);

        source = log["file0,0"]
        if source.startswith(branchPrefix):
            continue

        lastSourceRev = log["erev0,0"]

        sourceLog = p4CmdList("filelog -m 1 \"%s%s\"" % (source, lastSourceRev))
        if len(sourceLog) != 1:
            print "eek! I got confused by the source filelog of %s%s" % (source, lastSourceRev)
            sys.exit(1);
        sourceLog = sourceLog[0]

        change = int(sourceLog["change0"])
        merges.add(change)

#        relPath = source[len(globalPrefix):]
#
#        for branch in knownBranches:
#            if relPath.startswith(branch) and branch not in mergedBranches:
#                gitStream.write("merge refs/heads/%s\n" % branch)
#                mergedBranches.add(branch)
#                break

    for merge in merges:
        if merge in committedChanges:
            gitStream.write("merge :%s\n" % merge)

    for file in files:
        path = file["path"]
        if not path.startswith(branchPrefix):
            if not silent:
                print "\nchanged files: ignoring path %s outside of branch prefix %s in change %s" % (path, branchPrefix, details["change"])
            continue
        rev = file["rev"]
        depotPath = path + "#" + rev
        relPath = path[len(branchPrefix):]
        action = file["action"]

        if action == "delete":
            gitStream.write("D %s\n" % relPath)
        else:
            mode = 644
            if file["type"].startswith("x"):
                mode = 755

            data = os.popen("p4 print -q \"%s\"" % depotPath, "rb").read()

            gitStream.write("M %s inline %s\n" % (mode, relPath))
            gitStream.write("data %s\n" % len(data))
            gitStream.write(data)
            gitStream.write("\n")

    gitStream.write("\n")

    lastChange = int(details["change"])

def getUserMap():
    users = {}

    for output in p4CmdList("users"):
        if not output.has_key("User"):
            continue
        users[output["User"]] = output["FullName"] + " <" + output["Email"] + ">"
    return users

users = getUserMap()

if len(changeRange) == 0:
    try:
        sout, sin, serr = popen2.popen3("git-name-rev --tags `git-rev-parse %s`" % branch)
        output = sout.read()
        if output.endswith("\n"):
            output = output[:-1]
        tagIdx = output.index(" tags/p4/")
        caretIdx = output.find("^")
        endPos = len(output)
        if caretIdx != -1:
            endPos = caretIdx
        rev = int(output[tagIdx + 9 : endPos]) + 1
        changeRange = "@%s,#head" % rev
        initialParent = os.popen("git-rev-parse %s" % branch).read()[:-1]
        initialTag = "p4/%s" % (int(rev) - 1)
    except:
        pass

sys.stderr.write("\n")

tz = - time.timezone / 36
tzsign = ("%s" % tz)[0]
if tzsign != '+' and tzsign != '-':
    tz = "+" + ("%s" % tz)

gitOutput, gitStream, gitError = popen2.popen3("git-fast-import")

if len(revision) > 0:
    print "Doing initial import of %s from revision %s" % (globalPrefix, revision)

    details = { "user" : "git perforce import user", "time" : int(time.time()) }
    details["desc"] = "Initial import of %s from the state at revision %s" % (globalPrefix, revision)
    details["change"] = revision
    newestRevision = 0

    fileCnt = 0
    for info in p4CmdList("files %s...%s" % (globalPrefix, revision)):
        change = int(info["change"])
        if change > newestRevision:
            newestRevision = change

        if info["action"] == "delete":
            continue

        for prop in [ "depotFile", "rev", "action", "type" ]:
            details["%s%s" % (prop, fileCnt)] = info[prop]

        fileCnt = fileCnt + 1

    details["change"] = newestRevision

    try:
        commit(details, extractFilesFromCommit(details), branch, globalPrefix)
    except:
        print gitError.read()

else:
    changes = []

    if len(changesFile) > 0:
        output = open(changesFile).readlines()
        changeSet = Set()
        for line in output:
            changeSet.add(int(line))

        for change in changeSet:
            changes.append(change)

        changes.sort()
    else:
        output = os.popen("p4 changes %s...%s" % (globalPrefix, changeRange)).readlines()

        for line in output:
            changeNum = line.split(" ")[1]
            changes.append(changeNum)

        changes.reverse()

    if len(changes) == 0:
        if not silent:
            print "no changes to import!"
        sys.exit(1)

    cnt = 1
    for change in changes:
        description = p4Cmd("describe %s" % change)

        if not silent:
            sys.stdout.write("\rimporting revision %s (%s%%)" % (change, cnt * 100 / len(changes)))
            sys.stdout.flush()
        cnt = cnt + 1

#        try:
        files = extractFilesFromCommit(description)
        if detectBranches:
            for branch in branchesForCommit(files):
                knownBranches.add(branch)
                branchPrefix = globalPrefix + branch + "/"
                branch = "refs/heads/" + branch
                commit(description, files, branch, branchPrefix)
        else:
            commit(description, files, branch, globalPrefix)
#        except:
#            print gitError.read()
#            sys.exit(1)

if not silent:
    print ""

gitStream.write("reset refs/tags/p4/%s\n" % lastChange)
gitStream.write("from %s\n\n" % branch);


gitStream.close()
gitOutput.close()
gitError.close()

os.popen("git-repo-config p4.depotpath %s" % globalPrefix).read()
if len(initialTag) > 0:
    os.popen("git tag -d %s" % initialTag).read()

sys.exit(0)
