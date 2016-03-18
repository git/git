#! /usr/bin/env python2

# Copyright (c) 2015 Matthieu Moy and others
# Copyright (c) 2012-2014 Michael Haggerty and others
# Derived from contrib/hooks/post-receive-email, which is
# Copyright (c) 2007 Andy Parkins
# and also includes contributions by other authors.
#
# This file is part of git-multimail.
#
# git-multimail is free software: you can redistribute it and/or
# modify it under the terms of the GNU General Public License version
# 2 as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see
# <http://www.gnu.org/licenses/>.

"""Generate notification emails for pushes to a git repository.

This hook sends emails describing changes introduced by pushes to a
git repository.  For each reference that was changed, it emits one
ReferenceChange email summarizing how the reference was changed,
followed by one Revision email for each new commit that was introduced
by the reference change.

Each commit is announced in exactly one Revision email.  If the same
commit is merged into another branch in the same or a later push, then
the ReferenceChange email will list the commit's SHA1 and its one-line
summary, but no new Revision email will be generated.

This script is designed to be used as a "post-receive" hook in a git
repository (see githooks(5)).  It can also be used as an "update"
script, but this usage is not completely reliable and is deprecated.

To help with debugging, this script accepts a --stdout option, which
causes the emails to be written to standard output rather than sent
using sendmail.

See the accompanying README file for the complete documentation.

"""

import sys
import os
import re
import bisect
import socket
import subprocess
import shlex
import optparse
import smtplib
import time

try:
    from email.utils import make_msgid
    from email.utils import getaddresses
    from email.utils import formataddr
    from email.utils import formatdate
    from email.header import Header
except ImportError:
    # Prior to Python 2.5, the email module used different names:
    from email.Utils import make_msgid
    from email.Utils import getaddresses
    from email.Utils import formataddr
    from email.Utils import formatdate
    from email.Header import Header


DEBUG = False

ZEROS = '0' * 40
LOGBEGIN = '- Log -----------------------------------------------------------------\n'
LOGEND = '-----------------------------------------------------------------------\n'

ADDR_HEADERS = set(['from', 'to', 'cc', 'bcc', 'reply-to', 'sender'])

# It is assumed in many places that the encoding is uniformly UTF-8,
# so changing these constants is unsupported.  But define them here
# anyway, to make it easier to find (at least most of) the places
# where the encoding is important.
(ENCODING, CHARSET) = ('UTF-8', 'utf-8')


REF_CREATED_SUBJECT_TEMPLATE = (
    '%(emailprefix)s%(refname_type)s %(short_refname)s created'
    ' (now %(newrev_short)s)'
    )
REF_UPDATED_SUBJECT_TEMPLATE = (
    '%(emailprefix)s%(refname_type)s %(short_refname)s updated'
    ' (%(oldrev_short)s -> %(newrev_short)s)'
    )
REF_DELETED_SUBJECT_TEMPLATE = (
    '%(emailprefix)s%(refname_type)s %(short_refname)s deleted'
    ' (was %(oldrev_short)s)'
    )

COMBINED_REFCHANGE_REVISION_SUBJECT_TEMPLATE = (
    '%(emailprefix)s%(refname_type)s %(short_refname)s updated: %(oneline)s'
    )

REFCHANGE_HEADER_TEMPLATE = """\
Date: %(send_date)s
To: %(recipients)s
Subject: %(subject)s
MIME-Version: 1.0
Content-Type: text/plain; charset=%(charset)s
Content-Transfer-Encoding: 8bit
Message-ID: %(msgid)s
From: %(fromaddr)s
Reply-To: %(reply_to)s
X-Git-Host: %(fqdn)s
X-Git-Repo: %(repo_shortname)s
X-Git-Refname: %(refname)s
X-Git-Reftype: %(refname_type)s
X-Git-Oldrev: %(oldrev)s
X-Git-Newrev: %(newrev)s
Auto-Submitted: auto-generated
"""

REFCHANGE_INTRO_TEMPLATE = """\
This is an automated email from the git hooks/post-receive script.

%(pusher)s pushed a change to %(refname_type)s %(short_refname)s
in repository %(repo_shortname)s.

"""


FOOTER_TEMPLATE = """\

-- \n\
To stop receiving notification emails like this one, please contact
%(administrator)s.
"""


REWIND_ONLY_TEMPLATE = """\
This update removed existing revisions from the reference, leaving the
reference pointing at a previous point in the repository history.

 * -- * -- N   %(refname)s (%(newrev_short)s)
            \\
             O -- O -- O   (%(oldrev_short)s)

Any revisions marked "omits" are not gone; other references still
refer to them.  Any revisions marked "discards" are gone forever.
"""


NON_FF_TEMPLATE = """\
This update added new revisions after undoing existing revisions.
That is to say, some revisions that were in the old version of the
%(refname_type)s are not in the new version.  This situation occurs
when a user --force pushes a change and generates a repository
containing something like this:

 * -- * -- B -- O -- O -- O   (%(oldrev_short)s)
            \\
             N -- N -- N   %(refname)s (%(newrev_short)s)

You should already have received notification emails for all of the O
revisions, and so the following emails describe only the N revisions
from the common base, B.

Any revisions marked "omits" are not gone; other references still
refer to them.  Any revisions marked "discards" are gone forever.
"""


NO_NEW_REVISIONS_TEMPLATE = """\
No new revisions were added by this update.
"""


DISCARDED_REVISIONS_TEMPLATE = """\
This change permanently discards the following revisions:
"""


NO_DISCARDED_REVISIONS_TEMPLATE = """\
The revisions that were on this %(refname_type)s are still contained in
other references; therefore, this change does not discard any commits
from the repository.
"""


NEW_REVISIONS_TEMPLATE = """\
The %(tot)s revisions listed above as "new" are entirely new to this
repository and will be described in separate emails.  The revisions
listed as "adds" were already present in the repository and have only
been added to this reference.

"""


TAG_CREATED_TEMPLATE = """\
        at  %(newrev_short)-9s (%(newrev_type)s)
"""


TAG_UPDATED_TEMPLATE = """\
*** WARNING: tag %(short_refname)s was modified! ***

      from  %(oldrev_short)-9s (%(oldrev_type)s)
        to  %(newrev_short)-9s (%(newrev_type)s)
"""


TAG_DELETED_TEMPLATE = """\
*** WARNING: tag %(short_refname)s was deleted! ***

"""


# The template used in summary tables.  It looks best if this uses the
# same alignment as TAG_CREATED_TEMPLATE and TAG_UPDATED_TEMPLATE.
BRIEF_SUMMARY_TEMPLATE = """\
%(action)10s  %(rev_short)-9s %(text)s
"""


NON_COMMIT_UPDATE_TEMPLATE = """\
This is an unusual reference change because the reference did not
refer to a commit either before or after the change.  We do not know
how to provide full information about this reference change.
"""


REVISION_HEADER_TEMPLATE = """\
Date: %(send_date)s
To: %(recipients)s
Cc: %(cc_recipients)s
Subject: %(emailprefix)s%(num)02d/%(tot)02d: %(oneline)s
MIME-Version: 1.0
Content-Type: text/plain; charset=%(charset)s
Content-Transfer-Encoding: 8bit
From: %(fromaddr)s
Reply-To: %(reply_to)s
In-Reply-To: %(reply_to_msgid)s
References: %(reply_to_msgid)s
X-Git-Host: %(fqdn)s
X-Git-Repo: %(repo_shortname)s
X-Git-Refname: %(refname)s
X-Git-Reftype: %(refname_type)s
X-Git-Rev: %(rev)s
Auto-Submitted: auto-generated
"""

REVISION_INTRO_TEMPLATE = """\
This is an automated email from the git hooks/post-receive script.

%(pusher)s pushed a commit to %(refname_type)s %(short_refname)s
in repository %(repo_shortname)s.

"""


REVISION_FOOTER_TEMPLATE = FOOTER_TEMPLATE


# Combined, meaning refchange+revision email (for single-commit additions)
COMBINED_HEADER_TEMPLATE = """\
Date: %(send_date)s
To: %(recipients)s
Subject: %(subject)s
MIME-Version: 1.0
Content-Type: text/plain; charset=%(charset)s
Content-Transfer-Encoding: 8bit
Message-ID: %(msgid)s
From: %(fromaddr)s
Reply-To: %(reply_to)s
X-Git-Host: %(fqdn)s
X-Git-Repo: %(repo_shortname)s
X-Git-Refname: %(refname)s
X-Git-Reftype: %(refname_type)s
X-Git-Oldrev: %(oldrev)s
X-Git-Newrev: %(newrev)s
X-Git-Rev: %(rev)s
Auto-Submitted: auto-generated
"""

COMBINED_INTRO_TEMPLATE = """\
This is an automated email from the git hooks/post-receive script.

%(pusher)s pushed a commit to %(refname_type)s %(short_refname)s
in repository %(repo_shortname)s.

"""

COMBINED_FOOTER_TEMPLATE = FOOTER_TEMPLATE


class CommandError(Exception):
    def __init__(self, cmd, retcode):
        self.cmd = cmd
        self.retcode = retcode
        Exception.__init__(
            self,
            'Command "%s" failed with retcode %s' % (' '.join(cmd), retcode,)
            )


class ConfigurationException(Exception):
    pass


# The "git" program (this could be changed to include a full path):
GIT_EXECUTABLE = 'git'


# How "git" should be invoked (including global arguments), as a list
# of words.  This variable is usually initialized automatically by
# read_git_output() via choose_git_command(), but if a value is set
# here then it will be used unconditionally.
GIT_CMD = None


def choose_git_command():
    """Decide how to invoke git, and record the choice in GIT_CMD."""

    global GIT_CMD

    if GIT_CMD is None:
        try:
            # Check to see whether the "-c" option is accepted (it was
            # only added in Git 1.7.2).  We don't actually use the
            # output of "git --version", though if we needed more
            # specific version information this would be the place to
            # do it.
            cmd = [GIT_EXECUTABLE, '-c', 'foo.bar=baz', '--version']
            read_output(cmd)
            GIT_CMD = [GIT_EXECUTABLE, '-c', 'i18n.logoutputencoding=%s' % (ENCODING,)]
        except CommandError:
            GIT_CMD = [GIT_EXECUTABLE]


def read_git_output(args, input=None, keepends=False, **kw):
    """Read the output of a Git command."""

    if GIT_CMD is None:
        choose_git_command()

    return read_output(GIT_CMD + args, input=input, keepends=keepends, **kw)


def read_output(cmd, input=None, keepends=False, **kw):
    if input:
        stdin = subprocess.PIPE
    else:
        stdin = None
    p = subprocess.Popen(
        cmd, stdin=stdin, stdout=subprocess.PIPE, stderr=subprocess.PIPE, **kw
        )
    (out, err) = p.communicate(input)
    retcode = p.wait()
    if retcode:
        raise CommandError(cmd, retcode)
    if not keepends:
        out = out.rstrip('\n\r')
    return out


def read_git_lines(args, keepends=False, **kw):
    """Return the lines output by Git command.

    Return as single lines, with newlines stripped off."""

    return read_git_output(args, keepends=True, **kw).splitlines(keepends)


def git_rev_list_ish(cmd, spec, args=None, **kw):
    """Common functionality for invoking a 'git rev-list'-like command.

    Parameters:
      * cmd is the Git command to run, e.g., 'rev-list' or 'log'.
      * spec is a list of revision arguments to pass to the named
        command.  If None, this function returns an empty list.
      * args is a list of extra arguments passed to the named command.
      * All other keyword arguments (if any) are passed to the
        underlying read_git_lines() function.

    Return the output of the Git command in the form of a list, one
    entry per output line.
    """
    if spec is None:
        return []
    if args is None:
        args = []
    args = [cmd, '--stdin'] + args
    spec_stdin = ''.join(s + '\n' for s in spec)
    return read_git_lines(args, input=spec_stdin, **kw)


def git_rev_list(spec, **kw):
    """Run 'git rev-list' with the given list of revision arguments.

    See git_rev_list_ish() for parameter and return value
    documentation.
    """
    return git_rev_list_ish('rev-list', spec, **kw)


def git_log(spec, **kw):
    """Run 'git log' with the given list of revision arguments.

    See git_rev_list_ish() for parameter and return value
    documentation.
    """
    return git_rev_list_ish('log', spec, **kw)


def header_encode(text, header_name=None):
    """Encode and line-wrap the value of an email header field."""

    try:
        if isinstance(text, str):
            text = text.decode(ENCODING, 'replace')
        return Header(text, header_name=header_name).encode()
    except UnicodeEncodeError:
        return Header(text, header_name=header_name, charset=CHARSET,
                      errors='replace').encode()


def addr_header_encode(text, header_name=None):
    """Encode and line-wrap the value of an email header field containing
    email addresses."""

    return Header(
        ', '.join(
            formataddr((header_encode(name), emailaddr))
            for name, emailaddr in getaddresses([text])
            ),
        header_name=header_name
        ).encode()


class Config(object):
    def __init__(self, section, git_config=None):
        """Represent a section of the git configuration.

        If git_config is specified, it is passed to "git config" in
        the GIT_CONFIG environment variable, meaning that "git config"
        will read the specified path rather than the Git default
        config paths."""

        self.section = section
        if git_config:
            self.env = os.environ.copy()
            self.env['GIT_CONFIG'] = git_config
        else:
            self.env = None

    @staticmethod
    def _split(s):
        """Split NUL-terminated values."""

        words = s.split('\0')
        assert words[-1] == ''
        return words[:-1]

    def get(self, name, default=None):
        try:
            values = self._split(read_git_output(
                ['config', '--get', '--null', '%s.%s' % (self.section, name)],
                env=self.env, keepends=True,
                ))
            assert len(values) == 1
            return values[0]
        except CommandError:
            return default

    def get_bool(self, name, default=None):
        try:
            value = read_git_output(
                ['config', '--get', '--bool', '%s.%s' % (self.section, name)],
                env=self.env,
                )
        except CommandError:
            return default
        return value == 'true'

    def get_all(self, name, default=None):
        """Read a (possibly multivalued) setting from the configuration.

        Return the result as a list of values, or default if the name
        is unset."""

        try:
            return self._split(read_git_output(
                ['config', '--get-all', '--null', '%s.%s' % (self.section, name)],
                env=self.env, keepends=True,
                ))
        except CommandError, e:
            if e.retcode == 1:
                # "the section or key is invalid"; i.e., there is no
                # value for the specified key.
                return default
            else:
                raise

    def get_recipients(self, name, default=None):
        """Read a recipients list from the configuration.

        Return the result as a comma-separated list of email
        addresses, or default if the option is unset.  If the setting
        has multiple values, concatenate them with comma separators."""

        lines = self.get_all(name, default=None)
        if lines is None:
            return default
        return ', '.join(line.strip() for line in lines)

    def set(self, name, value):
        read_git_output(
            ['config', '%s.%s' % (self.section, name), value],
            env=self.env,
            )

    def add(self, name, value):
        read_git_output(
            ['config', '--add', '%s.%s' % (self.section, name), value],
            env=self.env,
            )

    def __contains__(self, name):
        return self.get_all(name, default=None) is not None

    # We don't use this method anymore internally, but keep it here in
    # case somebody is calling it from their own code:
    def has_key(self, name):
        return name in self

    def unset_all(self, name):
        try:
            read_git_output(
                ['config', '--unset-all', '%s.%s' % (self.section, name)],
                env=self.env,
                )
        except CommandError, e:
            if e.retcode == 5:
                # The name doesn't exist, which is what we wanted anyway...
                pass
            else:
                raise

    def set_recipients(self, name, value):
        self.unset_all(name)
        for pair in getaddresses([value]):
            self.add(name, formataddr(pair))


def generate_summaries(*log_args):
    """Generate a brief summary for each revision requested.

    log_args are strings that will be passed directly to "git log" as
    revision selectors.  Iterate over (sha1_short, subject) for each
    commit specified by log_args (subject is the first line of the
    commit message as a string without EOLs)."""

    cmd = [
        'log', '--abbrev', '--format=%h %s',
        ] + list(log_args) + ['--']
    for line in read_git_lines(cmd):
        yield tuple(line.split(' ', 1))


def limit_lines(lines, max_lines):
    for (index, line) in enumerate(lines):
        if index < max_lines:
            yield line

    if index >= max_lines:
        yield '... %d lines suppressed ...\n' % (index + 1 - max_lines,)


def limit_linelength(lines, max_linelength):
    for line in lines:
        # Don't forget that lines always include a trailing newline.
        if len(line) > max_linelength + 1:
            line = line[:max_linelength - 7] + ' [...]\n'
        yield line


class CommitSet(object):
    """A (constant) set of object names.

    The set should be initialized with full SHA1 object names.  The
    __contains__() method returns True iff its argument is an
    abbreviation of any the names in the set."""

    def __init__(self, names):
        self._names = sorted(names)

    def __len__(self):
        return len(self._names)

    def __contains__(self, sha1_abbrev):
        """Return True iff this set contains sha1_abbrev (which might be abbreviated)."""

        i = bisect.bisect_left(self._names, sha1_abbrev)
        return i < len(self) and self._names[i].startswith(sha1_abbrev)


class GitObject(object):
    def __init__(self, sha1, type=None):
        if sha1 == ZEROS:
            self.sha1 = self.type = self.commit_sha1 = None
        else:
            self.sha1 = sha1
            self.type = type or read_git_output(['cat-file', '-t', self.sha1])

            if self.type == 'commit':
                self.commit_sha1 = self.sha1
            elif self.type == 'tag':
                try:
                    self.commit_sha1 = read_git_output(
                        ['rev-parse', '--verify', '%s^0' % (self.sha1,)]
                        )
                except CommandError:
                    # Cannot deref tag to determine commit_sha1
                    self.commit_sha1 = None
            else:
                self.commit_sha1 = None

        self.short = read_git_output(['rev-parse', '--short', sha1])

    def get_summary(self):
        """Return (sha1_short, subject) for this commit."""

        if not self.sha1:
            raise ValueError('Empty commit has no summary')

        return iter(generate_summaries('--no-walk', self.sha1)).next()

    def __eq__(self, other):
        return isinstance(other, GitObject) and self.sha1 == other.sha1

    def __hash__(self):
        return hash(self.sha1)

    def __nonzero__(self):
        return bool(self.sha1)

    def __str__(self):
        return self.sha1 or ZEROS


class Change(object):
    """A Change that has been made to the Git repository.

    Abstract class from which both Revisions and ReferenceChanges are
    derived.  A Change knows how to generate a notification email
    describing itself."""

    def __init__(self, environment):
        self.environment = environment
        self._values = None

    def _compute_values(self):
        """Return a dictionary {keyword: expansion} for this Change.

        Derived classes overload this method to add more entries to
        the return value.  This method is used internally by
        get_values().  The return value should always be a new
        dictionary."""

        return self.environment.get_values()

    def get_values(self, **extra_values):
        """Return a dictionary {keyword: expansion} for this Change.

        Return a dictionary mapping keywords to the values that they
        should be expanded to for this Change (used when interpolating
        template strings).  If any keyword arguments are supplied, add
        those to the return value as well.  The return value is always
        a new dictionary."""

        if self._values is None:
            self._values = self._compute_values()

        values = self._values.copy()
        if extra_values:
            values.update(extra_values)
        return values

    def expand(self, template, **extra_values):
        """Expand template.

        Expand the template (which should be a string) using string
        interpolation of the values for this Change.  If any keyword
        arguments are provided, also include those in the keywords
        available for interpolation."""

        return template % self.get_values(**extra_values)

    def expand_lines(self, template, **extra_values):
        """Break template into lines and expand each line."""

        values = self.get_values(**extra_values)
        for line in template.splitlines(True):
            yield line % values

    def expand_header_lines(self, template, **extra_values):
        """Break template into lines and expand each line as an RFC 2822 header.

        Encode values and split up lines that are too long.  Silently
        skip lines that contain references to unknown variables."""

        values = self.get_values(**extra_values)
        for line in template.splitlines():
            (name, value) = line.split(':', 1)

            try:
                value = value % values
            except KeyError, e:
                if DEBUG:
                    self.environment.log_warning(
                        'Warning: unknown variable %r in the following line; line skipped:\n'
                        '    %s\n'
                        % (e.args[0], line,)
                        )
            else:
                if name.lower() in ADDR_HEADERS:
                    value = addr_header_encode(value, name)
                else:
                    value = header_encode(value, name)
                for splitline in ('%s: %s\n' % (name, value)).splitlines(True):
                    yield splitline

    def generate_email_header(self):
        """Generate the RFC 2822 email headers for this Change, a line at a time.

        The output should not include the trailing blank line."""

        raise NotImplementedError()

    def generate_email_intro(self):
        """Generate the email intro for this Change, a line at a time.

        The output will be used as the standard boilerplate at the top
        of the email body."""

        raise NotImplementedError()

    def generate_email_body(self):
        """Generate the main part of the email body, a line at a time.

        The text in the body might be truncated after a specified
        number of lines (see multimailhook.emailmaxlines)."""

        raise NotImplementedError()

    def generate_email_footer(self):
        """Generate the footer of the email, a line at a time.

        The footer is always included, irrespective of
        multimailhook.emailmaxlines."""

        raise NotImplementedError()

    def generate_email(self, push, body_filter=None, extra_header_values={}):
        """Generate an email describing this change.

        Iterate over the lines (including the header lines) of an
        email describing this change.  If body_filter is not None,
        then use it to filter the lines that are intended for the
        email body.

        The extra_header_values field is received as a dict and not as
        **kwargs, to allow passing other keyword arguments in the
        future (e.g. passing extra values to generate_email_intro()"""

        for line in self.generate_email_header(**extra_header_values):
            yield line
        yield '\n'
        for line in self.generate_email_intro():
            yield line

        body = self.generate_email_body(push)
        if body_filter is not None:
            body = body_filter(body)
        for line in body:
            yield line

        for line in self.generate_email_footer():
            yield line


class Revision(Change):
    """A Change consisting of a single git commit."""

    CC_RE = re.compile(r'^\s*C[Cc]:\s*(?P<to>[^#]+@[^\s#]*)\s*(#.*)?$')

    def __init__(self, reference_change, rev, num, tot):
        Change.__init__(self, reference_change.environment)
        self.reference_change = reference_change
        self.rev = rev
        self.change_type = self.reference_change.change_type
        self.refname = self.reference_change.refname
        self.num = num
        self.tot = tot
        self.author = read_git_output(['log', '--no-walk', '--format=%aN <%aE>', self.rev.sha1])
        self.recipients = self.environment.get_revision_recipients(self)

        self.cc_recipients = ''
        if self.environment.get_scancommitforcc():
            self.cc_recipients = ', '.join(to.strip() for to in self._cc_recipients())
            if self.cc_recipients:
                self.environment.log_msg(
                    'Add %s to CC for %s\n' % (self.cc_recipients, self.rev.sha1))

    def _cc_recipients(self):
        cc_recipients = []
        message = read_git_output(['log', '--no-walk', '--format=%b', self.rev.sha1])
        lines = message.strip().split('\n')
        for line in lines:
            m = re.match(self.CC_RE, line)
            if m:
                cc_recipients.append(m.group('to'))

        return cc_recipients

    def _compute_values(self):
        values = Change._compute_values(self)

        oneline = read_git_output(
            ['log', '--format=%s', '--no-walk', self.rev.sha1]
            )

        values['rev'] = self.rev.sha1
        values['rev_short'] = self.rev.short
        values['change_type'] = self.change_type
        values['refname'] = self.refname
        values['short_refname'] = self.reference_change.short_refname
        values['refname_type'] = self.reference_change.refname_type
        values['reply_to_msgid'] = self.reference_change.msgid
        values['num'] = self.num
        values['tot'] = self.tot
        values['recipients'] = self.recipients
        if self.cc_recipients:
            values['cc_recipients'] = self.cc_recipients
        values['oneline'] = oneline
        values['author'] = self.author

        reply_to = self.environment.get_reply_to_commit(self)
        if reply_to:
            values['reply_to'] = reply_to

        return values

    def generate_email_header(self, **extra_values):
        for line in self.expand_header_lines(
                REVISION_HEADER_TEMPLATE, **extra_values
                ):
            yield line

    def generate_email_intro(self):
        for line in self.expand_lines(REVISION_INTRO_TEMPLATE):
            yield line

    def generate_email_body(self, push):
        """Show this revision."""

        return read_git_lines(
            ['log'] + self.environment.commitlogopts + ['-1', self.rev.sha1],
            keepends=True,
            )

    def generate_email_footer(self):
        return self.expand_lines(REVISION_FOOTER_TEMPLATE)


class ReferenceChange(Change):
    """A Change to a Git reference.

    An abstract class representing a create, update, or delete of a
    Git reference.  Derived classes handle specific types of reference
    (e.g., tags vs. branches).  These classes generate the main
    reference change email summarizing the reference change and
    whether it caused any any commits to be added or removed.

    ReferenceChange objects are usually created using the static
    create() method, which has the logic to decide which derived class
    to instantiate."""

    REF_RE = re.compile(r'^refs\/(?P<area>[^\/]+)\/(?P<shortname>.*)$')

    @staticmethod
    def create(environment, oldrev, newrev, refname):
        """Return a ReferenceChange object representing the change.

        Return an object that represents the type of change that is being
        made. oldrev and newrev should be SHA1s or ZEROS."""

        old = GitObject(oldrev)
        new = GitObject(newrev)
        rev = new or old

        # The revision type tells us what type the commit is, combined with
        # the location of the ref we can decide between
        #  - working branch
        #  - tracking branch
        #  - unannotated tag
        #  - annotated tag
        m = ReferenceChange.REF_RE.match(refname)
        if m:
            area = m.group('area')
            short_refname = m.group('shortname')
        else:
            area = ''
            short_refname = refname

        if rev.type == 'tag':
            # Annotated tag:
            klass = AnnotatedTagChange
        elif rev.type == 'commit':
            if area == 'tags':
                # Non-annotated tag:
                klass = NonAnnotatedTagChange
            elif area == 'heads':
                # Branch:
                klass = BranchChange
            elif area == 'remotes':
                # Tracking branch:
                environment.log_warning(
                    '*** Push-update of tracking branch %r\n'
                    '***  - incomplete email generated.\n'
                    % (refname,)
                    )
                klass = OtherReferenceChange
            else:
                # Some other reference namespace:
                environment.log_warning(
                    '*** Push-update of strange reference %r\n'
                    '***  - incomplete email generated.\n'
                    % (refname,)
                    )
                klass = OtherReferenceChange
        else:
            # Anything else (is there anything else?)
            environment.log_warning(
                '*** Unknown type of update to %r (%s)\n'
                '***  - incomplete email generated.\n'
                % (refname, rev.type,)
                )
            klass = OtherReferenceChange

        return klass(
            environment,
            refname=refname, short_refname=short_refname,
            old=old, new=new, rev=rev,
            )

    def __init__(self, environment, refname, short_refname, old, new, rev):
        Change.__init__(self, environment)
        self.change_type = {
            (False, True): 'create',
            (True, True): 'update',
            (True, False): 'delete',
            }[bool(old), bool(new)]
        self.refname = refname
        self.short_refname = short_refname
        self.old = old
        self.new = new
        self.rev = rev
        self.msgid = make_msgid()
        self.diffopts = environment.diffopts
        self.graphopts = environment.graphopts
        self.logopts = environment.logopts
        self.commitlogopts = environment.commitlogopts
        self.showgraph = environment.refchange_showgraph
        self.showlog = environment.refchange_showlog

        self.header_template = REFCHANGE_HEADER_TEMPLATE
        self.intro_template = REFCHANGE_INTRO_TEMPLATE
        self.footer_template = FOOTER_TEMPLATE

    def _compute_values(self):
        values = Change._compute_values(self)

        values['change_type'] = self.change_type
        values['refname_type'] = self.refname_type
        values['refname'] = self.refname
        values['short_refname'] = self.short_refname
        values['msgid'] = self.msgid
        values['recipients'] = self.recipients
        values['oldrev'] = str(self.old)
        values['oldrev_short'] = self.old.short
        values['newrev'] = str(self.new)
        values['newrev_short'] = self.new.short

        if self.old:
            values['oldrev_type'] = self.old.type
        if self.new:
            values['newrev_type'] = self.new.type

        reply_to = self.environment.get_reply_to_refchange(self)
        if reply_to:
            values['reply_to'] = reply_to

        return values

    def send_single_combined_email(self, known_added_sha1s):
        """Determine if a combined refchange/revision email should be sent

        If there is only a single new (non-merge) commit added by a
        change, it is useful to combine the ReferenceChange and
        Revision emails into one.  In such a case, return the single
        revision; otherwise, return None.

        This method is overridden in BranchChange."""

        return None

    def generate_combined_email(self, push, revision, body_filter=None, extra_header_values={}):
        """Generate an email describing this change AND specified revision.

        Iterate over the lines (including the header lines) of an
        email describing this change.  If body_filter is not None,
        then use it to filter the lines that are intended for the
        email body.

        The extra_header_values field is received as a dict and not as
        **kwargs, to allow passing other keyword arguments in the
        future (e.g. passing extra values to generate_email_intro()

        This method is overridden in BranchChange."""

        raise NotImplementedError

    def get_subject(self):
        template = {
            'create': REF_CREATED_SUBJECT_TEMPLATE,
            'update': REF_UPDATED_SUBJECT_TEMPLATE,
            'delete': REF_DELETED_SUBJECT_TEMPLATE,
            }[self.change_type]
        return self.expand(template)

    def generate_email_header(self, **extra_values):
        if 'subject' not in extra_values:
            extra_values['subject'] = self.get_subject()

        for line in self.expand_header_lines(
                self.header_template, **extra_values
                ):
            yield line

    def generate_email_intro(self):
        for line in self.expand_lines(self.intro_template):
            yield line

    def generate_email_body(self, push):
        """Call the appropriate body-generation routine.

        Call one of generate_create_summary() /
        generate_update_summary() / generate_delete_summary()."""

        change_summary = {
            'create': self.generate_create_summary,
            'delete': self.generate_delete_summary,
            'update': self.generate_update_summary,
            }[self.change_type](push)
        for line in change_summary:
            yield line

        for line in self.generate_revision_change_summary(push):
            yield line

    def generate_email_footer(self):
        return self.expand_lines(self.footer_template)

    def generate_revision_change_graph(self, push):
        if self.showgraph:
            args = ['--graph'] + self.graphopts
            for newold in ('new', 'old'):
                has_newold = False
                spec = push.get_commits_spec(newold, self)
                for line in git_log(spec, args=args, keepends=True):
                    if not has_newold:
                        has_newold = True
                        yield '\n'
                        yield 'Graph of %s commits:\n\n' % (
                            {'new': 'new', 'old': 'discarded'}[newold],)
                    yield '  ' + line
                if has_newold:
                    yield '\n'

    def generate_revision_change_log(self, new_commits_list):
        if self.showlog:
            yield '\n'
            yield 'Detailed log of new commits:\n\n'
            for line in read_git_lines(
                    ['log', '--no-walk']
                    + self.logopts
                    + new_commits_list
                    + ['--'],
                    keepends=True,
                    ):
                yield line

    def generate_new_revision_summary(self, tot, new_commits_list, push):
        for line in self.expand_lines(NEW_REVISIONS_TEMPLATE, tot=tot):
            yield line
        for line in self.generate_revision_change_graph(push):
            yield line
        for line in self.generate_revision_change_log(new_commits_list):
            yield line

    def generate_revision_change_summary(self, push):
        """Generate a summary of the revisions added/removed by this change."""

        if self.new.commit_sha1 and not self.old.commit_sha1:
            # A new reference was created.  List the new revisions
            # brought by the new reference (i.e., those revisions that
            # were not in the repository before this reference
            # change).
            sha1s = list(push.get_new_commits(self))
            sha1s.reverse()
            tot = len(sha1s)
            new_revisions = [
                Revision(self, GitObject(sha1), num=i + 1, tot=tot)
                for (i, sha1) in enumerate(sha1s)
                ]

            if new_revisions:
                yield self.expand('This %(refname_type)s includes the following new commits:\n')
                yield '\n'
                for r in new_revisions:
                    (sha1, subject) = r.rev.get_summary()
                    yield r.expand(
                        BRIEF_SUMMARY_TEMPLATE, action='new', text=subject,
                        )
                yield '\n'
                for line in self.generate_new_revision_summary(
                        tot, [r.rev.sha1 for r in new_revisions], push):
                    yield line
            else:
                for line in self.expand_lines(NO_NEW_REVISIONS_TEMPLATE):
                    yield line

        elif self.new.commit_sha1 and self.old.commit_sha1:
            # A reference was changed to point at a different commit.
            # List the revisions that were removed and/or added *from
            # that reference* by this reference change, along with a
            # diff between the trees for its old and new values.

            # List of the revisions that were added to the branch by
            # this update.  Note this list can include revisions that
            # have already had notification emails; we want such
            # revisions in the summary even though we will not send
            # new notification emails for them.
            adds = list(generate_summaries(
                '--topo-order', '--reverse', '%s..%s'
                % (self.old.commit_sha1, self.new.commit_sha1,)
                ))

            # List of the revisions that were removed from the branch
            # by this update.  This will be empty except for
            # non-fast-forward updates.
            discards = list(generate_summaries(
                '%s..%s' % (self.new.commit_sha1, self.old.commit_sha1,)
                ))

            if adds:
                new_commits_list = push.get_new_commits(self)
            else:
                new_commits_list = []
            new_commits = CommitSet(new_commits_list)

            if discards:
                discarded_commits = CommitSet(push.get_discarded_commits(self))
            else:
                discarded_commits = CommitSet([])

            if discards and adds:
                for (sha1, subject) in discards:
                    if sha1 in discarded_commits:
                        action = 'discards'
                    else:
                        action = 'omits'
                    yield self.expand(
                        BRIEF_SUMMARY_TEMPLATE, action=action,
                        rev_short=sha1, text=subject,
                        )
                for (sha1, subject) in adds:
                    if sha1 in new_commits:
                        action = 'new'
                    else:
                        action = 'adds'
                    yield self.expand(
                        BRIEF_SUMMARY_TEMPLATE, action=action,
                        rev_short=sha1, text=subject,
                        )
                yield '\n'
                for line in self.expand_lines(NON_FF_TEMPLATE):
                    yield line

            elif discards:
                for (sha1, subject) in discards:
                    if sha1 in discarded_commits:
                        action = 'discards'
                    else:
                        action = 'omits'
                    yield self.expand(
                        BRIEF_SUMMARY_TEMPLATE, action=action,
                        rev_short=sha1, text=subject,
                        )
                yield '\n'
                for line in self.expand_lines(REWIND_ONLY_TEMPLATE):
                    yield line

            elif adds:
                (sha1, subject) = self.old.get_summary()
                yield self.expand(
                    BRIEF_SUMMARY_TEMPLATE, action='from',
                    rev_short=sha1, text=subject,
                    )
                for (sha1, subject) in adds:
                    if sha1 in new_commits:
                        action = 'new'
                    else:
                        action = 'adds'
                    yield self.expand(
                        BRIEF_SUMMARY_TEMPLATE, action=action,
                        rev_short=sha1, text=subject,
                        )

            yield '\n'

            if new_commits:
                for line in self.generate_new_revision_summary(
                        len(new_commits), new_commits_list, push):
                    yield line
            else:
                for line in self.expand_lines(NO_NEW_REVISIONS_TEMPLATE):
                    yield line
                for line in self.generate_revision_change_graph(push):
                    yield line

            # The diffstat is shown from the old revision to the new
            # revision.  This is to show the truth of what happened in
            # this change.  There's no point showing the stat from the
            # base to the new revision because the base is effectively a
            # random revision at this point - the user will be interested
            # in what this revision changed - including the undoing of
            # previous revisions in the case of non-fast-forward updates.
            yield '\n'
            yield 'Summary of changes:\n'
            for line in read_git_lines(
                    ['diff-tree']
                    + self.diffopts
                    + ['%s..%s' % (self.old.commit_sha1, self.new.commit_sha1,)],
                    keepends=True,
                    ):
                yield line

        elif self.old.commit_sha1 and not self.new.commit_sha1:
            # A reference was deleted.  List the revisions that were
            # removed from the repository by this reference change.

            sha1s = list(push.get_discarded_commits(self))
            tot = len(sha1s)
            discarded_revisions = [
                Revision(self, GitObject(sha1), num=i + 1, tot=tot)
                for (i, sha1) in enumerate(sha1s)
                ]

            if discarded_revisions:
                for line in self.expand_lines(DISCARDED_REVISIONS_TEMPLATE):
                    yield line
                yield '\n'
                for r in discarded_revisions:
                    (sha1, subject) = r.rev.get_summary()
                    yield r.expand(
                        BRIEF_SUMMARY_TEMPLATE, action='discards', text=subject,
                        )
                for line in self.generate_revision_change_graph(push):
                    yield line
            else:
                for line in self.expand_lines(NO_DISCARDED_REVISIONS_TEMPLATE):
                    yield line

        elif not self.old.commit_sha1 and not self.new.commit_sha1:
            for line in self.expand_lines(NON_COMMIT_UPDATE_TEMPLATE):
                yield line

    def generate_create_summary(self, push):
        """Called for the creation of a reference."""

        # This is a new reference and so oldrev is not valid
        (sha1, subject) = self.new.get_summary()
        yield self.expand(
            BRIEF_SUMMARY_TEMPLATE, action='at',
            rev_short=sha1, text=subject,
            )
        yield '\n'

    def generate_update_summary(self, push):
        """Called for the change of a pre-existing branch."""

        return iter([])

    def generate_delete_summary(self, push):
        """Called for the deletion of any type of reference."""

        (sha1, subject) = self.old.get_summary()
        yield self.expand(
            BRIEF_SUMMARY_TEMPLATE, action='was',
            rev_short=sha1, text=subject,
            )
        yield '\n'


class BranchChange(ReferenceChange):
    refname_type = 'branch'

    def __init__(self, environment, refname, short_refname, old, new, rev):
        ReferenceChange.__init__(
            self, environment,
            refname=refname, short_refname=short_refname,
            old=old, new=new, rev=rev,
            )
        self.recipients = environment.get_refchange_recipients(self)
        self._single_revision = None

    def send_single_combined_email(self, known_added_sha1s):
        if not self.environment.combine_when_single_commit:
            return None

        # In the sadly-all-too-frequent usecase of people pushing only
        # one of their commits at a time to a repository, users feel
        # the reference change summary emails are noise rather than
        # important signal.  This is because, in this particular
        # usecase, there is a reference change summary email for each
        # new commit, and all these summaries do is point out that
        # there is one new commit (which can readily be inferred by
        # the existence of the individual revision email that is also
        # sent).  In such cases, our users prefer there to be a combined
        # reference change summary/new revision email.
        #
        # So, if the change is an update and it doesn't discard any
        # commits, and it adds exactly one non-merge commit (gerrit
        # forces a workflow where every commit is individually merged
        # and the git-multimail hook fired off for just this one
        # change), then we send a combined refchange/revision email.
        try:
            # If this change is a reference update that doesn't discard
            # any commits...
            if self.change_type != 'update':
                return None

            if read_git_lines(
                    ['merge-base', self.old.sha1, self.new.sha1]
                    ) != [self.old.sha1]:
                return None

            # Check if this update introduced exactly one non-merge
            # commit:

            def split_line(line):
                """Split line into (sha1, [parent,...])."""

                words = line.split()
                return (words[0], words[1:])

            # Get the new commits introduced by the push as a list of
            # (sha1, [parent,...])
            new_commits = [
                split_line(line)
                for line in read_git_lines(
                    [
                        'log', '-3', '--format=%H %P',
                        '%s..%s' % (self.old.sha1, self.new.sha1),
                        ]
                    )
                ]

            if not new_commits:
                return None

            # If the newest commit is a merge, save it for a later check
            # but otherwise ignore it
            merge = None
            tot = len(new_commits)
            if len(new_commits[0][1]) > 1:
                merge = new_commits[0][0]
                del new_commits[0]

            # Our primary check: we can't combine if more than one commit
            # is introduced.  We also currently only combine if the new
            # commit is a non-merge commit, though it may make sense to
            # combine if it is a merge as well.
            if not (
                    len(new_commits) == 1
                    and len(new_commits[0][1]) == 1
                    and new_commits[0][0] in known_added_sha1s
                    ):
                return None

            # We do not want to combine revision and refchange emails if
            # those go to separate locations.
            rev = Revision(self, GitObject(new_commits[0][0]), 1, tot)
            if rev.recipients != self.recipients:
                return None

            # We ignored the newest commit if it was just a merge of the one
            # commit being introduced.  But we don't want to ignore that
            # merge commit it it involved conflict resolutions.  Check that.
            if merge and merge != read_git_output(['diff-tree', '--cc', merge]):
                return None

            # We can combine the refchange and one new revision emails
            # into one.  Return the Revision that a combined email should
            # be sent about.
            return rev
        except CommandError:
            # Cannot determine number of commits in old..new or new..old;
            # don't combine reference/revision emails:
            return None

    def generate_combined_email(self, push, revision, body_filter=None, extra_header_values={}):
        values = revision.get_values()
        if extra_header_values:
            values.update(extra_header_values)
        if 'subject' not in extra_header_values:
            values['subject'] = self.expand(COMBINED_REFCHANGE_REVISION_SUBJECT_TEMPLATE, **values)

        self._single_revision = revision
        self.header_template = COMBINED_HEADER_TEMPLATE
        self.intro_template = COMBINED_INTRO_TEMPLATE
        self.footer_template = COMBINED_FOOTER_TEMPLATE
        for line in self.generate_email(push, body_filter, values):
            yield line

    def generate_email_body(self, push):
        '''Call the appropriate body generation routine.

        If this is a combined refchange/revision email, the special logic
        for handling this combined email comes from this function.  For
        other cases, we just use the normal handling.'''

        # If self._single_revision isn't set; don't override
        if not self._single_revision:
            for line in super(BranchChange, self).generate_email_body(push):
                yield line
            return

        # This is a combined refchange/revision email; we first provide
        # some info from the refchange portion, and then call the revision
        # generate_email_body function to handle the revision portion.
        adds = list(generate_summaries(
            '--topo-order', '--reverse', '%s..%s'
            % (self.old.commit_sha1, self.new.commit_sha1,)
            ))

        yield self.expand("The following commit(s) were added to %(refname)s by this push:\n")
        for (sha1, subject) in adds:
            yield self.expand(
                BRIEF_SUMMARY_TEMPLATE, action='new',
                rev_short=sha1, text=subject,
                )

        yield self._single_revision.rev.short + " is described below\n"
        yield '\n'

        for line in self._single_revision.generate_email_body(push):
            yield line


class AnnotatedTagChange(ReferenceChange):
    refname_type = 'annotated tag'

    def __init__(self, environment, refname, short_refname, old, new, rev):
        ReferenceChange.__init__(
            self, environment,
            refname=refname, short_refname=short_refname,
            old=old, new=new, rev=rev,
            )
        self.recipients = environment.get_announce_recipients(self)
        self.show_shortlog = environment.announce_show_shortlog

    ANNOTATED_TAG_FORMAT = (
        '%(*objectname)\n'
        '%(*objecttype)\n'
        '%(taggername)\n'
        '%(taggerdate)'
        )

    def describe_tag(self, push):
        """Describe the new value of an annotated tag."""

        # Use git for-each-ref to pull out the individual fields from
        # the tag
        [tagobject, tagtype, tagger, tagged] = read_git_lines(
            ['for-each-ref', '--format=%s' % (self.ANNOTATED_TAG_FORMAT,), self.refname],
            )

        yield self.expand(
            BRIEF_SUMMARY_TEMPLATE, action='tagging',
            rev_short=tagobject, text='(%s)' % (tagtype,),
            )
        if tagtype == 'commit':
            # If the tagged object is a commit, then we assume this is a
            # release, and so we calculate which tag this tag is
            # replacing
            try:
                prevtag = read_git_output(['describe', '--abbrev=0', '%s^' % (self.new,)])
            except CommandError:
                prevtag = None
            if prevtag:
                yield '  replaces  %s\n' % (prevtag,)
        else:
            prevtag = None
            yield '    length  %s bytes\n' % (read_git_output(['cat-file', '-s', tagobject]),)

        yield ' tagged by  %s\n' % (tagger,)
        yield '        on  %s\n' % (tagged,)
        yield '\n'

        # Show the content of the tag message; this might contain a
        # change log or release notes so is worth displaying.
        yield LOGBEGIN
        contents = list(read_git_lines(['cat-file', 'tag', self.new.sha1], keepends=True))
        contents = contents[contents.index('\n') + 1:]
        if contents and contents[-1][-1:] != '\n':
            contents.append('\n')
        for line in contents:
            yield line

        if self.show_shortlog and tagtype == 'commit':
            # Only commit tags make sense to have rev-list operations
            # performed on them
            yield '\n'
            if prevtag:
                # Show changes since the previous release
                revlist = read_git_output(
                    ['rev-list', '--pretty=short', '%s..%s' % (prevtag, self.new,)],
                    keepends=True,
                    )
            else:
                # No previous tag, show all the changes since time
                # began
                revlist = read_git_output(
                    ['rev-list', '--pretty=short', '%s' % (self.new,)],
                    keepends=True,
                    )
            for line in read_git_lines(['shortlog'], input=revlist, keepends=True):
                yield line

        yield LOGEND
        yield '\n'

    def generate_create_summary(self, push):
        """Called for the creation of an annotated tag."""

        for line in self.expand_lines(TAG_CREATED_TEMPLATE):
            yield line

        for line in self.describe_tag(push):
            yield line

    def generate_update_summary(self, push):
        """Called for the update of an annotated tag.

        This is probably a rare event and may not even be allowed."""

        for line in self.expand_lines(TAG_UPDATED_TEMPLATE):
            yield line

        for line in self.describe_tag(push):
            yield line

    def generate_delete_summary(self, push):
        """Called when a non-annotated reference is updated."""

        for line in self.expand_lines(TAG_DELETED_TEMPLATE):
            yield line

        yield self.expand('   tag was  %(oldrev_short)s\n')
        yield '\n'


class NonAnnotatedTagChange(ReferenceChange):
    refname_type = 'tag'

    def __init__(self, environment, refname, short_refname, old, new, rev):
        ReferenceChange.__init__(
            self, environment,
            refname=refname, short_refname=short_refname,
            old=old, new=new, rev=rev,
            )
        self.recipients = environment.get_refchange_recipients(self)

    def generate_create_summary(self, push):
        """Called for the creation of an annotated tag."""

        for line in self.expand_lines(TAG_CREATED_TEMPLATE):
            yield line

    def generate_update_summary(self, push):
        """Called when a non-annotated reference is updated."""

        for line in self.expand_lines(TAG_UPDATED_TEMPLATE):
            yield line

    def generate_delete_summary(self, push):
        """Called when a non-annotated reference is updated."""

        for line in self.expand_lines(TAG_DELETED_TEMPLATE):
            yield line

        for line in ReferenceChange.generate_delete_summary(self, push):
            yield line


class OtherReferenceChange(ReferenceChange):
    refname_type = 'reference'

    def __init__(self, environment, refname, short_refname, old, new, rev):
        # We use the full refname as short_refname, because otherwise
        # the full name of the reference would not be obvious from the
        # text of the email.
        ReferenceChange.__init__(
            self, environment,
            refname=refname, short_refname=refname,
            old=old, new=new, rev=rev,
            )
        self.recipients = environment.get_refchange_recipients(self)


class Mailer(object):
    """An object that can send emails."""

    def send(self, lines, to_addrs):
        """Send an email consisting of lines.

        lines must be an iterable over the lines constituting the
        header and body of the email.  to_addrs is a list of recipient
        addresses (can be needed even if lines already contains a
        "To:" field).  It can be either a string (comma-separated list
        of email addresses) or a Python list of individual email
        addresses.

        """

        raise NotImplementedError()


class SendMailer(Mailer):
    """Send emails using 'sendmail -oi -t'."""

    SENDMAIL_CANDIDATES = [
        '/usr/sbin/sendmail',
        '/usr/lib/sendmail',
        ]

    @staticmethod
    def find_sendmail():
        for path in SendMailer.SENDMAIL_CANDIDATES:
            if os.access(path, os.X_OK):
                return path
        else:
            raise ConfigurationException(
                'No sendmail executable found.  '
                'Try setting multimailhook.sendmailCommand.'
                )

    def __init__(self, command=None, envelopesender=None):
        """Construct a SendMailer instance.

        command should be the command and arguments used to invoke
        sendmail, as a list of strings.  If an envelopesender is
        provided, it will also be passed to the command, via '-f
        envelopesender'."""

        if command:
            self.command = command[:]
        else:
            self.command = [self.find_sendmail(), '-oi', '-t']

        if envelopesender:
            self.command.extend(['-f', envelopesender])

    def send(self, lines, to_addrs):
        try:
            p = subprocess.Popen(self.command, stdin=subprocess.PIPE)
        except OSError, e:
            sys.stderr.write(
                '*** Cannot execute command: %s\n' % ' '.join(self.command)
                + '*** %s\n' % str(e)
                + '*** Try setting multimailhook.mailer to "smtp"\n'
                '*** to send emails without using the sendmail command.\n'
                )
            sys.exit(1)
        try:
            p.stdin.writelines(lines)
        except Exception, e:
            sys.stderr.write(
                '*** Error while generating commit email\n'
                '***  - mail sending aborted.\n'
                )
            try:
                # subprocess.terminate() is not available in Python 2.4
                p.terminate()
            except AttributeError:
                pass
            raise e
        else:
            p.stdin.close()
            retcode = p.wait()
            if retcode:
                raise CommandError(self.command, retcode)


class SMTPMailer(Mailer):
    """Send emails using Python's smtplib."""

    def __init__(self, envelopesender, smtpserver,
                 smtpservertimeout=10.0, smtpserverdebuglevel=0,
                 smtpencryption='none',
                 smtpuser='', smtppass='',
                 ):
        if not envelopesender:
            sys.stderr.write(
                'fatal: git_multimail: cannot use SMTPMailer without a sender address.\n'
                'please set either multimailhook.envelopeSender or user.email\n'
                )
            sys.exit(1)
        if smtpencryption == 'ssl' and not (smtpuser and smtppass):
            raise ConfigurationException(
                'Cannot use SMTPMailer with security option ssl '
                'without options username and password.'
                )
        self.envelopesender = envelopesender
        self.smtpserver = smtpserver
        self.smtpservertimeout = smtpservertimeout
        self.smtpserverdebuglevel = smtpserverdebuglevel
        self.security = smtpencryption
        self.username = smtpuser
        self.password = smtppass
        try:
            def call(klass, server, timeout):
                try:
                    return klass(server, timeout=timeout)
                except TypeError:
                    # Old Python versions do not have timeout= argument.
                    return klass(server)
            if self.security == 'none':
                self.smtp = call(smtplib.SMTP, self.smtpserver, timeout=self.smtpservertimeout)
            elif self.security == 'ssl':
                self.smtp = call(smtplib.SMTP_SSL, self.smtpserver, timeout=self.smtpservertimeout)
            elif self.security == 'tls':
                if ':' not in self.smtpserver:
                    self.smtpserver += ':587'  # default port for TLS
                self.smtp = call(smtplib.SMTP, self.smtpserver, timeout=self.smtpservertimeout)
                self.smtp.ehlo()
                self.smtp.starttls()
                self.smtp.ehlo()
            else:
                sys.stdout.write('*** Error: Control reached an invalid option. ***')
                sys.exit(1)
            if self.smtpserverdebuglevel > 0:
                sys.stdout.write(
                    "*** Setting debug on for SMTP server connection (%s) ***\n"
                    % self.smtpserverdebuglevel)
                self.smtp.set_debuglevel(self.smtpserverdebuglevel)
        except Exception, e:
            sys.stderr.write(
                '*** Error establishing SMTP connection to %s ***\n'
                % self.smtpserver)
            sys.stderr.write('*** %s\n' % str(e))
            sys.exit(1)

    def __del__(self):
        if hasattr(self, 'smtp'):
            self.smtp.quit()

    def send(self, lines, to_addrs):
        try:
            if self.username or self.password:
                sys.stderr.write("*** Authenticating as %s ***\n" % self.username)
                self.smtp.login(self.username, self.password)
            msg = ''.join(lines)
            # turn comma-separated list into Python list if needed.
            if isinstance(to_addrs, basestring):
                to_addrs = [email for (name, email) in getaddresses([to_addrs])]
            self.smtp.sendmail(self.envelopesender, to_addrs, msg)
        except Exception, e:
            sys.stderr.write('*** Error sending email ***\n')
            sys.stderr.write('*** %s\n' % str(e))
            self.smtp.quit()
            sys.exit(1)


class OutputMailer(Mailer):
    """Write emails to an output stream, bracketed by lines of '=' characters.

    This is intended for debugging purposes."""

    SEPARATOR = '=' * 75 + '\n'

    def __init__(self, f):
        self.f = f

    def send(self, lines, to_addrs):
        self.f.write(self.SEPARATOR)
        self.f.writelines(lines)
        self.f.write(self.SEPARATOR)


def get_git_dir():
    """Determine GIT_DIR.

    Determine GIT_DIR either from the GIT_DIR environment variable or
    from the working directory, using Git's usual rules."""

    try:
        return read_git_output(['rev-parse', '--git-dir'])
    except CommandError:
        sys.stderr.write('fatal: git_multimail: not in a git directory\n')
        sys.exit(1)


class Environment(object):
    """Describes the environment in which the push is occurring.

    An Environment object encapsulates information about the local
    environment.  For example, it knows how to determine:

    * the name of the repository to which the push occurred

    * what user did the push

    * what users want to be informed about various types of changes.

    An Environment object is expected to have the following methods:

        get_repo_shortname()

            Return a short name for the repository, for display
            purposes.

        get_repo_path()

            Return the absolute path to the Git repository.

        get_emailprefix()

            Return a string that will be prefixed to every email's
            subject.

        get_pusher()

            Return the username of the person who pushed the changes.
            This value is used in the email body to indicate who
            pushed the change.

        get_pusher_email() (may return None)

            Return the email address of the person who pushed the
            changes.  The value should be a single RFC 2822 email
            address as a string; e.g., "Joe User <user@example.com>"
            if available, otherwise "user@example.com".  If set, the
            value is used as the Reply-To address for refchange
            emails.  If it is impossible to determine the pusher's
            email, this attribute should be set to None (in which case
            no Reply-To header will be output).

        get_sender()

            Return the address to be used as the 'From' email address
            in the email envelope.

        get_fromaddr()

            Return the 'From' email address used in the email 'From:'
            headers.  (May be a full RFC 2822 email address like 'Joe
            User <user@example.com>'.)

        get_administrator()

            Return the name and/or email of the repository
            administrator.  This value is used in the footer as the
            person to whom requests to be removed from the
            notification list should be sent.  Ideally, it should
            include a valid email address.

        get_reply_to_refchange()
        get_reply_to_commit()

            Return the address to use in the email "Reply-To" header,
            as a string.  These can be an RFC 2822 email address, or
            None to omit the "Reply-To" header.
            get_reply_to_refchange() is used for refchange emails;
            get_reply_to_commit() is used for individual commit
            emails.

    They should also define the following attributes:

        announce_show_shortlog (bool)

            True iff announce emails should include a shortlog.

        refchange_showgraph (bool)

            True iff refchanges emails should include a detailed graph.

        refchange_showlog (bool)

            True iff refchanges emails should include a detailed log.

        diffopts (list of strings)

            The options that should be passed to 'git diff' for the
            summary email.  The value should be a list of strings
            representing words to be passed to the command.

        graphopts (list of strings)

            Analogous to diffopts, but contains options passed to
            'git log --graph' when generating the detailed graph for
            a set of commits (see refchange_showgraph)

        logopts (list of strings)

            Analogous to diffopts, but contains options passed to
            'git log' when generating the detailed log for a set of
            commits (see refchange_showlog)

        commitlogopts (list of strings)

            The options that should be passed to 'git log' for each
            commit mail.  The value should be a list of strings
            representing words to be passed to the command.

        quiet (bool)
            On success do not write to stderr

        stdout (bool)
            Write email to stdout rather than emailing. Useful for debugging

        combine_when_single_commit (bool)

            True if a combined email should be produced when a single
            new commit is pushed to a branch, False otherwise.

    """

    REPO_NAME_RE = re.compile(r'^(?P<name>.+?)(?:\.git)$')

    def __init__(self, osenv=None):
        self.osenv = osenv or os.environ
        self.announce_show_shortlog = False
        self.maxcommitemails = 500
        self.diffopts = ['--stat', '--summary', '--find-copies-harder']
        self.graphopts = ['--oneline', '--decorate']
        self.logopts = []
        self.refchange_showgraph = False
        self.refchange_showlog = False
        self.commitlogopts = ['-C', '--stat', '-p', '--cc']
        self.quiet = False
        self.stdout = False
        self.combine_when_single_commit = True

        self.COMPUTED_KEYS = [
            'administrator',
            'charset',
            'emailprefix',
            'fromaddr',
            'pusher',
            'pusher_email',
            'repo_path',
            'repo_shortname',
            'sender',
            ]

        self._values = None

    def get_repo_shortname(self):
        """Use the last part of the repo path, with ".git" stripped off if present."""

        basename = os.path.basename(os.path.abspath(self.get_repo_path()))
        m = self.REPO_NAME_RE.match(basename)
        if m:
            return m.group('name')
        else:
            return basename

    def get_pusher(self):
        raise NotImplementedError()

    def get_pusher_email(self):
        return None

    def get_fromaddr(self):
        config = Config('user')
        fromname = config.get('name', default='')
        fromemail = config.get('email', default='')
        if fromemail:
            return formataddr([fromname, fromemail])
        return self.get_sender()

    def get_administrator(self):
        return 'the administrator of this repository'

    def get_emailprefix(self):
        return ''

    def get_repo_path(self):
        if read_git_output(['rev-parse', '--is-bare-repository']) == 'true':
            path = get_git_dir()
        else:
            path = read_git_output(['rev-parse', '--show-toplevel'])
        return os.path.abspath(path)

    def get_charset(self):
        return CHARSET

    def get_values(self):
        """Return a dictionary {keyword: expansion} for this Environment.

        This method is called by Change._compute_values().  The keys
        in the returned dictionary are available to be used in any of
        the templates.  The dictionary is created by calling
        self.get_NAME() for each of the attributes named in
        COMPUTED_KEYS and recording those that do not return None.
        The return value is always a new dictionary."""

        if self._values is None:
            values = {}

            for key in self.COMPUTED_KEYS:
                value = getattr(self, 'get_%s' % (key,))()
                if value is not None:
                    values[key] = value

            self._values = values

        return self._values.copy()

    def get_refchange_recipients(self, refchange):
        """Return the recipients for notifications about refchange.

        Return the list of email addresses to which notifications
        about the specified ReferenceChange should be sent."""

        raise NotImplementedError()

    def get_announce_recipients(self, annotated_tag_change):
        """Return the recipients for notifications about annotated_tag_change.

        Return the list of email addresses to which notifications
        about the specified AnnotatedTagChange should be sent."""

        raise NotImplementedError()

    def get_reply_to_refchange(self, refchange):
        return self.get_pusher_email()

    def get_revision_recipients(self, revision):
        """Return the recipients for messages about revision.

        Return the list of email addresses to which notifications
        about the specified Revision should be sent.  This method
        could be overridden, for example, to take into account the
        contents of the revision when deciding whom to notify about
        it.  For example, there could be a scheme for users to express
        interest in particular files or subdirectories, and only
        receive notification emails for revisions that affecting those
        files."""

        raise NotImplementedError()

    def get_reply_to_commit(self, revision):
        return revision.author

    def filter_body(self, lines):
        """Filter the lines intended for an email body.

        lines is an iterable over the lines that would go into the
        email body.  Filter it (e.g., limit the number of lines, the
        line length, character set, etc.), returning another iterable.
        See FilterLinesEnvironmentMixin and MaxlinesEnvironmentMixin
        for classes implementing this functionality."""

        return lines

    def log_msg(self, msg):
        """Write the string msg on a log file or on stderr.

        Sends the text to stderr by default, override to change the behavior."""
        sys.stderr.write(msg)

    def log_warning(self, msg):
        """Write the string msg on a log file or on stderr.

        Sends the text to stderr by default, override to change the behavior."""
        sys.stderr.write(msg)

    def log_error(self, msg):
        """Write the string msg on a log file or on stderr.

        Sends the text to stderr by default, override to change the behavior."""
        sys.stderr.write(msg)


class ConfigEnvironmentMixin(Environment):
    """A mixin that sets self.config to its constructor's config argument.

    This class's constructor consumes the "config" argument.

    Mixins that need to inspect the config should inherit from this
    class (1) to make sure that "config" is still in the constructor
    arguments with its own constructor runs and/or (2) to be sure that
    self.config is set after construction."""

    def __init__(self, config, **kw):
        super(ConfigEnvironmentMixin, self).__init__(**kw)
        self.config = config


class ConfigOptionsEnvironmentMixin(ConfigEnvironmentMixin):
    """An Environment that reads most of its information from "git config"."""

    def __init__(self, config, **kw):
        super(ConfigOptionsEnvironmentMixin, self).__init__(
            config=config, **kw
            )

        for var, cfg in (
                ('announce_show_shortlog', 'announceshortlog'),
                ('refchange_showgraph', 'refchangeShowGraph'),
                ('refchange_showlog', 'refchangeshowlog'),
                ('quiet', 'quiet'),
                ('stdout', 'stdout'),
                ):
            val = config.get_bool(cfg)
            if val is not None:
                setattr(self, var, val)

        maxcommitemails = config.get('maxcommitemails')
        if maxcommitemails is not None:
            try:
                self.maxcommitemails = int(maxcommitemails)
            except ValueError:
                self.log_warning(
                    '*** Malformed value for multimailhook.maxCommitEmails: %s\n' % maxcommitemails
                    + '*** Expected a number.  Ignoring.\n'
                    )

        diffopts = config.get('diffopts')
        if diffopts is not None:
            self.diffopts = shlex.split(diffopts)

        graphopts = config.get('graphOpts')
        if graphopts is not None:
            self.graphopts = shlex.split(graphopts)

        logopts = config.get('logopts')
        if logopts is not None:
            self.logopts = shlex.split(logopts)

        commitlogopts = config.get('commitlogopts')
        if commitlogopts is not None:
            self.commitlogopts = shlex.split(commitlogopts)

        reply_to = config.get('replyTo')
        self.__reply_to_refchange = config.get('replyToRefchange', default=reply_to)
        if (
                self.__reply_to_refchange is not None
                and self.__reply_to_refchange.lower() == 'author'
                ):
            raise ConfigurationException(
                '"author" is not an allowed setting for replyToRefchange'
                )
        self.__reply_to_commit = config.get('replyToCommit', default=reply_to)

        combine = config.get_bool('combineWhenSingleCommit')
        if combine is not None:
            self.combine_when_single_commit = combine

    def get_administrator(self):
        return (
            self.config.get('administrator')
            or self.get_sender()
            or super(ConfigOptionsEnvironmentMixin, self).get_administrator()
            )

    def get_repo_shortname(self):
        return (
            self.config.get('reponame')
            or super(ConfigOptionsEnvironmentMixin, self).get_repo_shortname()
            )

    def get_emailprefix(self):
        emailprefix = self.config.get('emailprefix')
        if emailprefix is not None:
            emailprefix = emailprefix.strip()
            if emailprefix:
                return emailprefix + ' '
            else:
                return ''
        else:
            return '[%s] ' % (self.get_repo_shortname(),)

    def get_sender(self):
        return self.config.get('envelopesender')

    def get_fromaddr(self):
        fromaddr = self.config.get('from')
        if fromaddr:
            return fromaddr
        return super(ConfigOptionsEnvironmentMixin, self).get_fromaddr()

    def get_reply_to_refchange(self, refchange):
        if self.__reply_to_refchange is None:
            return super(ConfigOptionsEnvironmentMixin, self).get_reply_to_refchange(refchange)
        elif self.__reply_to_refchange.lower() == 'pusher':
            return self.get_pusher_email()
        elif self.__reply_to_refchange.lower() == 'none':
            return None
        else:
            return self.__reply_to_refchange

    def get_reply_to_commit(self, revision):
        if self.__reply_to_commit is None:
            return super(ConfigOptionsEnvironmentMixin, self).get_reply_to_commit(revision)
        elif self.__reply_to_commit.lower() == 'author':
            return revision.author
        elif self.__reply_to_commit.lower() == 'pusher':
            return self.get_pusher_email()
        elif self.__reply_to_commit.lower() == 'none':
            return None
        else:
            return self.__reply_to_commit

    def get_scancommitforcc(self):
        return self.config.get('scancommitforcc')


class FilterLinesEnvironmentMixin(Environment):
    """Handle encoding and maximum line length of body lines.

        emailmaxlinelength (int or None)

            The maximum length of any single line in the email body.
            Longer lines are truncated at that length with ' [...]'
            appended.

        strict_utf8 (bool)

            If this field is set to True, then the email body text is
            expected to be UTF-8.  Any invalid characters are
            converted to U+FFFD, the Unicode replacement character
            (encoded as UTF-8, of course).

    """

    def __init__(self, strict_utf8=True, emailmaxlinelength=500, **kw):
        super(FilterLinesEnvironmentMixin, self).__init__(**kw)
        self.__strict_utf8 = strict_utf8
        self.__emailmaxlinelength = emailmaxlinelength

    def filter_body(self, lines):
        lines = super(FilterLinesEnvironmentMixin, self).filter_body(lines)
        if self.__strict_utf8:
            lines = (line.decode(ENCODING, 'replace') for line in lines)
            # Limit the line length in Unicode-space to avoid
            # splitting characters:
            if self.__emailmaxlinelength:
                lines = limit_linelength(lines, self.__emailmaxlinelength)
            lines = (line.encode(ENCODING, 'replace') for line in lines)
        elif self.__emailmaxlinelength:
            lines = limit_linelength(lines, self.__emailmaxlinelength)

        return lines


class ConfigFilterLinesEnvironmentMixin(
        ConfigEnvironmentMixin,
        FilterLinesEnvironmentMixin,
        ):
    """Handle encoding and maximum line length based on config."""

    def __init__(self, config, **kw):
        strict_utf8 = config.get_bool('emailstrictutf8', default=None)
        if strict_utf8 is not None:
            kw['strict_utf8'] = strict_utf8

        emailmaxlinelength = config.get('emailmaxlinelength')
        if emailmaxlinelength is not None:
            kw['emailmaxlinelength'] = int(emailmaxlinelength)

        super(ConfigFilterLinesEnvironmentMixin, self).__init__(
            config=config, **kw
            )


class MaxlinesEnvironmentMixin(Environment):
    """Limit the email body to a specified number of lines."""

    def __init__(self, emailmaxlines, **kw):
        super(MaxlinesEnvironmentMixin, self).__init__(**kw)
        self.__emailmaxlines = emailmaxlines

    def filter_body(self, lines):
        lines = super(MaxlinesEnvironmentMixin, self).filter_body(lines)
        if self.__emailmaxlines:
            lines = limit_lines(lines, self.__emailmaxlines)
        return lines


class ConfigMaxlinesEnvironmentMixin(
        ConfigEnvironmentMixin,
        MaxlinesEnvironmentMixin,
        ):
    """Limit the email body to the number of lines specified in config."""

    def __init__(self, config, **kw):
        emailmaxlines = int(config.get('emailmaxlines', default='0'))
        super(ConfigMaxlinesEnvironmentMixin, self).__init__(
            config=config,
            emailmaxlines=emailmaxlines,
            **kw
            )


class FQDNEnvironmentMixin(Environment):
    """A mixin that sets the host's FQDN to its constructor argument."""

    def __init__(self, fqdn, **kw):
        super(FQDNEnvironmentMixin, self).__init__(**kw)
        self.COMPUTED_KEYS += ['fqdn']
        self.__fqdn = fqdn

    def get_fqdn(self):
        """Return the fully-qualified domain name for this host.

        Return None if it is unavailable or unwanted."""

        return self.__fqdn


class ConfigFQDNEnvironmentMixin(
        ConfigEnvironmentMixin,
        FQDNEnvironmentMixin,
        ):
    """Read the FQDN from the config."""

    def __init__(self, config, **kw):
        fqdn = config.get('fqdn')
        super(ConfigFQDNEnvironmentMixin, self).__init__(
            config=config,
            fqdn=fqdn,
            **kw
            )


class ComputeFQDNEnvironmentMixin(FQDNEnvironmentMixin):
    """Get the FQDN by calling socket.getfqdn()."""

    def __init__(self, **kw):
        super(ComputeFQDNEnvironmentMixin, self).__init__(
            fqdn=socket.getfqdn(),
            **kw
            )


class PusherDomainEnvironmentMixin(ConfigEnvironmentMixin):
    """Deduce pusher_email from pusher by appending an emaildomain."""

    def __init__(self, **kw):
        super(PusherDomainEnvironmentMixin, self).__init__(**kw)
        self.__emaildomain = self.config.get('emaildomain')

    def get_pusher_email(self):
        if self.__emaildomain:
            # Derive the pusher's full email address in the default way:
            return '%s@%s' % (self.get_pusher(), self.__emaildomain)
        else:
            return super(PusherDomainEnvironmentMixin, self).get_pusher_email()


class StaticRecipientsEnvironmentMixin(Environment):
    """Set recipients statically based on constructor parameters."""

    def __init__(
            self,
            refchange_recipients, announce_recipients, revision_recipients, scancommitforcc,
            **kw
            ):
        super(StaticRecipientsEnvironmentMixin, self).__init__(**kw)

        # The recipients for various types of notification emails, as
        # RFC 2822 email addresses separated by commas (or the empty
        # string if no recipients are configured).  Although there is
        # a mechanism to choose the recipient lists based on on the
        # actual *contents* of the change being reported, we only
        # choose based on the *type* of the change.  Therefore we can
        # compute them once and for all:
        if not (refchange_recipients
                or announce_recipients
                or revision_recipients
                or scancommitforcc):
            raise ConfigurationException('No email recipients configured!')
        self.__refchange_recipients = refchange_recipients
        self.__announce_recipients = announce_recipients
        self.__revision_recipients = revision_recipients

    def get_refchange_recipients(self, refchange):
        return self.__refchange_recipients

    def get_announce_recipients(self, annotated_tag_change):
        return self.__announce_recipients

    def get_revision_recipients(self, revision):
        return self.__revision_recipients


class ConfigRecipientsEnvironmentMixin(
        ConfigEnvironmentMixin,
        StaticRecipientsEnvironmentMixin
        ):
    """Determine recipients statically based on config."""

    def __init__(self, config, **kw):
        super(ConfigRecipientsEnvironmentMixin, self).__init__(
            config=config,
            refchange_recipients=self._get_recipients(
                config, 'refchangelist', 'mailinglist',
                ),
            announce_recipients=self._get_recipients(
                config, 'announcelist', 'refchangelist', 'mailinglist',
                ),
            revision_recipients=self._get_recipients(
                config, 'commitlist', 'mailinglist',
                ),
            scancommitforcc=config.get('scancommitforcc'),
            **kw
            )

    def _get_recipients(self, config, *names):
        """Return the recipients for a particular type of message.

        Return the list of email addresses to which a particular type
        of notification email should be sent, by looking at the config
        value for "multimailhook.$name" for each of names.  Use the
        value from the first name that is configured.  The return
        value is a (possibly empty) string containing RFC 2822 email
        addresses separated by commas.  If no configuration could be
        found, raise a ConfigurationException."""

        for name in names:
            retval = config.get_recipients(name)
            if retval is not None:
                return retval
        else:
            return ''


class ProjectdescEnvironmentMixin(Environment):
    """Make a "projectdesc" value available for templates.

    By default, it is set to the first line of $GIT_DIR/description
    (if that file is present and appears to be set meaningfully)."""

    def __init__(self, **kw):
        super(ProjectdescEnvironmentMixin, self).__init__(**kw)
        self.COMPUTED_KEYS += ['projectdesc']

    def get_projectdesc(self):
        """Return a one-line descripition of the project."""

        git_dir = get_git_dir()
        try:
            projectdesc = open(os.path.join(git_dir, 'description')).readline().strip()
            if projectdesc and not projectdesc.startswith('Unnamed repository'):
                return projectdesc
        except IOError:
            pass

        return 'UNNAMED PROJECT'


class GenericEnvironmentMixin(Environment):
    def get_pusher(self):
        return self.osenv.get('USER', self.osenv.get('USERNAME', 'unknown user'))


class GenericEnvironment(
        ProjectdescEnvironmentMixin,
        ConfigMaxlinesEnvironmentMixin,
        ComputeFQDNEnvironmentMixin,
        ConfigFilterLinesEnvironmentMixin,
        ConfigRecipientsEnvironmentMixin,
        PusherDomainEnvironmentMixin,
        ConfigOptionsEnvironmentMixin,
        GenericEnvironmentMixin,
        Environment,
        ):
    pass


class GitoliteEnvironmentMixin(Environment):
    def get_repo_shortname(self):
        # The gitolite environment variable $GL_REPO is a pretty good
        # repo_shortname (though it's probably not as good as a value
        # the user might have explicitly put in his config).
        return (
            self.osenv.get('GL_REPO', None)
            or super(GitoliteEnvironmentMixin, self).get_repo_shortname()
            )

    def get_pusher(self):
        return self.osenv.get('GL_USER', 'unknown user')

    def get_fromaddr(self):
        GL_USER = self.osenv.get('GL_USER')
        if GL_USER is not None:
            # Find the path to gitolite.conf.  Note that gitolite v3
            # did away with the GL_ADMINDIR and GL_CONF environment
            # variables (they are now hard-coded).
            GL_ADMINDIR = self.osenv.get(
                'GL_ADMINDIR',
                os.path.expanduser(os.path.join('~', '.gitolite')))
            GL_CONF = self.osenv.get(
                'GL_CONF',
                os.path.join(GL_ADMINDIR, 'conf', 'gitolite.conf'))
            if os.path.isfile(GL_CONF):
                f = open(GL_CONF, 'rU')
                try:
                    in_user_emails_section = False
                    re_template = r'^\s*#\s*{}\s*$'
                    re_begin, re_user, re_end = (
                        re.compile(re_template.format(x))
                        for x in (
                            r'BEGIN\s+USER\s+EMAILS',
                            re.escape(GL_USER) + r'\s+(.*)',
                            r'END\s+USER\s+EMAILS',
                            ))
                    for l in f:
                        l = l.rstrip('\n')
                        if not in_user_emails_section:
                            if re_begin.match(l):
                                in_user_emails_section = True
                            continue
                        if re_end.match(l):
                            break
                        m = re_user.match(l)
                        if m:
                            return m.group(1)
                finally:
                    f.close()
        return super(GitoliteEnvironmentMixin, self).get_fromaddr()


class IncrementalDateTime(object):
    """Simple wrapper to give incremental date/times.

    Each call will result in a date/time a second later than the
    previous call.  This can be used to falsify email headers, to
    increase the likelihood that email clients sort the emails
    correctly."""

    def __init__(self):
        self.time = time.time()

    def next(self):
        formatted = formatdate(self.time, True)
        self.time += 1
        return formatted


class GitoliteEnvironment(
        ProjectdescEnvironmentMixin,
        ConfigMaxlinesEnvironmentMixin,
        ComputeFQDNEnvironmentMixin,
        ConfigFilterLinesEnvironmentMixin,
        ConfigRecipientsEnvironmentMixin,
        PusherDomainEnvironmentMixin,
        ConfigOptionsEnvironmentMixin,
        GitoliteEnvironmentMixin,
        Environment,
        ):
    pass


class Push(object):
    """Represent an entire push (i.e., a group of ReferenceChanges).

    It is easy to figure out what commits were added to a *branch* by
    a Reference change:

        git rev-list change.old..change.new

    or removed from a *branch*:

        git rev-list change.new..change.old

    But it is not quite so trivial to determine which entirely new
    commits were added to the *repository* by a push and which old
    commits were discarded by a push.  A big part of the job of this
    class is to figure out these things, and to make sure that new
    commits are only detailed once even if they were added to multiple
    references.

    The first step is to determine the "other" references--those
    unaffected by the current push.  They are computed by listing all
    references then removing any affected by this push.  The results
    are stored in Push._other_ref_sha1s.

    The commits contained in the repository before this push were

        git rev-list other1 other2 other3 ... change1.old change2.old ...

    Where "changeN.old" is the old value of one of the references
    affected by this push.

    The commits contained in the repository after this push are

        git rev-list other1 other2 other3 ... change1.new change2.new ...

    The commits added by this push are the difference between these
    two sets, which can be written

        git rev-list \
            ^other1 ^other2 ... \
            ^change1.old ^change2.old ... \
            change1.new change2.new ...

    The commits removed by this push can be computed by

        git rev-list \
            ^other1 ^other2 ... \
            ^change1.new ^change2.new ... \
            change1.old change2.old ...

    The last point is that it is possible that other pushes are
    occurring simultaneously to this one, so reference values can
    change at any time.  It is impossible to eliminate all race
    conditions, but we reduce the window of time during which problems
    can occur by translating reference names to SHA1s as soon as
    possible and working with SHA1s thereafter (because SHA1s are
    immutable)."""

    # A map {(changeclass, changetype): integer} specifying the order
    # that reference changes will be processed if multiple reference
    # changes are included in a single push.  The order is significant
    # mostly because new commit notifications are threaded together
    # with the first reference change that includes the commit.  The
    # following order thus causes commits to be grouped with branch
    # changes (as opposed to tag changes) if possible.
    SORT_ORDER = dict(
        (value, i) for (i, value) in enumerate([
            (BranchChange, 'update'),
            (BranchChange, 'create'),
            (AnnotatedTagChange, 'update'),
            (AnnotatedTagChange, 'create'),
            (NonAnnotatedTagChange, 'update'),
            (NonAnnotatedTagChange, 'create'),
            (BranchChange, 'delete'),
            (AnnotatedTagChange, 'delete'),
            (NonAnnotatedTagChange, 'delete'),
            (OtherReferenceChange, 'update'),
            (OtherReferenceChange, 'create'),
            (OtherReferenceChange, 'delete'),
            ])
        )

    def __init__(self, changes, ignore_other_refs=False):
        self.changes = sorted(changes, key=self._sort_key)
        self.__other_ref_sha1s = None
        self.__cached_commits_spec = {}

        if ignore_other_refs:
            self.__other_ref_sha1s = set()

    @classmethod
    def _sort_key(klass, change):
        return (klass.SORT_ORDER[change.__class__, change.change_type], change.refname,)

    @property
    def _other_ref_sha1s(self):
        """The GitObjects referred to by references unaffected by this push.
        """
        if self.__other_ref_sha1s is None:
            # The refnames being changed by this push:
            updated_refs = set(
                change.refname
                for change in self.changes
                )

            # The SHA-1s of commits referred to by all references in this
            # repository *except* updated_refs:
            sha1s = set()
            fmt = (
                '%(objectname) %(objecttype) %(refname)\n'
                '%(*objectname) %(*objecttype) %(refname)'
                )
            for line in read_git_lines(
                    ['for-each-ref', '--format=%s' % (fmt,)]):
                (sha1, type, name) = line.split(' ', 2)
                if sha1 and type == 'commit' and name not in updated_refs:
                    sha1s.add(sha1)

            self.__other_ref_sha1s = sha1s

        return self.__other_ref_sha1s

    def _get_commits_spec_incl(self, new_or_old, reference_change=None):
        """Get new or old SHA-1 from one or each of the changed refs.

        Return a list of SHA-1 commit identifier strings suitable as
        arguments to 'git rev-list' (or 'git log' or ...).  The
        returned identifiers are either the old or new values from one
        or all of the changed references, depending on the values of
        new_or_old and reference_change.

        new_or_old is either the string 'new' or the string 'old'.  If
        'new', the returned SHA-1 identifiers are the new values from
        each changed reference.  If 'old', the SHA-1 identifiers are
        the old values from each changed reference.

        If reference_change is specified and not None, only the new or
        old reference from the specified reference is included in the
        return value.

        This function returns None if there are no matching revisions
        (e.g., because a branch was deleted and new_or_old is 'new').
        """

        if not reference_change:
            incl_spec = sorted(
                getattr(change, new_or_old).sha1
                for change in self.changes
                if getattr(change, new_or_old)
                )
            if not incl_spec:
                incl_spec = None
        elif not getattr(reference_change, new_or_old).commit_sha1:
            incl_spec = None
        else:
            incl_spec = [getattr(reference_change, new_or_old).commit_sha1]
        return incl_spec

    def _get_commits_spec_excl(self, new_or_old):
        """Get exclusion revisions for determining new or discarded commits.

        Return a list of strings suitable as arguments to 'git
        rev-list' (or 'git log' or ...) that will exclude all
        commits that, depending on the value of new_or_old, were
        either previously in the repository (useful for determining
        which commits are new to the repository) or currently in the
        repository (useful for determining which commits were
        discarded from the repository).

        new_or_old is either the string 'new' or the string 'old'.  If
        'new', the commits to be excluded are those that were in the
        repository before the push.  If 'old', the commits to be
        excluded are those that are currently in the repository.  """

        old_or_new = {'old': 'new', 'new': 'old'}[new_or_old]
        excl_revs = self._other_ref_sha1s.union(
            getattr(change, old_or_new).sha1
            for change in self.changes
            if getattr(change, old_or_new).type in ['commit', 'tag']
            )
        return ['^' + sha1 for sha1 in sorted(excl_revs)]

    def get_commits_spec(self, new_or_old, reference_change=None):
        """Get rev-list arguments for added or discarded commits.

        Return a list of strings suitable as arguments to 'git
        rev-list' (or 'git log' or ...) that select those commits
        that, depending on the value of new_or_old, are either new to
        the repository or were discarded from the repository.

        new_or_old is either the string 'new' or the string 'old'.  If
        'new', the returned list is used to select commits that are
        new to the repository.  If 'old', the returned value is used
        to select the commits that have been discarded from the
        repository.

        If reference_change is specified and not None, the new or
        discarded commits are limited to those that are reachable from
        the new or old value of the specified reference.

        This function returns None if there are no added (or discarded)
        revisions.
        """
        key = (new_or_old, reference_change)
        if key not in self.__cached_commits_spec:
            ret = self._get_commits_spec_incl(new_or_old, reference_change)
            if ret is not None:
                ret.extend(self._get_commits_spec_excl(new_or_old))
            self.__cached_commits_spec[key] = ret
        return self.__cached_commits_spec[key]

    def get_new_commits(self, reference_change=None):
        """Return a list of commits added by this push.

        Return a list of the object names of commits that were added
        by the part of this push represented by reference_change.  If
        reference_change is None, then return a list of *all* commits
        added by this push."""

        spec = self.get_commits_spec('new', reference_change)
        return git_rev_list(spec)

    def get_discarded_commits(self, reference_change):
        """Return a list of commits discarded by this push.

        Return a list of the object names of commits that were
        entirely discarded from the repository by the part of this
        push represented by reference_change."""

        spec = self.get_commits_spec('old', reference_change)
        return git_rev_list(spec)

    def send_emails(self, mailer, body_filter=None):
        """Use send all of the notification emails needed for this push.

        Use send all of the notification emails (including reference
        change emails and commit emails) needed for this push.  Send
        the emails using mailer.  If body_filter is not None, then use
        it to filter the lines that are intended for the email
        body."""

        # The sha1s of commits that were introduced by this push.
        # They will be removed from this set as they are processed, to
        # guarantee that one (and only one) email is generated for
        # each new commit.
        unhandled_sha1s = set(self.get_new_commits())
        send_date = IncrementalDateTime()
        for change in self.changes:
            sha1s = []
            for sha1 in reversed(list(self.get_new_commits(change))):
                if sha1 in unhandled_sha1s:
                    sha1s.append(sha1)
                    unhandled_sha1s.remove(sha1)

            # Check if we've got anyone to send to
            if not change.recipients:
                change.environment.log_warning(
                    '*** no recipients configured so no email will be sent\n'
                    '*** for %r update %s->%s\n'
                    % (change.refname, change.old.sha1, change.new.sha1,)
                    )
            else:
                if not change.environment.quiet:
                    change.environment.log_msg(
                        'Sending notification emails to: %s\n' % (change.recipients,))
                extra_values = {'send_date': send_date.next()}

                rev = change.send_single_combined_email(sha1s)
                if rev:
                    mailer.send(
                        change.generate_combined_email(self, rev, body_filter, extra_values),
                        rev.recipients,
                        )
                    # This change is now fully handled; no need to handle
                    # individual revisions any further.
                    continue
                else:
                    mailer.send(
                        change.generate_email(self, body_filter, extra_values),
                        change.recipients,
                        )

            max_emails = change.environment.maxcommitemails
            if max_emails and len(sha1s) > max_emails:
                change.environment.log_warning(
                    '*** Too many new commits (%d), not sending commit emails.\n' % len(sha1s)
                    + '*** Try setting multimailhook.maxCommitEmails to a greater value\n'
                    + '*** Currently, multimailhook.maxCommitEmails=%d\n' % max_emails
                    )
                return

            for (num, sha1) in enumerate(sha1s):
                rev = Revision(change, GitObject(sha1), num=num + 1, tot=len(sha1s))
                if not rev.recipients and rev.cc_recipients:
                    change.environment.log_msg('*** Replacing Cc: with To:\n')
                    rev.recipients = rev.cc_recipients
                    rev.cc_recipients = None
                if rev.recipients:
                    extra_values = {'send_date': send_date.next()}
                    mailer.send(
                        rev.generate_email(self, body_filter, extra_values),
                        rev.recipients,
                        )

        # Consistency check:
        if unhandled_sha1s:
            change.environment.log_error(
                'ERROR: No emails were sent for the following new commits:\n'
                '    %s\n'
                % ('\n    '.join(sorted(unhandled_sha1s)),)
                )


def run_as_post_receive_hook(environment, mailer):
    changes = []
    for line in sys.stdin:
        (oldrev, newrev, refname) = line.strip().split(' ', 2)
        changes.append(
            ReferenceChange.create(environment, oldrev, newrev, refname)
            )
    push = Push(changes)
    push.send_emails(mailer, body_filter=environment.filter_body)


def run_as_update_hook(environment, mailer, refname, oldrev, newrev, force_send=False):
    changes = [
        ReferenceChange.create(
            environment,
            read_git_output(['rev-parse', '--verify', oldrev]),
            read_git_output(['rev-parse', '--verify', newrev]),
            refname,
            ),
        ]
    push = Push(changes, force_send)
    push.send_emails(mailer, body_filter=environment.filter_body)


def choose_mailer(config, environment):
    mailer = config.get('mailer', default='sendmail')

    if mailer == 'smtp':
        smtpserver = config.get('smtpserver', default='localhost')
        smtpservertimeout = float(config.get('smtpservertimeout', default=10.0))
        smtpserverdebuglevel = int(config.get('smtpserverdebuglevel', default=0))
        smtpencryption = config.get('smtpencryption', default='none')
        smtpuser = config.get('smtpuser', default='')
        smtppass = config.get('smtppass', default='')
        mailer = SMTPMailer(
            envelopesender=(environment.get_sender() or environment.get_fromaddr()),
            smtpserver=smtpserver, smtpservertimeout=smtpservertimeout,
            smtpserverdebuglevel=smtpserverdebuglevel,
            smtpencryption=smtpencryption,
            smtpuser=smtpuser,
            smtppass=smtppass,
            )
    elif mailer == 'sendmail':
        command = config.get('sendmailcommand')
        if command:
            command = shlex.split(command)
        mailer = SendMailer(command=command, envelopesender=environment.get_sender())
    else:
        environment.log_error(
            'fatal: multimailhook.mailer is set to an incorrect value: "%s"\n' % mailer
            + 'please use one of "smtp" or "sendmail".\n'
            )
        sys.exit(1)
    return mailer


KNOWN_ENVIRONMENTS = {
    'generic': GenericEnvironmentMixin,
    'gitolite': GitoliteEnvironmentMixin,
    }


def choose_environment(config, osenv=None, env=None, recipients=None):
    if not osenv:
        osenv = os.environ

    environment_mixins = [
        ProjectdescEnvironmentMixin,
        ConfigMaxlinesEnvironmentMixin,
        ComputeFQDNEnvironmentMixin,
        ConfigFilterLinesEnvironmentMixin,
        PusherDomainEnvironmentMixin,
        ConfigOptionsEnvironmentMixin,
        ]
    environment_kw = {
        'osenv': osenv,
        'config': config,
        }

    if not env:
        env = config.get('environment')

    if not env:
        if 'GL_USER' in osenv and 'GL_REPO' in osenv:
            env = 'gitolite'
        else:
            env = 'generic'

    environment_mixins.append(KNOWN_ENVIRONMENTS[env])

    if recipients:
        environment_mixins.insert(0, StaticRecipientsEnvironmentMixin)
        environment_kw['refchange_recipients'] = recipients
        environment_kw['announce_recipients'] = recipients
        environment_kw['revision_recipients'] = recipients
        environment_kw['scancommitforcc'] = config.get('scancommitforcc')
    else:
        environment_mixins.insert(0, ConfigRecipientsEnvironmentMixin)

    environment_klass = type(
        'EffectiveEnvironment',
        tuple(environment_mixins) + (Environment,),
        {},
        )
    return environment_klass(**environment_kw)


def main(args):
    parser = optparse.OptionParser(
        description=__doc__,
        usage='%prog [OPTIONS]\n   or: %prog [OPTIONS] REFNAME OLDREV NEWREV',
        )

    parser.add_option(
        '--environment', '--env', action='store', type='choice',
        choices=['generic', 'gitolite'], default=None,
        help=(
            'Choose type of environment is in use.  Default is taken from '
            'multimailhook.environment if set; otherwise "generic".'
            ),
        )
    parser.add_option(
        '--stdout', action='store_true', default=False,
        help='Output emails to stdout rather than sending them.',
        )
    parser.add_option(
        '--recipients', action='store', default=None,
        help='Set list of email recipients for all types of emails.',
        )
    parser.add_option(
        '--show-env', action='store_true', default=False,
        help=(
            'Write to stderr the values determined for the environment '
            '(intended for debugging purposes).'
            ),
        )
    parser.add_option(
        '--force-send', action='store_true', default=False,
        help=(
            'Force sending refchange email when using as an update hook. '
            'This is useful to work around the unreliable new commits '
            'detection in this mode.'
            ),
        )

    (options, args) = parser.parse_args(args)

    config = Config('multimailhook')

    try:
        environment = choose_environment(
            config, osenv=os.environ,
            env=options.environment,
            recipients=options.recipients,
            )

        if options.show_env:
            sys.stderr.write('Environment values:\n')
            for (k, v) in sorted(environment.get_values().items()):
                sys.stderr.write('    %s : %r\n' % (k, v))
            sys.stderr.write('\n')

        if options.stdout or environment.stdout:
            mailer = OutputMailer(sys.stdout)
        else:
            mailer = choose_mailer(config, environment)

        # Dual mode: if arguments were specified on the command line, run
        # like an update hook; otherwise, run as a post-receive hook.
        if args:
            if len(args) != 3:
                parser.error('Need zero or three non-option arguments')
            (refname, oldrev, newrev) = args
            run_as_update_hook(environment, mailer, refname, oldrev, newrev, options.force_send)
        else:
            run_as_post_receive_hook(environment, mailer)
    except ConfigurationException, e:
        sys.exit(str(e))


if __name__ == '__main__':
    main(sys.argv[1:])
