# Documentation System

## Source Format

Most project manuals are written in AsciiDoc under `Documentation/`. These source files are used to produce manpages and HTML documentation.

## Main Build Entry

`Documentation/Makefile` classifies content into several groups:

- command manpages such as `git-*.adoc`
- section 5 and 7 guides
- how-to articles under `Documentation/howto/`
- technical documents under `Documentation/technical/`

## Output Model

The docs can be rendered through AsciiDoc or Asciidoctor into:

- manpages
- HTML
- other installable documentation artifacts

## Ace-Specific Manual Surface

Ace's custom user-facing command is documented in `Documentation/git-ace.adoc`. That file is the main reference for the project-specific branch-tree workflow layer.

## Opinionated Assessment

- The repository already has an unusually rich documentation system by normal project standards.
- Because of that, undocumented behavior stands out more here than it would in a smaller codebase.
- The fork becomes easier to understand when custom behavior is documented with the same discipline as upstream-style commands.
- `Documentation/project-summary/` should not compete with the official manuals; it should explain the shape and intent of the repository so readers know where to go next.
