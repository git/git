import os
import subprocess
import sys

from git_remote_helpers.util import check_call


class GitExporter(object):
    """An exporter for testgit repositories.

    The exporter simply delegates to git fast-export.
    """

    def __init__(self, repo):
        """Creates a new exporter for the specified repo.
        """

        self.repo = repo

    def export_repo(self, base, refs=None):
        """Exports a fast-export stream for the given directory.

        Simply delegates to git fast-epxort and pipes it through sed
        to make the refs show up under the prefix rather than the
        default refs/heads. This is to demonstrate how the export
        data can be stored under it's own ref (using the refspec
        capability).

        If None, refs defaults to ["HEAD"].
        """

        if not refs:
            refs = ["HEAD"]

        dirname = self.repo.get_base_path(base)
        path = os.path.abspath(os.path.join(dirname, 'testgit.marks'))

        if not os.path.exists(dirname):
            os.makedirs(dirname)

        print "feature relative-marks"
        if os.path.exists(os.path.join(dirname, 'git.marks')):
            print "feature import-marks=%s/git.marks" % self.repo.hash
        print "feature export-marks=%s/git.marks" % self.repo.hash
        sys.stdout.flush()

        args = ["git", "--git-dir=" + self.repo.gitpath, "fast-export", "--export-marks=" + path]

        if os.path.exists(path):
            args.append("--import-marks=" + path)

        args.extend(refs)

        p1 = subprocess.Popen(args, stdout=subprocess.PIPE)

        args = ["sed", "s_refs/heads/_" + self.repo.prefix + "_g"]

        check_call(args, stdin=p1.stdout)
