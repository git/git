#!/usr/bin/env python

"""Functionality for interacting with Git repositories.

This module provides classes for interfacing with a Git repository.
"""

import os
import re
import time
from binascii import hexlify
from cStringIO import StringIO
import unittest

from git_remote_helpers.util import debug, error, die, start_command, run_command


def get_git_dir ():
    """Return the path to the GIT_DIR for this repo."""
    args = ("git", "rev-parse", "--git-dir")
    exit_code, output, errors = run_command(args)
    if exit_code:
        die("Failed to retrieve git dir")
    assert not errors
    return output.strip()


def parse_git_config ():
    """Return a dict containing the parsed version of 'git config -l'."""
    exit_code, output, errors = run_command(("git", "config", "-z", "-l"))
    if exit_code:
        die("Failed to retrieve git configuration")
    assert not errors
    return dict([e.split('\n', 1) for e in output.split("\0") if e])


def git_config_bool (value):
    """Convert the given git config string value to True or False.

    Raise ValueError if the given string was not recognized as a
    boolean value.

    """
    norm_value = str(value).strip().lower()
    if norm_value in ("true", "1", "yes", "on", ""):
        return True
    if norm_value in ("false", "0", "no", "off", "none"):
        return False
    raise ValueError("Failed to parse '%s' into a boolean value" % (value))


def valid_git_ref (ref_name):
    """Return True iff the given ref name is a valid git ref name."""
    # The following is a reimplementation of the git check-ref-format
    # command.  The rules were derived from the git check-ref-format(1)
    # manual page.  This code should be replaced by a call to
    # check_ref_format() in the git library, when such is available.
    if ref_name.endswith('/') or \
       ref_name.startswith('.') or \
       ref_name.count('/.') or \
       ref_name.count('..') or \
       ref_name.endswith('.lock'):
        return False
    for c in ref_name:
        if ord(c) < 0x20 or ord(c) == 0x7f or c in " ~^:?*[":
            return False
    return True


class GitObjectFetcher(object):

    """Provide parsed access to 'git cat-file --batch'.

    This provides a read-only interface to the Git object database.

    """

    def __init__ (self):
        """Initiate a 'git cat-file --batch' session."""
        self.queue = []  # List of object names to be submitted
        self.in_transit = None  # Object name currently in transit

        # 'git cat-file --batch' produces binary output which is likely
        # to be corrupted by the default "rU"-mode pipe opened by
        # start_command.  (Mode == "rU" does universal new-line
        # conversion, which mangles carriage returns.) Therefore, we
        # open an explicitly binary-safe pipe for transferring the
        # output from 'git cat-file --batch'.
        pipe_r_fd, pipe_w_fd = os.pipe()
        pipe_r = os.fdopen(pipe_r_fd, "rb")
        pipe_w = os.fdopen(pipe_w_fd, "wb")
        self.proc = start_command(("git", "cat-file", "--batch"),
                                  stdout = pipe_w)
        self.f = pipe_r

    def __del__ (self):
        """Verify completed communication with 'git cat-file --batch'."""
        assert not self.queue
        assert self.in_transit is None
        self.proc.stdin.close()
        assert self.proc.wait() == 0  # Zero exit code
        assert self.f.read() == ""  # No remaining output

    def _submit_next_object (self):
        """Submit queue items to the 'git cat-file --batch' process.

        If there are items in the queue, and there is currently no item
        currently in 'transit', then pop the first item off the queue,
        and submit it.

        """
        if self.queue and self.in_transit is None:
            self.in_transit = self.queue.pop(0)
            print >> self.proc.stdin, self.in_transit[0]

    def push (self, obj, callback):
        """Push the given object name onto the queue.

        The given callback function will at some point in the future
        be called exactly once with the following arguments:
        - self - this GitObjectFetcher instance
        - obj  - the object name provided to push()
        - sha1 - the SHA1 of the object, if 'None' obj is missing
        - t    - the type of the object (tag/commit/tree/blob)
        - size - the size of the object in bytes
        - data - the object contents

        """
        self.queue.append((obj, callback))
        self._submit_next_object()  # (Re)start queue processing

    def process_next_entry (self):
        """Read the next entry off the queue and invoke callback."""
        obj, cb = self.in_transit
        self.in_transit = None
        header = self.f.readline()
        if header == "%s missing\n" % (obj):
            cb(self, obj, None, None, None, None)
            return
        sha1, t, size = header.split(" ")
        assert len(sha1) == 40
        assert t in ("tag", "commit", "tree", "blob")
        assert size.endswith("\n")
        size = int(size.strip())
        data = self.f.read(size)
        assert self.f.read(1) == "\n"
        cb(self, obj, sha1, t, size, data)
        self._submit_next_object()

    def process (self):
        """Process the current queue until empty."""
        while self.in_transit is not None:
            self.process_next_entry()

    # High-level convenience methods:

    def get_sha1 (self, objspec):
        """Return the SHA1 of the object specified by 'objspec'.

        Return None if 'objspec' does not specify an existing object.

        """
        class _ObjHandler(object):
            """Helper class for getting the returned SHA1."""
            def __init__ (self, parser):
                self.parser = parser
                self.sha1 = None

            def __call__ (self, parser, obj, sha1, t, size, data):
                # FIXME: Many unused arguments. Could this be cheaper?
                assert parser == self.parser
                self.sha1 = sha1

        handler = _ObjHandler(self)
        self.push(objspec, handler)
        self.process()
        return handler.sha1

    def open_obj (self, objspec):
        """Return a file object wrapping the contents of a named object.

        The caller is responsible for calling .close() on the returned
        file object.

        Raise KeyError if 'objspec' does not exist in the repo.

        """
        class _ObjHandler(object):
            """Helper class for parsing the returned git object."""
            def __init__ (self, parser):
                """Set up helper."""
                self.parser = parser
                self.contents = StringIO()
                self.err = None

            def __call__ (self, parser, obj, sha1, t, size, data):
                """Git object callback (see GitObjectFetcher documentation)."""
                assert parser == self.parser
                if not sha1:  # Missing object
                    self.err = "Missing object '%s'" % obj
                else:
                    assert size == len(data)
                    self.contents.write(data)

        handler = _ObjHandler(self)
        self.push(objspec, handler)
        self.process()
        if handler.err:
            raise KeyError(handler.err)
        handler.contents.seek(0)
        return handler.contents

    def walk_tree (self, tree_objspec, callback, prefix = ""):
        """Recursively walk the given Git tree object.

        Recursively walk all subtrees of the given tree object, and
        invoke the given callback passing three arguments:
        (path, mode, data) with the path, permission bits, and contents
        of all the blobs found in the entire tree structure.

        """
        class _ObjHandler(object):
            """Helper class for walking a git tree structure."""
            def __init__ (self, parser, cb, path, mode = None):
                """Set up helper."""
                self.parser = parser
                self.cb = cb
                self.path = path
                self.mode = mode
                self.err = None

            def parse_tree (self, treedata):
                """Parse tree object data, yield tree entries.

                Each tree entry is a 3-tuple (mode, sha1, path)

                self.path is prepended to all paths yielded
                from this method.

                """
                while treedata:
                    mode = int(treedata[:6], 10)
                    # Turn 100xxx into xxx
                    if mode > 100000:
                        mode -= 100000
                    assert treedata[6] == " "
                    i = treedata.find("\0", 7)
                    assert i > 0
                    path = treedata[7:i]
                    sha1 = hexlify(treedata[i + 1: i + 21])
                    yield (mode, sha1, self.path + path)
                    treedata = treedata[i + 21:]

            def __call__ (self, parser, obj, sha1, t, size, data):
                """Git object callback (see GitObjectFetcher documentation)."""
                assert parser == self.parser
                if not sha1:  # Missing object
                    self.err = "Missing object '%s'" % (obj)
                    return
                assert size == len(data)
                if t == "tree":
                    if self.path:
                        self.path += "/"
                    # Recurse into all blobs and subtrees
                    for m, s, p in self.parse_tree(data):
                        parser.push(s,
                                    self.__class__(self.parser, self.cb, p, m))
                elif t == "blob":
                    self.cb(self.path, self.mode, data)
                else:
                    raise ValueError("Unknown object type '%s'" % (t))

        self.push(tree_objspec, _ObjHandler(self, callback, prefix))
        self.process()


class GitRefMap(object):

    """Map Git ref names to the Git object names they currently point to.

    Behaves like a dictionary of Git ref names -> Git object names.

    """

    def __init__ (self, obj_fetcher):
        """Create a new Git ref -> object map."""
        self.obj_fetcher = obj_fetcher
        self._cache = {}  # dict: refname -> objname

    def _load (self, ref):
        """Retrieve the object currently bound to the given ref.

        The name of the object pointed to by the given ref is stored
        into this mapping, and also returned.

        """
        if ref not in self._cache:
            self._cache[ref] = self.obj_fetcher.get_sha1(ref)
        return self._cache[ref]

    def __contains__ (self, refname):
        """Return True if the given refname is present in this cache."""
        return bool(self._load(refname))

    def __getitem__ (self, refname):
        """Return the git object name pointed to by the given refname."""
        commit = self._load(refname)
        if commit is None:
            raise KeyError("Unknown ref '%s'" % (refname))
        return commit

    def get (self, refname, default = None):
        """Return the git object name pointed to by the given refname."""
        commit = self._load(refname)
        if commit is None:
            return default
        return commit


class GitFICommit(object):

    """Encapsulate the data in a Git fast-import commit command."""

    SHA1RE = re.compile(r'^[0-9a-f]{40}$')

    @classmethod
    def parse_mode (cls, mode):
        """Verify the given git file mode, and return it as a string."""
        assert mode in (644, 755, 100644, 100755, 120000)
        return "%i" % (mode)

    @classmethod
    def parse_objname (cls, objname):
        """Return the given object name (or mark number) as a string."""
        if isinstance(objname, int):  # Object name is a mark number
            assert objname > 0
            return ":%i" % (objname)

        # No existence check is done, only checks for valid format
        assert cls.SHA1RE.match(objname)  # Object name is valid SHA1
        return objname

    @classmethod
    def quote_path (cls, path):
        """Return a quoted version of the given path."""
        path = path.replace("\\", "\\\\")
        path = path.replace("\n", "\\n")
        path = path.replace('"', '\\"')
        return '"%s"' % (path)

    @classmethod
    def parse_path (cls, path):
        """Verify that the given path is valid, and quote it, if needed."""
        assert not isinstance(path, int)  # Cannot be a mark number

        # These checks verify the rules on the fast-import man page
        assert not path.count("//")
        assert not path.endswith("/")
        assert not path.startswith("/")
        assert not path.count("/./")
        assert not path.count("/../")
        assert not path.endswith("/.")
        assert not path.endswith("/..")
        assert not path.startswith("./")
        assert not path.startswith("../")

        if path.count('"') + path.count('\n') + path.count('\\'):
            return cls.quote_path(path)
        return path

    def __init__ (self, name, email, timestamp, timezone, message):
        """Create a new Git fast-import commit, with the given metadata."""
        self.name = name
        self.email = email
        self.timestamp = timestamp
        self.timezone = timezone
        self.message = message
        self.pathops = []  # List of path operations in this commit

    def modify (self, mode, blobname, path):
        """Add a file modification to this Git fast-import commit."""
        self.pathops.append(("M",
                             self.parse_mode(mode),
                             self.parse_objname(blobname),
                             self.parse_path(path)))

    def delete (self, path):
        """Add a file deletion to this Git fast-import commit."""
        self.pathops.append(("D", self.parse_path(path)))

    def copy (self, path, newpath):
        """Add a file copy to this Git fast-import commit."""
        self.pathops.append(("C",
                             self.parse_path(path),
                             self.parse_path(newpath)))

    def rename (self, path, newpath):
        """Add a file rename to this Git fast-import commit."""
        self.pathops.append(("R",
                             self.parse_path(path),
                             self.parse_path(newpath)))

    def note (self, blobname, commit):
        """Add a note object to this Git fast-import commit."""
        self.pathops.append(("N",
                             self.parse_objname(blobname),
                             self.parse_objname(commit)))

    def deleteall (self):
        """Delete all files in this Git fast-import commit."""
        self.pathops.append("deleteall")


class TestGitFICommit(unittest.TestCase):

    """GitFICommit selftests."""

    def test_basic (self):
        """GitFICommit basic selftests."""

        def expect_fail (method, data):
            """Verify that the method(data) raises an AssertionError."""
            try:
                method(data)
            except AssertionError:
                return
            raise AssertionError("Failed test for invalid data '%s(%s)'" %
                                 (method.__name__, repr(data)))

    def test_parse_mode (self):
        """GitFICommit.parse_mode() selftests."""
        self.assertEqual(GitFICommit.parse_mode(644), "644")
        self.assertEqual(GitFICommit.parse_mode(755), "755")
        self.assertEqual(GitFICommit.parse_mode(100644), "100644")
        self.assertEqual(GitFICommit.parse_mode(100755), "100755")
        self.assertEqual(GitFICommit.parse_mode(120000), "120000")
        self.assertRaises(AssertionError, GitFICommit.parse_mode, 0)
        self.assertRaises(AssertionError, GitFICommit.parse_mode, 123)
        self.assertRaises(AssertionError, GitFICommit.parse_mode, 600)
        self.assertRaises(AssertionError, GitFICommit.parse_mode, "644")
        self.assertRaises(AssertionError, GitFICommit.parse_mode, "abc")

    def test_parse_objname (self):
        """GitFICommit.parse_objname() selftests."""
        self.assertEqual(GitFICommit.parse_objname(1), ":1")
        self.assertRaises(AssertionError, GitFICommit.parse_objname, 0)
        self.assertRaises(AssertionError, GitFICommit.parse_objname, -1)
        self.assertEqual(GitFICommit.parse_objname("0123456789" * 4),
                         "0123456789" * 4)
        self.assertEqual(GitFICommit.parse_objname("2468abcdef" * 4),
                         "2468abcdef" * 4)
        self.assertRaises(AssertionError, GitFICommit.parse_objname,
                          "abcdefghij" * 4)

    def test_parse_path (self):
        """GitFICommit.parse_path() selftests."""
        self.assertEqual(GitFICommit.parse_path("foo/bar"), "foo/bar")
        self.assertEqual(GitFICommit.parse_path("path/with\n and \" in it"),
                         '"path/with\\n and \\" in it"')
        self.assertRaises(AssertionError, GitFICommit.parse_path, 1)
        self.assertRaises(AssertionError, GitFICommit.parse_path, 0)
        self.assertRaises(AssertionError, GitFICommit.parse_path, -1)
        self.assertRaises(AssertionError, GitFICommit.parse_path, "foo//bar")
        self.assertRaises(AssertionError, GitFICommit.parse_path, "foo/bar/")
        self.assertRaises(AssertionError, GitFICommit.parse_path, "/foo/bar")
        self.assertRaises(AssertionError, GitFICommit.parse_path, "foo/./bar")
        self.assertRaises(AssertionError, GitFICommit.parse_path, "foo/../bar")
        self.assertRaises(AssertionError, GitFICommit.parse_path, "foo/bar/.")
        self.assertRaises(AssertionError, GitFICommit.parse_path, "foo/bar/..")
        self.assertRaises(AssertionError, GitFICommit.parse_path, "./foo/bar")
        self.assertRaises(AssertionError, GitFICommit.parse_path, "../foo/bar")


class GitFastImport(object):

    """Encapsulate communication with git fast-import."""

    def __init__ (self, f, obj_fetcher, last_mark = 0):
        """Set up self to communicate with a fast-import process through f."""
        self.f = f  # File object where fast-import stream is written
        self.obj_fetcher = obj_fetcher  # GitObjectFetcher instance
        self.next_mark = last_mark + 1  # Next mark number
        self.refs = set()  # Keep track of the refnames we've seen

    def comment (self, s):
        """Write the given comment in the fast-import stream."""
        assert "\n" not in s, "Malformed comment: '%s'" % (s)
        self.f.write("# %s\n" % (s))

    def commit (self, ref, commitdata):
        """Make a commit on the given ref, with the given GitFICommit.

        Return the mark number identifying this commit.

        """
        self.f.write("""\
commit %(ref)s
mark :%(mark)i
committer %(name)s <%(email)s> %(timestamp)i %(timezone)s
data %(msgLength)i
%(msg)s
""" % {
    'ref': ref,
    'mark': self.next_mark,
    'name': commitdata.name,
    'email': commitdata.email,
    'timestamp': commitdata.timestamp,
    'timezone': commitdata.timezone,
    'msgLength': len(commitdata.message),
    'msg': commitdata.message,
})

        if ref not in self.refs:
            self.refs.add(ref)
            parent = ref + "^0"
            if self.obj_fetcher.get_sha1(parent):
                self.f.write("from %s\n" % (parent))

        for op in commitdata.pathops:
            self.f.write(" ".join(op))
            self.f.write("\n")
        self.f.write("\n")
        retval = self.next_mark
        self.next_mark += 1
        return retval

    def blob (self, data):
        """Import the given blob.

        Return the mark number identifying this blob.

        """
        self.f.write("blob\nmark :%i\ndata %i\n%s\n" %
                     (self.next_mark, len(data), data))
        retval = self.next_mark
        self.next_mark += 1
        return retval

    def reset (self, ref, objname):
        """Reset the given ref to point at the given Git object."""
        self.f.write("reset %s\nfrom %s\n\n" %
                     (ref, GitFICommit.parse_objname(objname)))
        if ref not in self.refs:
            self.refs.add(ref)


class GitNotes(object):

    """Encapsulate access to Git notes.

    Simulates a dictionary of object name (SHA1) -> Git note mappings.

    """

    def __init__ (self, notes_ref, obj_fetcher):
        """Create a new Git notes interface, bound to the given notes ref."""
        self.notes_ref = notes_ref
        self.obj_fetcher = obj_fetcher  # Used to get objects from repo
        self.imports = []  # list: (objname, note data blob name) tuples

    def __del__ (self):
        """Verify that self.commit_notes() was called before destruction."""
        if self.imports:
            error("Missing call to self.commit_notes().")
            error("%i notes are not committed!", len(self.imports))

    def _load (self, objname):
        """Return the note data associated with the given git object.

        The note data is returned in string form. If no note is found
        for the given object, None is returned.

        """
        try:
            f = self.obj_fetcher.open_obj("%s:%s" % (self.notes_ref, objname))
            ret = f.read()
            f.close()
        except KeyError:
            ret = None
        return ret

    def __getitem__ (self, objname):
        """Return the note contents associated with the given object.

        Raise KeyError if given object has no associated note.

        """
        blobdata = self._load(objname)
        if blobdata is None:
            raise KeyError("Object '%s' has no note" % (objname))
        return blobdata

    def get (self, objname, default = None):
        """Return the note contents associated with the given object.

        Return given default if given object has no associated note.

        """
        blobdata = self._load(objname)
        if blobdata is None:
            return default
        return blobdata

    def import_note (self, objname, data, gfi):
        """Tell git fast-import to store data as a note for objname.

        This method uses the given GitFastImport object to create a
        blob containing the given note data.  Also an entry mapping the
        given object name to the created blob is stored until
        commit_notes() is called.

        Note that this method only works if it is later followed by a
        call to self.commit_notes() (which produces the note commit
        that refers to the blob produced here).

        """
        if not data.endswith("\n"):
            data += "\n"
        gfi.comment("Importing note for object %s" % (objname))
        mark = gfi.blob(data)
        self.imports.append((objname, mark))

    def commit_notes (self, gfi, author, message):
        """Produce a git fast-import note commit for the imported notes.

        This method uses the given GitFastImport object to create a
        commit on the notes ref, introducing the notes previously
        submitted to import_note().

        """
        if not self.imports:
            return
        commitdata = GitFICommit(author[0], author[1],
                                 time.time(), "0000", message)
        for objname, blobname in self.imports:
            assert isinstance(objname, int) and objname > 0
            assert isinstance(blobname, int) and blobname > 0
            commitdata.note(blobname, objname)
        gfi.commit(self.notes_ref, commitdata)
        self.imports = []


class GitCachedNotes(GitNotes):

    """Encapsulate access to Git notes (cached version).

    Only use this class if no caching is done at a higher level.

    Simulates a dictionary of object name (SHA1) -> Git note mappings.

    """

    def __init__ (self, notes_ref, obj_fetcher):
        """Set up a caching wrapper around GitNotes."""
        GitNotes.__init__(self, notes_ref, obj_fetcher)
        self._cache = {}  # Cache: object name -> note data

    def __del__ (self):
        """Verify that GitNotes' destructor is called."""
        GitNotes.__del__(self)

    def _load (self, objname):
        """Extend GitNotes._load() with a local objname -> note cache."""
        if objname not in self._cache:
            self._cache[objname] = GitNotes._load(self, objname)
        return self._cache[objname]

    def import_note (self, objname, data, gfi):
        """Extend GitNotes.import_note() with a local objname -> note cache."""
        if not data.endswith("\n"):
            data += "\n"
        assert objname not in self._cache
        self._cache[objname] = data
        GitNotes.import_note(self, objname, data, gfi)


if __name__ == '__main__':
    unittest.main()
