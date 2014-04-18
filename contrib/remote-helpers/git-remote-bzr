#!/usr/bin/env python
#
# Copyright (c) 2012 Felipe Contreras
#

#
# Just copy to your ~/bin, or anywhere in your $PATH.
# Then you can clone with:
# % git clone bzr::/path/to/bzr/repo/or/url
#
# For example:
# % git clone bzr::$HOME/myrepo
# or
# % git clone bzr::lp:myrepo
#
# If you want to specify which branches you want to track (per repo):
# % git config remote.origin.bzr-branches 'trunk, devel, test'
#
# Where 'origin' is the name of the repository you want to specify the
# branches.
#

import sys

import bzrlib
if hasattr(bzrlib, "initialize"):
    bzrlib.initialize()

import bzrlib.plugin
bzrlib.plugin.load_plugins()

import bzrlib.generate_ids
import bzrlib.transport
import bzrlib.errors
import bzrlib.ui
import bzrlib.urlutils
import bzrlib.branch

import sys
import os
import json
import re
import StringIO
import atexit, shutil, hashlib, urlparse, subprocess

NAME_RE = re.compile('^([^<>]+)')
AUTHOR_RE = re.compile('^([^<>]+?)? ?[<>]([^<>]*)(?:$|>)')
EMAIL_RE = re.compile(r'([^ \t<>]+@[^ \t<>]+)')
RAW_AUTHOR_RE = re.compile('^(\w+) (.+)? <(.*)> (\d+) ([+-]\d+)')

def die(msg, *args):
    sys.stderr.write('ERROR: %s\n' % (msg % args))
    sys.exit(1)

def warn(msg, *args):
    sys.stderr.write('WARNING: %s\n' % (msg % args))

def gittz(tz):
    return '%+03d%02d' % (tz / 3600, tz % 3600 / 60)

def get_config(config):
    cmd = ['git', 'config', '--get', config]
    process = subprocess.Popen(cmd, stdout=subprocess.PIPE)
    output, _ = process.communicate()
    return output

class Marks:

    def __init__(self, path):
        self.path = path
        self.tips = {}
        self.marks = {}
        self.rev_marks = {}
        self.last_mark = 0
        self.load()

    def load(self):
        if not os.path.exists(self.path):
            return

        tmp = json.load(open(self.path))
        self.tips = tmp['tips']
        self.marks = tmp['marks']
        self.last_mark = tmp['last-mark']

        for rev, mark in self.marks.iteritems():
            self.rev_marks[mark] = rev

    def dict(self):
        return { 'tips': self.tips, 'marks': self.marks, 'last-mark' : self.last_mark }

    def store(self):
        json.dump(self.dict(), open(self.path, 'w'))

    def __str__(self):
        return str(self.dict())

    def from_rev(self, rev):
        return self.marks[rev]

    def to_rev(self, mark):
        return str(self.rev_marks[mark])

    def next_mark(self):
        self.last_mark += 1
        return self.last_mark

    def get_mark(self, rev):
        self.last_mark += 1
        self.marks[rev] = self.last_mark
        return self.last_mark

    def is_marked(self, rev):
        return rev in self.marks

    def new_mark(self, rev, mark):
        self.marks[rev] = mark
        self.rev_marks[mark] = rev
        self.last_mark = mark

    def get_tip(self, branch):
        try:
            return str(self.tips[branch])
        except KeyError:
            return None

    def set_tip(self, branch, tip):
        self.tips[branch] = tip

class Parser:

    def __init__(self, repo):
        self.repo = repo
        self.line = self.get_line()

    def get_line(self):
        return sys.stdin.readline().strip()

    def __getitem__(self, i):
        return self.line.split()[i]

    def check(self, word):
        return self.line.startswith(word)

    def each_block(self, separator):
        while self.line != separator:
            yield self.line
            self.line = self.get_line()

    def __iter__(self):
        return self.each_block('')

    def next(self):
        self.line = self.get_line()
        if self.line == 'done':
            self.line = None

    def get_mark(self):
        i = self.line.index(':') + 1
        return int(self.line[i:])

    def get_data(self):
        if not self.check('data'):
            return None
        i = self.line.index(' ') + 1
        size = int(self.line[i:])
        return sys.stdin.read(size)

    def get_author(self):
        m = RAW_AUTHOR_RE.match(self.line)
        if not m:
            return None
        _, name, email, date, tz = m.groups()
        name = name.decode('utf-8')
        committer = '%s <%s>' % (name, email)
        tz = int(tz)
        tz = ((tz / 100) * 3600) + ((tz % 100) * 60)
        return (committer, int(date), tz)

def rev_to_mark(rev):
    return marks.from_rev(rev)

def mark_to_rev(mark):
    return marks.to_rev(mark)

def fixup_user(user):
    name = mail = None
    user = user.replace('"', '')
    m = AUTHOR_RE.match(user)
    if m:
        name = m.group(1)
        mail = m.group(2).strip()
    else:
        m = EMAIL_RE.match(user)
        if m:
            mail = m.group(1)
        else:
            m = NAME_RE.match(user)
            if m:
                name = m.group(1).strip()

    if not name:
        name = 'unknown'
    if not mail:
        mail = 'Unknown'

    return '%s <%s>' % (name, mail)

def get_filechanges(cur, prev):
    modified = {}
    removed = {}

    changes = cur.changes_from(prev)

    def u(s):
        return s.encode('utf-8')

    for path, fid, kind in changes.added:
        modified[u(path)] = fid
    for path, fid, kind in changes.removed:
        removed[u(path)] = None
    for path, fid, kind, mod, _ in changes.modified:
        modified[u(path)] = fid
    for oldpath, newpath, fid, kind, mod, _ in changes.renamed:
        removed[u(oldpath)] = None
        if kind == 'directory':
            lst = cur.list_files(from_dir=newpath, recursive=True)
            for path, file_class, kind, fid, entry in lst:
                if kind != 'directory':
                    modified[u(newpath + '/' + path)] = fid
        else:
            modified[u(newpath)] = fid

    return modified, removed

def export_files(tree, files):
    final = []
    for path, fid in files.iteritems():
        kind = tree.kind(fid)

        h = tree.get_file_sha1(fid)

        if kind == 'symlink':
            d = tree.get_symlink_target(fid)
            mode = '120000'
        elif kind == 'file':

            if tree.is_executable(fid):
                mode = '100755'
            else:
                mode = '100644'

            # is the blob already exported?
            if h in filenodes:
                mark = filenodes[h]
                final.append((mode, mark, path))
                continue

            d = tree.get_file_text(fid)
        elif kind == 'directory':
            continue
        else:
            die("Unhandled kind '%s' for path '%s'" % (kind, path))

        mark = marks.next_mark()
        filenodes[h] = mark

        print "blob"
        print "mark :%u" % mark
        print "data %d" % len(d)
        print d

        final.append((mode, mark, path))

    return final

def export_branch(repo, name):
    ref = '%s/heads/%s' % (prefix, name)
    tip = marks.get_tip(name)

    branch = get_remote_branch(name)
    repo = branch.repository

    branch.lock_read()
    revs = branch.iter_merge_sorted_revisions(None, tip, 'exclude', 'forward')
    try:
        tip_revno = branch.revision_id_to_revno(tip)
        last_revno, _ = branch.last_revision_info()
        total = last_revno - tip_revno
    except bzrlib.errors.NoSuchRevision:
        tip_revno = 0
        total = 0

    for revid, _, seq, _ in revs:

        if marks.is_marked(revid):
            continue

        rev = repo.get_revision(revid)
        revno = seq[0]

        parents = rev.parent_ids
        time = rev.timestamp
        tz = rev.timezone
        committer = rev.committer.encode('utf-8')
        committer = "%s %u %s" % (fixup_user(committer), time, gittz(tz))
        authors = rev.get_apparent_authors()
        if authors:
            author = authors[0].encode('utf-8')
            author = "%s %u %s" % (fixup_user(author), time, gittz(tz))
        else:
            author = committer
        msg = rev.message.encode('utf-8')

        msg += '\n'

        if len(parents) == 0:
            parent = bzrlib.revision.NULL_REVISION
        else:
            parent = parents[0]

        cur_tree = repo.revision_tree(revid)
        prev = repo.revision_tree(parent)
        modified, removed = get_filechanges(cur_tree, prev)

        modified_final = export_files(cur_tree, modified)

        if len(parents) == 0:
            print 'reset %s' % ref

        print "commit %s" % ref
        print "mark :%d" % (marks.get_mark(revid))
        print "author %s" % (author)
        print "committer %s" % (committer)
        print "data %d" % (len(msg))
        print msg

        for i, p in enumerate(parents):
            try:
                m = rev_to_mark(p)
            except KeyError:
                # ghost?
                continue
            if i == 0:
                print "from :%s" % m
            else:
                print "merge :%s" % m

        for f in removed:
            print "D %s" % (f,)
        for f in modified_final:
            print "M %s :%u %s" % f
        print

        if len(seq) > 1:
            # let's skip branch revisions from the progress report
            continue

        progress = (revno - tip_revno)
        if (progress % 100 == 0):
            if total:
                print "progress revision %d '%s' (%d/%d)" % (revno, name, progress, total)
            else:
                print "progress revision %d '%s' (%d)" % (revno, name, progress)

    branch.unlock()

    revid = branch.last_revision()

    # make sure the ref is updated
    print "reset %s" % ref
    print "from :%u" % rev_to_mark(revid)
    print

    marks.set_tip(name, revid)

def export_tag(repo, name):
    ref = '%s/tags/%s' % (prefix, name)
    print "reset %s" % ref
    print "from :%u" % rev_to_mark(tags[name])
    print

def do_import(parser):
    repo = parser.repo
    path = os.path.join(dirname, 'marks-git')

    print "feature done"
    if os.path.exists(path):
        print "feature import-marks=%s" % path
    print "feature export-marks=%s" % path
    print "feature force"
    sys.stdout.flush()

    while parser.check('import'):
        ref = parser[1]
        if ref.startswith('refs/heads/'):
            name = ref[len('refs/heads/'):]
            export_branch(repo, name)
        if ref.startswith('refs/tags/'):
            name = ref[len('refs/tags/'):]
            export_tag(repo, name)
        parser.next()

    print 'done'

    sys.stdout.flush()

def parse_blob(parser):
    parser.next()
    mark = parser.get_mark()
    parser.next()
    data = parser.get_data()
    blob_marks[mark] = data
    parser.next()

class CustomTree():

    def __init__(self, branch, revid, parents, files):
        self.updates = {}
        self.branch = branch

        def copy_tree(revid):
            files = files_cache[revid] = {}
            branch.lock_read()
            tree = branch.repository.revision_tree(revid)
            try:
                for path, entry in tree.iter_entries_by_dir():
                    files[path] = [entry.file_id, None]
            finally:
                branch.unlock()
            return files

        if len(parents) == 0:
            self.base_id = bzrlib.revision.NULL_REVISION
            self.base_files = {}
        else:
            self.base_id = parents[0]
            self.base_files = files_cache.get(self.base_id, None)
            if not self.base_files:
                self.base_files = copy_tree(self.base_id)

        self.files = files_cache[revid] = self.base_files.copy()
        self.rev_files = {}

        for path, data in self.files.iteritems():
            fid, mark = data
            self.rev_files[fid] = [path, mark]

        for path, f in files.iteritems():
            fid, mark = self.files.get(path, [None, None])
            if not fid:
                fid = bzrlib.generate_ids.gen_file_id(path)
            f['path'] = path
            self.rev_files[fid] = [path, mark]
            self.updates[fid] = f

    def last_revision(self):
        return self.base_id

    def iter_changes(self):
        changes = []

        def get_parent(dirname, basename):
            parent_fid, mark = self.base_files.get(dirname, [None, None])
            if parent_fid:
                return parent_fid
            parent_fid, mark = self.files.get(dirname, [None, None])
            if parent_fid:
                return parent_fid
            if basename == '':
                return None
            fid = bzrlib.generate_ids.gen_file_id(path)
            add_entry(fid, dirname, 'directory')
            return fid

        def add_entry(fid, path, kind, mode=None):
            dirname, basename = os.path.split(path)
            parent_fid = get_parent(dirname, basename)

            executable = False
            if mode == '100755':
                executable = True
            elif mode == '120000':
                kind = 'symlink'

            change = (fid,
                    (None, path),
                    True,
                    (False, True),
                    (None, parent_fid),
                    (None, basename),
                    (None, kind),
                    (None, executable))
            self.files[path] = [change[0], None]
            changes.append(change)

        def update_entry(fid, path, kind, mode=None):
            dirname, basename = os.path.split(path)
            parent_fid = get_parent(dirname, basename)

            executable = False
            if mode == '100755':
                executable = True
            elif mode == '120000':
                kind = 'symlink'

            change = (fid,
                    (path, path),
                    True,
                    (True, True),
                    (None, parent_fid),
                    (None, basename),
                    (None, kind),
                    (None, executable))
            self.files[path] = [change[0], None]
            changes.append(change)

        def remove_entry(fid, path, kind):
            dirname, basename = os.path.split(path)
            parent_fid = get_parent(dirname, basename)
            change = (fid,
                    (path, None),
                    True,
                    (True, False),
                    (parent_fid, None),
                    (None, None),
                    (None, None),
                    (None, None))
            del self.files[path]
            changes.append(change)

        for fid, f in self.updates.iteritems():
            path = f['path']

            if 'deleted' in f:
                remove_entry(fid, path, 'file')
                continue

            if path in self.base_files:
                update_entry(fid, path, 'file', f['mode'])
            else:
                add_entry(fid, path, 'file', f['mode'])

            self.files[path][1] = f['mark']
            self.rev_files[fid][1] = f['mark']

        return changes

    def get_content(self, file_id):
        path, mark = self.rev_files[file_id]
        if mark:
            return blob_marks[mark]

        # last resort
        tree = self.branch.repository.revision_tree(self.base_id)
        return tree.get_file_text(file_id)

    def get_file_with_stat(self, file_id, path=None):
        content = self.get_content(file_id)
        return (StringIO.StringIO(content), None)

    def get_symlink_target(self, file_id):
        return self.get_content(file_id)

    def id2path(self, file_id):
        path, mark = self.rev_files[file_id]
        return path

def c_style_unescape(string):
    if string[0] == string[-1] == '"':
        return string.decode('string-escape')[1:-1]
    return string

def parse_commit(parser):
    parents = []

    ref = parser[1]
    parser.next()

    if ref.startswith('refs/heads/'):
        name = ref[len('refs/heads/'):]
        branch = get_remote_branch(name)
    else:
        die('unknown ref')

    commit_mark = parser.get_mark()
    parser.next()
    author = parser.get_author()
    parser.next()
    committer = parser.get_author()
    parser.next()
    data = parser.get_data()
    parser.next()
    if parser.check('from'):
        parents.append(parser.get_mark())
        parser.next()
    while parser.check('merge'):
        parents.append(parser.get_mark())
        parser.next()

    # fast-export adds an extra newline
    if data[-1] == '\n':
        data = data[:-1]

    files = {}

    for line in parser:
        if parser.check('M'):
            t, m, mark_ref, path = line.split(' ', 3)
            mark = int(mark_ref[1:])
            f = { 'mode' : m, 'mark' : mark }
        elif parser.check('D'):
            t, path = line.split(' ', 1)
            f = { 'deleted' : True }
        else:
            die('Unknown file command: %s' % line)
        path = c_style_unescape(path).decode('utf-8')
        files[path] = f

    committer, date, tz = committer
    author, _, _ = author
    parents = [mark_to_rev(p) for p in parents]
    revid = bzrlib.generate_ids.gen_revision_id(committer, date)
    props = {}
    props['branch-nick'] = branch.nick
    props['authors'] = author

    mtree = CustomTree(branch, revid, parents, files)
    changes = mtree.iter_changes()

    branch.lock_write()
    try:
        builder = branch.get_commit_builder(parents, None, date, tz, committer, props, revid)
        try:
            list(builder.record_iter_changes(mtree, mtree.last_revision(), changes))
            builder.finish_inventory()
            builder.commit(data.decode('utf-8', 'replace'))
        except Exception, e:
            builder.abort()
            raise
    finally:
        branch.unlock()

    parsed_refs[ref] = revid
    marks.new_mark(revid, commit_mark)

def parse_reset(parser):
    ref = parser[1]
    parser.next()

    # ugh
    if parser.check('commit'):
        parse_commit(parser)
        return
    if not parser.check('from'):
        return
    from_mark = parser.get_mark()
    parser.next()

    parsed_refs[ref] = mark_to_rev(from_mark)

def do_export(parser):
    parser.next()

    for line in parser.each_block('done'):
        if parser.check('blob'):
            parse_blob(parser)
        elif parser.check('commit'):
            parse_commit(parser)
        elif parser.check('reset'):
            parse_reset(parser)
        elif parser.check('tag'):
            pass
        elif parser.check('feature'):
            pass
        else:
            die('unhandled export command: %s' % line)

    for ref, revid in parsed_refs.iteritems():
        if ref.startswith('refs/heads/'):
            name = ref[len('refs/heads/'):]
            branch = get_remote_branch(name)
            branch.generate_revision_history(revid, marks.get_tip(name))

            if name in peers:
                peer = bzrlib.branch.Branch.open(peers[name],
                                                 possible_transports=transports)
                try:
                    peer.bzrdir.push_branch(branch, revision_id=revid,
                                            overwrite=force)
                except bzrlib.errors.DivergedBranches:
                    print "error %s non-fast forward" % ref
                    continue

            try:
                wt = branch.bzrdir.open_workingtree()
                wt.update()
            except bzrlib.errors.NoWorkingTree:
                pass
        elif ref.startswith('refs/tags/'):
            # TODO: implement tag push
            print "error %s pushing tags not supported" % ref
            continue
        else:
            # transport-helper/fast-export bugs
            continue

        print "ok %s" % ref

    print

def do_capabilities(parser):
    print "import"
    print "export"
    print "refspec refs/heads/*:%s/heads/*" % prefix
    print "refspec refs/tags/*:%s/tags/*" % prefix

    path = os.path.join(dirname, 'marks-git')

    if os.path.exists(path):
        print "*import-marks %s" % path
    print "*export-marks %s" % path

    print "option"
    print

class InvalidOptionValue(Exception):
    pass

def get_bool_option(val):
    if val == 'true':
        return True
    elif val == 'false':
        return False
    else:
        raise InvalidOptionValue()

def do_option(parser):
    global force
    opt, val = parser[1:3]
    try:
        if opt == 'force':
            force = get_bool_option(val)
            print 'ok'
        else:
            print 'unsupported'
    except InvalidOptionValue:
        print "error '%s' is not a valid value for option '%s'" % (val, opt)

def ref_is_valid(name):
    return not True in [c in name for c in '~^: \\']

def do_list(parser):
    master_branch = None

    for name in branches:
        if not master_branch:
            master_branch = name
        print "? refs/heads/%s" % name

    branch = get_remote_branch(master_branch)
    branch.lock_read()
    for tag, revid in branch.tags.get_tag_dict().items():
        try:
            branch.revision_id_to_dotted_revno(revid)
        except bzrlib.errors.NoSuchRevision:
            continue
        if not ref_is_valid(tag):
            continue
        print "? refs/tags/%s" % tag
        tags[tag] = revid
    branch.unlock()

    print "@refs/heads/%s HEAD" % master_branch
    print

def clone(path, remote_branch):
    try:
        bdir = bzrlib.bzrdir.BzrDir.create(path, possible_transports=transports)
    except bzrlib.errors.AlreadyControlDirError:
        bdir = bzrlib.bzrdir.BzrDir.open(path, possible_transports=transports)
    repo = bdir.find_repository()
    repo.fetch(remote_branch.repository)
    return remote_branch.sprout(bdir, repository=repo)

def get_remote_branch(name):
    remote_branch = bzrlib.branch.Branch.open(branches[name],
                                              possible_transports=transports)
    if isinstance(remote_branch.bzrdir.root_transport, bzrlib.transport.local.LocalTransport):
        return remote_branch

    branch_path = os.path.join(dirname, 'clone', name)

    try:
        branch = bzrlib.branch.Branch.open(branch_path,
                                           possible_transports=transports)
    except bzrlib.errors.NotBranchError:
        # clone
        branch = clone(branch_path, remote_branch)
    else:
        # pull
        try:
            branch.pull(remote_branch, overwrite=True)
        except bzrlib.errors.DivergedBranches:
            # use remote branch for now
            return remote_branch

    return branch

def find_branches(repo):
    transport = repo.bzrdir.root_transport

    for fn in transport.iter_files_recursive():
        if not fn.endswith('.bzr/branch-format'):
            continue

        name = subdir = fn[:-len('/.bzr/branch-format')]
        name = name if name != '' else 'master'
        name = name.replace('/', '+')

        try:
            cur = transport.clone(subdir)
            branch = bzrlib.branch.Branch.open_from_transport(cur)
        except bzrlib.errors.NotBranchError:
            continue
        else:
            yield name, branch.base

def get_repo(url, alias):
    normal_url = bzrlib.urlutils.normalize_url(url)
    origin = bzrlib.bzrdir.BzrDir.open(url, possible_transports=transports)
    is_local = isinstance(origin.transport, bzrlib.transport.local.LocalTransport)

    shared_path = os.path.join(gitdir, 'bzr')
    try:
        shared_dir = bzrlib.bzrdir.BzrDir.open(shared_path,
                                               possible_transports=transports)
    except bzrlib.errors.NotBranchError:
        shared_dir = bzrlib.bzrdir.BzrDir.create(shared_path,
                                                 possible_transports=transports)
    try:
        shared_repo = shared_dir.open_repository()
    except bzrlib.errors.NoRepositoryPresent:
        shared_repo = shared_dir.create_repository(shared=True)

    if not is_local:
        clone_path = os.path.join(dirname, 'clone')
        if not os.path.exists(clone_path):
            os.mkdir(clone_path)
        else:
            # check and remove old organization
            try:
                bdir = bzrlib.bzrdir.BzrDir.open(clone_path,
                                                 possible_transports=transports)
                bdir.destroy_repository()
            except bzrlib.errors.NotBranchError:
                pass
            except bzrlib.errors.NoRepositoryPresent:
                pass

    wanted = get_config('remote.%s.bzr-branches' % alias).rstrip().split(', ')
    # stupid python
    wanted = [e for e in wanted if e]
    if not wanted:
        wanted = get_config('remote-bzr.branches').rstrip().split(', ')
        # stupid python
        wanted = [e for e in wanted if e]

    if not wanted:
        try:
            repo = origin.open_repository()
            if not repo.bzrdir.root_transport.listable():
                # this repository is not usable for us
                raise bzrlib.errors.NoRepositoryPresent(repo.bzrdir)
        except bzrlib.errors.NoRepositoryPresent:
            wanted = ['master']

    if wanted:
        def list_wanted(url, wanted):
            for name in wanted:
                subdir = name if name != 'master' else ''
                yield name, bzrlib.urlutils.join(url, subdir)

        branch_list = list_wanted(url, wanted)
    else:
        branch_list = find_branches(repo)

    for name, url in branch_list:
        if not is_local:
            peers[name] = url
        branches[name] = url

    return origin

def fix_path(alias, orig_url):
    url = urlparse.urlparse(orig_url, 'file')
    if url.scheme != 'file' or os.path.isabs(url.path):
        return
    abs_url = urlparse.urljoin("%s/" % os.getcwd(), orig_url)
    cmd = ['git', 'config', 'remote.%s.url' % alias, "bzr::%s" % abs_url]
    subprocess.call(cmd)

def main(args):
    global marks, prefix, gitdir, dirname
    global tags, filenodes
    global blob_marks
    global parsed_refs
    global files_cache
    global is_tmp
    global branches, peers
    global transports
    global force

    marks = None
    is_tmp = False
    gitdir = os.environ.get('GIT_DIR', None)

    if len(args) < 3:
        die('Not enough arguments.')

    if not gitdir:
        die('GIT_DIR not set')

    alias = args[1]
    url = args[2]

    tags = {}
    filenodes = {}
    blob_marks = {}
    parsed_refs = {}
    files_cache = {}
    branches = {}
    peers = {}
    transports = []
    force = False

    if alias[5:] == url:
        is_tmp = True
        alias = hashlib.sha1(alias).hexdigest()

    prefix = 'refs/bzr/%s' % alias
    dirname = os.path.join(gitdir, 'bzr', alias)

    if not is_tmp:
        fix_path(alias, url)

    if not os.path.exists(dirname):
        os.makedirs(dirname)

    if hasattr(bzrlib.ui.ui_factory, 'be_quiet'):
        bzrlib.ui.ui_factory.be_quiet(True)

    repo = get_repo(url, alias)

    marks_path = os.path.join(dirname, 'marks-int')
    marks = Marks(marks_path)

    parser = Parser(repo)
    for line in parser:
        if parser.check('capabilities'):
            do_capabilities(parser)
        elif parser.check('list'):
            do_list(parser)
        elif parser.check('import'):
            do_import(parser)
        elif parser.check('export'):
            do_export(parser)
        elif parser.check('option'):
            do_option(parser)
        else:
            die('unhandled command: %s' % line)
        sys.stdout.flush()

def bye():
    if not marks:
        return
    if not is_tmp:
        marks.store()
    else:
        shutil.rmtree(dirname)

atexit.register(bye)
sys.exit(main(sys.argv))
