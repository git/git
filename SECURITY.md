# Security Policy

## Reporting a vulnerability

Please send a detailed mail to git-security@googlegroups.com to
report vulnerabilities in Git.

Even when unsure whether the bug in question is an exploitable
vulnerability, it is recommended to send the report to
git-security@googlegroups.com (and obviously not to discuss the
issue anywhere else).

Vulnerabilities are expected to be discussed _only_ on that
list, and not in public, until the official announcement on the
Git mailing list on the release date.

Examples for details to include:

- Ideally a short description (or a script) to demonstrate an
  exploit.
- The affected platforms and scenarios (the vulnerability might
  only affect setups with case-sensitive file systems, for
  example).
- The name and affiliation of the security researchers who are
  involved in the discovery, if any.
- Whether the vulnerability has already been disclosed.
- How long an embargo would be required to be safe.

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

As a friendly fork of Git (the "upstream" project), Git for Windows is closely corelated to that project.

Consequently, Git for Windows publishes versions based on Git's release candidates (for upcoming "`.0`" versions, see [Git's release schedule](https://tinyurl.com/gitCal)). These versions end in `-rc<n>`, starting with `-rc0` for a very early preview of what is to come, and as with regular versions, Git for Windows tries to follow Git's releases as quickly as possible.

Note: there is currently a bug in the "Check daily for updates" code, where it mistakes the final version as a downgrade from release candidates. Example: if you installed Git for Windows v2.23.0-rc3 and enabled the auto-updater, it would ask you whether you want to "downgrade" to v2.23.0 when that version was available.

[All releases](https://github.com/git-for-windows/git/releases/), including release candidates, are listed via a link at the footer of the [Git for Windows](https://gitforwindows.org/) home page.

## Snapshot versions ('nightly builds')

Git for Windows also provides snapshots (these are not releases) of the the current development as per git-for-Windows/git's `master` branch at the [Snapshots](https://wingit.blob.core.windows.net/files/index.html) page. This link is also listed in the footer of the [Git for Windows](https://gitforwindows.org/) home page.

Note: even if those builds are not exactly "nightly", they are sometimes referred to as "nightly builds" to keep with other projects' nomenclature.

## Following upstream's developments

The [gitforwindows/git repository](https://github.com/git-for-windows/git) also provides the `shears/*` branches. The `shears/*` branches reflect Git for Windows' patches, rebased onto the upstream integration branches, [updated (mostly) via automated CI builds](https://dev.azure.com/git-for-windows/git/_build?definitionId=25).
