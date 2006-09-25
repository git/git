#!/usr/bin/python
#
# Copyright (C) 2005 Fredrik Kuivinen
#

import sys
sys.path.append('''@@GIT_PYTHON_PATH@@''')

import math, random, os, re, signal, tempfile, stat, errno, traceback
from heapq import heappush, heappop
from sets import Set

from gitMergeCommon import *

outputIndent = 0
def output(*args):
    sys.stdout.write('  '*outputIndent)
    printList(args)

originalIndexFile = os.environ.get('GIT_INDEX_FILE',
                                   os.environ.get('GIT_DIR', '.git') + '/index')
temporaryIndexFile = os.environ.get('GIT_DIR', '.git') + \
                     '/merge-recursive-tmp-index'
def setupIndex(temporary):
    try:
        os.unlink(temporaryIndexFile)
    except OSError:
        pass
    if temporary:
        newIndex = temporaryIndexFile
    else:
        newIndex = originalIndexFile
    os.environ['GIT_INDEX_FILE'] = newIndex

# This is a global variable which is used in a number of places but
# only written to in the 'merge' function.

# cacheOnly == True  => Don't leave any non-stage 0 entries in the cache and
#                       don't update the working directory.
#              False => Leave unmerged entries in the cache and update
#                       the working directory.

cacheOnly = False

# The entry point to the merge code
# ---------------------------------

def merge(h1, h2, branch1Name, branch2Name, graph, callDepth=0, ancestor=None):
    '''Merge the commits h1 and h2, return the resulting virtual
    commit object and a flag indicating the cleanness of the merge.'''
    assert(isinstance(h1, Commit) and isinstance(h2, Commit))

    global outputIndent

    output('Merging:')
    output(h1)
    output(h2)
    sys.stdout.flush()

    if ancestor:
        ca = [ancestor]
    else:
        assert(isinstance(graph, Graph))
        ca = getCommonAncestors(graph, h1, h2)
    output('found', len(ca), 'common ancestor(s):')
    for x in ca:
        output(x)
    sys.stdout.flush()

    mergedCA = ca[0]
    for h in ca[1:]:
        outputIndent = callDepth+1
        [mergedCA, dummy] = merge(mergedCA, h,
                                  'Temporary merge branch 1',
                                  'Temporary merge branch 2',
                                  graph, callDepth+1)
        outputIndent = callDepth
        assert(isinstance(mergedCA, Commit))

    global cacheOnly
    if callDepth == 0:
        setupIndex(False)
        cacheOnly = False
    else:
        setupIndex(True)
        runProgram(['git-read-tree', h1.tree()])
        cacheOnly = True

    [shaRes, clean] = mergeTrees(h1.tree(), h2.tree(), mergedCA.tree(),
                                 branch1Name, branch2Name)

    if graph and (clean or cacheOnly):
        res = Commit(None, [h1, h2], tree=shaRes)
        graph.addNode(res)
    else:
        res = None

    return [res, clean]

getFilesRE = re.compile(r'^([0-7]+) (\S+) ([0-9a-f]{40})\t(.*)$', re.S)
def getFilesAndDirs(tree):
    files = Set()
    dirs = Set()
    out = runProgram(['git-ls-tree', '-r', '-z', '-t', tree])
    for l in out.split('\0'):
        m = getFilesRE.match(l)
        if m:
            if m.group(2) == 'tree':
                dirs.add(m.group(4))
            elif m.group(2) == 'blob':
                files.add(m.group(4))

    return [files, dirs]

# Those two global variables are used in a number of places but only
# written to in 'mergeTrees' and 'uniquePath'. They keep track of
# every file and directory in the two branches that are about to be
# merged.
currentFileSet = None
currentDirectorySet = None

def mergeTrees(head, merge, common, branch1Name, branch2Name):
    '''Merge the trees 'head' and 'merge' with the common ancestor
    'common'. The name of the head branch is 'branch1Name' and the name of
    the merge branch is 'branch2Name'. Return a tuple (tree, cleanMerge)
    where tree is the resulting tree and cleanMerge is True iff the
    merge was clean.'''
    
    assert(isSha(head) and isSha(merge) and isSha(common))

    if common == merge:
        output('Already uptodate!')
        return [head, True]

    if cacheOnly:
        updateArg = '-i'
    else:
        updateArg = '-u'

    [out, code] = runProgram(['git-read-tree', updateArg, '-m',
                                common, head, merge], returnCode = True)
    if code != 0:
        die('git-read-tree:', out)

    [tree, code] = runProgram('git-write-tree', returnCode=True)
    tree = tree.rstrip()
    if code != 0:
        global currentFileSet, currentDirectorySet
        [currentFileSet, currentDirectorySet] = getFilesAndDirs(head)
        [filesM, dirsM] = getFilesAndDirs(merge)
        currentFileSet.union_update(filesM)
        currentDirectorySet.union_update(dirsM)

        entries = unmergedCacheEntries()
        renamesHead =  getRenames(head, common, head, merge, entries)
        renamesMerge = getRenames(merge, common, head, merge, entries)

        cleanMerge = processRenames(renamesHead, renamesMerge,
                                    branch1Name, branch2Name)
        for entry in entries:
            if entry.processed:
                continue
            if not processEntry(entry, branch1Name, branch2Name):
                cleanMerge = False
                
        if cleanMerge or cacheOnly:
            tree = runProgram('git-write-tree').rstrip()
        else:
            tree = None
    else:
        cleanMerge = True

    return [tree, cleanMerge]

# Low level file merging, update and removal
# ------------------------------------------

def mergeFile(oPath, oSha, oMode, aPath, aSha, aMode, bPath, bSha, bMode,
              branch1Name, branch2Name):

    merge = False
    clean = True

    if stat.S_IFMT(aMode) != stat.S_IFMT(bMode):
        clean = False
        if stat.S_ISREG(aMode):
            mode = aMode
            sha = aSha
        else:
            mode = bMode
            sha = bSha
    else:
        if aSha != oSha and bSha != oSha:
            merge = True

        if aMode == oMode:
            mode = bMode
        else:
            mode = aMode

        if aSha == oSha:
            sha = bSha
        elif bSha == oSha:
            sha = aSha
        elif stat.S_ISREG(aMode):
            assert(stat.S_ISREG(bMode))

            orig = runProgram(['git-unpack-file', oSha]).rstrip()
            src1 = runProgram(['git-unpack-file', aSha]).rstrip()
            src2 = runProgram(['git-unpack-file', bSha]).rstrip()
            try:
                [out, code] = runProgram(['merge',
                                          '-L', branch1Name + '/' + aPath,
                                          '-L', 'orig/' + oPath,
                                          '-L', branch2Name + '/' + bPath,
                                          src1, orig, src2], returnCode=True)
            except ProgramError, e:
                print >>sys.stderr, e
                die("Failed to execute 'merge'. merge(1) is used as the "
                    "file-level merge tool. Is 'merge' in your path?")

            sha = runProgram(['git-hash-object', '-t', 'blob', '-w',
                              src1]).rstrip()

            os.unlink(orig)
            os.unlink(src1)
            os.unlink(src2)

            clean = (code == 0)
        else:
            assert(stat.S_ISLNK(aMode) and stat.S_ISLNK(bMode))
            sha = aSha

            if aSha != bSha:
                clean = False

    return [sha, mode, clean, merge]

def updateFile(clean, sha, mode, path):
    updateCache = cacheOnly or clean
    updateWd = not cacheOnly

    return updateFileExt(sha, mode, path, updateCache, updateWd)

def updateFileExt(sha, mode, path, updateCache, updateWd):
    if cacheOnly:
        updateWd = False

    if updateWd:
        pathComponents = path.split('/')
        for x in xrange(1, len(pathComponents)):
            p = '/'.join(pathComponents[0:x])

            try:
                createDir = not stat.S_ISDIR(os.lstat(p).st_mode)
            except OSError:
                createDir = True
            
            if createDir:
                try:
                    os.mkdir(p)
                except OSError, e:
                    die("Couldn't create directory", p, e.strerror)

        prog = ['git-cat-file', 'blob', sha]
        if stat.S_ISREG(mode):
            try:
                os.unlink(path)
            except OSError:
                pass
            if mode & 0100:
                mode = 0777
            else:
                mode = 0666
            fd = os.open(path, os.O_WRONLY | os.O_TRUNC | os.O_CREAT, mode)
            proc = subprocess.Popen(prog, stdout=fd)
            proc.wait()
            os.close(fd)
        elif stat.S_ISLNK(mode):
            linkTarget = runProgram(prog)
            os.symlink(linkTarget, path)
        else:
            assert(False)

    if updateWd and updateCache:
        runProgram(['git-update-index', '--add', '--', path])
    elif updateCache:
        runProgram(['git-update-index', '--add', '--cacheinfo',
                    '0%o' % mode, sha, path])

def setIndexStages(path,
                   oSHA1, oMode,
                   aSHA1, aMode,
                   bSHA1, bMode,
                   clear=True):
    istring = []
    if clear:
        istring.append("0 " + ("0" * 40) + "\t" + path + "\0")
    if oMode:
        istring.append("%o %s %d\t%s\0" % (oMode, oSHA1, 1, path))
    if aMode:
        istring.append("%o %s %d\t%s\0" % (aMode, aSHA1, 2, path))
    if bMode:
        istring.append("%o %s %d\t%s\0" % (bMode, bSHA1, 3, path))

    runProgram(['git-update-index', '-z', '--index-info'],
               input="".join(istring))

def removeFile(clean, path):
    updateCache = cacheOnly or clean
    updateWd = not cacheOnly

    if updateCache:
        runProgram(['git-update-index', '--force-remove', '--', path])

    if updateWd:
        try:
            os.unlink(path)
        except OSError, e:
            if e.errno != errno.ENOENT and e.errno != errno.EISDIR:
                raise
        try:
            os.removedirs(os.path.dirname(path))
        except OSError:
            pass

def uniquePath(path, branch):
    def fileExists(path):
        try:
            os.lstat(path)
            return True
        except OSError, e:
            if e.errno == errno.ENOENT:
                return False
            else:
                raise

    branch = branch.replace('/', '_')
    newPath = path + '~' + branch
    suffix = 0
    while newPath in currentFileSet or \
          newPath in currentDirectorySet  or \
          fileExists(newPath):
        suffix += 1
        newPath = path + '~' + branch + '_' + str(suffix)
    currentFileSet.add(newPath)
    return newPath

# Cache entry management
# ----------------------

class CacheEntry:
    def __init__(self, path):
        class Stage:
            def __init__(self):
                self.sha1 = None
                self.mode = None

            # Used for debugging only
            def __str__(self):
                if self.mode != None:
                    m = '0%o' % self.mode
                else:
                    m = 'None'

                if self.sha1:
                    sha1 = self.sha1
                else:
                    sha1 = 'None'
                return 'sha1: ' + sha1 + ' mode: ' + m
        
        self.stages = [Stage(), Stage(), Stage(), Stage()]
        self.path = path
        self.processed = False

    def __str__(self):
        return 'path: ' + self.path + ' stages: ' + repr([str(x) for x in self.stages])

class CacheEntryContainer:
    def __init__(self):
        self.entries = {}

    def add(self, entry):
        self.entries[entry.path] = entry

    def get(self, path):
        return self.entries.get(path)

    def __iter__(self):
        return self.entries.itervalues()
    
unmergedRE = re.compile(r'^([0-7]+) ([0-9a-f]{40}) ([1-3])\t(.*)$', re.S)
def unmergedCacheEntries():
    '''Create a dictionary mapping file names to CacheEntry
    objects. The dictionary contains one entry for every path with a
    non-zero stage entry.'''

    lines = runProgram(['git-ls-files', '-z', '--unmerged']).split('\0')
    lines.pop()

    res = CacheEntryContainer()
    for l in lines:
        m = unmergedRE.match(l)
        if m:
            mode = int(m.group(1), 8)
            sha1 = m.group(2)
            stage = int(m.group(3))
            path = m.group(4)

            e = res.get(path)
            if not e:
                e = CacheEntry(path)
                res.add(e)

            e.stages[stage].mode = mode
            e.stages[stage].sha1 = sha1
        else:
            die('Error: Merge program failed: Unexpected output from',
                'git-ls-files:', l)
    return res

lsTreeRE = re.compile(r'^([0-7]+) (\S+) ([0-9a-f]{40})\t(.*)\n$', re.S)
def getCacheEntry(path, origTree, aTree, bTree):
    '''Returns a CacheEntry object which doesn't have to correspond to
    a real cache entry in Git's index.'''
    
    def parse(out):
        if out == '':
            return [None, None]
        else:
            m = lsTreeRE.match(out)
            if not m:
                die('Unexpected output from git-ls-tree:', out)
            elif m.group(2) == 'blob':
                return [m.group(3), int(m.group(1), 8)]
            else:
                return [None, None]

    res = CacheEntry(path)

    [oSha, oMode] = parse(runProgram(['git-ls-tree', origTree, '--', path]))
    [aSha, aMode] = parse(runProgram(['git-ls-tree', aTree, '--', path]))
    [bSha, bMode] = parse(runProgram(['git-ls-tree', bTree, '--', path]))

    res.stages[1].sha1 = oSha
    res.stages[1].mode = oMode
    res.stages[2].sha1 = aSha
    res.stages[2].mode = aMode
    res.stages[3].sha1 = bSha
    res.stages[3].mode = bMode

    return res

# Rename detection and handling
# -----------------------------

class RenameEntry:
    def __init__(self,
                 src, srcSha, srcMode, srcCacheEntry,
                 dst, dstSha, dstMode, dstCacheEntry,
                 score):
        self.srcName = src
        self.srcSha = srcSha
        self.srcMode = srcMode
        self.srcCacheEntry = srcCacheEntry
        self.dstName = dst
        self.dstSha = dstSha
        self.dstMode = dstMode
        self.dstCacheEntry = dstCacheEntry
        self.score = score

        self.processed = False

class RenameEntryContainer:
    def __init__(self):
        self.entriesSrc = {}
        self.entriesDst = {}

    def add(self, entry):
        self.entriesSrc[entry.srcName] = entry
        self.entriesDst[entry.dstName] = entry

    def getSrc(self, path):
        return self.entriesSrc.get(path)

    def getDst(self, path):
        return self.entriesDst.get(path)

    def __iter__(self):
        return self.entriesSrc.itervalues()

parseDiffRenamesRE = re.compile('^:([0-7]+) ([0-7]+) ([0-9a-f]{40}) ([0-9a-f]{40}) R([0-9]*)$')
def getRenames(tree, oTree, aTree, bTree, cacheEntries):
    '''Get information of all renames which occured between 'oTree' and
    'tree'. We need the three trees in the merge ('oTree', 'aTree' and
    'bTree') to be able to associate the correct cache entries with
    the rename information. 'tree' is always equal to either aTree or bTree.'''

    assert(tree == aTree or tree == bTree)
    inp = runProgram(['git-diff-tree', '-M', '--diff-filter=R', '-r',
                      '-z', oTree, tree])

    ret = RenameEntryContainer()
    try:
        recs = inp.split("\0")
        recs.pop() # remove last entry (which is '')
        it = recs.__iter__()
        while True:
            rec = it.next()
            m = parseDiffRenamesRE.match(rec)

            if not m:
                die('Unexpected output from git-diff-tree:', rec)

            srcMode = int(m.group(1), 8)
            dstMode = int(m.group(2), 8)
            srcSha = m.group(3)
            dstSha = m.group(4)
            score = m.group(5)
            src = it.next()
            dst = it.next()

            srcCacheEntry = cacheEntries.get(src)
            if not srcCacheEntry:
                srcCacheEntry = getCacheEntry(src, oTree, aTree, bTree)
                cacheEntries.add(srcCacheEntry)

            dstCacheEntry = cacheEntries.get(dst)
            if not dstCacheEntry:
                dstCacheEntry = getCacheEntry(dst, oTree, aTree, bTree)
                cacheEntries.add(dstCacheEntry)

            ret.add(RenameEntry(src, srcSha, srcMode, srcCacheEntry,
                                dst, dstSha, dstMode, dstCacheEntry,
                                score))
    except StopIteration:
        pass
    return ret

def fmtRename(src, dst):
    srcPath = src.split('/')
    dstPath = dst.split('/')
    path = []
    endIndex = min(len(srcPath), len(dstPath)) - 1
    for x in range(0, endIndex):
        if srcPath[x] == dstPath[x]:
            path.append(srcPath[x])
        else:
            endIndex = x
            break

    if len(path) > 0:
        return '/'.join(path) + \
               '/{' + '/'.join(srcPath[endIndex:]) + ' => ' + \
               '/'.join(dstPath[endIndex:]) + '}'
    else:
        return src + ' => ' + dst

def processRenames(renamesA, renamesB, branchNameA, branchNameB):
    srcNames = Set()
    for x in renamesA:
        srcNames.add(x.srcName)
    for x in renamesB:
        srcNames.add(x.srcName)

    cleanMerge = True
    for path in srcNames:
        if renamesA.getSrc(path):
            renames1 = renamesA
            renames2 = renamesB
            branchName1 = branchNameA
            branchName2 = branchNameB
        else:
            renames1 = renamesB
            renames2 = renamesA
            branchName1 = branchNameB
            branchName2 = branchNameA
        
        ren1 = renames1.getSrc(path)
        ren2 = renames2.getSrc(path)

        ren1.dstCacheEntry.processed = True
        ren1.srcCacheEntry.processed = True

        if ren1.processed:
            continue

        ren1.processed = True

        if ren2:
            # Renamed in 1 and renamed in 2
            assert(ren1.srcName == ren2.srcName)
            ren2.dstCacheEntry.processed = True
            ren2.processed = True

            if ren1.dstName != ren2.dstName:
                output('CONFLICT (rename/rename): Rename',
                       fmtRename(path, ren1.dstName), 'in branch', branchName1,
                       'rename', fmtRename(path, ren2.dstName), 'in',
                       branchName2)
                cleanMerge = False

                if ren1.dstName in currentDirectorySet:
                    dstName1 = uniquePath(ren1.dstName, branchName1)
                    output(ren1.dstName, 'is a directory in', branchName2,
                           'adding as', dstName1, 'instead.')
                    removeFile(False, ren1.dstName)
                else:
                    dstName1 = ren1.dstName

                if ren2.dstName in currentDirectorySet:
                    dstName2 = uniquePath(ren2.dstName, branchName2)
                    output(ren2.dstName, 'is a directory in', branchName1,
                           'adding as', dstName2, 'instead.')
                    removeFile(False, ren2.dstName)
                else:
                    dstName2 = ren2.dstName
                setIndexStages(dstName1,
                               None, None,
                               ren1.dstSha, ren1.dstMode,
			       None, None)
                setIndexStages(dstName2,
                               None, None,
                               None, None,
                               ren2.dstSha, ren2.dstMode)

            else:
                removeFile(True, ren1.srcName)

                [resSha, resMode, clean, merge] = \
                         mergeFile(ren1.srcName, ren1.srcSha, ren1.srcMode,
                                   ren1.dstName, ren1.dstSha, ren1.dstMode,
                                   ren2.dstName, ren2.dstSha, ren2.dstMode,
                                   branchName1, branchName2)

                if merge or not clean:
                    output('Renaming', fmtRename(path, ren1.dstName))

                if merge:
                    output('Auto-merging', ren1.dstName)

                if not clean:
                    output('CONFLICT (content): merge conflict in',
                           ren1.dstName)
                    cleanMerge = False

                    if not cacheOnly:
                        setIndexStages(ren1.dstName,
                                       ren1.srcSha, ren1.srcMode,
                                       ren1.dstSha, ren1.dstMode,
                                       ren2.dstSha, ren2.dstMode)

                updateFile(clean, resSha, resMode, ren1.dstName)
        else:
            removeFile(True, ren1.srcName)

            # Renamed in 1, maybe changed in 2
            if renamesA == renames1:
                stage = 3
            else:
                stage = 2
                
            srcShaOtherBranch  = ren1.srcCacheEntry.stages[stage].sha1
            srcModeOtherBranch = ren1.srcCacheEntry.stages[stage].mode

            dstShaOtherBranch  = ren1.dstCacheEntry.stages[stage].sha1
            dstModeOtherBranch = ren1.dstCacheEntry.stages[stage].mode

            tryMerge = False
            
            if ren1.dstName in currentDirectorySet:
                newPath = uniquePath(ren1.dstName, branchName1)
                output('CONFLICT (rename/directory): Rename',
                       fmtRename(ren1.srcName, ren1.dstName), 'in', branchName1,
                       'directory', ren1.dstName, 'added in', branchName2)
                output('Renaming', ren1.srcName, 'to', newPath, 'instead')
                cleanMerge = False
                removeFile(False, ren1.dstName)
                updateFile(False, ren1.dstSha, ren1.dstMode, newPath)
            elif srcShaOtherBranch == None:
                output('CONFLICT (rename/delete): Rename',
                       fmtRename(ren1.srcName, ren1.dstName), 'in',
                       branchName1, 'and deleted in', branchName2)
                cleanMerge = False
                updateFile(False, ren1.dstSha, ren1.dstMode, ren1.dstName)
            elif dstShaOtherBranch:
                newPath = uniquePath(ren1.dstName, branchName2)
                output('CONFLICT (rename/add): Rename',
                       fmtRename(ren1.srcName, ren1.dstName), 'in',
                       branchName1 + '.', ren1.dstName, 'added in', branchName2)
                output('Adding as', newPath, 'instead')
                updateFile(False, dstShaOtherBranch, dstModeOtherBranch, newPath)
                cleanMerge = False
                tryMerge = True
            elif renames2.getDst(ren1.dstName):
                dst2 = renames2.getDst(ren1.dstName)
                newPath1 = uniquePath(ren1.dstName, branchName1)
                newPath2 = uniquePath(dst2.dstName, branchName2)
                output('CONFLICT (rename/rename): Rename',
                       fmtRename(ren1.srcName, ren1.dstName), 'in',
                       branchName1+'. Rename',
                       fmtRename(dst2.srcName, dst2.dstName), 'in', branchName2)
                output('Renaming', ren1.srcName, 'to', newPath1, 'and',
                       dst2.srcName, 'to', newPath2, 'instead')
                removeFile(False, ren1.dstName)
                updateFile(False, ren1.dstSha, ren1.dstMode, newPath1)
                updateFile(False, dst2.dstSha, dst2.dstMode, newPath2)
                dst2.processed = True
                cleanMerge = False
            else:
                tryMerge = True

            if tryMerge:

                oName, oSHA1, oMode = ren1.srcName, ren1.srcSha, ren1.srcMode
                aName, bName = ren1.dstName, ren1.srcName
                aSHA1, bSHA1 = ren1.dstSha, srcShaOtherBranch
                aMode, bMode = ren1.dstMode, srcModeOtherBranch
                aBranch, bBranch = branchName1, branchName2

                if renamesA != renames1:
                    aName, bName = bName, aName
                    aSHA1, bSHA1 = bSHA1, aSHA1
                    aMode, bMode = bMode, aMode
                    aBranch, bBranch = bBranch, aBranch

                [resSha, resMode, clean, merge] = \
                         mergeFile(oName, oSHA1, oMode,
                                   aName, aSHA1, aMode,
                                   bName, bSHA1, bMode,
                                   aBranch, bBranch);

                if merge or not clean:
                    output('Renaming', fmtRename(ren1.srcName, ren1.dstName))

                if merge:
                    output('Auto-merging', ren1.dstName)

                if not clean:
                    output('CONFLICT (rename/modify): Merge conflict in',
                           ren1.dstName)
                    cleanMerge = False

                    if not cacheOnly:
                        setIndexStages(ren1.dstName,
                                       oSHA1, oMode,
                                       aSHA1, aMode,
                                       bSHA1, bMode)

                updateFile(clean, resSha, resMode, ren1.dstName)

    return cleanMerge

# Per entry merge function
# ------------------------

def processEntry(entry, branch1Name, branch2Name):
    '''Merge one cache entry.'''

    debug('processing', entry.path, 'clean cache:', cacheOnly)

    cleanMerge = True

    path = entry.path
    oSha = entry.stages[1].sha1
    oMode = entry.stages[1].mode
    aSha = entry.stages[2].sha1
    aMode = entry.stages[2].mode
    bSha = entry.stages[3].sha1
    bMode = entry.stages[3].mode

    assert(oSha == None or isSha(oSha))
    assert(aSha == None or isSha(aSha))
    assert(bSha == None or isSha(bSha))

    assert(oMode == None or type(oMode) is int)
    assert(aMode == None or type(aMode) is int)
    assert(bMode == None or type(bMode) is int)

    if (oSha and (not aSha or not bSha)):
    #
    # Case A: Deleted in one
    #
        if (not aSha     and not bSha) or \
           (aSha == oSha and not bSha) or \
           (not aSha     and bSha == oSha):
    # Deleted in both or deleted in one and unchanged in the other
            if aSha:
                output('Removing', path)
            removeFile(True, path)
        else:
    # Deleted in one and changed in the other
            cleanMerge = False
            if not aSha:
                output('CONFLICT (delete/modify):', path, 'deleted in',
                       branch1Name, 'and modified in', branch2Name + '.',
                       'Version', branch2Name, 'of', path, 'left in tree.')
                mode = bMode
                sha = bSha
            else:
                output('CONFLICT (modify/delete):', path, 'deleted in',
                       branch2Name, 'and modified in', branch1Name + '.',
                       'Version', branch1Name, 'of', path, 'left in tree.')
                mode = aMode
                sha = aSha

            updateFile(False, sha, mode, path)

    elif (not oSha and aSha     and not bSha) or \
         (not oSha and not aSha and bSha):
    #
    # Case B: Added in one.
    #
        if aSha:
            addBranch = branch1Name
            otherBranch = branch2Name
            mode = aMode
            sha = aSha
            conf = 'file/directory'
        else:
            addBranch = branch2Name
            otherBranch = branch1Name
            mode = bMode
            sha = bSha
            conf = 'directory/file'
    
        if path in currentDirectorySet:
            cleanMerge = False
            newPath = uniquePath(path, addBranch)
            output('CONFLICT (' + conf + '):',
                   'There is a directory with name', path, 'in',
                   otherBranch + '. Adding', path, 'as', newPath)

            removeFile(False, path)
            updateFile(False, sha, mode, newPath)
        else:
            output('Adding', path)
            updateFile(True, sha, mode, path)
    
    elif not oSha and aSha and bSha:
    #
    # Case C: Added in both (check for same permissions).
    #
        if aSha == bSha:
            if aMode != bMode:
                cleanMerge = False
                output('CONFLICT: File', path,
                       'added identically in both branches, but permissions',
                       'conflict', '0%o' % aMode, '->', '0%o' % bMode)
                output('CONFLICT: adding with permission:', '0%o' % aMode)

                updateFile(False, aSha, aMode, path)
            else:
                # This case is handled by git-read-tree
                assert(False)
        else:
            cleanMerge = False
            newPath1 = uniquePath(path, branch1Name)
            newPath2 = uniquePath(path, branch2Name)
            output('CONFLICT (add/add): File', path,
                   'added non-identically in both branches. Adding as',
                   newPath1, 'and', newPath2, 'instead.')
            removeFile(False, path)
            updateFile(False, aSha, aMode, newPath1)
            updateFile(False, bSha, bMode, newPath2)

    elif oSha and aSha and bSha:
    #
    # case D: Modified in both, but differently.
    #
        output('Auto-merging', path)
        [sha, mode, clean, dummy] = \
              mergeFile(path, oSha, oMode,
                        path, aSha, aMode,
                        path, bSha, bMode,
                        branch1Name, branch2Name)
        if clean:
            updateFile(True, sha, mode, path)
        else:
            cleanMerge = False
            output('CONFLICT (content): Merge conflict in', path)

            if cacheOnly:
                updateFile(False, sha, mode, path)
            else:
                updateFileExt(sha, mode, path, updateCache=False, updateWd=True)
    else:
        die("ERROR: Fatal merge failure, shouldn't happen.")

    return cleanMerge

def usage():
    die('Usage:', sys.argv[0], ' <base>... -- <head> <remote>..')

# main entry point as merge strategy module
# The first parameters up to -- are merge bases, and the rest are heads.

if len(sys.argv) < 4:
    usage()

bases = []
for nextArg in xrange(1, len(sys.argv)):
    if sys.argv[nextArg] == '--':
        if len(sys.argv) != nextArg + 3:
            die('Not handling anything other than two heads merge.')
        try:
            h1 = firstBranch = sys.argv[nextArg + 1]
            h2 = secondBranch = sys.argv[nextArg + 2]
        except IndexError:
            usage()
        break
    else:
        bases.append(sys.argv[nextArg])

print 'Merging', h1, 'with', h2

try:
    h1 = runProgram(['git-rev-parse', '--verify', h1 + '^0']).rstrip()
    h2 = runProgram(['git-rev-parse', '--verify', h2 + '^0']).rstrip()

    if len(bases) == 1:
        base = runProgram(['git-rev-parse', '--verify',
                           bases[0] + '^0']).rstrip()
        ancestor = Commit(base, None)
        [dummy, clean] = merge(Commit(h1, None), Commit(h2, None),
                               firstBranch, secondBranch, None, 0,
                               ancestor)
    else:
        graph = buildGraph([h1, h2])
        [dummy, clean] = merge(graph.shaMap[h1], graph.shaMap[h2],
                               firstBranch, secondBranch, graph)

    print ''
except:
    if isinstance(sys.exc_info()[1], SystemExit):
        raise
    else:
        traceback.print_exc(None, sys.stderr)
        sys.exit(2)

if clean:
    sys.exit(0)
else:
    sys.exit(1)
