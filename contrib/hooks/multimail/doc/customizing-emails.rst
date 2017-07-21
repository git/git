Customizing the content and formatting of emails
================================================

Overloading template strings
----------------------------

The content of emails is generated based on template strings defined
in ``git_multimail.py``. You can customize these template strings
without changing the script itself, by defining a Python wrapper
around it. The python wrapper should ``import git_multimail`` and then
override the ``git_multimail.*`` strings like this::

  import sys  # needed for sys.argv

  # Import and customize git_multimail:
  import git_multimail
  git_multimail.REVISION_INTRO_TEMPLATE = """..."""
  git_multimail.COMBINED_INTRO_TEMPLATE = git_multimail.REVISION_INTRO_TEMPLATE

  # start git_multimail itself:
  git_multimail.main(sys.argv[1:])

The template strings can use any value already used in the existing
templates (read the source code).

Using HTML in template strings
------------------------------

If ``multimailhook.commitEmailFormat`` is set to HTML, then
git-multimail will generate HTML emails for commit notifications. The
log and diff will be formatted automatically by git-multimail. By
default, any HTML special character in the templates will be escaped.

To use HTML formatting in the introduction of the email, set
``multimailhook.htmlInIntro`` to ``true``. Then, the template can
contain any HTML tags, that will be sent as-is in the email. For
example, to add some formatting and a link to the online commit, use
a format like::

  git_multimail.REVISION_INTRO_TEMPLATE = """\
  <span style="color:#808080">This is an automated email from the git hooks/post-receive script.</span><br /><br />

  <strong>%(pusher)s</strong> pushed a commit to %(refname_type)s %(short_refname)s
  in repository %(repo_shortname)s.<br />

  <a href="https://github.com/git-multimail/git-multimail/commit/%(newrev)s">View on GitHub</a>.
  """

Note that the values expanded from ``%(variable)s`` in the format
strings will still be escaped.

For a less flexible but easier to set up way to add a link to commit
emails, see ``multimailhook.commitBrowseURL``.

Similarly, one can set ``multimailhook.htmlInFooter`` and override any
of the ``*_FOOTER*`` template strings.
