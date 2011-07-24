# Copyright (C) 2008 Canonical Ltd
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

"""Processor of import commands.

This module provides core processing functionality including an abstract class
for basing real processors on. See the processors package for examples.
"""

import os
import shutil

from mercurial import context
from mercurial.node import nullid, hex

from git_remote_helpers.util import die
from git_remote_helpers.fastimport import processor, parser


class commit(object):
    def __init__(self, author, date, desc, parents, branch=None, rev=None,
                 extra={}, sortkey=None):
        self.author = author or 'unknown'
        self.date = date or '0 0'
        self.desc = desc
        self.parents = parents
        self.branch = branch
        self.rev = rev
        self.extra = extra
        self.sortkey = sortkey


class HgImportProcessor(processor.ImportProcessor):

    def __init__(self, ui, repo):
        super(HgImportProcessor, self).__init__()
        self.ui = ui
        self.repo = repo

        self.branchnames = True

        self.idmap = {}
        self.commitmap = {}             # map commit ID (":1") to commit object
        self.branchmap = {}             # map branch name to list of heads

        self.tags = []                  # list of (tag, mark) tuples

        self.numblobs = 0               # for progress reporting
        self.blobdir = None

    def setup(self):
        """Setup before processing any streams."""
        pass

    def teardown(self):
        """Cleanup after processing all streams."""
        if self.blobdir and os.path.exists(self.blobdir):
            self.ui.status("Removing blob dir %r ...\n" % self.blobdir)
            shutil.rmtree(self.blobdir)

    def load_marksfile(self, name):
        try:
            f = open(name)
            lines = f.readlines()
            f.close()
            parsed = [i.strip().split(' ') for i in lines]
            self.idmap = dict((i[0], i[1]) for i in parsed)
        except IOError, e:
            die("load: %s", str(e))

    def write_marksfile(self, name):
        try:
            f = open(name, "w")
            for pair in sorted(self.idmap.iteritems()):
                f.write("%s %s\n" % pair)
            f.close()
        except IOError, e:
            die("write: %s", str(e))

    def progress_handler(self, cmd):
        self.ui.write("Progress: %s\n" % cmd.message)

    def blob_handler(self, cmd):
        self.writeblob(cmd.id, cmd.data)

    def _getblobfilename(self, blobid):
        if self.blobdir is None:
            raise RuntimeError("no blobs seen, so no blob directory created")
        # XXX should escape ":" for windows
        return os.path.join(self.blobdir, "blob-" + blobid)

    def getblob(self, fileid):
        (commitid, blobid) = fileid
        f = open(self._getblobfilename(blobid), "rb")
        try:
            return f.read()
        finally:
            f.close()

    def writeblob(self, blobid, data):
        if self.blobdir is None:        # no blobs seen yet
            self.blobdir = os.path.join(self.repo.root, ".hg", "blobs")
            if not os.path.exists(self.blobdir):
                os.mkdir(self.blobdir)

        fn = self._getblobfilename(blobid)
        blobfile = open(fn, "wb")
        #self.ui.debug("writing blob %s to %s (%d bytes)\n"
        #              % (blobid, fn, len(data)))
        blobfile.write(data)
        blobfile.close()

        self.numblobs += 1
        if self.numblobs % 500 == 0:
            self.ui.status("%d blobs read\n" % self.numblobs)

    def getmode(self, name, fileid):
        (commitid, blobid) = fileid
        return self.filemodes[commitid][name]

    def checkpoint_handler(self, cmd):
        # This command means nothing to us
        pass

    def _getcommit(self, committish):
        """Given a mark reference or a branch name, return the
        appropriate commit object.  Return None if committish is a
        branch with no commits.  Raises KeyError if anything else is out
        of whack.
        """
        if committish.startswith(":"):
            # KeyError here indicates the input stream is broken.
            return self.commitmap[committish]
        else:
            branch = self._getbranch(committish)
            if branch is None:
                raise ValueError("invalid committish: %r" % committish)

            heads = self.branchmap.get(branch)
            if heads is None:
                return None
            else:
                # KeyError here indicates bad commit id in self.branchmap.
                return self.commitmap[heads[-1]]

    def _getbranch(self, ref):
        """Translate a Git head ref to corresponding Mercurial branch
        name.  E.g. \"refs/heads/foo\" is translated to \"foo\".
        Special case: \"refs/heads/master\" becomes \"default\".  If
        'ref' is not a head ref, return None.
        """
        prefix = "refs/heads/"
        if ref.startswith(prefix):
            branch = ref[len(prefix):]
            if branch == "master":
                return "default"
            else:
                return branch
        else:
            return None

    def commit_handler(self, cmd):
        # XXX this assumes the fixup branch name used by cvs2git.  In
        # contrast, git-fast-import(1) recommends "TAG_FIXUP" (not under
        # refs/heads), and implies that it can be called whatever the
        # creator of the fastimport dump wants to call it.  So the name
        # of the fixup branch should be configurable!
        fixup = (cmd.ref == "refs/heads/TAG.FIXUP")

        if cmd.from_:
            first_parent = cmd.from_
        else:
            first_parent = self._getcommit(cmd.ref) # commit object
            if first_parent is not None:
                first_parent = first_parent.rev     # commit id

        if cmd.merges:
            if len(cmd.merges) > 1:
                raise NotImplementedError("Can't handle more than two parents")
            second_parent = cmd.merges[0]
        else:
            second_parent = None

        if first_parent is None and second_parent is not None:
            # First commit on a new branch that has 'merge' but no 'from':
            # special case meaning branch starts with no files; the contents of
            # the first commit (this one) determine the list of files at branch
            # time.
            first_parent = second_parent
            second_parent = None
            no_files = True             # XXX this is ignored...

        self.ui.debug("commit %s: first_parent = %r, second_parent = %r\n"
                      % (cmd, first_parent, second_parent))
        assert ((first_parent != second_parent) or
                (first_parent is second_parent is None)), \
               ("commit %s: first_parent == second parent = %r"
                % (cmd, first_parent))

        # Figure out the Mercurial branch name.
        if fixup and first_parent is not None:
            # If this is a fixup commit, pretend it happened on the same
            # branch as its first parent.  (We don't want a Mercurial
            # named branch called "TAG.FIXUP" in the output repository.)
            branch = self.commitmap[first_parent].branch
        else:
            branch = self._getbranch(cmd.ref)

        commit_handler = HgImportCommitHandler(
            self, cmd, self.ui)
        commit_handler.process()
        modified = dict(commit_handler.modified)
        modes = commit_handler.mode
        copies = commit_handler.copies

        # in case we are converting from git or bzr, prefer author but
        # fallback to committer (committer is required, author is
        # optional)
        userinfo = cmd.author or cmd.committer
        if userinfo[0] == userinfo[1]:
            # In order to conform to fastimport syntax, cvs2git with no
            # authormap produces author names like "jsmith <jsmith>"; if
            # we see that, revert to plain old "jsmith".
            user = userinfo[0]
        else:
            user = "%s <%s>" % (userinfo[0], userinfo[1])

        assert type(cmd.message) is unicode
        text = cmd.message.encode("utf-8")
        date = self.convert_date(userinfo)

        parents = [self.idmap[i] for i in first_parent, second_parent if i]
        cmt = commit(user, date, text, parents, branch, rev=cmd.id)

        self.commitmap[cmd.id] = cmt
        heads = self.branchmap.get(branch)
        if heads is None:
            heads = [cmd.id]
        else:
            # adding to an existing branch: replace the previous head
            try:
                heads.remove(first_parent)
            except ValueError:          # first parent not a head: no problem
                pass
            heads.append(cmd.id)        # at end means this is tipmost
        self.branchmap[branch] = heads
        self.ui.debug("processed commit %s\n" % cmd)

        self.idmap[cmd.id] = self.putcommit(modified, modes, copies, cmt)

    def putcommit(self, files, modes, copies, commit):

        def getfilectx(repo, memctx, name):
            fileid = files[name]
            if fileid is None:  # deleted file
                raise IOError
            data = self.getblob(fileid)
            ctx = context.memfilectx(name, data, 'l' in modes,
                                     'x' in modes, copies.get(name))
            return ctx

        parents = list(set(commit.parents))
        nparents = len(parents)

        if len(parents) < 2:
            parents.append(nullid)
        if len(parents) < 2:
            parents.append(nullid)
        p2 = parents.pop(0)

        text = commit.desc
        extra = commit.extra.copy()
        if self.branchnames and commit.branch:
            extra['branch'] = commit.branch

        while parents:
            p1 = p2
            p2 = parents.pop(0)
            ctx = context.memctx(self.repo, (p1, p2), text, files.keys(),
                                 getfilectx, commit.author, commit.date, extra)
            self.repo.commitctx(ctx)
            text = "(octopus merge fixup)\n"
            p2 = hex(self.repo.changelog.tip())

        return p2

    def convert_date(self, c):
        res = (int(c[2]), int(c[3]))
        #print c, res
        #print type((0, 0)), type(res), len(res), type(res) is type((0, 0))
        #if type(res) is type((0, 0)) and len(res) == 2:
        #    print "go for it"
        #return res
        return "%d %d" % res

    def reset_handler(self, cmd):
        tagprefix = "refs/tags/"
        branch = self._getbranch(cmd.ref)
        if branch:
            # The usual case for 'reset': (re)create the named branch.
            # XXX what should we do if cmd.from_ is None?
            if cmd.from_ is not None:
                self.branchmap[branch] = [cmd.from_]
            else:
                # pretend the branch never existed... is this right?!?
                try:
                    del self.branchmap[branch]
                except KeyError:
                    pass
            #else:
            #    # XXX filename? line number?
            #    self.ui.warn("ignoring branch reset with no 'from'\n")
        elif cmd.ref.startswith(tagprefix):
            # Create a "lightweight tag" in Git terms.  As I understand
            # it, that's a tag with no description and no history --
            # rather like CVS tags.  cvs2git turns CVS tags into Git
            # lightweight tags, so we should make sure they become
            # Mercurial tags.  But we don't have to fake a history for
            # them; save them up for the end.
            tag = cmd.ref[len(tagprefix):]
            self.tags.append((tag, cmd.from_))

    def tag_handler(self, cmd):
        pass

    def feature_handler(self, cmd):
        if cmd.feature_name == 'done':
            return
        raise NotImplementedError(self.feature_handler)


class HgImportCommitHandler(processor.CommitHandler):

    def __init__(self, parent, command, ui):
        self.parent = parent            # HgImportProcessor running the show
        self.command = command          # CommitCommand that we're processing
        self.ui = ui

        # Files changes by this commit as a list of (filename, id)
        # tuples where id is (commitid, blobid).  The blobid is
        # needed to fetch the file's contents later, and the commitid
        # is needed to fetch the mode.
        # (XXX what about inline file contents?)
        # (XXX how to describe deleted files?)
        self.modified = []

        # mode of files listed in self.modified: '', 'x', or 'l'
        self.mode = {}

        # dictionary of src: dest (renamed files are in here and self.modified)
        self.copies = {}

        # number of inline files seen in this commit
        self.inlinecount = 0

    def modify_handler(self, filecmd):
        if filecmd.dataref:
            blobid = filecmd.dataref    # blobid is the mark of the blob
        else:
            blobid = "%s-inline:%d" % (self.command.id, self.inlinecount)
            assert filecmd.data is not None
            self.parent.writeblob(blobid, filecmd.data)
            self.inlinecount += 1

        fileid = (self.command.id, blobid)

        self.modified.append((filecmd.path, fileid))
        if filecmd.mode.endswith("644"): # normal file
            mode = ''
        elif filecmd.mode.endswith("755"): # executable
            mode = 'x'
        elif filecmd.mode == "120000":  # symlink
            mode = 'l'
        else:
            raise RuntimeError("mode %r unsupported" % filecmd.mode)

        self.mode[filecmd.path] = mode

    def delete_handler(self, filecmd):
        self.modified.append((filecmd.path, None))

    def copy_handler(self, filecmd):
        self.copies[filecmd.src_path] = filecmd.dest_path

    def rename_handler(self, filecmd):
        # copy oldname to newname and delete oldname
        self.copies[filecmd.oldname] = filecmd.newname
        self.files.append((filecmd.path, None))
