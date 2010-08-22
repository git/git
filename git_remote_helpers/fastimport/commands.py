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

"""Import command classes."""

import os

# There is a bug in git 1.5.4.3 and older by which unquoting a string consumes
# one extra character. Set this variable to True to work-around it. It only
# happens when renaming a file whose name contains spaces and/or quotes, and
# the symptom is:
#   % git-fast-import
#   fatal: Missing space after source: R "file 1.txt" file 2.txt
# http://git.kernel.org/?p=git/git.git;a=commit;h=c8744d6a8b27115503565041566d97c21e722584
GIT_FAST_IMPORT_NEEDS_EXTRA_SPACE_AFTER_QUOTE = False


# Lists of command names
COMMAND_NAMES = ['blob', 'checkpoint', 'commit', 'feature', 'progress',
    'reset', 'tag']
FILE_COMMAND_NAMES = ['filemodify', 'filedelete', 'filecopy', 'filerename',
    'filedeleteall']


# Feature names
MULTIPLE_AUTHORS_FEATURE = "multiple-authors"
COMMIT_PROPERTIES_FEATURE = "commit-properties"
EMPTY_DIRS_FEATURE = "empty-directories"
FEATURE_NAMES = [
    MULTIPLE_AUTHORS_FEATURE,
    COMMIT_PROPERTIES_FEATURE,
    EMPTY_DIRS_FEATURE,
    ]


# for classes with no meaningful __str__()
def _simplerepr(self):
    return "<%s at 0x%x>" % (self.__class__.__name__, id(self))

# classes that define __str__() should use this instead
def _detailrepr(self):
    return ("<%s at 0x%x: %s>"
            % (self.__class__.__name__, id(self), str(self)))


class ImportCommand(object):
    """Base class for import commands."""

    def __init__(self, name):
        self.name = name
        # List of field names not to display
        self._binary = []

    __repr__ = _simplerepr

    def format(self):
        """Format this command as a fastimport dump fragment.

        Returns a (possibly multiline) string that, if seen in a
        fastimport stream, would parse to an equivalent command object.
        """
        raise NotImplementedError("abstract method")

    def dump_str(self, names=None, child_lists=None, verbose=False):
        """Dump fields as a string.

        :param names: the list of fields to include or
            None for all public fields
        :param child_lists: dictionary of child command names to
            fields for that child command to include
        :param verbose: if True, prefix each line with the command class and
            display fields as a dictionary; if False, dump just the field
            values with tabs between them
        """
        interesting = {}
        if names is None:
            fields = [k for k in self.__dict__.keys() if not k.startswith('_')]
        else:
            fields = names
        for field in fields:
            value = self.__dict__.get(field)
            if field in self._binary and value is not None:
                value = '(...)'
            interesting[field] = value
        if verbose:
            return "%s: %s" % (self.__class__.__name__, interesting)
        else:
            return "\t".join([repr(interesting[k]) for k in fields])


class _MarkMixin(object):
    """mixin for fastimport commands with a mark: blob, commit."""
    def __init__(self, mark, location):
        self.mark= mark
        self.location = location

        # Provide a unique id in case the mark is missing
        if mark is None:
            self.id = '%s@%d' % (os.path.basename(location[0]), location[1])
        else:
            self.id = ':%s' % mark

    def __str__(self):
        return self.id

    __repr__ = _detailrepr


class BlobCommand(ImportCommand, _MarkMixin):

    def __init__(self, mark, data, location):
        ImportCommand.__init__(self, 'blob')
        _MarkMixin.__init__(self, mark, location)
        self.data = data
        self._binary = ['data']

    def format(self):
        if self.mark is None:
            mark_line = ""
        else:
            mark_line = "\nmark :%s" % self.mark
        return "blob%s\ndata %d\n%s" % (mark_line, len(self.data), self.data)


class CheckpointCommand(ImportCommand):

    def __init__(self):
        ImportCommand.__init__(self, 'checkpoint')

    def format(self):
        return "checkpoint"


class CommitCommand(ImportCommand, _MarkMixin):

    def __init__(self, ref, mark, author, committer, message, from_,
        merges, file_cmds, location=None, more_authors=None, properties=None):
        ImportCommand.__init__(self, 'commit')
        _MarkMixin.__init__(self, mark, location)
        self.ref = ref
        self.author = author
        self.committer = committer
        self.message = message
        self.from_ = from_
        self.merges = merges
        self.file_cmds = file_cmds
        self.more_authors = more_authors
        self.properties = properties
        self._binary = ['file_cmds']

    def format(self, use_features=True, include_file_contents=True):
        if self.mark is None:
            mark_line = ""
        else:
            mark_line = "\nmark :%s" % self.mark
        if self.author is None:
            author_section = ""
        else:
            author_section = "\nauthor %s" % format_who_when(self.author)
            if use_features and self.more_authors:
                for author in self.more_authors:
                    author_section += "\nauthor %s" % format_who_when(author)
        committer = "committer %s" % format_who_when(self.committer)
        if self.message is None:
            msg_section = ""
        else:
            msg = self.message.encode('utf8')
            msg_section = "\ndata %d\n%s" % (len(msg), msg)
        if self.from_ is None:
            from_line = ""
        else:
            from_line = "\nfrom %s" % self.from_
        if self.merges is None:
            merge_lines = ""
        else:
            merge_lines = "".join(["\nmerge %s" % (m,)
                for m in self.merges])
        if use_features and self.properties:
            property_lines = []
            for name in sorted(self.properties):
                value = self.properties[name]
                property_lines.append("\n" + format_property(name, value))
            properties_section = "".join(property_lines)
        else:
            properties_section = ""
        if self.file_cmds is None:
            filecommands = ""
        else:
            if include_file_contents:
                format_str = "\n%r"
            else:
                format_str = "\n%s"
            filecommands = "".join(
                ["\n" + fc.format() for fc in self.file_cmds])
        return "commit %s%s%s\n%s%s%s%s%s%s" % (self.ref, mark_line,
            author_section, committer, msg_section, from_line, merge_lines,
            properties_section, filecommands)

    def dump_str(self, names=None, child_lists=None, verbose=False):
        result = [ImportCommand.dump_str(self, names, verbose=verbose)]
        for f in self.file_cmds:
            if child_lists is None:
                continue
            try:
                child_names = child_lists[f.name]
            except KeyError:
                continue
            result.append("\t%s" % f.dump_str(child_names, verbose=verbose))
        return '\n'.join(result)


class FeatureCommand(ImportCommand):

    def __init__(self, feature_name, value=None, location=None):
        ImportCommand.__init__(self, 'feature')
        self.feature_name = feature_name
        self.value = value
        self.location = location

    def format(self):
        if self.value is None:
            value_text = ""
        else:
            value_text = "=%s" % self.value
        return "feature %s%s" % (self.feature_name, value_text)


class ProgressCommand(ImportCommand):

    def __init__(self, message):
        ImportCommand.__init__(self, 'progress')
        self.message = message

    def format(self):
        return "progress %s" % (self.message,)


class ResetCommand(ImportCommand):

    def __init__(self, ref, from_):
        ImportCommand.__init__(self, 'reset')
        self.ref = ref
        self.from_ = from_

    def format(self):
        if self.from_ is None:
            from_line = ""
        else:
            # According to git-fast-import(1), the extra LF is optional here;
            # however, versions of git up to 1.5.4.3 had a bug by which the LF
            # was needed. Always emit it, since it doesn't hurt and maintains
            # compatibility with older versions.
            # http://git.kernel.org/?p=git/git.git;a=commit;h=655e8515f279c01f525745d443f509f97cd805ab
            from_line = "\nfrom %s\n" % self.from_
        return "reset %s%s" % (self.ref, from_line)


class TagCommand(ImportCommand):

    def __init__(self, id, from_, tagger, message):
        ImportCommand.__init__(self, 'tag')
        self.id = id
        self.from_ = from_
        self.tagger = tagger
        self.message = message

    def __str__(self):
        return self.id

    __repr__ = _detailrepr

    def format(self):
        if self.from_ is None:
            from_line = ""
        else:
            from_line = "\nfrom %s" % self.from_
        if self.tagger is None:
            tagger_line = ""
        else:
            tagger_line = "\ntagger %s" % format_who_when(self.tagger)
        if self.message is None:
            msg_section = ""
        else:
            msg = self.message.encode('utf8')
            msg_section = "\ndata %d\n%s" % (len(msg), msg)
        return "tag %s%s%s%s" % (self.id, from_line, tagger_line, msg_section)


class FileCommand(ImportCommand):
    """Base class for file commands."""
    pass


class FileModifyCommand(FileCommand):

    def __init__(self, path, mode, dataref, data):
        # Either dataref or data should be null
        FileCommand.__init__(self, 'filemodify')
        self.path = check_path(path)
        self.mode = mode
        self.dataref = dataref
        self.data = data
        self._binary = ['data']

    def __str__(self):
        return self.path

    __repr__ = _detailrepr

    def format(self, include_file_contents=True):
        datastr = ""
        if self.dataref is None:
            dataref = "inline"
            if include_file_contents:
                datastr = "\ndata %d\n%s" % (len(self.data), self.data)
        else:
            dataref = "%s" % (self.dataref,)
        path = format_path(self.path)
        return "M %s %s %s%s" % (self.mode, dataref, path, datastr)

    def is_regular(self):
        """Return true if this is a regular file (mode 644)."""
        return self.mode.endswith("644")

    def is_executable(self):
        """Return true if this is an executable file (mode 755)."""
        return self.mode.endswith("755")

    def is_symlink(self):
        """Return true if this is a symlink (mode 120000)."""
        return self.mode == "120000"

    def is_gitlink(self):
        """Return true if this is a gitlink (mode 160000)."""
        return self.mode == "160000"


class FileDeleteCommand(FileCommand):

    def __init__(self, path):
        FileCommand.__init__(self, 'filedelete')
        self.path = check_path(path)

    def __str__(self):
        return self.path

    __repr__ = _detailrepr

    def format(self):
        return "D %s" % (format_path(self.path),)


class FileCopyCommand(FileCommand):

    def __init__(self, src_path, dest_path):
        FileCommand.__init__(self, 'filecopy')
        self.src_path = check_path(src_path)
        self.dest_path = check_path(dest_path)

    def __str__(self):
        return "%s -> %s" % (self.src_path, self.dest_path)

    __repr__ = _detailrepr

    def format(self):
        return "C %s %s" % (
            format_path(self.src_path, quote_spaces=True),
            format_path(self.dest_path))


class FileRenameCommand(FileCommand):

    def __init__(self, old_path, new_path):
        FileCommand.__init__(self, 'filerename')
        self.old_path = check_path(old_path)
        self.new_path = check_path(new_path)

    def __str__(self):
        return "%s -> %s" % (self.old_path, self.new_path)

    __repr__ = _detailrepr

    def format(self):
        return "R %s %s" % (
            format_path(self.old_path, quote_spaces=True),
            format_path(self.new_path))


class FileDeleteAllCommand(FileCommand):

    def __init__(self):
        FileCommand.__init__(self, 'filedeleteall')

    def format(self):
        return "deleteall"


def check_path(path):
    """Check that a path is legal.

    :return: the path if all is OK
    :raise ValueError: if the path is illegal
    """
    if path is None or path == '':
        raise ValueError("illegal path '%s'" % path)
    return path


def format_path(p, quote_spaces=False):
    """Format a path in utf8, quoting it if necessary."""
    if '\n' in p:
        import re
        p = re.sub('\n', '\\n', p)
        quote = True
    else:
        quote = p[0] == '"' or (quote_spaces and ' ' in p)
    if quote:
        extra = GIT_FAST_IMPORT_NEEDS_EXTRA_SPACE_AFTER_QUOTE and ' ' or ''
        p = '"%s"%s' % (p, extra)
    return p.encode('utf8')


def format_who_when(fields):
    """Format a tuple of name,email,secs-since-epoch,utc-offset-secs as a string."""
    offset = fields[3]
    if offset < 0:
        offset_sign = '-'
        offset = abs(offset)
    else:
        offset_sign = '+'
    offset_hours = offset / 3600
    offset_minutes = offset / 60 - offset_hours * 60
    offset_str = "%s%02d%02d" % (offset_sign, offset_hours, offset_minutes)
    name = fields[0]
    if name == '':
        sep = ''
    else:
        sep = ' '
    if isinstance(name, unicode):
        name = name.encode('utf8')
    email = fields[1]
    if isinstance(email, unicode):
        email = email.encode('utf8')
    result = "%s%s<%s> %d %s" % (name, sep, email, fields[2], offset_str)
    return result


def format_property(name, value):
    """Format the name and value (both unicode) of a property as a string."""
    utf8_name = name.encode('utf8')
    if value is not None:
        utf8_value = value.encode('utf8')
        result = "property %s %d %s" % (utf8_name, len(utf8_value), utf8_value)
    else:
        result = "property %s" % (utf8_name,)
    return result
