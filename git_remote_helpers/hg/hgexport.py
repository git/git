import binascii
import os.path
import sys


LF = '\n'
SP = ' '


class HgExportGenerator(object):
    def __init__(self, repo):
        self.git_hg = repo.git_hg
        self.repo = repo
        self.prefix = repo.prefix
        self.nullref = "0" * 40
        self.next_id = 0
        self.mapping = {}
        self.debugging = True

    def nextid(self):
        self.next_id += 1
        return self.next_id

    def tohex(self, binhex):
        return binascii.hexlify(binhex)

    def mode(self, fctx):
        flags = fctx.flags()

        if 'l' in flags:
          mode = '120000'
        elif 'x' in flags:
          mode = '100755'
        else:
          mode = '100644'

        return mode

    def parents(self, parents):
        parents = [self.tohex(i.node()) for i in parents]
        parents = [i for i in parents if i != self.nullref]
        assert all(i in self.mapping for i in parents)
        parents = [':%d' % self.mapping[i] for i in parents]

        return parents

    def ref(self, ctx):
        return self.prefix + ctx.branch()

    def write(self, *args):
        msg = ''.join([str(i) for i in args])
        sys.stdout.write(msg)

    def debug(self, msg):
        assert LF not in msg
        self.write('#', SP, msg, LF)

    def feature(self, feature, value=None):
        if value:
            self.write('feature', SP, feature, '=', value, LF)
        else:
            self.write('feature', SP, feature, LF)

    def option(self, option, value=None):
        if value:
            self.write('option', SP, option, '=', value, LF)
        else:
            self.write('option', SP, option, LF)

    def option_quiet(self):
        self.option('quiet')

    def feature_relative_marks(self):
        self.feature('relative-marks')

    def feature_export_marks(self, marks):
        self.feature('export-marks', marks)

    def feature_import_marks(self, marks):
        self.feature('import-marks', marks)

    def feature_force(self):
        self.feature('force')

    def progress(self, message):
        self.write('progress', SP, message, LF)

    def write_data(self, data):
        count = len(data)
        self.write('data', SP, count, LF)
        self.write(data, LF)

    def write_mark(self, idnum):
        self.write('mark', SP, ':', idnum, LF)

    def write_blob(self, data, idnum):
        self.write('blob', LF)
        self.write_mark(idnum)
        self.write_data(data)

    def write_file(self, ctx, file, idnum):
        fctx = ctx.filectx(file)
        data = fctx.data()

        self.write_blob(data, idnum)

    def write_commit(self, ref):
        self.write('commit', SP, ref, LF)

    def write_author(self, author):
        self.write('author', SP, author, LF)

    def write_committer(self, committer):
        self.write('committer', SP, committer, LF)

    def write_from(self, parent):
        self.write('from', SP, parent, LF)

    def write_merge(self, parent):
        self.write('merge', SP, parent, LF)

    def write_reset(self, ref, idnum):
        self.write('reset', SP, ref, LF)
        self.write('from', SP, ':', idnum, LF)

    def write_parents(self, parents):
        parents = self.parents(parents)

        # first commit
        if not parents:
            return

        parent = parents[0]

        self.write_from(parent)

        for parent in parents[1:]:
            self.write_merge(parent)

    def write_filedeleteall(self):
        self.write('deleteall', LF)

    def write_filedelete(self, ctx, name):
        self.write('D', SP, name, LF)

    def write_filemodify_mark(self, mode, name, mark):
        self.write('M', SP, mode, SP, ':', mark, SP, name, LF)

    def write_filemodify_inline(self, mode, name, data):
        self.write('M', SP, mode, SP, 'inline', SP, name, LF)
        self.write_data(data)

    def write_filemodify(self, ctx, name):
        fctx = ctx.filectx(name)
        man = ctx.manifest()
        nodesha = man[name]
        hash = self.tohex(nodesha)
        mode = self.mode(fctx)

        if hash in self.mapping:
            mark = self.mapping[hash]
            self.write_filemodify_mark(mode, name, mark)
        else:
            data = fctx.data()
            self.write_filemodify_inline(mode, name, data)

    def write_files(self, ctx):
        man = ctx.manifest()

        if len(ctx.parents()) == 2:
            self.write_filedeleteall()
            for name in man:
                self.write_filemodify(ctx, name)
        else:
            for name in ctx.files():
                # file got deleted
                if name not in man:
                    self.write_filedelete(ctx, name)
                else:
                    self.write_filemodify(ctx, name)

    def export_files(self, ctx):
        man = ctx.manifest()

        for name in [i for i in ctx.files() if i in man]:
            idnum = self.nextid()
            nodesha = man[name]
            hash = self.tohex(nodesha)

            self.write_file(ctx, name, idnum)
            self.mapping[hash] = idnum

    def export_commit(self, ctx, ref, idnum, msg, parents):
        author = self.git_hg.get_author(ctx)
        committer = self.git_hg.get_committer(ctx)
        committer = committer if committer else author

        self.debug('exporting commit')
        self.write_commit(ref)
        self.write_mark(idnum)
        self.write_author(author)
        self.write_committer(committer)
        self.write_data(msg)
        self.write_parents(parents)
        self.write_files(ctx)
        self.debug('commit exported')

    def export_revision(self, ctx):
        nodesha = ctx.node()
        hash = self.tohex(nodesha)

        if hash in self.mapping:
            return False

        self.export_files(ctx)

        idnum = self.nextid()

        ref = self.ref(ctx)
        msg = self.git_hg.get_message(ctx)
        parents = self.git_hg.get_parents(ctx)

        self.export_commit(ctx, ref, idnum, msg, parents)
        self.mapping[hash] = idnum

        return True

    def export_branch(self, name, rev):
        ctx = self.repo.changectx(rev)
        nodesha = ctx.node()
        hash = self.tohex(nodesha)
        idnum = self.mapping[hash]

        ref = self.prefix + name

        self.write_reset(ref, idnum)

    def export_repo(self, refs):
        self.option_quiet()
        self.feature_force()

        exported = printed = False

        for rev in self.repo.changelog:
            ctx = self.repo.changectx(rev)
            exported = self.export_revision(ctx) or exported

            if (exported and not printed) or (exported and rev%1000 == 0):
                self.progress("Exported revision %d.\n" % rev)
                printed = True

    def write_marks(self, base):
        dirname = self.repo.get_base_path(base)
        path = os.path.join(dirname, 'hg.marks')
        if not os.path.exists(dirname):
            os.makedirs(dirname)
        f = open(path, 'w') #self.repo.opener(self.marksfile, 'w', atomictemp=True)

        second = lambda (a, b): b

        for hash, mark in sorted(self.mapping.iteritems(), key=second):
            f.write(':%d %s\n' % (mark, hash))

        f.close() #f.rename()

    def read_marks(self, base):
        dirname = self.repo.get_base_path(base)
        path = os.path.join(dirname, 'hg.marks')

        if not os.path.exists(path):
            sys.stderr.write("warning: cannot find " + path)
            return

        f = open(path) #self.repo.opener(self.marksfile)

        marks = [i.strip().split(' ') for i in f.readlines()]

        self.mapping = dict((i[1], int(i[0][1:])) for i in marks)
        self.next_id = max(self.mapping.values())

