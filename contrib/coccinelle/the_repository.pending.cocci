// This file is used for the ongoing refactoring of
// bringing the index or repository struct in all of
// our code base.

@@
@@
(
// refs.h
- dwim_ref
+ repo_dwim_ref
// rerere.h
|
- rerere
+ repo_rerere
// revision.h
|
- init_revisions
+ repo_init_revisions
)
  (
+ the_repository,
  ...)
