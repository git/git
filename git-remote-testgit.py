#!/usr/bin/env python

import sys
import os
sys.path.insert(0, os.getenv("GITPYTHONLIB","."))

from git_remote_helpers.helper import RemoteHelper
from git_remote_helpers.util import check_output, debug
from git_remote_helpers.git.repo import GitRepo
from git_remote_helpers.git.exporter import GitExporter
from git_remote_helpers.git.importer import GitImporter
from git_remote_helpers.git.non_local import NonLocalGit


class TestgitRemoteHelper(RemoteHelper):
    def get_repo(self, alias, url):
        """Returns a git repository object initialized for usage.
        """

        repo = GitRepo(url)
        repo.get_revs()
        repo.get_head()

        prefix = 'refs/testgit/%s/' % alias
        debug("prefix: '%s'", prefix)

        repo.marksfile = 'testgit.marks'
        repo.prefix = prefix

        self.setup_repo(repo, alias)

        repo.exporter = GitExporter(repo)
        repo.importer = GitImporter(repo)
        repo.non_local = NonLocalGit(repo)

        return repo

    def local_repo(self, repo, path):
        """Returns a git repository object initalized for usage.
        """

        local = GitRepo(path)

        self.setup_local_repo(local, repo)

        local.exporter = GitExporter(local)
        local.importer = GitImporter(local)

        return local

    def do_list(self, repo, args):
        """Lists all known references.

        Bug: This will always set the remote head to master for non-local
        repositories, since we have no way of determining what the remote
        head is at clone time.
        """

        for ref in repo.revs:
            debug("? refs/heads/%s", ref)
            print "? refs/heads/%s" % ref

        if repo.head:
            debug("@refs/heads/%s HEAD" % repo.head)
            print "@refs/heads/%s HEAD" % repo.head
        else:
            debug("@refs/heads/master HEAD")
            print "@refs/heads/master HEAD"

        print # end list

    def sanitize(self, value):
        """Cleans up the url.
        """

        if value.startswith('testgit::'):
            value = value[9:]

        return value

    def get_refs(self, repo, gitdir):
        """Returns a dictionary with refs.
        """
        args = ["git", "--git-dir=" + gitdir, "for-each-ref", "refs/heads"]
        lines = check_output(args).strip().split('\n')
        refs = {}
        for line in lines:
            value, name = line.split(' ')
            name = name.strip('commit\t')
            refs[name] = value
        return refs


if __name__ == '__main__':
    sys.exit(TestgitRemoteHelper().main(sys.argv))
