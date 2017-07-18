Troubleshooting issues with git-multimail: a FAQ
================================================

Git is not using the right address in the From/To/Reply-To field
----------------------------------------------------------------

First, make sure that git-multimail actually uses what you think it is
using. A lot happens to your email (especially when posting to a
mailing-list) between the time `git_multimail.py` sends it and the
time it reaches your inbox.

A simple test (to do on a test repository, do not use in production as
it would disable email sending): change your post-receive hook to call
`git_multimail.py` with the `--stdout` option, and try to push to the
repository. You should see something like::

  Counting objects: 3, done.
  Writing objects: 100% (3/3), 263 bytes | 0 bytes/s, done.
  Total 3 (delta 0), reused 0 (delta 0)
  remote: Sending notification emails to: foo.bar@example.com
  remote: ===========================================================================
  remote: Date: Mon, 25 Apr 2016 18:39:59 +0200
  remote: To: foo.bar@example.com
  remote: Subject: [git] branch master updated: foo
  remote: MIME-Version: 1.0
  remote: Content-Type: text/plain; charset=utf-8
  remote: Content-Transfer-Encoding: 8bit
  remote: Message-ID: <20160425163959.2311.20498@anie>
  remote: From: Auth Or <Foo.Bar@example.com>
  remote: Reply-To: Auth Or <Foo.Bar@example.com>
  remote: X-Git-Host: example
  ...
  remote: --
  remote: To stop receiving notification emails like this one, please contact
  remote: the administrator of this repository.
  remote: ===========================================================================
  To /path/to/repo
     6278f04..e173f20  master -> master

Note: this does not include the sender (Return-Path: header), as it is
not part of the message content but passed to the mailer. Some mailer
show the ``Sender:`` field instead of the ``From:`` field (for
example, Zimbra Webmail shows ``From: <sender-field> on behalf of
<from-field>``).
