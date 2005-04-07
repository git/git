
	GIT - the stupid content tracker

"git" can mean anything, depending on your mood.

 - random three-letter combination that is pronounceable, and not
   actually used by any common UNIX command.  The fact that it is a
   mispronounciation of "get" may or may not be relevant.
 - stupid. contemptible and despicable. simple. Take your pick from the
   dictionary of slang.
 - "global information tracker": you're in a good mood, and it actually
   works for you. Angels sing, and a light suddenly fills the room. 
 - "goddamn idiotic truckload of sh*t": when it breaks

This is a stupid (but extremely fast) directory content manager.  It
doesn't do a whole lot, but what it _does_ do is track directory
contents efficiently. 

There are two object abstractions: the "object database", and the
"current directory cache".

	The Object Database (SHA1_FILE_DIRECTORY)

The object database is literally just a content-addressable collection
of objects.  All objects are named by their content, which is
approximated by the SHA1 hash of the object itself.  Objects may refer
to other objects (by referencing their SHA1 hash), and so you can build
up a hierarchy of objects. 

There are several kinds of objects in the content-addressable collection
database.  They are all in deflated with zlib, and start off with a tag
of their type, and size information about the data.  The SHA1 hash is
always the hash of the _compressed_ object, not the original one.

In particular, the consistency of an object can always be tested
independently of the contents or the type of the object: all objects can
be validated by verifying that (a) their hashes match the content of the
file and (b) the object successfully inflates to a stream of bytes that
forms a sequence of <ascii tag without space> + <space> + <ascii decimal
size> + <byte\0> + <binary object data>. 

BLOB: A "blob" object is nothing but a binary blob of data, and doesn't
refer to anything else.  There is no signature or any other verification
of the data, so while the object is consistent (it _is_ indexed by its
sha1 hash, so the data itself is certainly correct), it has absolutely
no other attributes.  No name associations, no permissions.  It is
purely a blob of data (ie normally "file contents"). 

TREE: The next hierarchical object type is the "tree" object.  A tree
object is a list of permission/name/blob data, sorted by name.  In other
words the tree object is uniquely determined by the set contents, and so
two separate but identical trees will always share the exact same
object. 

Again, a "tree" object is just a pure data abstraction: it has no
history, no signatures, no verification of validity, except that the
contents are again protected by the hash itself.  So you can trust the
contents of a tree, the same way you can trust the contents of a blob,
but you don't know where those contents _came_ from. 

Side note on trees: since a "tree" object is a sorted list of
"filename+content", you can create a diff between two trees without
actually having to unpack two trees.  Just ignore all common parts, and
your diff will look right.  In other words, you can effectively (and
efficiently) tell the difference between any two random trees by O(n)
where "n" is the size of the difference, rather than the size of the
tree. 

Side note 2 on trees: since the name of a "blob" depends entirely and
exclusively on its contents (ie there are no names or permissions
involved), you can see trivial renames or permission changes by noticing
that the blob stayed the same.  However, renames with data changes need
a smarter "diff" implementation. 

CHANGESET: The "changeset" object is an object that introduces the
notion of history into the picture.  In contrast to the other objects,
it doesn't just describe the physical state of a tree, it describes how
we got there, and why. 

A "changeset" is defined by the tree-object that it results in, the
parent changesets (zero, one or more) that led up to that point, and a
comment on what happened. Again, a changeset is not trusted per se:
the contents are well-defined and "safe" due to the cryptographically
strong signatures at all levels, but there is no reason to believe that
the tree is "good" or that the merge information makes sense. The
parents do not have to actually have any relationship with the result,
for example.

Note on changesets: unlike real SCM's, changesets do not contain rename
information or file mode chane information.  All of that is implicit in
the trees involved (the result tree, and the result trees of the
parents), and describing that makes no sense in this idiotic file
manager.

TRUST: The notion of "trust" is really outside the scope of "git", but
it's worth noting a few things. First off, since everything is hashed
with SHA1, you _can_ trust that an object is intact and has not been
messed with by external sources. So the name of an object uniquely
identifies a known state - just not a state that you may want to trust.

Furthermore, since the SHA1 signature of a changeset refers to the
SHA1 signatures of the tree it is associated with and the signatures
of the parent, a single named changeset specifies uniquely a whole
set of history, with full contents. You can't later fake any step of
the way once you have the name of a changeset.

So to introduce some real trust in the system, the only thing you need
to do is to digitally sign just _one_ special note, which includes the
name of a top-level changeset.  Your digital signature shows others that
you trust that changeset, and the immutability of the history of
changesets tells others that they can trust the whole history.

In other words, you can easily validate a whole archive by just sending
out a single email that tells the people the name (SHA1 hash) of the top
changeset, and digitally sign that email using something like GPG/PGP.

In particular, you can also have a separate archive of "trust points" or
tags, which document your (and other peoples) trust.  You may, of
course, archive these "certificates of trust" using "git" itself, but
it's not something "git" does for you. 

Another way of saying the same thing: "git" itself only handles content
integrity, the trust has to come from outside. 

	Current Directory Cache (".dircache/index")

The "current directory cache" is a simple binary file, which contains an
efficient representation of a virtual directory content at some random
time.  It does so by a simple array that associates a set of names,
dates, permissions and content (aka "blob") objects together.  The cache
is always kept ordered by name, and names are unique at any point in
time, but the cache has no long-term meaning, and can be partially
updated at any time. 

In particular, the "current directory cache" certainly does not need to
be consistent with the current directory contents, but it has two very
important attributes:

 (a) it can re-generate the full state it caches (not just the directory
     structure: through the "blob" object it can regenerate the data too)

     As a special case, there is a clear and unambiguous one-way mapping
     from a current directory cache to a "tree object", which can be
     efficiently created from just the current directory cache without
     actually looking at any other data.  So a directory cache at any
     one time uniquely specifies one and only one "tree" object (but
     has additional data to make it easy to match up that tree object
     with what has happened in the directory)
    

and

 (b) it has efficient methods for finding inconsistencies between that
     cached state ("tree object waiting to be instantiated") and the
     current state. 

Those are the two ONLY things that the directory cache does.  It's a
cache, and the normal operation is to re-generate it completely from a
known tree object, or update/compare it with a live tree that is being
developed.  If you blow the directory cache away entirely, you haven't
lost any information as long as you have the name of the tree that it
described. 

(But directory caches can also have real information in them: in
particular, they can have the representation of an intermediate tree
that has not yet been instantiated.  So they do have meaning and usage
outside of caching - in one sense you can think of the current directory
cache as being the "work in progress" towards a tree commit).
