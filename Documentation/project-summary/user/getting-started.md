# Getting Started

## What To Learn First

This project should be approached in two layers:

1. standard Git concepts and commands
2. Ace-specific branch-tree workflows when needed

This is not just a teaching order. It is the intended product shape. Users should not need to unlearn Git in order to use Ace.

## Recommended Reading Order

1. `README.md`
2. `Documentation/gittutorial.adoc`
3. `Documentation/giteveryday.adoc`
4. `Documentation/user-manual.adoc`
5. `Documentation/git-ace.adoc` for the Ace workflow layer

## Practical Entry Points

- `git help <command>` for command manuals
- `man git-<command>` where installed manpages are available
- `git ace` for the Ace branch-tree feature set

## What Stays Familiar

Repositories, commits, trees, blobs, and refs remain Git-native. Users who do not need Ace workflows can continue to use the normal Git command set.

## Opinionated Advice For Users

- Use normal Git unless you have a real need for branch-tree or stack-aware workflow management.
- Reach for `git ace` when your branch structure is the problem, not when plain Git commands are already enough.
- Treat the experimental commands as advanced tools, not as the default way to do everyday Git work.
