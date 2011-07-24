import os
import subprocess

from git_remote_helpers.util import check_call


def sanitize(rev, sep='\t'):
    """Converts a for-each-ref line to a name/value pair.
    """

    splitrev = rev.split(sep)
    branchval = splitrev[0]
    branchname = splitrev[1].strip()
    if branchname.startswith("refs/heads/"):
        branchname = branchname[11:]

    return branchname, branchval

def is_remote(url):
    """Checks whether the specified value is a remote url.
    """

    prefixes = ["http", "file", "git"]

    for prefix in prefixes:
        if url.startswith(prefix):
            return True
    return False

class GitRepo(object):
    """Repo object representing a repo.
    """

    def __init__(self, path):
        """Initializes a new repo at the given path.
        """

        self.path = path
        self.head = None
        self.revmap = {}
        self.local = lambda: not is_remote(self.path)

        if(self.path.endswith('.git')):
            self.gitpath = self.path
        else:
            self.gitpath = os.path.join(self.path, '.git')

        if self.local() and not os.path.exists(self.gitpath):
            os.makedirs(self.gitpath)

    def get_revs(self):
        """Fetches all revs from the remote.
        """

        args = ["git", "ls-remote", self.gitpath]
        path = ".cached_revs"
        ofile = open(path, "w")

        check_call(args, stdout=ofile)
        output = open(path).readlines()
        self.revmap = dict(sanitize(i) for i in output)
        if "HEAD" in self.revmap:
            del self.revmap["HEAD"]
        self.revs = self.revmap.keys()
        ofile.close()

    def get_head(self):
        """Determines the head of a local repo.
        """

        if not self.local():
            return

        path = os.path.join(self.gitpath, "HEAD")
        head = open(path).readline()
        self.head, _ = sanitize(head, ' ')
