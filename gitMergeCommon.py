import sys, re, os, traceback
from sets import Set

def die(*args):
    printList(args, sys.stderr)
    sys.exit(2)

def printList(list, file=sys.stdout):
    for x in list:
        file.write(str(x))
        file.write(' ')
    file.write('\n')

import subprocess

# Debugging machinery
# -------------------

DEBUG = 0
functionsToDebug = Set()

def addDebug(func):
    if type(func) == str:
        functionsToDebug.add(func)
    else:
        functionsToDebug.add(func.func_name)

def debug(*args):
    if DEBUG:
        funcName = traceback.extract_stack()[-2][2]
        if funcName in functionsToDebug:
            printList(args)

# Program execution
# -----------------

class ProgramError(Exception):
    def __init__(self, progStr, error):
        self.progStr = progStr
        self.error = error

    def __str__(self):
        return self.progStr + ': ' + self.error

addDebug('runProgram')
def runProgram(prog, input=None, returnCode=False, env=None, pipeOutput=True):
    debug('runProgram prog:', str(prog), 'input:', str(input))
    if type(prog) is str:
        progStr = prog
    else:
        progStr = ' '.join(prog)
    
    try:
        if pipeOutput:
            stderr = subprocess.STDOUT
            stdout = subprocess.PIPE
        else:
            stderr = None
            stdout = None
        pop = subprocess.Popen(prog,
                               shell = type(prog) is str,
                               stderr=stderr,
                               stdout=stdout,
                               stdin=subprocess.PIPE,
                               env=env)
    except OSError, e:
        debug('strerror:', e.strerror)
        raise ProgramError(progStr, e.strerror)

    if input != None:
        pop.stdin.write(input)
    pop.stdin.close()

    if pipeOutput:
        out = pop.stdout.read()
    else:
        out = ''

    code = pop.wait()
    if returnCode:
        ret = [out, code]
    else:
        ret = out
    if code != 0 and not returnCode:
        debug('error output:', out)
        debug('prog:', prog)
        raise ProgramError(progStr, out)
#    debug('output:', out.replace('\0', '\n'))
    return ret

# Code for computing common ancestors
# -----------------------------------

currentId = 0
def getUniqueId():
    global currentId
    currentId += 1
    return currentId

# The 'virtual' commit objects have SHAs which are integers
shaRE = re.compile('^[0-9a-f]{40}$')
def isSha(obj):
    return (type(obj) is str and bool(shaRE.match(obj))) or \
           (type(obj) is int and obj >= 1)

class Commit:
    def __init__(self, sha, parents, tree=None):
        self.parents = parents
        self.firstLineMsg = None
        self.children = []

        if tree:
            tree = tree.rstrip()
            assert(isSha(tree))
        self._tree = tree

        if not sha:
            self.sha = getUniqueId()
            self.virtual = True
            self.firstLineMsg = 'virtual commit'
            assert(isSha(tree))
        else:
            self.virtual = False
            self.sha = sha.rstrip()
        assert(isSha(self.sha))

    def tree(self):
        self.getInfo()
        assert(self._tree != None)
        return self._tree

    def shortInfo(self):
        self.getInfo()
        return str(self.sha) + ' ' + self.firstLineMsg

    def __str__(self):
        return self.shortInfo()

    def getInfo(self):
        if self.virtual or self.firstLineMsg != None:
            return
        else:
            info = runProgram(['git-cat-file', 'commit', self.sha])
            info = info.split('\n')
            msg = False
            for l in info:
                if msg:
                    self.firstLineMsg = l
                    break
                else:
                    if l.startswith('tree'):
                        self._tree = l[5:].rstrip()
                    elif l == '':
                        msg = True

class Graph:
    def __init__(self):
        self.commits = []
        self.shaMap = {}

    def addNode(self, node):
        assert(isinstance(node, Commit))
        self.shaMap[node.sha] = node
        self.commits.append(node)
        for p in node.parents:
            p.children.append(node)
        return node

    def reachableNodes(self, n1, n2):
        res = {}
        def traverse(n):
            res[n] = True
            for p in n.parents:
                traverse(p)

        traverse(n1)
        traverse(n2)
        return res

    def fixParents(self, node):
        for x in range(0, len(node.parents)):
            node.parents[x] = self.shaMap[node.parents[x]]

# addDebug('buildGraph')
def buildGraph(heads):
    debug('buildGraph heads:', heads)
    for h in heads:
        assert(isSha(h))

    g = Graph()

    out = runProgram(['git-rev-list', '--parents'] + heads)
    for l in out.split('\n'):
        if l == '':
            continue
        shas = l.split(' ')

        # This is a hack, we temporarily use the 'parents' attribute
        # to contain a list of SHA1:s. They are later replaced by proper
        # Commit objects.
        c = Commit(shas[0], shas[1:])

        g.commits.append(c)
        g.shaMap[c.sha] = c

    for c in g.commits:
        g.fixParents(c)

    for c in g.commits:
        for p in c.parents:
            p.children.append(c)
    return g

# Write the empty tree to the object database and return its SHA1
def writeEmptyTree():
    tmpIndex = os.environ['GIT_DIR'] + '/merge-tmp-index'
    def delTmpIndex():
        try:
            os.unlink(tmpIndex)
        except OSError:
            pass
    delTmpIndex()
    newEnv = os.environ.copy()
    newEnv['GIT_INDEX_FILE'] = tmpIndex
    res = runProgram(['git-write-tree'], env=newEnv).rstrip()
    delTmpIndex()
    return res

def addCommonRoot(graph):
    roots = []
    for c in graph.commits:
        if len(c.parents) == 0:
            roots.append(c)

    superRoot = Commit(sha=None, parents=[], tree=writeEmptyTree())
    graph.addNode(superRoot)
    for r in roots:
        r.parents = [superRoot]
    superRoot.children = roots
    return superRoot

def getCommonAncestors(graph, commit1, commit2):
    '''Find the common ancestors for commit1 and commit2'''
    assert(isinstance(commit1, Commit) and isinstance(commit2, Commit))

    def traverse(start, set):
        stack = [start]
        while len(stack) > 0:
            el = stack.pop()
            set.add(el)
            for p in el.parents:
                if p not in set:
                    stack.append(p)
    h1Set = Set()
    h2Set = Set()
    traverse(commit1, h1Set)
    traverse(commit2, h2Set)
    shared = h1Set.intersection(h2Set)

    if len(shared) == 0:
        shared = [addCommonRoot(graph)]
        
    res = Set()

    for s in shared:
        if len([c for c in s.children if c in shared]) == 0:
            res.add(s)
    return list(res)
