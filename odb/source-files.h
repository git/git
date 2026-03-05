#ifndef ODB_SOURCE_FILES_H
#define ODB_SOURCE_FILES_H

struct odb_source_loose;
struct odb_source;
struct packfile_store;

/*
 * The files object database source uses a combination of loose objects and
 * packfiles. It is the default backend used by Git to store objects.
 */
struct odb_source_files {
	struct odb_source *source;
	struct odb_source_loose *loose;
	struct packfile_store *packed;
};

/* Allocate and initialize a new object source. */
struct odb_source_files *odb_source_files_new(struct odb_source *source);

/* Free the object source and release all associated resources. */
void odb_source_files_free(struct odb_source_files *files);

#endif
