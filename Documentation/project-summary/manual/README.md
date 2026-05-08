# Manual Summary

## Where The Manuals Live

- `Documentation/` is the main source of truth for project manuals.
- Most command docs are AsciiDoc files like `Documentation/git-commit.adoc` and `Documentation/git-ace.adoc`.
- Tutorials and conceptual guides live alongside command docs, for example `gittutorial.adoc`, `giteveryday.adoc`, and `user-manual.adoc`.
- Technical deep dives live in `Documentation/technical/`.

## Manual Structure

- Command manpages: `git-*.adoc`
- User/reference guides: `git*.adoc` files such as `gitcli.adoc`, `gitglossary.adoc`, and `gitworkflows.adoc`
- Contributor/process docs: `SubmittingPatches`, `CodingGuidelines`, `ReviewingGuidelines.adoc`
- How-to articles: `Documentation/howto/`

## Build Flow

`Documentation/Makefile` gathers manpage sources, user guides, how-tos, and technical articles, then builds manpages and HTML through AsciiDoc or Asciidoctor tooling.

Additional detail:

- `custom-commands.md`: the commands that make Ace feel meaningfully different from stock Git.
- `documentation-system.md`: how documentation is organized and built.
- `map.md`: where to look for different kinds of manuals.
