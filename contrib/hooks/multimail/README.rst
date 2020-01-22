git-multimail version 1.5.0
===========================

.. image:: https://travis-ci.org/git-multimail/git-multimail.svg?branch=master
    :target: https://travis-ci.org/git-multimail/git-multimail

git-multimail is a tool for sending notification emails on pushes to a
Git repository.  It includes a Python module called ``git_multimail.py``,
which can either be used as a hook script directly or can be imported
as a Python module into another script.

git-multimail is derived from the Git project's old
contrib/hooks/post-receive-email, and is mostly compatible with that
script.  See README.migrate-from-post-receive-email for details about
the differences and for how to migrate from post-receive-email to
git-multimail.

git-multimail, like the rest of the Git project, is licensed under
GPLv2 (see the COPYING file for details).

Please note: although, as a convenience, git-multimail may be
distributed along with the main Git project, development of
git-multimail takes place in its own, separate project.  Please, read
`<CONTRIBUTING.rst>`__ for more information.


By default, for each push received by the repository, git-multimail:

1. Outputs one email summarizing each reference that was changed.
   These "reference change" (called "refchange" below) emails describe
   the nature of the change (e.g., was the reference created, deleted,
   fast-forwarded, etc.) and include a one-line summary of each commit
   that was added to the reference.

2. Outputs one email for each new commit that was introduced by the
   reference change.  These "commit" emails include a list of the
   files changed by the commit, followed by the diffs of files
   modified by the commit.  The commit emails are threaded to the
   corresponding reference change email via "In-Reply-To".  This style
   (similar to the "git format-patch" style used on the Git mailing
   list) makes it easy to scan through the emails, jump to patches
   that need further attention, and write comments about specific
   commits.  Commits are handled in reverse topological order (i.e.,
   parents shown before children).  For example::

     [git] branch master updated
     + [git] 01/08: doc: fix xref link from api docs to manual pages
     + [git] 02/08: api-credentials.txt: show the big picture first
     + [git] 03/08: api-credentials.txt: mention credential.helper explicitly
     + [git] 04/08: api-credentials.txt: add "see also" section
     + [git] 05/08: t3510 (cherry-pick-sequence): add missing '&&'
     + [git] 06/08: Merge branch 'rr/maint-t3510-cascade-fix'
     + [git] 07/08: Merge branch 'mm/api-credentials-doc'
     + [git] 08/08: Git 1.7.11-rc2

   By default, each commit appears in exactly one commit email, the
   first time that it is pushed to the repository.  If a commit is later
   merged into another branch, then a one-line summary of the commit
   is included in the reference change email (as usual), but no
   additional commit email is generated. See
   `multimailhook.refFilter(Inclusion|Exclusion|DoSend|DontSend)Regex`
   below to configure which branches and tags are watched by the hook.

   By default, reference change emails have their "Reply-To" field set
   to the person who pushed the change, and commit emails have their
   "Reply-To" field set to the author of the commit.

3. Output one "announce" mail for each new annotated tag, including
   information about the tag and optionally a shortlog describing the
   changes since the previous tag.  Such emails might be useful if you
   use annotated tags to mark releases of your project.


Requirements
------------

* Python 2.x, version 2.4 or later.  No non-standard Python modules
  are required.  git-multimail has preliminary support for Python 3
  (but it has been better tested with Python 2).

* The ``git`` command must be in your PATH.  git-multimail is known to
  work with Git versions back to 1.7.1.  (Earlier versions have not
  been tested; if you do so, please report your results.)

* To send emails using the default configuration, a standard sendmail
  program must be located at '/usr/sbin/sendmail' or
  '/usr/lib/sendmail' and must be configured correctly to send emails.
  If this is not the case, set multimailhook.sendmailCommand, or see
  the multimailhook.mailer configuration variable below for how to
  configure git-multimail to send emails via an SMTP server.

* git-multimail is currently tested only on Linux. It may or may not
  work on other platforms such as Windows and Mac OS. See
  `<CONTRIBUTING.rst>`__ to improve the situation.


Invocation
----------

``git_multimail.py`` is designed to be used as a ``post-receive`` hook in a
Git repository (see githooks(5)).  Link or copy it to
$GIT_DIR/hooks/post-receive within the repository for which email
notifications are desired.  Usually it should be installed on the
central repository for a project, to which all commits are eventually
pushed.

For use on pre-v1.5.1 Git servers, ``git_multimail.py`` can also work as
an ``update`` hook, taking its arguments on the command line.  To use
this script in this manner, link or copy it to $GIT_DIR/hooks/update.
Please note that the script is not completely reliable in this mode
[1]_.

Alternatively, ``git_multimail.py`` can be imported as a Python module
into your own Python post-receive script.  This method is a bit more
work, but allows the behavior of the hook to be customized using
arbitrary Python code.  For example, you can use a custom environment
(perhaps inheriting from GenericEnvironment or GitoliteEnvironment) to

* change how the user who did the push is determined

* read users' email addresses from an LDAP server or from a database

* decide which users should be notified about which commits based on
  the contents of the commits (e.g., for users who want to be notified
  only about changes affecting particular files or subdirectories)

Or you can change how emails are sent by writing your own Mailer
class.  The ``post-receive`` script in this directory demonstrates how
to use ``git_multimail.py`` as a Python module.  (If you make interesting
changes of this type, please consider sharing them with the
community.)


Troubleshooting/FAQ
-------------------

Please read `<doc/troubleshooting.rst>`__ for frequently asked
questions and common issues with git-multimail.


Configuration
-------------

By default, git-multimail mostly takes its configuration from the
following ``git config`` settings:

multimailhook.environment
    This describes the general environment of the repository. In most
    cases, you do not need to specify a value for this variable:
    `git-multimail` will autodetect which environment to use.
    Currently supported values:

    generic
      the username of the pusher is read from $USER or $USERNAME and
      the repository name is derived from the repository's path.

    gitolite
      Environment to use when ``git-multimail`` is ran as a gitolite_
      hook.

      The username of the pusher is read from $GL_USER, the repository
      name is read from $GL_REPO, and the From: header value is
      optionally read from gitolite.conf (see multimailhook.from).

      For more information about gitolite and git-multimail, read
      `<doc/gitolite.rst>`__

    stash
      Environment to use when ``git-multimail`` is ran as an Atlassian
      BitBucket Server (formerly known as Atlassian Stash) hook.

      **Warning:** this mode was provided by a third-party contributor
      and never tested by the git-multimail maintainers. It is
      provided as-is and may or may not work for you.

      This value is automatically assumed when the stash-specific
      flags (``--stash-user`` and ``--stash-repo``) are specified on
      the command line. When this environment is active, the username
      and repo come from these two command line flags, which must be
      specified.

    gerrit
      Environment to use when ``git-multimail`` is ran as a
      ``ref-updated`` Gerrit hook.

      This value is used when the gerrit-specific command line flags
      (``--oldrev``, ``--newrev``, ``--refname``, ``--project``) for
      gerrit's ref-updated hook are present. When this environment is
      active, the username of the pusher is taken from the
      ``--submitter`` argument if that command line option is passed,
      otherwise 'Gerrit' is used. The repository name is taken from
      the ``--project`` option on the command line, which must be passed.

      For more information about gerrit and git-multimail, read
      `<doc/gerrit.rst>`__

    If none of these environments is suitable for your setup, then you
    can implement a Python class that inherits from Environment and
    instantiate it via a script that looks like the example
    post-receive script.

    The environment value can be specified on the command line using
    the ``--environment`` option. If it is not specified on the
    command line or by ``multimailhook.environment``, the value is
    guessed as follows:

    * If stash-specific (respectively gerrit-specific) command flags
      are present on the command-line, then ``stash`` (respectively
      ``gerrit``) is used.

    * If the environment variables $GL_USER and $GL_REPO are set, then
      ``gitolite`` is used.

    * If none of the above apply, then ``generic`` is used.

multimailhook.repoName
    A short name of this Git repository, to be used in various places
    in the notification email text.  The default is to use $GL_REPO
    for gitolite repositories, or otherwise to derive this value from
    the repository path name.

multimailhook.mailingList
    The list of email addresses to which notification emails should be
    sent, as RFC 2822 email addresses separated by commas.  This
    configuration option can be multivalued.  Leave it unset or set it
    to the empty string to not send emails by default.  The next few
    settings can be used to configure specific address lists for
    specific types of notification email.

multimailhook.refchangeList
    The list of email addresses to which summary emails about
    reference changes should be sent, as RFC 2822 email addresses
    separated by commas.  This configuration option can be
    multivalued.  The default is the value in
    multimailhook.mailingList.  Set this value to "none" (or the empty
    string) to prevent reference change emails from being sent even if
    multimailhook.mailingList is set.

multimailhook.announceList
    The list of email addresses to which emails about new annotated
    tags should be sent, as RFC 2822 email addresses separated by
    commas.  This configuration option can be multivalued.  The
    default is the value in multimailhook.refchangeList or
    multimailhook.mailingList.  Set this value to "none" (or the empty
    string) to prevent annotated tag announcement emails from being sent
    even if one of the other values is set.

multimailhook.commitList
    The list of email addresses to which emails about individual new
    commits should be sent, as RFC 2822 email addresses separated by
    commas.  This configuration option can be multivalued.  The
    default is the value in multimailhook.mailingList.  Set this value
    to "none" (or the empty string) to prevent notification emails about
    individual commits from being sent even if
    multimailhook.mailingList is set.

multimailhook.announceShortlog
    If this option is set to true, then emails about changes to
    annotated tags include a shortlog of changes since the previous
    tag.  This can be useful if the annotated tags represent releases;
    then the shortlog will be a kind of rough summary of what has
    happened since the last release.  But if your tagging policy is
    not so straightforward, then the shortlog might be confusing
    rather than useful.  Default is false.

multimailhook.commitEmailFormat
    The format of email messages for the individual commits, can be "text" or
    "html". In the latter case, the emails will include diffs using colorized
    HTML instead of plain text used by default. Note that this  currently the
    ref change emails are always sent in plain text.

    Note that when using "html", the formatting is done by parsing the
    output of ``git log`` with ``-p``. When using
    ``multimailhook.commitLogOpts`` to specify a ``--format`` for
    ``git log``, one may get false positive (e.g. lines in the body of
    the message starting with ``+++`` or ``---`` colored in red or
    green).

    By default, all the message is HTML-escaped. See
    ``multimailhook.htmlInIntro`` to change this behavior.

multimailhook.commitBrowseURL
    Used to generate a link to an online repository browser in commit
    emails. This variable must be a string. Format directives like
    ``%(<variable>)s`` will be expanded the same way as template
    strings. In particular, ``%(id)s`` will be replaced by the full
    Git commit identifier (40-chars hexadecimal).

    If the string does not contain any format directive, then
    ``%(id)s`` will be automatically added to the string. If you don't
    want ``%(id)s`` to be automatically added, use the empty format
    directive ``%()s`` anywhere in the string.

    For example, a suitable value for the git-multimail project itself
    would be
    ``https://github.com/git-multimail/git-multimail/commit/%(id)s``.

multimailhook.htmlInIntro, multimailhook.htmlInFooter
    When generating an HTML message, git-multimail escapes any HTML
    sequence by default. This means that if a template contains HTML
    like ``<a href="foo">link</a>``, the reader will see the HTML
    source code and not a proper link.

    Set ``multimailhook.htmlInIntro`` to true to allow writing HTML
    formatting in introduction templates. Similarly, set
    ``multimailhook.htmlInFooter`` for HTML in the footer.

    Variables expanded in the template are still escaped. For example,
    if a repository's path contains a ``<``, it will be rendered as
    such in the message.

    Read `<doc/customizing-emails.rst>`__ for more details and
    examples.

multimailhook.refchangeShowGraph
    If this option is set to true, then summary emails about reference
    changes will additionally include:

    * a graph of the added commits (if any)

    * a graph of the discarded commits (if any)

    The log is generated by running ``git log --graph`` with the options
    specified in graphOpts.  The default is false.

multimailhook.refchangeShowLog
    If this option is set to true, then summary emails about reference
    changes will include a detailed log of the added commits in
    addition to the one line summary.  The log is generated by running
    ``git log`` with the options specified in multimailhook.logOpts.
    Default is false.

multimailhook.mailer
    This option changes the way emails are sent.  Accepted values are:

    * **sendmail (the default)**: use the command ``/usr/sbin/sendmail`` or
      ``/usr/lib/sendmail`` (or sendmailCommand, if configured).  This
      mode can be further customized via the following options:

      multimailhook.sendmailCommand
          The command used by mailer ``sendmail`` to send emails.  Shell
          quoting is allowed in the value of this setting, but remember that
          Git requires double-quotes to be escaped; e.g.::

              git config multimailhook.sendmailcommand '/usr/sbin/sendmail -oi -t -F \"Git Repo\"'

          Default is '/usr/sbin/sendmail -oi -t' or
          '/usr/lib/sendmail -oi -t' (depending on which file is
          present and executable).

      multimailhook.envelopeSender
          If set then pass this value to sendmail via the -f option to set
          the envelope sender address.

    * **smtp**: use Python's smtplib.  This is useful when the sendmail
      command is not available on the system.  This mode can be
      further customized via the following options:

      multimailhook.smtpServer
          The name of the SMTP server to connect to.  The value can
          also include a colon and a port number; e.g.,
          ``mail.example.com:25``.  Default is 'localhost' using port 25.

      multimailhook.smtpUser, multimailhook.smtpPass
          Server username and password. Required if smtpEncryption is 'ssl'.
          Note that the username and password currently need to be
          set cleartext in the configuration file, which is not
          recommended. If you need to use this option, be sure your
          configuration file is read-only.

      multimailhook.envelopeSender
        The sender address to be passed to the SMTP server.  If
        unset, then the value of multimailhook.from is used.

      multimailhook.smtpServerTimeout
        Timeout in seconds. Default is 10.

      multimailhook.smtpEncryption
        Set the security type. Allowed values: ``none``, ``ssl``, ``tls`` (starttls).
        Default is ``none``.

      multimailhook.smtpCACerts
        Set the path to a list of trusted CA certificate to verify the
        server certificate, only supported when ``smtpEncryption`` is
        ``tls``. If unset or empty, the server certificate is not
        verified. If it targets a file containing a list of trusted CA
        certificates (PEM format) these CAs will be used to verify the
        server certificate. For debian, you can set
        ``/etc/ssl/certs/ca-certificates.crt`` for using the system
        trusted CAs. For self-signed server, you can add your server
        certificate to the system store::

            cd /usr/local/share/ca-certificates/
            openssl s_client -starttls smtp \
                   -connect mail.example.net:587 -showcerts \
                   </dev/null 2>/dev/null \
                 | openssl x509 -outform PEM >mail.example.net.crt
            update-ca-certificates

        and used the updated ``/etc/ssl/certs/ca-certificates.crt``. Or
        directly use your ``/path/to/mail.example.net.crt``. Default is
        unset.

      multimailhook.smtpServerDebugLevel
        Integer number. Set to greater than 0 to activate debugging.

multimailhook.from, multimailhook.fromCommit, multimailhook.fromRefchange
    If set, use this value in the From: field of generated emails.
    ``fromCommit`` is used for commit emails, ``fromRefchange`` is
    used for refchange emails, and ``from`` is used as fall-back in
    all cases.

    The value for these variables can be either:

    - An email address, which will be used directly.

    - The value ``pusher``, in which case the pusher's address (if
      available) will be used.

    - The value ``author`` (meaningful only for ``fromCommit``), in which
      case the commit author's address will be used.

    If config values are unset, the value of the From: header is
    determined as follows:

    1. (gitolite environment only)
       1.a) If ``multimailhook.MailaddressMap`` is set, and is a path
       to an existing file (if relative, it is considered relative to
       the place where ``gitolite.conf`` is located), then this file
       should contain lines like::

           username Firstname Lastname <email@example.com>

       git-multimail will then look for a line where ``$GL_USER``
       matches the ``username`` part, and use the rest of the line for
       the ``From:`` header.

       1.b) Parse gitolite.conf, looking for a block of comments that
       looks like this::

           # BEGIN USER EMAILS
           # username Firstname Lastname <email@example.com>
           # END USER EMAILS

       If that block exists, and there is a line between the BEGIN
       USER EMAILS and END USER EMAILS lines where the first field
       matches the gitolite username ($GL_USER), use the rest of the
       line for the From: header.

    2. If the user.email configuration setting is set, use its value
       (and the value of user.name, if set).

    3. Use the value of multimailhook.envelopeSender.

multimailhook.MailaddressMap
    (gitolite environment only)
    File to look for a ``From:`` address based on the user doing the
    push. Defaults to unset. See ``multimailhook.from`` for details.

multimailhook.administrator
    The name and/or email address of the administrator of the Git
    repository; used in FOOTER_TEMPLATE.  Default is
    multimailhook.envelopesender if it is set; otherwise a generic
    string is used.

multimailhook.emailPrefix
    All emails have this string prepended to their subjects, to aid
    email filtering (though filtering based on the X-Git-* email
    headers is probably more robust).  Default is the short name of
    the repository in square brackets; e.g., ``[myrepo]``.  Set this
    value to the empty string to suppress the email prefix. You may
    use the placeholder ``%(repo_shortname)s`` for the short name of
    the repository.

multimailhook.emailMaxLines
    The maximum number of lines that should be included in the body of
    a generated email.  If not specified, there is no limit.  Lines
    beyond the limit are suppressed and counted, and a final line is
    added indicating the number of suppressed lines.

multimailhook.emailMaxLineLength
    The maximum length of a line in the email body.  Lines longer than
    this limit are truncated to this length with a trailing ``[...]``
    added to indicate the missing text.  The default is 500, because
    (a) diffs with longer lines are probably from binary files, for
    which a diff is useless, and (b) even if a text file has such long
    lines, the diffs are probably unreadable anyway.  To disable line
    truncation, set this option to 0.

multimailhook.subjectMaxLength
    The maximum length of the subject line (i.e. the ``oneline`` field
    in templates, not including the prefix). Lines longer than this
    limit are truncated to this length with a trailing ``[...]`` added
    to indicate the missing text. This option The default is to use
    ``multimailhook.emailMaxLineLength``. This option avoids sending
    emails with overly long subject lines, but should not be needed if
    the commit messages follow the Git convention (one short subject
    line, then a blank line, then the message body). To disable line
    truncation, set this option to 0.

multimailhook.maxCommitEmails
    The maximum number of commit emails to send for a given change.
    When the number of patches is larger that this value, only the
    summary refchange email is sent.  This can avoid accidental
    mailbombing, for example on an initial push.  To disable commit
    emails limit, set this option to 0.  The default is 500.

multimailhook.excludeMergeRevisions
    When sending out revision emails, do not consider merge commits (the
    functional equivalent of `rev-list --no-merges`).
    The default is `false` (send merge commit emails).

multimailhook.emailStrictUTF8
    If this boolean option is set to `true`, then the main part of the
    email body is forced to be valid UTF-8.  Any characters that are
    not valid UTF-8 are converted to the Unicode replacement
    character, U+FFFD.  The default is `true`.

    This option is ineffective with Python 3, where non-UTF-8
    characters are unconditionally replaced.

multimailhook.diffOpts
    Options passed to ``git diff-tree`` when generating the summary
    information for ReferenceChange emails.  Default is ``--stat
    --summary --find-copies-harder``.  Add -p to those options to
    include a unified diff of changes in addition to the usual summary
    output.  Shell quoting is allowed; see ``multimailhook.logOpts`` for
    details.

multimailhook.graphOpts
    Options passed to ``git log --graph`` when generating graphs for the
    reference change summary emails (used only if refchangeShowGraph
    is true).  The default is '--oneline --decorate'.

    Shell quoting is allowed; see logOpts for details.

multimailhook.logOpts
    Options passed to ``git log`` to generate additional info for
    reference change emails (used only if refchangeShowLog is set).
    For example, adding -p will show each commit's complete diff.  The
    default is empty.

    Shell quoting is allowed; for example, a log format that contains
    spaces can be specified using something like::

      git config multimailhook.logopts '--pretty=format:"%h %aN <%aE>%n%s%n%n%b%n"'

    If you want to set this by editing your configuration file
    directly, remember that Git requires double-quotes to be escaped
    (see git-config(1) for more information)::

      [multimailhook]
              logopts = --pretty=format:\"%h %aN <%aE>%n%s%n%n%b%n\"

multimailhook.commitLogOpts
    Options passed to ``git log`` to generate additional info for
    revision change emails.  For example, adding --ignore-all-spaces
    will suppress whitespace changes.  The default options are ``-C
    --stat -p --cc``.  Shell quoting is allowed; see
    multimailhook.logOpts for details.

multimailhook.dateSubstitute
    String to use as a substitute for ``Date:`` in the output of ``git
    log`` while formatting commit messages. This is useful to avoid
    emitting a line that can be interpreted by mailers as the start of
    a cited message (Zimbra webmail in particular). Defaults to
    ``CommitDate:``. Set to an empty string or ``none`` to deactivate
    the behavior.

multimailhook.emailDomain
    Domain name appended to the username of the person doing the push
    to convert it into an email address
    (via ``"%s@%s" % (username, emaildomain)``). More complicated
    schemes can be implemented by overriding Environment and
    overriding its get_pusher_email() method.

multimailhook.replyTo, multimailhook.replyToCommit, multimailhook.replyToRefchange
    Addresses to use in the Reply-To: field for commit emails
    (replyToCommit) and refchange emails (replyToRefchange).
    multimailhook.replyTo is used as default when replyToCommit or
    replyToRefchange is not set. The shortcuts ``pusher`` and
    ``author`` are allowed with the same semantics as for
    ``multimailhook.from``. In addition, the value ``none`` can be
    used to omit the ``Reply-To:`` field.

    The default is ``pusher`` for refchange emails, and ``author`` for
    commit emails.

multimailhook.quiet
    Do not output the list of email recipients from the hook

multimailhook.stdout
    For debugging, send emails to stdout rather than to the
    mailer.  Equivalent to the --stdout command line option

multimailhook.scanCommitForCc
    If this option is set to true, than recipients from lines in commit body
    that starts with ``CC:`` will be added to CC list.
    Default: false

multimailhook.combineWhenSingleCommit
    If this option is set to true and a single new commit is pushed to
    a branch, combine the summary and commit email messages into a
    single email.
    Default: true

multimailhook.refFilterInclusionRegex, multimailhook.refFilterExclusionRegex, multimailhook.refFilterDoSendRegex, multimailhook.refFilterDontSendRegex
    **Warning:** these options are experimental. They should work, but
    the user-interface is not stable yet (in particular, the option
    names may change). If you want to participate in stabilizing the
    feature, please contact the maintainers and/or send pull-requests.
    If you are happy with the current shape of the feature, please
    report it too.

    Regular expressions that can be used to limit refs for which email
    updates will be sent.  It is an error to specify both an inclusion
    and an exclusion regex.  If a ``refFilterInclusionRegex`` is
    specified, emails will only be sent for refs which match this
    regex.  If a ``refFilterExclusionRegex`` regex is specified,
    emails will be sent for all refs except those that match this
    regex (or that match a predefined regex specific to the
    environment, such as "^refs/notes" for most environments and
    "^refs/notes|^refs/changes" for the gerrit environment).

    The expressions are matched against the complete refname, and is
    considered to match if any substring matches. For example, to
    filter-out all tags, set ``refFilterExclusionRegex`` to
    ``^refs/tags/`` (note the leading ``^`` but no trailing ``$``). If
    you set ``refFilterExclusionRegex`` to ``master``, then any ref
    containing ``master`` will be excluded (the ``master`` branch, but
    also ``refs/tags/master`` or ``refs/heads/foo-master-bar``).

    ``refFilterDoSendRegex`` and ``refFilterDontSendRegex`` are
    analogous to ``refFilterInclusionRegex`` and
    ``refFilterExclusionRegex`` with one difference: with
    ``refFilterDoSendRegex`` and ``refFilterDontSendRegex``, commits
    introduced by one excluded ref will not be considered as new when
    they reach an included ref. Typically, if you add a branch ``foo``
    to  ``refFilterDontSendRegex``, push commits to this branch, and
    later merge branch ``foo`` into ``master``, then the notification
    email for ``master`` will contain a commit email only for the
    merge commit. If you include ``foo`` in
    ``refFilterExclusionRegex``, then at the time of merge, you will
    receive one commit email per commit in the branch.

    These variables can be multi-valued, like::

      [multimailhook]
              refFilterExclusionRegex = ^refs/tags/
              refFilterExclusionRegex = ^refs/heads/master$

    You can also provide a whitespace-separated list like::

      [multimailhook]
              refFilterExclusionRegex = ^refs/tags/ ^refs/heads/master$

    Both examples exclude tags and the master branch, and are
    equivalent to::

      [multimailhook]
              refFilterExclusionRegex = ^refs/tags/|^refs/heads/master$

    ``refFilterInclusionRegex`` and ``refFilterExclusionRegex`` are
    strictly stronger than ``refFilterDoSendRegex`` and
    ``refFilterDontSendRegex``. In other words, adding a ref to a
    DoSend/DontSend regex has no effect if it is already excluded by a
    Exclusion/Inclusion regex.

multimailhook.logFile, multimailhook.errorLogFile, multimailhook.debugLogFile

    When set, these variable designate path to files where
    git-multimail will log some messages. Normal messages and error
    messages are sent to ``logFile``, and error messages are also sent
    to ``errorLogFile``. Debug messages and all other messages are
    sent to ``debugLogFile``. The recommended way is to set only one
    of these variables, but it is also possible to set several of them
    (part of the information is then duplicated in several log files,
    for example errors are duplicated to all log files).

    Relative path are relative to the Git repository where the push is
    done.

multimailhook.verbose

    Verbosity level of git-multimail on its standard output. By
    default, show only error and info messages. If set to true, show
    also debug messages.

Email filtering aids
--------------------

All emails include extra headers to enable fine tuned filtering and
give information for debugging.  All emails include the headers
``X-Git-Host``, ``X-Git-Repo``, ``X-Git-Refname``, and ``X-Git-Reftype``.
ReferenceChange emails also include headers ``X-Git-Oldrev`` and ``X-Git-Newrev``;
Revision emails also include header ``X-Git-Rev``.


Customizing email contents
--------------------------

git-multimail mostly generates emails by expanding templates.  The
templates can be customized.  To avoid the need to edit
``git_multimail.py`` directly, the preferred way to change the templates
is to write a separate Python script that imports ``git_multimail.py`` as
a module, then replaces the templates in place.  See the provided
post-receive script for an example of how this is done.


Customizing git-multimail for your environment
----------------------------------------------

git-multimail is mostly customized via an "environment" that describes
the local environment in which Git is running.  Two types of
environment are built in:

GenericEnvironment
    a stand-alone Git repository.

GitoliteEnvironment
    a Git repository that is managed by gitolite_.  For such
    repositories, the identity of the pusher is read from
    environment variable $GL_USER, the name of the repository is read
    from $GL_REPO (if it is not overridden by multimailhook.reponame),
    and the From: header value is optionally read from gitolite.conf
    (see multimailhook.from).

By default, git-multimail assumes GitoliteEnvironment if $GL_USER and
$GL_REPO are set, and otherwise assumes GenericEnvironment.
Alternatively, you can choose one of these two environments explicitly
by setting a ``multimailhook.environment`` config setting (which can
have the value `generic` or `gitolite`) or by passing an --environment
option to the script.

If you need to customize the script in ways that are not supported by
the existing environments, you can define your own environment class
class using arbitrary Python code.  To do so, you need to import
``git_multimail.py`` as a Python module, as demonstrated by the example
post-receive script.  Then implement your environment class; it should
usually inherit from one of the existing Environment classes and
possibly one or more of the EnvironmentMixin classes.  Then set the
``environment`` variable to an instance of your own environment class
and pass it to ``run_as_post_receive_hook()``.

The standard environment classes, GenericEnvironment and
GitoliteEnvironment, are in fact themselves put together out of a
number of mixin classes, each of which handles one aspect of the
customization.  For the finest control over your configuration, you
can specify exactly which mixin classes your own environment class
should inherit from, and override individual methods (or even add your
own mixin classes) to implement entirely new behaviors.  If you
implement any mixins that might be useful to other people, please
consider sharing them with the community!


Getting involved
----------------

Please, read `<CONTRIBUTING.rst>`__ for instructions on how to
contribute to git-multimail.


Footnotes
---------

.. [1] Because of the way information is passed to update hooks, the
       script's method of determining whether a commit has already
       been seen does not work when it is used as an ``update`` script.
       In particular, no notification email will be generated for a
       new commit that is added to multiple references in the same
       push. A workaround is to use --force-send to force sending the
       emails.

.. _gitolite: https://github.com/sitaramc/gitolite
