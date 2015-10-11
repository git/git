Setting up git-multimail on Gerrit
==================================

Gerrit has its own email-sending system, but you may prefer using
``git-multimail`` instead. It supports Gerrit natively as a Gerrit
``ref-updated`` hook (Warning: `Gerrit hooks
<https://gerrit-review.googlesource.com/Documentation/config-hooks.html>`__
are distinct from Git hooks). Setting up ``git-multimail`` on a Gerrit
installation can be done following the instructions below.

The explanations show an easy way to set up ``git-multimail``,
but leave ``git-multimail`` installed and unconfigured for a while. If
you run Gerrit on a production server, it is advised that you
execute the step "Set up the hook" last to avoid confusing your users
in the meantime.

Set up the hook
---------------

Create a directory ``$site_path/hooks/`` if it does not exist (if you
don't know what ``$site_path`` is, run ``gerrit.sh status`` and look
for a ``GERRIT_SITE`` line). Either copy ``git_multimail.py`` to
``$site_path/hooks/ref-updated`` or create a wrapper script like
this::

  #! /bin/sh
  exec /path/to/git_multimail.py "$@"

In both cases, make sure the file is named exactly
``$site_path/hooks/ref-updated`` and is executable.

(Alternatively, you may configure the ``[hooks]`` section of
gerrit.config)

Configuration
-------------

Log on the gerrit server and edit ``$site_path/git/$project/config``
to configure ``git-multimail``.

Troubleshooting
---------------

Warning: this will disable ``git-multimail`` during the debug, and
could confuse your users. Don't run on a production server.

To debug configuration issues with ``git-multimail``, you can add the
``--stdout`` option when calling ``git_multimail.py`` like this::

  #!/bin/sh
  exec /path/to/git-multimail/git-multimail/git_multimail.py \
    --stdout "$@" >> /tmp/log.txt

and try pushing from a test repository. You should see the source of
the email that would have been sent in the output of ``git push`` in
the file ``/tmp/log.txt``.
