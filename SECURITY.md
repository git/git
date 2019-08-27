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

## Release Candidate (rc) versions

Git for Windows also releases versions that reflect the [upstream release candidates](https://tinyurl.com/gitCal). These contain the `-rc<n>` suffix to the expected regular git version, and before the 'windows' suffix. These releases are independent of upstream but are tied together by convention. It should be noted that these rc versions currently sort after their formal release, so appear to be newer to the updater software.

[All releases](https://github.com/git-for-windows/git/releases/) are listed via a link at the footer of the [Git for Windows](https://gitforwindows.org/) home page.

## Snapshot versions ('nightlies')

Git for Windows also provides snapshots (these are not releases) of the progressing upstream development from the Git-for-Windows "master" branch at the [Snapshots](https://wingit.blob.core.windows.net/files/index.html) page. Link also at the footer of the [Git for Windows](https://gitforwindows.org/) home page.

## Following 'upstream' developments

The [gitforwindows/git repository](https://github.com/git-for-windows/git) also provides the shears/* and vs/master branches. The shears branches follow the upstream development with the addition of the Windows specific patches via automated continuous integration. The vs/master branch adds a commit on top of Git-for-Windows "master", providing the project files ready to build Git in Visual Studio using the MSVC tool chain.

## Reporting a Vulnerability

Please send a mail to git-security@googlegroups.com when you found a security issue in Git or in Git for Windows, even when you are not 100% certain that it is _actually_ a security issue. Typically, you will receive an answer within a day or even within a few hours.
