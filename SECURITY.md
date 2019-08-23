# Security Policy

## Supported Versions

Git for Windows is a "friendly fork" of [Git](https://git-scm.com/), i.e. changes in Git for Windows are frequently contributed back, and Git for Windows' release cycle closely following Git's.

While Git maintains several release trains (when v2.19.1 was released, there were updates to v2.14.x-v2.18.x, too, for example), Git for Windows follows only the latest Git release. For example, there is no Git for Windows release corresponding to Git v2.16.5 (which was released after v2.19.0).

One exception is [MinGit for Windows](https://github.com/git-for-windows/git/wiki/MinGit) (a minimal subset of Git for Windows, intended for bundling with third-party applications that do not need any interactive commands nor support for `git svn`): critical security fixes are backported to the v2.11.x, v2.14.x, v2.19.x, v2.21.x and v2.23.x release trains.

## Version number scheme

The Git for Windows versions reflect the Git version on which they are based. For example, Git for Windows v2.21.0 is based on Git v2.21.0.

As Git for Windows bundles more than just Git (such as Bash, OpenSSL, OpenSSH, GNU Privacy Guard), sometimes there are interim releases without corresponding Git releases. In these cases, Git for Windows appends a number in parentheses, starting with the number 2, then 3, etc. For example, both Git for Windows v2.17.1 and v2.17.1(2) were based on Git v2.17.1, but the latter included updates for Git Credential Manager and Git LFS, fixing critical regressions.

## Tag naming scheme

Every Git for Windows version is tagged using a name that starts with the Git version on which it is based, with the suffix `.windows.<patchlevel>` appended. For example, Git for Windows v2.17.1' source code is tagged as [`v2.17.1.windows.1`](https://github.com/git-for-windows/git/releases/tag/v2.17.1.windows.1) (the patch level is always at least 1, given that Git for Windows always has patches on top of Git). Likewise, Git for Windows v2.17.1(2)' source code is tagged as [`v2.17.1.windows.2`](https://github.com/git-for-windows/git/releases/tag/v2.17.1.windows.2).

## Reporting a Vulnerability

Please send a mail to git-security@googlegroups.com when you found a security issue in Git or in Git for Windows, even when you are not 100% certain that it is _actually_ a security issue. Typically, you will receive an answer within a day or even within a few hours.
