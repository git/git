#!/usr/bin/python

import sys, math, random, os, re, signal, tempfile, stat, errno, traceback
from heapq import heappush, heappop
from sets import Set

sys.path.append('@@GIT_PYTHON_PATH@@')
from gitMergeCommon import *

alwaysWriteTree = False

# The actual merge code
# ---------------------

def merge(h1, h2, branch1Name, branch2Name, graph, callDepth=0):
    '''Merge the commits h1 and h2, return the resulting virtual
    commit object and a flag indicating the cleaness of the merge.'''
    assert(isinstance(h1, Commit) and isinstance(h2, Commit))
    assert(isinstance(graph, Graph))

    def infoMsg(*args):
        sys.stdout.write('  '*callDepth)
        printList(args)
    infoMsg('Merging:')
    infoMsg(h1)
    infoMsg(h2)
    sys.stdout.flush()

    ca = getCommonAncestors(graph, h1, h2)
    infoMsg('found', len(ca), 'common ancestor(s):')
    for x in ca:
        infoMsg(x)
    sys.stdout.flush()

    Ms = ca[0]
    for h in ca[1:]:
        [Ms, ignore] = merge(Ms, h,
                             'Temporary shared merge branch 1',
                             'Temporary shared merge branch 2',
                             graph, callDepth+1)
        assert(isinstance(Ms, Commit))

    if callDepth == 0:
        if len(ca) > 1:
            runProgram(['git-read-tree', h1.tree()])
            runProgram(['git-update-cache', '-q', '--refresh'])
        # Use the original index if we only have one common ancestor
        
        updateWd = True
        if alwaysWriteTree:
            cleanCache = True
        else:
            cleanCache = False
    else:
        runProgram(['git-read-tree', h1.tree()])
        updateWd = False
        cleanCache = True

    [shaRes, clean] = mergeTrees(h1.tree(), h2.tree(), Ms.tree(),
                                 branch1Name, branch2Name,
                                 cleanCache, updateWd)

    if clean or cleanCache:
        res = Commit(None, [h1, h2], tree=shaRes)
        graph.addNode(res)
    else:
        res = None

    return [res, clean]

getFilesRE = re.compile('([0-9]+) ([a-z0-9]+) ([0-9a-f]{40})\t(.*)')
def getFilesAndDirs(tree):
    files = Set()
    dirs = Set()
    out = runProgram(['git-ls-tree', '-r', '-z', tree])
    for l in out.split('\0'):
        m = getFilesRE.match(l)
        if m:
            if m.group(2) == 'tree':
                dirs.add(m.group(4))
            elif m.group(2) == 'blob':
                files.add(m.group(4))

    return [files, dirs]

class CacheEntry:
    def __init__(self, path):
        class Stage:
            def __init__(self):
                self.sha1 = None
                self.mode = None
        
        self.stages = [Stage(), Stage(), Stage()]
        self.path = path

unmergedRE = re.compile('^([0-9]+) ([0-9a-f]{40}) ([1-3])\t(.*)$')
def unmergedCacheEntries():
    '''Create a dictionary mapping file names to CacheEntry
    objects. The dictionary contains one entry for every path with a
    non-zero stage entry.'''

    lines = runProgram(['git-ls-files', '-z', '--unmerged']).split('\0')
    lines.pop()

    res = {}
    for l in lines:
        m = unmergedRE.match(l)
        if m:
            mode = int(m.group(1), 8)
            sha1 = m.group(2)
            stage = int(m.group(3)) - 1
            path = m.group(4)

            if res.has_key(path):
                e = res[path]
            else:
                e = CacheEntry(path)
                res[path] = e
                
            e.stages[stage].mode = mode
            e.stages[stage].sha1 = sha1
        else:
            die('Error: Merge program failed: Unexpected output from', \
                'git-ls-files:', l)
    return res

def mergeTrees(head, merge, common, branch1Name, branch2Name,
               cleanCache, updateWd):
    '''Merge the trees 'head' and 'merge' with the common ancestor
    'common'. The name of the head branch is 'branch1Name' and the name of
    the merge branch is 'branch2Name'. Return a tuple (tree, cleanMerge)
    where tree is the resulting tree and cleanMerge is True iff the
    merge was clean.'''
    
    assert(isSha(head) and isSha(merge) and isSha(common))

    if common == merge:
        print 'Already uptodate!'
        return [head, True]

    if updateWd:
        updateArg = '-u'
    else:
        updateArg = '-i'
    runProgram(['git-read-tree', updateArg, '-m', common, head, merge])
    cleanMerge = True

    [tree, code] = runProgram('git-write-tree', returnCode=True)
    tree = tree.rstrip()
    if code != 0:
        [files, dirs] = getFilesAndDirs(head)
        [filesM, dirsM] = getFilesAndDirs(merge)
        files.union_update(filesM)
        dirs.union_update(dirsM)
        
        cleanMerge = True
        entries = unmergedCacheEntries()
        for name in entries:
            if not processEntry(entries[name], branch1Name, branch2Name,
                                files, dirs, cleanCache, updateWd):
                cleanMerge = False
                
        if cleanMerge or cleanCache:
            tree = runProgram('git-write-tree').rstrip()
        else:
            tree = None
    else:
        cleanMerge = True

    return [tree, cleanMerge]

def processEntry(entry, branch1Name, branch2Name, files, dirs,
                 cleanCache, updateWd):
    '''Merge one cache entry. 'files' is a Set with the files in both of
    the heads that we are going to merge. 'dirs' contains the
    corresponding data for directories. If 'cleanCache' is True no
    non-zero stages will be left in the cache for the path
    corresponding to the entry 'entry'.'''

# cleanCache == True  => Don't leave any non-stage 0 entries in the cache.
#               False => Leave unmerged entries

# updateWd  == True  => Update the working directory to correspond to the cache
#              False => Leave the working directory unchanged

# clean     == True  => non-conflict case
#              False => conflict case

# If cleanCache == False then the cache shouldn't be updated if clean == False

    def updateFile(clean, sha, mode, path):
        if cleanCache or (not cleanCache and clean):
            runProgram(['git-update-cache', '--add', '--cacheinfo',
                        '0%o' % mode, sha, path])

        if updateWd:
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
            runProgram(['git-update-cache', '--', path])

    def removeFile(clean, path):
        if cleanCache or (not cleanCache and clean):
            runProgram(['git-update-cache', '--force-remove', '--', path])

        if updateWd:
            try:
                os.unlink(path)
            except OSError, e:
                if e.errno != errno.ENOENT and e.errno != errno.EISDIR:
                    raise

    def uniquePath(path, branch):
        newPath = path + '_' + branch
        suffix = 0
        while newPath in files or newPath in dirs:
            suffix += 1
            newPath = path + '_' + branch + '_' + str(suffix)
        files.add(newPath)
        return newPath

    debug('processing', entry.path, 'clean cache:', cleanCache,
          'wd:', updateWd)

    cleanMerge = True

    path = entry.path
    oSha = entry.stages[0].sha1
    oMode = entry.stages[0].mode
    aSha = entry.stages[1].sha1
    aMode = entry.stages[1].mode
    bSha = entry.stages[2].sha1
    bMode = entry.stages[2].mode

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
                print 'Removing ' + path
            removeFile(True, path)
        else:
    # Deleted in one and changed in the other
            cleanMerge = False
            if not aSha:
                print 'CONFLICT (del/mod): "' + path + '" deleted in', \
                      branch1Name, 'and modified in', branch2Name, \
                      '. Version', branch2Name, ' of "' + path + \
                      '" left in tree'
                mode = bMode
                sha = bSha
            else:
                print 'CONFLICT (mod/del): "' + path + '" deleted in', \
                      branch2Name, 'and modified in', branch1Name + \
                      '. Version', branch1Name, 'of "' + path + \
                      '" left in tree'
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
            conf = 'file/dir'
        else:
            addBranch = branch2Name
            otherBranch = branch1Name
            mode = bMode
            sha = bSha
            conf = 'dir/file'
    
        if path in dirs:
            cleanMerge = False
            newPath = uniquePath(path, addBranch)
            print 'CONFLICT (' + conf + \
                  '): There is a directory with name "' + path + '" in', \
                  otherBranch + '. Adding "' + path + '" as "' + newPath + '"'

            removeFile(False, path)
            path = newPath
        else:
            print 'Adding "' + path + '"'

        updateFile(True, sha, mode, path)
    
    elif not oSha and aSha and bSha:
    #
    # Case C: Added in both (check for same permissions).
    #
        if aSha == bSha:
            if aMode != bMode:
                cleanMerge = False
                print 'CONFLICT: File "' + path + \
                      '" added identically in both branches,'
                print 'CONFLICT: but permissions conflict', '0%o' % aMode, \
                      '->', '0%o' % bMode
                print 'CONFLICT: adding with permission:', '0%o' % aMode

                updateFile(False, aSha, aMode, path)
            else:
                # This case is handled by git-read-tree
                assert(False)
        else:
            cleanMerge = False
            newPath1 = uniquePath(path, branch1Name)
            newPath2 = uniquePath(path, branch2Name)
            print 'CONFLICT (add/add): File "' + path + \
                  '" added non-identically in both branches.', \
                  'Adding "' + newPath1 + '" and "' + newPath2 + '" instead.'
            removeFile(False, path)
            updateFile(False, aSha, aMode, newPath1)
            updateFile(False, bSha, bMode, newPath2)

    elif oSha and aSha and bSha:
    #
    # case D: Modified in both, but differently.
    #
        print 'Auto-merging', path 
        orig = runProgram(['git-unpack-file', oSha]).rstrip()
        src1 = runProgram(['git-unpack-file', aSha]).rstrip()
        src2 = runProgram(['git-unpack-file', bSha]).rstrip()
        [out, ret] = runProgram(['merge',
                                 '-L', branch1Name + '/' + path,
                                 '-L', 'orig/' + path,
                                 '-L', branch2Name + '/' + path,
                                 src1, orig, src2], returnCode=True)

        if aMode == oMode:
            mode = bMode
        else:
            mode = aMode

        sha = runProgram(['git-hash-object', '-t', 'blob', '-w',
                          src1]).rstrip()

        if ret != 0:
            cleanMerge = False
            print 'CONFLICT (content): Merge conflict in "' + path + '".'
            updateFile(False, sha, mode, path)
        else:
            updateFile(True, sha, mode, path)

        os.unlink(orig)
        os.unlink(src1)
        os.unlink(src2)
    else:
        die("ERROR: Fatal merge failure, shouldn't happen.")

    return cleanMerge

def usage():
    die('Usage:', sys.argv[0], ' <base>... -- <head> <remote>..')

# main entry point as merge strategy module
# The first parameters up to -- are merge bases, and the rest are heads.
# This strategy module figures out merge bases itself, so we only
# get heads.

if len(sys.argv) < 4:
    usage()

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

print 'Merging', h1, 'with', h2

try:
    h1 = runProgram(['git-rev-parse', '--verify', h1 + '^0']).rstrip()
    h2 = runProgram(['git-rev-parse', '--verify', h2 + '^0']).rstrip()

    graph = buildGraph([h1, h2])

    [res, clean] = merge(graph.shaMap[h1], graph.shaMap[h2],
                         firstBranch, secondBranch, graph)

    print ''
except:
    traceback.print_exc(None, sys.stderr)
    sys.exit(2)

if clean:
    sys.exit(0)
else:
    print 'Automatic merge failed, fix up by hand'
    sys.exit(1)
