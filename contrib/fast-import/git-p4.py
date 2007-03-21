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
import tempfile, getopt, sha, os.path, time
from sets import Set;

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

class Command:
    def __init__(self):
        self.usage = "usage: %prog [options]"

class P4Debug(Command):
    def __init__(self):
        self.options = [
        ]
        self.description = "A tool to debug the output of p4 -G."

    def run(self, args):
        for output in p4CmdList(" ".join(args)):
            print output
        return True

class P4CleanTags(Command):
    def __init__(self):
        Command.__init__(self)
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
        return True

class P4Sync(Command):
    def __init__(self):
        Command.__init__(self)
        self.options = [
                optparse.make_option("--continue", action="store_false", dest="firstTime"),
                optparse.make_option("--origin", dest="origin"),
                optparse.make_option("--reset", action="store_true", dest="reset"),
                optparse.make_option("--master", dest="master"),
                optparse.make_option("--log-substitutions", dest="substFile"),
                optparse.make_option("--noninteractive", action="store_false"),
                optparse.make_option("--dry-run", action="store_true"),
                optparse.make_option("--apply-as-patch", action="store_true", dest="applyAsPatch")
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
        self.applyAsPatch = False

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

        if not self.applyAsPatch:
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

        if self.applyAsPatch:
            system("git-diff-tree -p \"%s^\" \"%s\" | patch -p1" % (id, id))
        else:
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
                if not self.applyAsPatch:
                    print "Deleting temporary p4-sync branch and going back to %s" % self.master
                    system("git checkout %s" % self.master)
                    system("git branch -D p4-sync")
                    print "Cleaning out your perforce checkout by doing p4 edit ... ; p4 revert ..."
                    system("p4 edit ... >/dev/null")
                    system("p4 revert ... >/dev/null")
            os.remove(self.configFile)

        return True

class GitSync(Command):
    def __init__(self):
        Command.__init__(self)
        self.options = [
                optparse.make_option("--branch", dest="branch"),
                optparse.make_option("--detect-branches", dest="detectBranches", action="store_true"),
                optparse.make_option("--changesfile", dest="changesFile"),
                optparse.make_option("--silent", dest="silent", action="store_true"),
                optparse.make_option("--known-branches", dest="knownBranches"),
                optparse.make_option("--cache", dest="doCache", action="store_true"),
                optparse.make_option("--command-cache", dest="commandCache", action="store_true")
        ]
        self.description = """Imports from Perforce into a git repository.\n
    example:
    //depot/my/project/ -- to import the current head
    //depot/my/project/@all -- to import everything
    //depot/my/project/@1,6 -- to import only from revision 1 to 6

    (a ... is not needed in the path p4 specification, it's added implicitly)"""

        self.usage += " //depot/path[@revRange]"

        self.dataCache = False
        self.commandCache = False
        self.silent = False
        self.knownBranches = Set()
        self.createdBranches = Set()
        self.committedChanges = Set()
        self.branch = "master"
        self.detectBranches = False
        self.changesFile = ""

    def p4File(self, depotPath):
        return os.popen("p4 print -q \"%s\"" % depotPath, "rb").read()

    def extractFilesFromCommit(self, commit):
        files = []
        fnum = 0
        while commit.has_key("depotFile%s" % fnum):
            path =  commit["depotFile%s" % fnum]
            if not path.startswith(self.globalPrefix):
    #            if not self.silent:
    #                print "\nchanged files: ignoring path %s outside of %s in change %s" % (path, self.globalPrefix, change)
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

    def isSubPathOf(self, first, second):
        if not first.startswith(second):
            return False
        if first == second:
            return True
        return first[len(second)] == "/"

    def branchesForCommit(self, files):
        branches = Set()

        for file in files:
            relativePath = file["path"][len(self.globalPrefix):]
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
                if self.isSubPathOf(relativePath, branch):
                    knownBranch = True
                    break
    #            if branch.startswith(relativePath):
                if self.isSubPathOf(branch, relativePath):
                    branches.remove(branch)
                    break

            if knownBranch:
                continue

            for branch in knownBranches:
                #if relativePath.startswith(branch):
                if self.isSubPathOf(relativePath, branch):
                    if len(branches) == 0:
                        relativePath = branch
                    else:
                        knownBranch = True
                    break

            if knownBranch:
                continue

            branches.add(relativePath)
            self.knownBranches.add(relativePath)

        return branches

    def findBranchParent(self, branchPrefix, files):
        for file in files:
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

            relPath = source[len(self.globalPrefix):]
            # strip off the filename
            relPath = relPath[0:relPath.rfind("/")]

            for branch in self.knownBranches:
                if self.isSubPathOf(relPath, branch):
    #                print "determined parent branch branch %s due to change in file %s" % (branch, source)
                    return branch
    #            else:
    #                print "%s is not a subpath of branch %s" % (relPath, branch)

        return ""

    def commit(self, details, files, branch, branchPrefix, parent = "", merged = ""):
        epoch = details["time"]
        author = details["user"]

        self.gitStream.write("commit %s\n" % branch)
    #    gitStream.write("mark :%s\n" % details["change"])
        self.committedChanges.add(int(details["change"]))
        committer = ""
        if author in self.users:
            committer = "%s %s %s" % (self.users[author], epoch, self.tz)
        else:
            committer = "%s <a@b> %s %s" % (author, epoch, self.tz)

        self.gitStream.write("committer %s\n" % committer)

        self.gitStream.write("data <<EOT\n")
        self.gitStream.write(details["desc"])
        self.gitStream.write("\n[ imported from %s; change %s ]\n" % (branchPrefix, details["change"]))
        self.gitStream.write("EOT\n\n")

        if len(parent) > 0:
            self.gitStream.write("from %s\n" % parent)

        if len(merged) > 0:
            self.gitStream.write("merge %s\n" % merged)

        for file in files:
            path = file["path"]
            if not path.startswith(branchPrefix):
    #            if not silent:
    #                print "\nchanged files: ignoring path %s outside of branch prefix %s in change %s" % (path, branchPrefix, details["change"])
                continue
            rev = file["rev"]
            depotPath = path + "#" + rev
            relPath = path[len(branchPrefix):]
            action = file["action"]

            if file["type"] == "apple":
                print "\nfile %s is a strange apple file that forks. Ignoring!" % path
                continue

            if action == "delete":
                self.gitStream.write("D %s\n" % relPath)
            else:
                mode = 644
                if file["type"].startswith("x"):
                    mode = 755

                data = self.p4File(depotPath)

                self.gitStream.write("M %s inline %s\n" % (mode, relPath))
                self.gitStream.write("data %s\n" % len(data))
                self.gitStream.write(data)
                self.gitStream.write("\n")

        self.gitStream.write("\n")

        self.lastChange = int(details["change"])

    def extractFilesInCommitToBranch(self, files, branchPrefix):
        newFiles = []

        for file in files:
            path = file["path"]
            if path.startswith(branchPrefix):
                newFiles.append(file)

        return newFiles

    def findBranchSourceHeuristic(self, files, branch, branchPrefix):
        for file in files:
            action = file["action"]
            if action != "integrate" and action != "branch":
                continue
            path = file["path"]
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

            relPath = source[len(self.globalPrefix):]
            # strip off the filename
            relPath = relPath[0:relPath.rfind("/")]

            for candidate in self.knownBranches:
                if self.isSubPathOf(relPath, candidate) and candidate != branch:
                    return candidate

        return ""

    def changeIsBranchMerge(self, sourceBranch, destinationBranch, change):
        sourceFiles = {}
        for file in p4CmdList("files %s...@%s" % (self.globalPrefix + sourceBranch + "/", change)):
            if file["action"] == "delete":
                continue
            sourceFiles[file["depotFile"]] = file

        destinationFiles = {}
        for file in p4CmdList("files %s...@%s" % (self.globalPrefix + destinationBranch + "/", change)):
            destinationFiles[file["depotFile"]] = file

        for fileName in sourceFiles.keys():
            integrations = []
            deleted = False
            integrationCount = 0
            for integration in p4CmdList("integrated \"%s\"" % fileName):
                toFile = integration["fromFile"] # yes, it's true, it's fromFile
                if not toFile in destinationFiles:
                    continue
                destFile = destinationFiles[toFile]
                if destFile["action"] == "delete":
    #                print "file %s has been deleted in %s" % (fileName, toFile)
                    deleted = True
                    break
                integrationCount += 1
                if integration["how"] == "branch from":
                    continue

                if int(integration["change"]) == change:
                    integrations.append(integration)
                    continue
                if int(integration["change"]) > change:
                    continue

                destRev = int(destFile["rev"])

                startRev = integration["startFromRev"][1:]
                if startRev == "none":
                    startRev = 0
                else:
                    startRev = int(startRev)

                endRev = integration["endFromRev"][1:]
                if endRev == "none":
                    endRev = 0
                else:
                    endRev = int(endRev)

                initialBranch = (destRev == 1 and integration["how"] != "branch into")
                inRange = (destRev >= startRev and destRev <= endRev)
                newer = (destRev > startRev and destRev > endRev)

                if initialBranch or inRange or newer:
                    integrations.append(integration)

            if deleted:
                continue

            if len(integrations) == 0 and integrationCount > 1:
                print "file %s was not integrated from %s into %s" % (fileName, sourceBranch, destinationBranch)
                return False

        return True

    def getUserMap(self):
        self.users = {}

        for output in p4CmdList("users"):
            if not output.has_key("User"):
                continue
            self.users[output["User"]] = output["FullName"] + " <" + output["Email"] + ">"

    def run(self, args):
        self.branch = "refs/heads/" + self.branch
        self.globalPrefix = self.previousDepotPath = os.popen("git-repo-config --get p4.depotpath").read()
        if len(self.globalPrefix) != 0:
            self.globalPrefix = self.globalPrefix[:-1]

        if len(args) == 0 and len(self.globalPrefix) != 0:
            if not self.silent:
                print "[using previously specified depot path %s]" % self.globalPrefix
        elif len(args) != 1:
            return False
        else:
            if len(self.globalPrefix) != 0 and self.globalPrefix != args[0]:
                print "previous import used depot path %s and now %s was specified. this doesn't work!" % (self.globalPrefix, args[0])
                sys.exit(1)
            self.globalPrefix = args[0]

        self.changeRange = ""
        self.revision = ""
        self.users = {}
        self.initialParent = ""
        self.lastChange = 0
        self.initialTag = ""

        if self.globalPrefix.find("@") != -1:
            atIdx = self.globalPrefix.index("@")
            self.changeRange = self.globalPrefix[atIdx:]
            if self.changeRange == "@all":
                self.changeRange = ""
            elif self.changeRange.find(",") == -1:
                self.revision = self.changeRange
                self.changeRange = ""
            self.globalPrefix = self.globalPrefix[0:atIdx]
        elif self.globalPrefix.find("#") != -1:
            hashIdx = self.globalPrefix.index("#")
            self.revision = self.globalPrefix[hashIdx:]
            self.globalPrefix = self.globalPrefix[0:hashIdx]
        elif len(self.previousDepotPath) == 0:
            self.revision = "#head"

        if self.globalPrefix.endswith("..."):
            self.globalPrefix = self.globalPrefix[:-3]

        if not self.globalPrefix.endswith("/"):
            self.globalPrefix += "/"

        self.getUserMap()

        if len(self.changeRange) == 0:
            try:
                sout, sin, serr = popen2.popen3("git-name-rev --tags `git-rev-parse %s`" % self.branch)
                output = sout.read()
                if output.endswith("\n"):
                    output = output[:-1]
                tagIdx = output.index(" tags/p4/")
                caretIdx = output.find("^")
                endPos = len(output)
                if caretIdx != -1:
                    endPos = caretIdx
                self.rev = int(output[tagIdx + 9 : endPos]) + 1
                self.changeRange = "@%s,#head" % self.rev
                self.initialParent = os.popen("git-rev-parse %s" % self.branch).read()[:-1]
                self.initialTag = "p4/%s" % (int(self.rev) - 1)
            except:
                pass

        self.tz = - time.timezone / 36
        tzsign = ("%s" % self.tz)[0]
        if tzsign != '+' and tzsign != '-':
            self.tz = "+" + ("%s" % self.tz)

        self.gitOutput, self.gitStream, self.gitError = popen2.popen3("git-fast-import")

        if len(self.revision) > 0:
            print "Doing initial import of %s from revision %s" % (self.globalPrefix, self.revision)

            details = { "user" : "git perforce import user", "time" : int(time.time()) }
            details["desc"] = "Initial import of %s from the state at revision %s" % (self.globalPrefix, self.revision)
            details["change"] = self.revision
            newestRevision = 0

            fileCnt = 0
            for info in p4CmdList("files %s...%s" % (self.globalPrefix, self.revision)):
                change = int(info["change"])
                if change > newestRevision:
                    newestRevision = change

                if info["action"] == "delete":
                    fileCnt = fileCnt + 1
                    continue

                for prop in [ "depotFile", "rev", "action", "type" ]:
                    details["%s%s" % (prop, fileCnt)] = info[prop]

                fileCnt = fileCnt + 1

            details["change"] = newestRevision

            try:
                self.commit(details, self.extractFilesFromCommit(details), self.branch, self.globalPrefix)
            except IOError:
                print self.gitError.read()

        else:
            changes = []

            if len(self.changesFile) > 0:
                output = open(self.changesFile).readlines()
                changeSet = Set()
                for line in output:
                    changeSet.add(int(line))

                for change in changeSet:
                    changes.append(change)

                changes.sort()
            else:
                output = os.popen("p4 changes %s...%s" % (self.globalPrefix, self.changeRange)).readlines()

                for line in output:
                    changeNum = line.split(" ")[1]
                    changes.append(changeNum)

                changes.reverse()

            if len(changes) == 0:
                if not self.silent:
                    print "no changes to import!"
                sys.exit(1)

            cnt = 1
            for change in changes:
                description = p4Cmd("describe %s" % change)

                if not self.silent:
                    sys.stdout.write("\rimporting revision %s (%s%%)" % (change, cnt * 100 / len(changes)))
                    sys.stdout.flush()
                cnt = cnt + 1

                try:
                    files = self.extractFilesFromCommit(description)
                    if self.detectBranches:
                        for branch in self.branchesForCommit(files):
                            self.knownBranches.add(branch)
                            branchPrefix = self.globalPrefix + branch + "/"

                            filesForCommit = self.extractFilesInCommitToBranch(files, branchPrefix)

                            merged = ""
                            parent = ""
                            ########### remove cnt!!!
                            if branch not in self.createdBranches and cnt > 2:
                                self.createdBranches.add(branch)
                                parent = self.findBranchParent(branchPrefix, files)
                                if parent == branch:
                                    parent = ""
            #                    elif len(parent) > 0:
            #                        print "%s branched off of %s" % (branch, parent)

                            if len(parent) == 0:
                                merged = self.findBranchSourceHeuristic(filesForCommit, branch, branchPrefix)
                                if len(merged) > 0:
                                    print "change %s could be a merge from %s into %s" % (description["change"], merged, branch)
                                    if not self.changeIsBranchMerge(merged, branch, int(description["change"])):
                                        merged = ""

                            branch = "refs/heads/" + branch
                            if len(parent) > 0:
                                parent = "refs/heads/" + parent
                            if len(merged) > 0:
                                merged = "refs/heads/" + merged
                            self.commit(description, files, branch, branchPrefix, parent, merged)
                    else:
                        self.commit(description, files, self.branch, self.globalPrefix, self.initialParent)
                        self.initialParent = ""
                except IOError:
                    print self.gitError.read()
                    sys.exit(1)

        if not self.silent:
            print ""

        self.gitStream.write("reset refs/tags/p4/%s\n" % self.lastChange)
        self.gitStream.write("from %s\n\n" % self.branch);


        self.gitStream.close()
        self.gitOutput.close()
        self.gitError.close()

        os.popen("git-repo-config p4.depotpath %s" % self.globalPrefix).read()
        if len(self.initialTag) > 0:
            os.popen("git tag -d %s" % self.initialTag).read()

        return True

class HelpFormatter(optparse.IndentedHelpFormatter):
    def __init__(self):
        optparse.IndentedHelpFormatter.__init__(self)

    def format_description(self, description):
        if description:
            return description + "\n"
        else:
            return ""

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
    "submit" : P4Sync(),
    "sync" : GitSync()
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

parser = optparse.OptionParser(cmd.usage.replace("%prog", "%prog " + cmdName),
                               options,
                               description = cmd.description,
                               formatter = HelpFormatter())

(cmd, args) = parser.parse_args(sys.argv[2:], cmd);

gitdir = cmd.gitdir
if len(gitdir) == 0:
    gitdir = ".git"

if not isValidGitDir(gitdir):
    if isValidGitDir(gitdir + "/.git"):
        gitdir += "/.git"
    else:
        die("fatal: cannot locate git repository at %s" % gitdir)

os.environ["GIT_DIR"] = gitdir

if not cmd.run(args):
    parser.print_help()

