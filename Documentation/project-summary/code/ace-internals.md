# Ace Internals

## Why Ace Exists As A Layer

Ace's core trick is not new storage. It is new workflow structure.

Git already has excellent object storage, commit identity, ref transport, and repository interoperability. Ace layers a branch-tree model on top of that so users can work with explicit dependency structure instead of pretending that slash-delimited branch names alone are enough.

## Metadata Layout

Ace stores metadata under `.git/ace/branches/`.

For each virtual branch, Ace creates a directory that mirrors the virtual branch name. Inside that directory, the current implementation uses small metadata files:

- `ref`: stores the backing real branch name
- `parent`: stores the virtual parent branch name when one exists

This is a very inspectable design. A developer can understand Ace state by reading normal filesystem entries instead of reverse-engineering a custom database.

## Virtual Versus Real Branches

Ace distinguishes between:

- virtual branch names such as `feature/base/parser`
- backing Git branch names such as generated `ace/...` refs

This is the key workaround for a real Git limitation: Git cannot simultaneously store every slash-prefix branch as a normal ref when names overlap structurally. Ace preserves the user-facing hierarchy by decoupling the display name from the backing ref name.

The script generates backing branch names from:

- a sanitized slug derived from the virtual name
- a short object hash derived from that virtual name

Opinionated take:

- This is one of the smartest parts of the current design.
- It solves a real workflow problem while staying completely inside Git's ref model.
- It is also the area where accidental metadata drift would be most damaging, so it deserves strong tests.

## Parent-Child Model

Ace records parent-child relationships explicitly instead of inferring them purely from branch naming.

That matters because:

- names are helpful for humans but not enough as the source of truth
- rename operations need hierarchy-aware rewrites
- stack operations need reliable ancestry information
- some valid hierarchies may not match naive path-prefix inference

The implementation includes cycle checks before setting or changing parent relationships.

## Operational Behavior

From the current `git-ace.sh` implementation, Ace supports these major workflow mechanics:

- create a branch with inferred or explicit parentage
- resolve a virtual branch to a backing branch
- rename an entire virtual subtree while updating related parent metadata
- delete a branch or full subtree with safety checks
- print parent, children, chain, and tree views
- run stack-aware recursive `rebase` and `merge` sequences
- run arbitrary commands across descendants in depth-first order

## Agent Integration Model

Ace exports branch context to external tools through environment variables, including:

- `ACE_AGENT_NAME`
- `ACE_BRANCH`
- `ACE_REAL_BRANCH`
- `ACE_PARENT_BRANCH`
- `ACE_REPOSITORY_ROOT`
- `ACE_VCS_BACKEND`
- `ACE_INTEROP_TARGETS`

This is a simple but strong design choice. Instead of hard-coding AI behavior into repository internals, Ace exposes structured context and lets external agents use it.

## Mercurial Interop Model

Ace's Mercurial support is intentionally branch-pointer-oriented.

- bookmark export/import writes simple files mapping commit IDs to virtual branch names
- named-branch export/import does the same for root Ace branches

Opinionated take:

- This is the right level of ambition.
- It aims for interoperability without pretending Git and Mercurial are the same system.
- It preserves Git-native storage while still respecting mixed-tool workflows.

## Current Limits

The current implementation is elegant, but its simplicity implies a few constraints:

- direct low-level Git branch changes can desynchronize Ace metadata
- stack operations rely on repeated checkout/rebase/merge sequencing, so failures need careful recovery
- metadata currently lives in small filesystem files, which is easy to inspect but may become harder to scale or validate if the feature set expands significantly

## What This Means For Contributors

- Treat `.git/ace/branches/` as a user-visible contract, even though it is internal metadata.
- Be very careful with rename, delete, and parent-update logic.
- Prefer additive designs that keep Ace inspectable and Git-compatible.
- If a future feature needs much richer Ace state, document why filesystem metadata is no longer enough before replacing it.
+
+### Agent Environment Contract
+
+The specific contract provided to agents executed via `git ace agent run <agent-name>` is as follows:
+
+- `ACE_AGENT_NAME`: The resolved name of the agent executable being invoked.
+- `ACE_BRANCH`: The current or target Ace virtual branch (e.g., `feature/ui/header`).
+- `ACE_REAL_BRANCH`: The underlying Git ref (e.g., `ace/feature_ui_header-a1b2c3d4e5f6`).
+- `ACE_PARENT_BRANCH`: The Ace virtual branch parent, if one is configured.
+- `ACE_REPOSITORY_ROOT`: The absolute path to the repository root directory.
+- `ACE_VCS_BACKEND`: Hard-coded to `git` to signal the underlying storage engine.
+- `ACE_INTEROP_TARGETS`: A comma-separated list of active interop targets, currently `git,mercurial`.
