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

import warnings

"""Parser of import data into command objects.

In order to reuse existing front-ends, the stream format is a subset of
the one used by git-fast-import (as of the 1.5.4 release of git at least).
The grammar is:

  stream ::= cmd*;

  cmd ::= new_blob
        | new_commit
        | new_tag
        | reset_branch
        | checkpoint
        | progress
        ;

  new_blob ::= 'blob' lf
    mark?
    file_content;
  file_content ::= data;

  new_commit ::= 'commit' sp ref_str lf
    mark?
    ('author' sp name '<' email '>' when lf)?
    'committer' sp name '<' email '>' when lf
    commit_msg
    ('from' sp (ref_str | hexsha1 | sha1exp_str | idnum) lf)?
    ('merge' sp (ref_str | hexsha1 | sha1exp_str | idnum) lf)*
    file_change*
    lf?;
  commit_msg ::= data;

  file_change ::= file_clr
    | file_del
    | file_rnm
    | file_cpy
    | file_obm
    | file_inm;
  file_clr ::= 'deleteall' lf;
  file_del ::= 'D' sp path_str lf;
  file_rnm ::= 'R' sp path_str sp path_str lf;
  file_cpy ::= 'C' sp path_str sp path_str lf;
  file_obm ::= 'M' sp mode sp (hexsha1 | idnum) sp path_str lf;
  file_inm ::= 'M' sp mode sp 'inline' sp path_str lf
    data;

  new_tag ::= 'tag' sp tag_str lf
    'from' sp (ref_str | hexsha1 | sha1exp_str | idnum) lf
    'tagger' sp name '<' email '>' when lf
    tag_msg;
  tag_msg ::= data;

  reset_branch ::= 'reset' sp ref_str lf
    ('from' sp (ref_str | hexsha1 | sha1exp_str | idnum) lf)?
    lf?;

  checkpoint ::= 'checkpoint' lf
    lf?;

  progress ::= 'progress' sp not_lf* lf
    lf?;

     # note: the first idnum in a stream should be 1 and subsequent
     # idnums should not have gaps between values as this will cause
     # the stream parser to reserve space for the gapped values.  An
     # idnum can be updated in the future to a new object by issuing
     # a new mark directive with the old idnum.
     #
  mark ::= 'mark' sp idnum lf;
  data ::= (delimited_data | exact_data)
    lf?;

    # note: delim may be any string but must not contain lf.
    # data_line may contain any data but must not be exactly
    # delim. The lf after the final data_line is included in
    # the data.
  delimited_data ::= 'data' sp '<<' delim lf
    (data_line lf)*
    delim lf;

     # note: declen indicates the length of binary_data in bytes.
     # declen does not include the lf preceeding the binary data.
     #
  exact_data ::= 'data' sp declen lf
    binary_data;

     # note: quoted strings are C-style quoting supporting \c for
     # common escapes of 'c' (e..g \n, \t, \\, \") or \nnn where nnn
     # is the signed byte value in octal.  Note that the only
     # characters which must actually be escaped to protect the
     # stream formatting is: \, \" and LF.  Otherwise these values
     # are UTF8.
     #
  ref_str     ::= ref;
  sha1exp_str ::= sha1exp;
  tag_str     ::= tag;
  path_str    ::= path    | '"' quoted(path)    '"' ;
  mode        ::= '100644' | '644'
                | '100755' | '755'
                | '120000'
                ;

  declen ::= # unsigned 32 bit value, ascii base10 notation;
  bigint ::= # unsigned integer value, ascii base10 notation;
  binary_data ::= # file content, not interpreted;

  when         ::= raw_when | rfc2822_when;
  raw_when     ::= ts sp tz;
  rfc2822_when ::= # Valid RFC 2822 date and time;

  sp ::= # ASCII space character;
  lf ::= # ASCII newline (LF) character;

     # note: a colon (':') must precede the numerical value assigned to
     # an idnum.  This is to distinguish it from a ref or tag name as
     # GIT does not permit ':' in ref or tag strings.
     #
  idnum   ::= ':' bigint;
  path    ::= # GIT style file path, e.g. \"a/b/c\";
  ref     ::= # GIT ref name, e.g. \"refs/heads/MOZ_GECKO_EXPERIMENT\";
  tag     ::= # GIT tag name, e.g. \"FIREFOX_1_5\";
  sha1exp ::= # Any valid GIT SHA1 expression;
  hexsha1 ::= # SHA1 in hexadecimal format;

     # note: name and email are UTF8 strings, however name must not
     # contain '<' or lf and email must not contain any of the
     # following: '<', '>', lf.
     #
  name  ::= # valid GIT author/committer name;
  email ::= # valid GIT author/committer email;
  ts    ::= # time since the epoch in seconds, ascii base10 notation;
  tz    ::= # GIT style timezone;

     # note: comments may appear anywhere in the input, except
     # within a data command.  Any form of the data command
     # always escapes the related input from comment processing.
     #
     # In case it is not clear, the '#' that starts the comment
     # must be the first character on that the line (an lf have
     # preceeded it).
     #
  comment ::= '#' not_lf* lf;
  not_lf  ::= # Any byte that is not ASCII newline (LF);
"""


import re
import sys

from git_remote_helpers.fastimport import (
    commands,
    dates,
    errors
    )


## Stream parsing ##

class LineBasedParser(object):

    def __init__(self, input, filename=None):
        """A Parser that keeps track of line numbers.

        :param input: the file-like object to read from
        """
        self.input = input
        if filename is None:
            try:
                self.filename = input.name
            except AttributeError:
                self.filename = "(unknown)"
        else:
            self.filename = filename
        self.lineno = 0
        # Lines pushed back onto the input stream
        self._buffer = []

    def abort(self, exception, *args):
        """Raise an exception providing line number information."""
        raise exception(self.filename, self.lineno, *args)

    def readline(self):
        """Get the next line including the newline or '' on EOF."""
        self.lineno += 1
        if self._buffer:
            return self._buffer.pop()
        else:
            return self.input.readline()

    def next_line(self):
        """Get the next line without the newline or None on EOF."""
        line = self.readline()
        if line:
            return line[:-1]
        else:
            return None

    def push_line(self, line):
        """Push line back onto the line buffer.
        
        :param line: the line with no trailing newline
        """
        self.lineno -= 1
        self._buffer.append(line + "\n")

    def read_bytes(self, count):
        """Read a given number of bytes from the input stream.
        
        Throws MissingBytes if the bytes are not found.

        Note: This method does not read from the line buffer.

        :return: a string
        """
        result = self.input.read(count)
        found = len(result)
        self.lineno += result.count("\n")
        if found != count:
            self.abort(errors.MissingBytes, count, found)
        return result

    def read_until(self, terminator):
        """Read the input stream until the terminator is found.
        
        Throws MissingTerminator if the terminator is not found.

        Note: This method does not read from the line buffer.

        :return: the bytes read up to but excluding the terminator.
        """
        
        lines = []
        term = terminator + '\n'
        while True:
            line = self.input.readline()
            if line == term:
                break
            else:
                lines.append(line)
        return ''.join(lines)


# Regular expression used for parsing. (Note: The spec states that the name
# part should be non-empty but git-fast-export doesn't always do that so
# the first bit is \w*, not \w+.) Also git-fast-import code says the
# space before the email is optional.
_WHO_AND_WHEN_RE = re.compile(r'([^<]*)<(.*)> (.+)')
_WHO_RE = re.compile(r'([^<]*)<(.*)>')


class ImportParser(LineBasedParser):

    def __init__(self, input, filename=None):
        """A Parser of import commands.

        :param input: the file-like object to read from
        :param verbose: display extra information of not
        """
        LineBasedParser.__init__(self, input, filename)

        # We auto-detect the date format when a date is first encountered
        self.date_parser = None

    def warning(self, msg):
        sys.stderr.write("warning line %d: %s\n" % (self.lineno, msg))

    def parse(self):
        """Parse the input stream, yielding a sequence of ImportCommand
        objects.  Iteration terminates on EOF.  Raises InvalidCommand on
        parse error."""
        while True:
            line = self.next_line()
            if line is None:
                break
            elif len(line) == 0 or line.startswith('#'):
                continue
            # Search for commands in order of likelihood
            elif line.startswith('commit '):
                yield self._parse_commit(line[len('commit '):])
            elif line.startswith('blob'):
                yield self._parse_blob()
            elif line.startswith('done'):
                break
            elif line.startswith('progress '):
                yield commands.ProgressCommand(line[len('progress '):])
            elif line.startswith('reset '):
                yield self._parse_reset(line[len('reset '):])
            elif line.startswith('tag '):
                yield self._parse_tag(line[len('tag '):])
            elif line.startswith('checkpoint'):
                yield commands.CheckpointCommand()
            elif line.startswith('feature'):
                yield self._parse_feature(line[len('feature '):])
            else:
                self.abort(errors.InvalidCommand, line)

    def iter_commands(self):
        warnings.warn("iter_commands() deprecated: use parse()",
                      DeprecationWarning, stacklevel=2)
        return self.parse()

    def iter_file_commands(self):
        """Iterator returning FileCommand objects.
        
        If an invalid file command is found, the line is silently
        pushed back and iteration ends.
        """
        while True:
            line = self.next_line()
            if line is None:
                break
            elif len(line) == 0 or line.startswith('#'):
                continue
            # Search for file commands in order of likelihood
            elif line.startswith('M '):
                yield self._parse_file_modify(line[2:])
            elif line.startswith('D '):
                path = self._path(line[2:])
                yield commands.FileDeleteCommand(path)
            elif line.startswith('R '):
                old, new = self._path_pair(line[2:])
                yield commands.FileRenameCommand(old, new)
            elif line.startswith('C '):
                src, dest = self._path_pair(line[2:])
                yield commands.FileCopyCommand(src, dest)
            elif line.startswith('deleteall'):
                yield commands.FileDeleteAllCommand()
            else:
                self.push_line(line)
                break

    def _parse_blob(self):
        """Parse a blob command."""
        location = (self.filename, self.lineno)
        mark = self._get_mark_if_any()
        data = self._get_data('blob')
        return commands.BlobCommand(mark, data, location)

    def _parse_commit(self, ref):
        """Parse a commit command."""
        location = (self.filename, self.lineno)
        mark = self._get_mark_if_any()
        author = self._get_user_info('commit', 'author', False)
        more_authors = []
        while True:
            another_author = self._get_user_info('commit', 'author', False)
            if another_author is not None:
                more_authors.append(another_author)
            else:
                break
        committer = self._get_user_info('commit', 'committer')
        message = self._get_data('commit', 'message')
        try:
            message = message.decode('utf_8')
        except UnicodeDecodeError:
            self.warning(
                "commit message not in utf8 - replacing unknown characters")
            message = message.decode('utf_8', 'replace')
        from_ = self._get_from()
        merges = []
        while True:
            merge = self._get_merge()
            if merge is not None:
                # while the spec suggests it's illegal, git-fast-export
                # outputs multiple merges on the one line, e.g.
                # merge :x :y :z
                these_merges = merge.split(" ")
                merges.extend(these_merges)
            else:
                break
        properties = {}
        while True:
            name_value = self._get_property()
            if name_value is not None:
                name, value = name_value
                properties[name] = value
            else:
                break
        file_cmds = list(self.iter_file_commands())
        return commands.CommitCommand(ref, mark, author, committer, message,
            from_, merges, file_cmds, location,
            more_authors=more_authors, properties=properties)

    def _parse_feature(self, info):
        """Parse a feature command."""
        parts = info.split("=", 1)
        name = parts[0]
        if len(parts) > 1:
            value = self._path(parts[1])
        else:
            value = None
        location = (self.filename, self.lineno)
        return commands.FeatureCommand(name, value, location=location)


    def _parse_file_modify(self, info):
        """Parse a filemodify command within a commit.

        :param info: a string in the format "mode dataref path"
          (where dataref might be the hard-coded literal 'inline').
        """
        params = info.split(' ', 2)
        path = self._path(params[2])
        mode = params[0]
        if params[1] == 'inline':
            dataref = None
            data = self._get_data('filemodify')
        else:
            dataref = params[1]
            data = None
        return commands.FileModifyCommand(path, mode, dataref, data)

    def _parse_reset(self, ref):
        """Parse a reset command."""
        from_ = self._get_from()
        return commands.ResetCommand(ref, from_)

    def _parse_tag(self, name):
        """Parse a tag command."""
        from_ = self._get_from('tag')
        tagger = self._get_user_info('tag', 'tagger', accept_just_who=True)
        message = self._get_data('tag', 'message').decode('utf_8')
        return commands.TagCommand(name, from_, tagger, message)

    def _get_mark_if_any(self):
        """Parse a mark section."""
        line = self.next_line()
        if line.startswith('mark :'):
            return line[len('mark :'):]
        else:
            self.push_line(line)
            return None

    def _get_from(self, required_for=None):
        """Parse a from section."""
        line = self.next_line()
        if line is None:
            return None
        elif line.startswith('from '):
            return line[len('from '):]
        elif required_for:
            self.abort(errors.MissingSection, required_for, 'from')
        else:
            self.push_line(line)
            return None

    def _get_merge(self):
        """Parse a merge section."""
        line = self.next_line()
        if line is None:
            return None
        elif line.startswith('merge '):
            return line[len('merge '):]
        else:
            self.push_line(line)
            return None

    def _get_property(self):
        """Parse a property section."""
        line = self.next_line()
        if line is None:
            return None
        elif line.startswith('property '):
            return self._name_value(line[len('property '):])
        else:
            self.push_line(line)
            return None

    def _get_user_info(self, cmd, section, required=True,
        accept_just_who=False):
        """Parse a user section."""
        line = self.next_line()
        if line.startswith(section + ' '):
            return self._who_when(line[len(section + ' '):], cmd, section,
                accept_just_who=accept_just_who)
        elif required:
            self.abort(errors.MissingSection, cmd, section)
        else:
            self.push_line(line)
            return None

    def _get_data(self, required_for, section='data'):
        """Parse a data section."""
        line = self.next_line()
        if line.startswith('data '):
            rest = line[len('data '):]
            if rest.startswith('<<'):
                return self.read_until(rest[2:])
            else:
                size = int(rest)
                read_bytes = self.read_bytes(size)
                # optional LF after data.
                next = self.input.readline()
                self.lineno += 1
                if len(next) > 1 or next != "\n":
                    self.push_line(next[:-1])
                return read_bytes
        else:
            self.abort(errors.MissingSection, required_for, section)

    def _who_when(self, s, cmd, section, accept_just_who=False):
        """Parse who and when information from a string.
        
        :return: a tuple of (name,email,timestamp,timezone). name may be
            the empty string if only an email address was given.
        """
        match = _WHO_AND_WHEN_RE.search(s)
        if match:
            datestr = match.group(3)
            if self.date_parser is None:
                # auto-detect the date format
                if len(datestr.split(' ')) == 2:
                    format = 'raw'
                elif datestr == 'now':
                    format = 'now'
                else:
                    format = 'rfc2822'
                self.date_parser = dates.DATE_PARSERS_BY_NAME[format]
            when = self.date_parser(datestr, self.lineno)
        else:
            match = _WHO_RE.search(s)
            if accept_just_who and match:
                # HACK around missing time
                # TODO: output a warning here
                when = dates.DATE_PARSERS_BY_NAME['now']('now')
            else:
                self.abort(errors.BadFormat, cmd, section, s)

        # Do not attempt to decode name or email address; they are just
        # bytes.  (Everything will work out better if they are in UTF-8,
        # but that's not guaranteed.)
        name = match.group(1).rstrip()
        email = match.group(2)
        return (name, email, when[0], when[1])

    def _name_value(self, s):
        """Parse a (name,value) tuple from 'name value-length value'."""
        parts = s.split(' ', 2)
        name = parts[0]
        if len(parts) == 1:
            value = None
        else:
            size = int(parts[1])
            value = parts[2]
            still_to_read = size - len(value)
            if still_to_read == 1:
                value += "\n"
            elif still_to_read > 0:
                read_bytes = self.read_bytes(still_to_read - 1)
                value += "\n" + read_bytes
            value = value.decode('utf8')
        return (name, value)

    def _path(self, s):
        """Parse a path."""
        if s.startswith('"'):
            if s[-1] != '"':
                self.abort(errors.BadFormat, '?', '?', s)
            else:
                return _unquote_c_string(s[1:-1])

        # Do *not* decode the path to a Unicode string: filenames on
        # Unix are just bytes.  Git and Mercurial, at least, inherit
        # this stance.  git-fast-import(1) merely says "It is
        # recommended that <path> always be encoded using UTF-8.", which
        # is good advice ... but not something we can count on here.
        return s

    def _path_pair(self, s):
        """Parse two paths separated by a space."""
        # TODO: handle a space in the first path
        if s.startswith('"'):
            parts = s[1:].split('" ', 1)
        else:
            parts = s.split(' ', 1)
        if len(parts) != 2:
            self.abort(errors.BadFormat, '?', '?', s)
        elif parts[1].startswith('"') and parts[1].endswith('"'):
            parts[1] = parts[1][1:-1]
        elif parts[1].startswith('"') or parts[1].endswith('"'):
            self.abort(errors.BadFormat, '?', '?', s)
        return map(_unquote_c_string, parts)

    def _mode(self, s):
        """Parse a file mode into executable and symlink flags.
        
        :return (is_executable, is_symlink)
        """
        # Note: Output from git-fast-export slightly different to spec
        if s in ['644', '100644', '0100644']:
            return False, False
        elif s in ['755', '100755', '0100755']:
            return True, False
        elif s in ['120000', '0120000']:
            return False, True
        else:
            self.abort(errors.BadFormat, 'filemodify', 'mode', s)


def _unquote_c_string(s):
    """replace C-style escape sequences (\n, \", etc.) with real chars."""
    # HACK: Python strings are close enough
    return s.decode('string_escape', 'replace')
