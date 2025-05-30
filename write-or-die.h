#ifndef WRITE_OR_DIE_H
#define WRITE_OR_DIE_H

void maybe_flush_or_die(FILE *, const char *);
__attribute__((format (printf, 2, 3)))
void fprintf_or_die(FILE *, const char *fmt, ...);
void fwrite_or_die(FILE *f, const void *buf, size_t count);
void fflush_or_die(FILE *f);
void write_or_die(int fd, const void *buf, size_t count);
void writev_or_die(int fd, struct git_iovec *, int iovcnt);
void fwritev_or_die(FILE *, const struct git_iovec *, int iovcnt);

/*
 * These values are used to help identify parts of a repository to fsync.
 * FSYNC_COMPONENT_NONE identifies data that will not be a persistent part of the
 * repository and so shouldn't be fsynced.
 */
enum fsync_component {
	FSYNC_COMPONENT_NONE,
	FSYNC_COMPONENT_LOOSE_OBJECT		= 1 << 0,
	FSYNC_COMPONENT_PACK			= 1 << 1,
	FSYNC_COMPONENT_PACK_METADATA		= 1 << 2,
	FSYNC_COMPONENT_COMMIT_GRAPH		= 1 << 3,
	FSYNC_COMPONENT_INDEX			= 1 << 4,
	FSYNC_COMPONENT_REFERENCE		= 1 << 5,
};

#define FSYNC_COMPONENTS_OBJECTS (FSYNC_COMPONENT_LOOSE_OBJECT | \
				  FSYNC_COMPONENT_PACK)

#define FSYNC_COMPONENTS_DERIVED_METADATA (FSYNC_COMPONENT_PACK_METADATA | \
					   FSYNC_COMPONENT_COMMIT_GRAPH)

#define FSYNC_COMPONENTS_DEFAULT ((FSYNC_COMPONENTS_OBJECTS | \
				   FSYNC_COMPONENTS_DERIVED_METADATA) & \
				  ~FSYNC_COMPONENT_LOOSE_OBJECT)

#define FSYNC_COMPONENTS_COMMITTED (FSYNC_COMPONENTS_OBJECTS | \
				    FSYNC_COMPONENT_REFERENCE)

#define FSYNC_COMPONENTS_ADDED (FSYNC_COMPONENTS_COMMITTED | \
				FSYNC_COMPONENT_INDEX)

#define FSYNC_COMPONENTS_ALL (FSYNC_COMPONENT_LOOSE_OBJECT | \
			      FSYNC_COMPONENT_PACK | \
			      FSYNC_COMPONENT_PACK_METADATA | \
			      FSYNC_COMPONENT_COMMIT_GRAPH | \
			      FSYNC_COMPONENT_INDEX | \
			      FSYNC_COMPONENT_REFERENCE)

#ifndef FSYNC_COMPONENTS_PLATFORM_DEFAULT
#define FSYNC_COMPONENTS_PLATFORM_DEFAULT FSYNC_COMPONENTS_DEFAULT
#endif

/* IO helper functions */
void fsync_or_die(int fd, const char *);
int fsync_component(enum fsync_component component, int fd);
void fsync_component_or_die(enum fsync_component component, int fd, const char *msg);

/*
 * A bitmask indicating which components of the repo should be fsynced.
 */
extern enum fsync_component fsync_components;
extern int fsync_object_files;
extern int use_fsync;

enum fsync_method {
	FSYNC_METHOD_FSYNC,
	FSYNC_METHOD_WRITEOUT_ONLY,
	FSYNC_METHOD_BATCH,
};

extern enum fsync_method fsync_method;

static inline int batch_fsync_enabled(enum fsync_component component)
{
	return (fsync_components & component) && (fsync_method == FSYNC_METHOD_BATCH);
}

#endif /* WRITE_OR_DIE_H */
