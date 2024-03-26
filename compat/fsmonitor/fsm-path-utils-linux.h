#ifndef FSM_PATH_UTILS_LINUX_H
#define FSM_PATH_UTILS_LINUX_H
#endif

#ifdef HAVE_LINUX_MAGIC_H
#include <linux/magic.h>
#endif

#ifndef ACFS_SUPER_MAGIC
#define ACFS_SUPER_MAGIC 0x61636673
#endif

#ifndef AFS_SUPER_MAGIC
#define AFS_SUPER_MAGIC 0x5346414f
#endif

#ifndef CEPH_SUPER_MAGIC
#define CEPH_SUPER_MAGIC 0x00c36400
#endif

#ifndef CIFS_SUPER_MAGIC
#define CIFS_SUPER_MAGIC 0xff534d42
#endif

#ifndef CODA_SUPER_MAGIC
#define CODA_SUPER_MAGIC 0x73757245
#endif

#ifndef FHGFS_SUPER_MAGIC
#define FHGFS_SUPER_MAGIC 0x19830326
#endif

#ifndef GFS_SUPER_MAGIC
#define GFS_SUPER_MAGIC 0x1161970
#endif

#ifndef GPFS_SUPER_MAGIC
#define GPFS_SUPER_MAGIC 0x47504653
#endif

#ifndef IBRIX_SUPER_MAGIC
#define IBRIX_SUPER_MAGIC 0x013111a8
#endif

#ifndef KAFS_SUPER_MAGIC
#define KAFS_SUPER_MAGIC 0x6b414653
#endif

#ifndef LUSTRE_SUPER_MAGIC
#define LUSTRE_SUPER_MAGIC 0x0bd00bd0
#endif

#ifndef NCP_SUPER_MAGIC
#define NCP_SUPER_MAGIC 0x564c
#endif

#ifndef NFS_SUPER_MAGIC
#define NFS_SUPER_MAGIC 0x6969
#endif

#ifndef NFSD_SUPER_MAGIC
#define NFSD_SUPER_MAGIC 0x6e667364
#endif

#ifndef OCFS2_SUPER_MAGIC
#define OCFS2_SUPER_MAGIC 0x7461636f
#endif

#ifndef PANFS_SUPER_MAGIC
#define PANFS_SUPER_MAGIC 0xaad7aaea
#endif

#ifndef SMB_SUPER_MAGIC
#define SMB_SUPER_MAGIC 0x517b
#endif

#ifndef SMB2_SUPER_MAGIC
#define SMB2_SUPER_MAGIC 0xfe534d42
#endif

#ifndef SNFS_SUPER_MAGIC
#define SNFS_SUPER_MAGIC 0xbeefdead
#endif

#ifndef VMHGFS_SUPER_MAGIC
#define VMHGFS_SUPER_MAGIC 0xbacbacbc
#endif

#ifndef VXFS_SUPER_MAGIC
#define VXFS_SUPER_MAGIC 0xa501fcf5
#endif
