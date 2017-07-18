This directory contains various modules for Emacs support.

To make the modules available to Emacs, you should add this directory
to your load-path, and then require the modules you want. This can be
done by adding to your .emacs something like this:

  (add-to-list 'load-path ".../git/contrib/emacs")
  (require 'git)
  (require 'git-blame)


The following modules are available:

* git.el:

  Status manager that displays the state of all the files of the
  project, and provides easy access to the most frequently used git
  commands. The user interface is as far as possible compatible with
  the pcl-cvs mode. It can be started with `M-x git-status'.

* git-blame.el:

  Emacs implementation of incremental git-blame.  When you turn it on
  while viewing a file, the editor buffer will be updated by setting
  the background of individual lines to a color that reflects which
  commit it comes from.  And when you move around the buffer, a
  one-line summary will be shown in the echo area.

* vc-git.el:

  This file used to contain the VC-mode backend for git, but it is no
  longer distributed with git. It is now maintained as part of Emacs
  and included in standard Emacs distributions starting from version
  22.2.

  If you have an earlier Emacs version, upgrading to Emacs 22 is
  recommended, since the VC mode in older Emacs is not generic enough
  to be able to support git in a reasonable manner, and no attempt has
  been made to backport vc-git.el.
